using System.Windows.Controls;

namespace SmpackGui.Views;

public partial class ArchivesView : UserControl
{
    public ArchivesView() => InitializeComponent();

    private void OnAutoColumn(object? sender, DataGridAutoGeneratingColumnEventArgs e)
    {
        e.Column.Header = e.PropertyName.Replace('_', ' ').ToUpperInvariant();

        if (e.PropertyName == "name")
            e.Column.Width = new DataGridLength(1, DataGridLengthUnitType.Star);
    }
}