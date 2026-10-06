using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SmpackGui.Core.Imaging;

namespace SmpackGui.Services;

public enum ChannelMode
{
    RGBA,
    RGB,
    R,
    G,
    B,
    A
}

public static class ImageUtil
{
    /// <summary>RGBA8 to frozen Bgra32 bitmap, optionally isolating a channel or rebuilding a normal map's Z.</summary>
    public static BitmapSource ToBitmap(DecodedImage img, ChannelMode mode, bool reconstruct_z = false)
    {
        var src = img.Rgba;
        int w = img.Width, h = img.Height;
        var dst = new byte[w * h * 4];

        Parallel.For(0, h, y =>
        {
            int o = y * w * 4;

            for (int x = 0; x < w; x++, o += 4)
            {
                byte r = src[o], g = src[o + 1], b = src[o + 2], a = src[o + 3];

                if (reconstruct_z)
                {
                    double nx = r / 127.5 - 1, ny = g / 127.5 - 1;
                    double nz = Math.Sqrt(Math.Max(0, 1 - nx * nx - ny * ny));
                    b = (byte)Math.Clamp((int)Math.Round((nz * 0.5 + 0.5) * 255), 0, 255);
                }

                switch (mode)
                {
                    case ChannelMode.RGBA:
                        dst[o] = b;
                        dst[o + 1] = g;
                        dst[o + 2] = r;
                        dst[o + 3] = a;
                        break;

                    case ChannelMode.RGB:
                        dst[o] = b;
                        dst[o + 1] = g;
                        dst[o + 2] = r;
                        dst[o + 3] = 255;
                        break;

                    case ChannelMode.R:
                        dst[o] = dst[o + 1] = dst[o + 2] = r;
                        dst[o + 3] = 255;
                        break;

                    case ChannelMode.G:
                        dst[o] = dst[o + 1] = dst[o + 2] = g;
                        dst[o + 3] = 255;
                        break;

                    case ChannelMode.B:
                        dst[o] = dst[o + 1] = dst[o + 2] = b;
                        dst[o + 3] = 255;
                        break;

                    case ChannelMode.A:
                        dst[o] = dst[o + 1] = dst[o + 2] = a;
                        dst[o + 3] = 255;
                        break;
                }
            }
        });

        var bmp = BitmapSource.Create(w, h, 96, 96, PixelFormats.Bgra32, null, dst, w * 4);
        bmp.Freeze();
        return bmp;
    }

    public static void SavePng(BitmapSource bmp, string path)
    {
        var enc = new PngBitmapEncoder();
        enc.Frames.Add(BitmapFrame.Create(bmp));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);

