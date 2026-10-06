using System.Windows.Media.Imaging;
using SmpackGui.Core.Imaging;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

/// <summary>Decoded view of one DDS. Mip, slice, channel selection, HDR exposure, normalmap Z rebuild.</summary>
public sealed class TexturePreviewViewModel : ObservableObject
{
    private DdsFile? m_Dds;
    private DecodedImage? m_Decoded;
    private int m_Version;

    private BitmapSource? m_Bitmap;
    public BitmapSource? Bitmap
    {
        get => m_Bitmap;
        private set => Set(ref m_Bitmap, value);
    }

    private bool m_IsLoading;
    public bool IsLoading
    {
        get => m_IsLoading;
        set => Set(ref m_IsLoading, value);
    }

    private string? m_Error;
    public string? Error
    {
        get => m_Error;
        set => Set(ref m_Error, value);
    }

    public bool HasImage => m_Dds != null;
    public bool IsHdr => m_Dds != null && DxgiFormats.IsHdr(m_Dds.Dxgi);
    public bool IsTwoChannel => m_Dds != null && DxgiFormats.Family(m_Dds.Dxgi) == DxgiFormats.BC5_UNORM;
    public bool IsMultiSlice => (m_Dds?.Slices ?? 1) > 1 || (m_Dds?.Depth ?? 1) > 1;

    public List<string> MipLabels
    {
        get;
        private set;
    } = [];
    public List<string> SliceLabels
    {
        get;
        private set;
    } = [];

    private int m_Mip;
    public int Mip
    {
        get => m_Mip;
        set
        {
            if (Set(ref m_Mip, Math.Max(0, value)))
                _ = DecodeAsync();
            else if (value < 0) // A ComboBox whose items were replaced pushes '-1'.
                System.Windows.Application.Current.Dispatcher.BeginInvoke(() => OnPropertyChanged(nameof(Mip)));
        }
    }

    private int m_Slice;
    public int Slice
    {
        get => m_Slice;
        set
        {
            if (Set(ref m_Slice, Math.Max(0, value)))
                _ = DecodeAsync();
            else if (value < 0) // A ComboBox whose items were replaced pushes '-1'.
                System.Windows.Application.Current.Dispatcher.BeginInvoke(() => OnPropertyChanged(nameof(Slice)));
        }
    }

    private ChannelMode m_Channel = ChannelMode.RGBA;
    public ChannelMode Channel
    { 
        get => m_Channel;
        set
        { 
            if (Set(ref m_Channel, value))
                Rebuild();
        }
    }

    private bool m_ReconstructZ;
    public bool ReconstructZ
    {
        get => m_ReconstructZ;
        set
        {
            if (Set(ref m_ReconstructZ, value))
                Rebuild();
        }
    }

    private double m_Exposure = 1;
    public double Exposure
    {
        get => m_Exposure;

        set
        {
            if (!Set(ref m_Exposure, value) || m_Decoded?.Hdr == null)
                return;

            m_Decoded.ToneMap((float)value);
            Rebuild();
        }
    }

    public string FormatLabel => m_Dds == null ? "" : DxgiFormats.Friendly(m_Dds.Dxgi);
    public string SizeLabel => m_Dds == null ? "" : $"{m_Dds.MipWidth(Mip)}x{m_Dds.MipHeight(Mip)}" + (m_Dds.IsVolume ? $"x{m_Dds.MipDepth(Mip)}" : "");
    public int Width => m_Decoded?.Width ?? 0;
    public int Height => m_Decoded?.Height ?? 0;
    public string? SourcePath
    {
        get;
        private set;
    }

    public void Clear()
    {
        m_Version++;
        m_Dds = null;
        m_Decoded = null;
        Bitmap = null;
        Error = null;
        SourcePath = null;

        NotifyAll();
    }

