using System.Windows;
using SmpackGui.Services;
using SmpackGui.ViewModels;

namespace SmpackGui;

public partial class MainWindow : Window
{
    private readonly MainViewModel m_Vm;

    public MainWindow()
    {
        InitializeComponent();
        Native.ApplyDarkChrome(this);

        m_Vm = new MainViewModel();

        DataContext = m_Vm;
        Width = m_Vm.Settings.WindowWidth;
        Height = m_Vm.Settings.WindowHeight;

        if (m_Vm.Settings.WindowMaximized)
            WindowState = WindowState.Maximized;

        m_Vm.Log.Appended += _ =>
        {
            if (LogList.Items.Count > 0)
                LogList.ScrollIntoView(LogList.Items[^1]);
        };

        Loaded += async (_, _) => await m_Vm.InitializeAsync();

        Closing += (_, _) =>
        {
            m_Vm.Settings.WindowMaximized = WindowState == WindowState.Maximized;

            if (WindowState == WindowState.Normal)
            {
                m_Vm.Settings.WindowWidth = ActualWidth;
                m_Vm.Settings.WindowHeight = ActualHeight;
            }

            m_Vm.Settings.Save();
        };
    }
}