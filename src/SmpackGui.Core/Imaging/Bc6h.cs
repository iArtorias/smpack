namespace SmpackGui.Core.Imaging;

/// <summary>BC6H (UF16/SF16) block decoder producing linear float RGBA.</summary>
internal static class Bc6h
{
    // Index. w = 0, x = 1, y = 2, z = 3.
    private ref struct Fields
    {
        public BitReader m_Br;
        public Span<int> m_R, m_G, m_B;
        // Read 'n' bits into value.
        public void F(Span<int> v, int e, int lo, int n) => v[e] |= m_Br.Read(n) << lo;
        // Single bit.
        public void Bt(Span<int> v, int e, int bit) => v[e] |= m_Br.Bit() << bit;
    }

    // Endpoint bits, delta bits (r, g, b), transformed, regions.
    private static readonly (int Ep, int Dr, int Dg, int Db, bool T, int Ns)[] INFO =
    [
        (10, 5, 5, 5, true, 2),
        (7, 6, 6, 6, true, 2),
        (11, 5, 4, 4, true, 2),
        (11, 4, 5, 4, true, 2),
        (11, 4, 4, 5, true, 2),
        (9, 5, 5, 5, true, 2),
        (8, 6, 5, 5, true, 2),
        (8, 5, 6, 5, true, 2),
        (8, 5, 5, 6, true, 2),
        (6, 6, 6, 6, false, 2),
        (10, 10, 10, 10, false, 1),
        (11, 9, 9, 9, true, 1),
        (12, 8, 8, 8, true, 1),
        (16, 4, 4, 4, true, 1),
    ];

