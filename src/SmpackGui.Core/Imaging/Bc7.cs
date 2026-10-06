namespace SmpackGui.Core.Imaging;

/// <summary>BC7 block decoder. All 8 modes.</summary>
internal static class Bc7
{
    // NS, PB, RB, ISB, CB, AB, EPB, SPB, IB, IB2
    private static readonly int[,] MODES =
    {
        { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
        { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
        { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
        { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
        { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
        { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
        { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
        { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
    };

    public static void DecodeBlock(ReadOnlySpan<byte> block, Span<byte> px)
    {
        int mode = 0;
        while (mode < 8 && (block[0] & (1 << mode)) == 0)
            mode++;

        if (mode == 8)
        {
            px[..64].Clear();
            return;
        }

        var br = new BitReader(block);
        br.Read(mode + 1);

        int ns = MODES[mode, 0], pb = MODES[mode, 1], rb = MODES[mode, 2], isb = MODES[mode, 3];
        int cb = MODES[mode, 4], ab = MODES[mode, 5], epb = MODES[mode, 6], spb = MODES[mode, 7];
        int ib = MODES[mode, 8], ib2 = MODES[mode, 9];

        int partition = br.Read(pb);
        int rotation = br.Read(rb);
        int idx_mode = br.Read(isb);

        // endpoints [subset * 2 + e, channel]
        Span<int> ep = stackalloc int[6 * 4];
        int n_ep = ns * 2;

        for (int c = 0; c < 3; c++)
            for (int e = 0; e < n_ep; e++)
                ep[e * 4 + c] = br.Read(cb);

        if (ab > 0)
            for (int e = 0; e < n_ep; e++)
                ep[e * 4 + 3] = br.Read(ab);

        int c_bits = cb, a_bits = ab;
        if (epb != 0)
        {
            for (int e = 0; e < n_ep; e++)
            {
                int p = br.Bit();
                for (int c = 0; c < 3; c++)
                    ep[e * 4 + c] = ep[e * 4 + c] << 1 | p;

                if (ab > 0)
                    ep[e * 4 + 3] = ep[e * 4 + 3] << 1 | p;
            }

            c_bits++;

            if (ab > 0)
                a_bits++;
        }
        else if (spb != 0)
        {
            for (int s = 0; s < ns; s++)
            {
                int p = br.Bit();

                for (int e = s * 2; e < s * 2 + 2; e++)
                    for (int c = 0; c < 3; c++)
                        ep[e * 4 + c] = ep[e * 4 + c] << 1 | p;
            }

            c_bits++;
        }

        for (int e = 0; e < n_ep; e++)
        {
            for (int c = 0; c < 3; c++)
                ep[e * 4 + c] = Unq(ep[e * 4 + c], c_bits);

            ep[e * 4 + 3] = ab > 0 ? Unq(ep[e * 4 + 3], a_bits) : 255;
        }

        Span<int> idx = stackalloc int[16];
        Span<int> idx2 = stackalloc int[16];

        for (int i = 0; i < 16; i++)
        {
            bool anchor = BcTables.IsAnchor(ns, partition, i);
            idx[i] = br.Read(anchor ? ib - 1 : ib);
        }

        if (ib2 > 0)
            for (int i = 0; i < 16; i++) idx2[i] = br.Read(i == 0 ? ib2 - 1 : ib2);

        var w1 = BcTables.Weights(ib);
        var w2 = ib2 > 0 ? BcTables.Weights(ib2) : w1;

        for (int i = 0; i < 16; i++)
        {
            int s = BcTables.Subset(ns, partition, i);
            int e0 = s * 8, e1 = s * 8 + 4;
            int cw, aw;

            if (ib2 == 0)
            {
                cw = w1[idx[i]]; aw = cw;
            }
            else if (idx_mode == 0)
            {
                cw = w1[idx[i]];
                aw = w2[idx2[i]];
            }
            else
            {
                cw = w2[idx2[i]];
                aw = w1[idx[i]];
            }

            int r = Lerp(ep[e0], ep[e1], cw), g = Lerp(ep[e0 + 1], ep[e1 + 1], cw), b = Lerp(ep[e0 + 2], ep[e1 + 2], cw);
            int a = Lerp(ep[e0 + 3], ep[e1 + 3], aw);

            switch (rotation)
            {
                case 1:
                    (a, r) = (r, a);
                    break;

                case 2:
                    (a, g) = (g, a);
                    break;

                case 3:
                    (a, b) = (b, a);
                    break;
            }

            px[i * 4] = (byte)r;
            px[i * 4 + 1] = (byte)g;
            px[i * 4 + 2] = (byte)b;
            px[i * 4 + 3] = (byte)a;
        }
    }

    private static int Unq(int v, int bits)
    {
        v <<= 8 - bits;
        return v | v >> bits;
    }

    private static int Lerp(int a, int b, int w) => ((64 - w) * a + w * b + 32) >> 6;
}