using System.Windows;
using SmpackGui.Services;

namespace SmpackGui.Views;

public partial class InputDialog : Window
{
    private InputDialog()
    {
        InitializeComponent();
        Native.ApplyDarkChrome(this);

        Loaded += (_, _) =>
        {
            Input.Focus();
            Input.SelectAll();
        };
    }

    public static string? Ask(Window? owner, string title, string prompt, string initial = "")
    {
        var d = new InputDialog
        {
            Title = title
        };

        if (owner != null && owner.IsLoaded)
            d.Owner = owner;
        else
            d.WindowStartupLocation = WindowStartupLocation.CenterScreen;

        d.Heading.Text = title;
        d.Prompt.Text = prompt;
        d.Input.Text = initial;
        return d.ShowDialog() == true ? d.Input.Text.Trim() : null;
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(Input.Text))
            return;

        DialogResult = true;
    }
}