    public static void DecodeBlock(ReadOnlySpan<byte> block, Span<float> px, bool signed)
    {
        Span<int> r = stackalloc int[4], g = stackalloc int[4], b = stackalloc int[4];

        var f = new Fields
        {
            m_Br = new BitReader(block),
            m_R = r,
            m_G = g,
            m_B = b
        };

        int m = f.m_Br.Read(2);

        if (m > 1)
            m |= f.m_Br.Read(3) << 2;

        int mode;
        int partition = 0;
        switch (m)
        {
            case 0b00: // Mode 1
                mode = 0;

                f.Bt(g, 2, 4);
                f.Bt(b, 2, 4);
                f.Bt(b, 3, 4);
                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 5);
                f.Bt(g, 3, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 5);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 5);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 5);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 5);
                f.Bt(b, 3, 3);

                partition = f.m_Br.Read(5);
                break;

            case 0b01: // Mode 2
                mode = 1;

                f.Bt(g, 2, 5);
                f.Bt(g, 3, 4);
                f.Bt(g, 3, 5);
                f.F(r, 0, 0, 7);
                f.Bt(b, 3, 0);
                f.Bt(b, 3, 1);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 7);
                f.Bt(b, 2, 5);
                f.Bt(b, 3, 2);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 7);
                f.Bt(b, 3, 3);
                f.Bt(b, 3, 5);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 6);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 6);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 6);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 6);
                f.F(r, 3, 0, 6);

                partition = f.m_Br.Read(5);
                break;

            case 0b00010: // Mode 3
                mode = 2;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 5);
                f.Bt(r, 0, 10);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 4);
                f.Bt(g, 0, 10);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 4);
                f.Bt(b, 0, 10);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 5);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 5);
                f.Bt(b, 3, 3);

                partition = f.m_Br.Read(5);
                break;

            case 0b00110: // Mode 4
                mode = 3;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 4);
                f.Bt(r, 0, 10);
                f.Bt(g, 3, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 5);
                f.Bt(g, 0, 10);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 4);
                f.Bt(b, 0, 10);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 4);
                f.Bt(b, 3, 0);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 4);
                f.Bt(g, 2, 4);
                f.Bt(b, 3, 3);

                partition = f.m_Br.Read(5);
                break;

            case 0b01010: // Mode 5
                mode = 4;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 4);
                f.Bt(r, 0, 10);
                f.Bt(b, 2, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 4);
                f.Bt(g, 0, 10);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 5);
                f.Bt(b, 0, 10);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 4);
                f.Bt(b, 3, 1);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 4);
                f.Bt(b, 3, 4);
                f.Bt(b, 3, 3);

                partition = f.m_Br.Read(5);
                break;

            case 0b01110: // Mode 6
                mode = 5;

                f.F(r, 0, 0, 9);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 9);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 9);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 5);
                f.Bt(g, 3, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 5);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 5);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 5);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 5);
                f.Bt(b, 3, 3);

                partition = f.m_Br.Read(5);
                break;

            case 0b10010: // Mode 7
                mode = 6;

                f.F(r, 0, 0, 8);
                f.Bt(g, 3, 4);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 8);
                f.Bt(b, 3, 2);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 8);
                f.Bt(b, 3, 3);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 6);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 5);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 5);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 6);
                f.F(r, 3, 0, 6);

                partition = f.m_Br.Read(5);
                break;

            case 0b10110: // Mode 8
                mode = 7;

                f.F(r, 0, 0, 8);
                f.Bt(b, 3, 0);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 8);
                f.Bt(g, 2, 5);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 8);
                f.Bt(g, 3, 5);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 5);
                f.Bt(g, 3, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 6);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 5);
                f.Bt(b, 3, 1);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 5);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 5);
                f.Bt(b, 3, 3);
                partition = f.m_Br.Read(5);
                break;

            case 0b11010: // Mode 9
                mode = 8;

                f.F(r, 0, 0, 8);
                f.Bt(b, 3, 1);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 8);
                f.Bt(b, 2, 5);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 8);
                f.Bt(b, 3, 5);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 5);
                f.Bt(g, 3, 4);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 5);
                f.Bt(b, 3, 0);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 6);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 5);
                f.Bt(b, 3, 2);
                f.F(r, 3, 0, 5);
                f.Bt(b, 3, 3);
                partition = f.m_Br.Read(5);
                break;

            case 0b11110: // Mode 10
                mode = 9;

                f.F(r, 0, 0, 6);
                f.Bt(g, 3, 4);
                f.Bt(b, 3, 0);
                f.Bt(b, 3, 1);
                f.Bt(b, 2, 4);
                f.F(g, 0, 0, 6);
                f.Bt(g, 2, 5);
                f.Bt(b, 2, 5);
                f.Bt(b, 3, 2);
                f.Bt(g, 2, 4);
                f.F(b, 0, 0, 6);
                f.Bt(g, 3, 5);
                f.Bt(b, 3, 3);
                f.Bt(b, 3, 5);
                f.Bt(b, 3, 4);
                f.F(r, 1, 0, 6);
                f.F(g, 2, 0, 4);
                f.F(g, 1, 0, 6);
                f.F(g, 3, 0, 4);
                f.F(b, 1, 0, 6);
                f.F(b, 2, 0, 4);
                f.F(r, 2, 0, 6);
                f.F(r, 3, 0, 6);
                partition = f.m_Br.Read(5);
                break;

            case 0b00011: // Mode 11
                mode = 10;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 10);
                f.F(g, 1, 0, 10);
                f.F(b, 1, 0, 10);
                break;

            case 0b00111: // Mode 12
                mode = 11;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 9);
                f.Bt(r, 0, 10);
                f.F(g, 1, 0, 9);
                f.Bt(g, 0, 10);
                f.F(b, 1, 0, 9);
                f.Bt(b, 0, 10);
                break;

            case 0b01011: // Mode 13
                mode = 12;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 8);
                f.Bt(r, 0, 11);
                f.Bt(r, 0, 10);
                f.F(g, 1, 0, 8);
                f.Bt(g, 0, 11);
                f.Bt(g, 0, 10);
                f.F(b, 1, 0, 8);
                f.Bt(b, 0, 11);
                f.Bt(b, 0, 10);
                break;

            case 0b01111: // Mode 14
                mode = 13;

                f.F(r, 0, 0, 10);
                f.F(g, 0, 0, 10);
                f.F(b, 0, 0, 10);
                f.F(r, 1, 0, 4);

                for (int i = 15; i >= 10; i--)
                    f.Bt(r, 0, i);

                f.F(g, 1, 0, 4);

                for (int i = 15; i >= 10; i--)
                    f.Bt(g, 0, i);

                f.F(b, 1, 0, 4);

                for (int i = 15; i >= 10; i--)
                    f.Bt(b, 0, i);
                break;

            default:
                // Reserved mode. Decodes to black.
                px[..64].Clear();
                for (int i = 0; i < 16; i++)
                    px[i * 4 + 3] = 1f;
                return;
        }

        var (ep_bits, dr, dg, db, transformed, ns) = INFO[mode];
        int n_ep = ns * 2;

        // Sign extend, apply deltas.
        if (signed)
        {
            r[0] = SignExtend(r[0], ep_bits); g[0] = SignExtend(g[0], ep_bits); b[0] = SignExtend(b[0], ep_bits);
        }
        for (int e = 1; e < n_ep; e++)
        {
            if (transformed)
            {
                r[e] = SignExtend(r[e], dr);
                g[e] = SignExtend(g[e], dg);
                b[e] = SignExtend(b[e], db);
                r[e] = Wrap(r[0] + r[e], ep_bits, signed);
                g[e] = Wrap(g[0] + g[e], ep_bits, signed);
                b[e] = Wrap(b[0] + b[e], ep_bits, signed);
            }
            else if (signed)
            {
                r[e] = SignExtend(r[e], ep_bits);
                g[e] = SignExtend(g[e], ep_bits);
                b[e] = SignExtend(b[e], ep_bits);
            }
        }
        for (int e = 0; e < n_ep; e++)
        {
            r[e] = Unquantize(r[e], ep_bits, signed);
            g[e] = Unquantize(g[e], ep_bits, signed);
            b[e] = Unquantize(b[e], ep_bits, signed);
        }

        int ib = ns == 1 ? 4 : 3;
        var w = BcTables.Weights(ib);

        for (int i = 0; i < 16; i++)
        {
            bool anchor = i == 0 || (ns == 2 && BcTables.Anchor2[partition] == i);
            int idx = f.m_Br.Read(anchor ? ib - 1 : ib);
            int s = ns == 2 ? BcTables.Partition2[partition] >> i & 1 : 0;
            int wi = w[idx];

            px[i * 4] = Finish(Lerp(r[s * 2], r[s * 2 + 1], wi), signed);
            px[i * 4 + 1] = Finish(Lerp(g[s * 2], g[s * 2 + 1], wi), signed);
            px[i * 4 + 2] = Finish(Lerp(b[s * 2], b[s * 2 + 1], wi), signed);
            px[i * 4 + 3] = 1f;
        }
    }

    private static int SignExtend(int v, int bits)
    {
        int shift = 32 - bits;
        return v << shift >> shift;
    }

    private static int Wrap(int v, int bits, bool signed)
    {
        v &= (1 << bits) - 1;
        return signed ? SignExtend(v, bits) : v;
    }

    private static int Unquantize(int v, int bits, bool signed)
    {
        if (!signed)
        {
            if (bits >= 15)
                return v;

            if (v == 0)
                return 0;

            if (v == (1 << bits) - 1)
                return 0xFFFF;

            return ((v << 16) + 0x8000) >> bits;
        }

        if (bits >= 16)
            return v;

        bool neg = v < 0;
        if (neg)
            v = -v;

        int u;
        if (v == 0)
            u = 0;
        else if (v >= (1 << (bits - 1)) - 1)
            u = 0x7FFF;
        else
            u = ((v << 15) + 0x4000) >> (bits - 1);

        return neg ? -u : u;
    }

    private static int Lerp(int a, int b, int w) => ((64 - w) * a + w * b + 32) >> 6;

    private static float Finish(int c, bool signed)
    {
        ushort bits;
        if (!signed)
            bits = (ushort)((c * 31) >> 6);
        else
        {
            int v = c < 0 ? -(((-c) * 31) >> 5) : (c * 31) >> 5;
            bits = v < 0 ? (ushort)(0x8000 | -v) : (ushort)v;
        }

        return (float)BitConverter.UInt16BitsToHalf(bits);
    }
}