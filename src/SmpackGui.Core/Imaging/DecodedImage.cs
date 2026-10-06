namespace SmpackGui.Core.Imaging;

/// <summary>
/// One decoded surface. LDR formats fill <see cref="Rgba"/> directly,
/// HDR formats keep linear floats in <see cref="Hdr"/> and produce
/// <see cref="Rgba"/> through <see cref="ToneMap"/>.
/// </summary>
public sealed class DecodedImage
{
    public int Width { get; }
    public int Height { get; }
    /// <summary>RGBA8, row major, R first.</summary>
    public byte[] Rgba
    {
        get;
        private set;
    }
    /// <summary>Linear RGBA floats for HDR sources, otherwise null.</summary>
    public float[]? Hdr { get; }
    public bool IsHdr => Hdr != null;

    public DecodedImage(int w, int h, byte[] rgba)
    {
        Width = w;
        Height = h;
        Rgba = rgba;
    }

    public DecodedImage(int w, int h, float[] hdr, float exposure = 1f)
    {
        Width = w;
        Height = h;
        Hdr = hdr;
        Rgba = new byte[w * h * 4];

        ToneMap(exposure);
    }

    /// <summary>Rederive the 8 bit view from HDR data.</summary>
    public void ToneMap(float exposure)
    {
        if (Hdr == null)
            return;

        var lut = new byte[4097];
        for (int i = 0; i <= 4096; i++)
        {
            double l = i / 4096.0;
            double s = l <= 0.0031308 ? l * 12.92 : 1.055 * Math.Pow(l, 1 / 2.4) - 0.055;
            lut[i] = (byte)Math.Clamp((int)Math.Round(s * 255), 0, 255);
        }

        var hdr = Hdr;
        var dst = Rgba;

        Parallel.For(0, Height, y =>
        {
            int o = y * Width * 4;

            for (int x = 0; x < Width; x++, o += 4)
            {
                for (int c = 0; c < 3; c++)
                {
                    float v = hdr[o + c] * exposure;

                    if (float.IsNaN(v) || v <= 0)
                        dst[o + c] = 0;
                    else dst[o + c] = lut[(int)(Math.Min(v, 1f) * 4096f)];
                }

                float a = hdr[o + 3];
                dst[o + 3] = (byte)Math.Clamp((int)(a * 255f + 0.5f), 0, 255);
            }
        });
    }
}