using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using SmpackGui.ViewModels;

namespace SmpackGui.Views;

public partial class AudioView : UserControl
{
    public AudioView() => InitializeComponent();

    private void OnSeekStarted(object sender, DragStartedEventArgs e)
    {
        if (DataContext is AudioViewModel vm)
            vm.Player.BeginSeek();
    }

    private void OnSeekCompleted(object sender, DragCompletedEventArgs e)
    {
        if (DataContext is AudioViewModel vm)
            vm.Player.EndSeek();
    }
}