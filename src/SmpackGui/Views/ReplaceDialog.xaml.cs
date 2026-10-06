using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using SmpackGui.Core.Imaging;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Services;

namespace SmpackGui.Views;

public partial class ReplaceDialog : Window
{
    private readonly TextureItem m_Item;
    private readonly string m_File;
    private readonly List<(RadioButton Button, int W, int H)> m_Sizes = [];

    public ReplaceRequest? Request 
    { 
        get;
        private set;
    }

    public ReplaceDialog(TextureItem item, ExportedTexture original, string file, bool texconv_available, BcQuality quality)
    {
        InitializeComponent();
        Native.ApplyDarkChrome(this);

        m_Item = item;
        m_File = file;
        TexName.Text = item.Name;
        QualityBox.SelectedIndex = (int)quality;

        var sc = original.LoadSidecar();
        int ow = sc.Width, oh = sc.Height;
        int dxgi = sc.Dxgi != 0 ? sc.Dxgi : DxgiFormats.FromAgcName(sc.AgcFormat);

        OrigImage.Source = ImageUtil.Thumbnail(original.DdsPath, 512);
        OrigInfo.Text = $"{ow}x{oh}{(sc.Depth > 1 ? $"x{sc.Depth}" : "")}, {DxgiFormats.Friendly(dxgi)}, {sc.Mips} mips"
                        + (sc.Slices > 1 ? $", {sc.TexType} x{sc.Slices}" : sc.Depth > 1 ? ", 3D volume" : "");
        NewImage.Source = ImageUtil.LoadPreview(file, 512);

        var size = ImageUtil.ReadImageSize(file);
        NewInfo.Text = $"{Path.GetFileName(file)}" + (size is { } s ? $", {s.W}x{s.H}" : "");
        FormatInfo.Text = $"Encoded as {DxgiFormats.Name(dxgi)} with a full mip chain. {DxgiFormats.UsageHint(dxgi)}";

        bool is_dds = file.EndsWith(".dds", StringComparison.OrdinalIgnoreCase);
        bool fixed_size = sc.IsWad && sc.Streamed; // Only the WAD's low mip copy is available.
        bool resizable = sc.IsTexpack || (sc.IsWad && !sc.Streamed);

        AddSize($"Original size  ({ow}x{oh})", ow, oh, true);

        if (resizable && !fixed_size)
        {
            if (size is {} img && (img.W != ow || img.H != oh))
            {
                bool same_aspect = (long)img.W * oh == (long)img.H * ow;
                AddSize($"Your image's size  ({img.W}x{img.H})" + (same_aspect ? "" : "  (aspect ratio differs)"), img.W, img.H, false);
            }

            if (ow * 2 <= 8192 && oh * 2 <= 8192)
                AddSize($"Double  ({ow * 2}x{oh * 2})", ow * 2, oh * 2, false);

            if (ow >= 8 && oh >= 8)
                AddSize($"Half  ({ow / 2}x{oh / 2})", ow / 2, oh / 2, false);
        }

        if (!item.Streamed && item.Wads.Count > 1)
        {
            AllWads.Visibility = Visibility.Visible;
            AllWads.Content = $"Replace it in all {item.Wads.Count} WADs that contain a copy";
        }

        var problems = new List<string>();
        var notes = new List<string>();

        if (!is_dds && !texconv_available)
            problems.Add("Converting images needs texconv.exe. Open 'Settings' > 'Tools' and download it or pick a DDS already in the right format.");

        if (!is_dds && sc.IsLayered)
            problems.Add(sc.Depth > 1
                ? $"This is a 3D volume texture ({ow}x{oh}x{sc.Depth}). Replace it with a 3D DDS of the same size and format."
                : $"This is a {sc.TexType} texture with {sc.Slices} faces or slices. Replace it with a DDS of the same layout.");

        if (fixed_size)
            notes.Add("Only the WAD's small distant copy of this texture was found (its texture pack isn't in the index). Rebuild the index to edit the full resolution version.");

        if (is_dds)
        {
            try
            {
                var d = DdsFile.LoadHeader(file);

                if (!DxgiFormats.Compatible(dxgi, d.Dxgi))
                    notes.Add($"Your DDS is {DxgiFormats.Name(d.Dxgi)}. It will be reencoded to {DxgiFormats.Name(dxgi)}" + (texconv_available ? "." : ", which needs texconv."));
            }
            catch (Exception ex)
            {
                problems.Add("Can't read the DDS: " + ex.Message);
            }
        }

        if (NewImage.Source == null && !is_dds && !file.EndsWith(".exr", StringComparison.OrdinalIgnoreCase) && !file.EndsWith(".hdr", StringComparison.OrdinalIgnoreCase)
            && !file.EndsWith(".tga", StringComparison.OrdinalIgnoreCase))
            notes.Add("No preview available for this file. texconv will still try to read it.");

        if (problems.Count > 0)
            ShowMessage(string.Join("\n\n", problems), error: true);
        else if (notes.Count > 0)
            ShowMessage(string.Join("\n\n", notes), error: false);

        OkBtn.IsEnabled = problems.Count == 0;
    }

    private void AddSize(string label, int w, int h, bool is_checked)
    {
        var rb = new RadioButton
        {
            Content = label,
            GroupName = "size",
            IsChecked = is_checked,
            Margin = new Thickness(0, 0, 0, 8)
        };

        SizeOptions.Children.Add(rb);
        m_Sizes.Add((rb, w, h));
    }

    private void ShowMessage(string text, bool error)
    {
        MsgBox.Visibility = Visibility.Visible;
        MsgBox.Background = (Brush)FindResource(error ? "DangerSoft" : "WarningSoft");
        MsgText.Foreground = (Brush)FindResource(error ? "Danger" : "Warning");
        MsgText.Text = text;
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        var sel = m_Sizes.FirstOrDefault(s => s.Button.IsChecked == true);

        Request = new ReplaceRequest
        {
            Texture = m_Item,
            SourceFile = m_File,
            Width = sel.W,
            Height = sel.H,
            Quality = (BcQuality)Math.Max(0, QualityBox.SelectedIndex),
            AllWadCopies = AllWads.IsChecked == true,
        };

        DialogResult = true;
    }
}