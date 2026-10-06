using System.Windows;
using SmpackGui.Core.Game;
using SmpackGui.Services;

namespace SmpackGui.Views;

public partial class ExtractDialog : Window
{
    public string OutputDir => OutDir.Text.Trim();
    public ExtractOptions Options
    {
        get;
        private set;
    } = new();

    public ExtractDialog(GameFile file, string default_out)
    {
        InitializeComponent();
        Native.ApplyDarkChrome(this);

        Heading.Text = $"Extract {file.Name}";
        OutDir.Text = default_out;

        bool wad = file.Kind is GameFileKind.WAD or GameFileKind.WYPDB or GameFileKind.DATA_COMPILER;
        NoDds.IsEnabled = wad;
        Raw.IsEnabled = file.Kind == GameFileKind.TEXPACK;
        Blocks.IsEnabled = file.Kind == GameFileKind.LODPACK;
        Blocks.IsChecked = Blocks.IsEnabled;
    }

    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        var d = Dialogs.PickFolder("Extract to...");

        if (d != null)
            OutDir.Text = d;
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        if (OutputDir.Length == 0)
            return;

        Options = new ExtractOptions
        {
            Filter = Filter.Text.Trim(),
            NoDds = NoDds.IsChecked == true,
            Raw = Raw.IsChecked == true,
            Blocks = Blocks.IsChecked == true,
        };

        DialogResult = true;
    }
}