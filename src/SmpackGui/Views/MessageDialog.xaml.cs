using System.Windows;
using System.Windows.Media;
using SmpackGui.Services;

namespace SmpackGui.Views;

public partial class MessageDialog : Window
{
    public enum Kind 
    {
        INFO,
        QUESTION,
        ERROR, 
        DANGER
    }

    private MessageDialog()
    {
        InitializeComponent();
        Native.ApplyDarkChrome(this);
    }

    public static bool Show(Window? owner, string title, string message, string ok, string? cancel, Kind kind)
    {
        var d = new MessageDialog
        {
            Title = title
        };

        if (owner != null && owner.IsLoaded)
            d.Owner = owner;
        else
            d.WindowStartupLocation = WindowStartupLocation.CenterScreen;

        d.Heading.Text = title;
        d.Body.Text = message;
        d.OkBtn.Content = ok;

        if (cancel == null)
            d.CancelBtn.Visibility = Visibility.Collapsed;
        else d.CancelBtn.Content = cancel;

        (d.Glyph.Text, var brush) = kind switch
        {
            Kind.ERROR => ("\uEA39", "Danger"),
            Kind.DANGER => ("\uE7BA", "Warning"),
            Kind.QUESTION => ("\uE897", "Accent"),
            _ => ("\uE946", "Accent"),
        };

        d.Glyph.Foreground = (Brush)Application.Current.FindResource(brush);

        if (kind == Kind.DANGER)
            d.OkBtn.Style = (Style)Application.Current.FindResource("DangerButton");

        return d.ShowDialog() == true;
    }

    private void Ok_Click(object sender, RoutedEventArgs e) => DialogResult = true;
    private void Cancel_Click(object sender, RoutedEventArgs e) => DialogResult = false;
}