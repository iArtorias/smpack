using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using SmpackGui.ViewModels;

namespace SmpackGui.Views;

public partial class TexturesView : UserControl
{
    public TexturesView()
    {
        InitializeComponent();

        Viewer.PixelHover += OnHover;
        Viewer.PixelLeave += () => HoverText.Text = " ";

        Viewer.FileDropped += async f =>
        {
            if (DataContext is TexturesViewModel vm && vm.Selected != null)
                await vm.ReplaceAsync(f);
        };

        InputBindings.Add(new KeyBinding(new Mvvm.RelayCommand(() =>
        {
            Search.Focus();
            Search.SelectAll();
        }), Key.F, ModifierKeys.Control));
    }

    private TexturesViewModel? m_Vm => DataContext as TexturesViewModel;

    private void OnHover(int x, int y)
    {
        var p = m_Vm?.Preview;

        if (p == null)
            return;

        var px = p.PixelAt(x, y);

        if (px == null)
        {
            HoverText.Text = " ";
            return;
        }

        var (r, g, b, a) = px.Value;
        var text = $"x {x,5}  y {y,5}    R {r,3}  G {g,3}  B {b,3}  A {a,3}    #{r:X2}{g:X2}{b:X2}{a:X2}";

        if (p.HdrAt(x, y) is {} h)
            text += $"    linear {h.R:0.###} {h.G:0.###} {h.B:0.###}";

        HoverText.Text = text;
    }

    private void Grid_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (m_Vm == null)
            return;

        m_Vm.SelectedItems = Grid.SelectedItems;
        m_Vm.SelectedCount = Grid.SelectedItems.Count;

        CommandManager.InvalidateRequerySuggested();
    }

    private void Grid_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is FrameworkElement fe && fe.DataContext is TextureRow && m_Vm?.ReplaceCommand.CanExecute(null) == true)
            m_Vm.ReplaceCommand.Execute(null);
    }
}