    public async Task LoadAsync(string dds_path, bool keep_view = true)
    {
        int v = ++m_Version;
        IsLoading = true;
        Error = null;

        try
        {
            var dds = await Task.Run(() => DdsFile.Load(dds_path));

            if (v != m_Version)
                return;

            bool same_shape = keep_view && m_Dds != null && m_Dds.MipCount == dds.MipCount && m_Dds.Slices == dds.Slices;

            m_Dds = dds;
            SourcePath = dds_path;
            MipLabels = [.. Enumerable.Range(0, dds.MipCount).Select(m => $"Mip {m} ({dds.MipWidth(m)}x{dds.MipHeight(m)})")];
            SliceLabels = dds.IsVolume
                ? [.. Enumerable.Range(0, dds.Depth).Select(z => $"Depth {z} / {dds.Depth}")]
                : [.. Enumerable.Range(0, dds.Slices).Select(s => dds.IsCube ? $"Face {s % 6} {(new[] { "+X", "-X", "+Y", "-Y", "+Z", "-Z" })[s % 6]}" + (dds.Slices > 6 ? $" #{s / 6}" : "") : $"Slice {s}")];

            if (!same_shape)
            {
                m_Mip = 0;
                m_Slice = 0;
                m_ReconstructZ = IsTwoChannel;
                m_Exposure = 1;

                if (!TextureDecoder.IsSupported(dds.Dxgi))
                    throw new NotSupportedException($"Preview for {dds.FormatName} is not supported");
            }

            NotifyAll();
            await DecodeAsync();
        }
        catch (Exception ex)
        {
            if (v == m_Version)
            {
                Error = ex.Message;
                Bitmap = null;
            }
        }
        finally
        {
            if (v == m_Version)
                IsLoading = false;
        }
    }

    private async Task DecodeAsync()
    {
        var dds = m_Dds;

        if (dds == null)
            return;

        int v = ++m_Version;
        int mip = Math.Min(Mip, dds.MipCount - 1);
        int layer = dds.IsVolume ? Math.Min(Slice, dds.MipDepth(mip) - 1) : Math.Min(Slice, dds.Slices - 1);
        int slice = dds.IsVolume ? 0 : layer, depth_index = dds.IsVolume ? layer : 0;

        IsLoading = true;

        try
        {
            var mode = Channel;
            var z = ReconstructZ;
            float exp = (float)Exposure;

            var (img, bmp) = await Task.Run(() =>
            {
                var d = TextureDecoder.Decode(dds, slice, mip, exp, depth_index);
                return (d, ImageUtil.ToBitmap(d, mode, z));
            });

            if (v != m_Version)
                return;

            m_Decoded = img;
            Bitmap = bmp;
            Error = null;

            OnPropertyChanged(nameof(SizeLabel));
            OnPropertyChanged(nameof(Width));
            OnPropertyChanged(nameof(Height));
        }
        catch (Exception ex)
        {
            if (v == m_Version)
                Error = ex.Message;
        }
        finally
        {
            if (v == m_Version)
                IsLoading = false;
        }
    }

    private void Rebuild()
    {
        var d = m_Decoded;

        if (d == null)
            return;

        Bitmap = ImageUtil.ToBitmap(d, Channel, ReconstructZ);
    }

    /// <summary>RGBA at a pixel of the current mip for the hover readout.</summary>
    public (byte R, byte G, byte B, byte A)? PixelAt(int x, int y)
    {
        var d = m_Decoded;

        if (d == null || x < 0 || y < 0 || x >= d.Width || y >= d.Height)
            return null;

        int o = (y * d.Width + x) * 4;
        return (d.Rgba[o], d.Rgba[o + 1], d.Rgba[o + 2], d.Rgba[o + 3]);
    }

    public (float R, float G, float B)? HdrAt(int x, int y)
    {
        var d = m_Decoded;

        if (d?.Hdr == null || x < 0 || y < 0 || x >= d.Width || y >= d.Height)
            return null;

        int o = (y * d.Width + x) * 4;
        return (d.Hdr[o], d.Hdr[o + 1], d.Hdr[o + 2]);
    }

    /// <summary>Current view with channel mode applied for PNG export.</summary>
    public BitmapSource? CurrentBitmap => Bitmap;

    private void NotifyAll()
    {
        foreach (var p in new[] { nameof(HasImage), nameof(IsHdr), nameof(IsTwoChannel), nameof(IsMultiSlice), nameof(MipLabels), nameof(SliceLabels),
                     nameof(Mip), nameof(Slice), nameof(ReconstructZ), nameof(Exposure), nameof(FormatLabel), nameof(SizeLabel) })
            OnPropertyChanged(p);
    }
}