        using var fs = File.Create(path);
        enc.Save(fs);
    }

    /// <summary>Decode mip 0 (slice 0) of a DDS and write it as PNG.</summary>
    public static void DdsToPng(string dds, string png, bool reconstruct_z = false)
    {
        var d = DdsFile.Load(dds);

        if (!d.IsVolume)
        {
            SavePng(ToBitmap(TextureDecoder.Decode(d, 0, 0), ChannelMode.RGBA, reconstruct_z), png);
            return;
        }

        // Volume texture. All depth layers side by side in one strip.
        int w = d.Width, h = d.Height, n = d.Depth;
        var strip = new byte[w * n * h * 4];

        for (int z = 0; z < n; z++)
        {
            var layer = TextureDecoder.Decode(d, 0, 0, 1f, z).Rgba;

            for (int y = 0; y < h; y++)
                Buffer.BlockCopy(layer, y * w * 4, strip, (y * w * n + z * w) * 4, w * 4);
        }

        SavePng(ToBitmap(new DecodedImage(w * n, h, strip), ChannelMode.RGBA, reconstruct_z), png);
    }

    /// <summary>
    /// Write a color map with a separate cutout map in its alpha channel. Without a color map the
    /// RGB is a dark neutral (lashes, hair cards that take their color from the shader).
    /// </summary>
    public static void DdsWithOpacityToPng(string? colour_dds, string opacity_dds, string png)
    {
        var op = DdsFile.Load(opacity_dds);
        DecodedImage color;

        if (colour_dds != null)
        {
            color = TextureDecoder.Decode(DdsFile.Load(colour_dds), 0, 0);
        }
        else
        {
            int w0 = op.Width, h0 = op.Height;
            var px = new byte[w0 * h0 * 4];

            for (int i = 0; i < px.Length; i += 4)
            {
                px[i] = 40;
                px[i + 1] = 34;
                px[i + 2] = 30;
                px[i + 3] = 255;
            }

            color = new DecodedImage(w0, h0, px);
        }

        int w = color.Width, h = color.Height;

        // The smallest opacity mip that still covers the color map.
        int mip = 0;
        while (mip + 1 < op.MipCount && op.MipWidth(mip + 1) >= w && op.MipHeight(mip + 1) >= h)
            mip++;

        var a = TextureDecoder.Decode(op, 0, mip);
        var dst = color.Rgba;

        Parallel.For(0, h, y =>
        {
            int sy = Math.Min(a.Height - 1, (int)((y + 0.5) * a.Height / h));

            for (int x = 0; x < w; x++)
            {
                int sx = Math.Min(a.Width - 1, (int)((x + 0.5) * a.Width / w));
                dst[(y * w + x) * 4 + 3] = a.Rgba[(sy * a.Width + sx) * 4];
            }
        });

        SavePng(ToBitmap(color, ChannelMode.RGBA), png);
    }

    /// <summary>Small preview. The first mip no larger than <paramref name="max"/>.</summary>
    public static BitmapSource? Thumbnail(string dds_path, int max = 160)
    {
        try
        {
            var d = DdsFile.Load(dds_path);

            int mip = 0;
            while (mip + 1 < d.MipCount && Math.Max(d.MipWidth(mip), d.MipHeight(mip)) > max)
                mip++;

            var img = TextureDecoder.Decode(d, 0, mip);
            return ToBitmap(img, ChannelMode.RGB);
        }
        catch
        {
            return null;
        }
    }

    /// <summary>Pixel size of an image file without fully decoding it (null if unknown).</summary>
    public static (int W, int H)? ReadImageSize(string path)
    {
        try
        {
            var ext = Path.GetExtension(path).ToLowerInvariant();

            if (ext == ".dds")
            {
                var d = DdsFile.LoadHeader(path);
                return (d.Width, d.Height);
            }

            if (ext == ".tga")
            {
                var b = new byte[18];
                using var fs = File.OpenRead(path);
                fs.ReadExactly(b);
                return (BitConverter.ToUInt16(b, 12), BitConverter.ToUInt16(b, 14));
            }

            if (ext == ".hdr")
            {
                using var sr = new StreamReader(path);

                for (int i = 0; i < 64; i++)
                {
                    var line = sr.ReadLine();

                    if (line == null)
                        break;

                    var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);

                    if (p.Length == 4 && p[0].EndsWith('Y') && p[2].EndsWith('X'))
                        return (int.Parse(p[3]), int.Parse(p[1]));
                }

                return null;
            }

            if (ext == ".exr")
                return null;

            using (var fs = File.OpenRead(path))
            {
                var dec = BitmapDecoder.Create(fs, BitmapCreateOptions.DelayCreation | BitmapCreateOptions.IgnoreColorProfile, BitmapCacheOption.None);
                var f = dec.Frames[0];
                return (f.PixelWidth, f.PixelHeight);
            }
        }
        catch
        {
            return null;
        }
    }

    /// <summary>Preview of an arbitrary user image for the replace dialog.</summary>
    public static BitmapSource? LoadPreview(string path, int max = 512)
    {
        try
        {
            if (path.EndsWith(".dds", StringComparison.OrdinalIgnoreCase))
                return Thumbnail(path, max);

            var bi = new BitmapImage();

            bi.BeginInit();
            bi.CacheOption = BitmapCacheOption.OnLoad;
            bi.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
            bi.UriSource = new Uri(path);
            bi.DecodePixelWidth = max;
            bi.EndInit();
            bi.Freeze();
            return bi;
        }
        catch
        {
            return null;
        }
    }

    public static string HumanBytes(long b)
    {
        string[] u = ["B", "KB", "MB", "GB", "TB"];

        double v = b;
        int i = 0;
        while (v >= 1024 && i < u.Length - 1)
        {
            v /= 1024;
            i++;
        }

        return i == 0 ? $"{b} B" : $"{v:0.#} {u[i]}";
    }

    public static Color Hex(string s) => (Color)ColorConverter.ConvertFromString(s);
}