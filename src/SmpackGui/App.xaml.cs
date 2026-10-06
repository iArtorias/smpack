using System.IO;
using System.Windows;
using System.Windows.Threading;
using SmpackGui.Core;

namespace SmpackGui;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        DispatcherUnhandledException += OnUnhandled;
        AppDomain.CurrentDomain.UnhandledException += (_, a) => WriteCrash(a.ExceptionObject as Exception);

        TaskScheduler.UnobservedTaskException += (_, a) =>
        {
            WriteCrash(a.Exception);
            a.SetObserved();
        };

        try
        {
            var w = new MainWindow();

            MainWindow = w;
            w.Show();
        }
        catch (Exception ex)
        {
            WriteCrash(ex);
            MessageBox.Show($"smpack GUI failed to start:\n\n{ex.Message}\n\nDetails: {Path.Combine(AppSettings.LocalDir, "errors.log")}",
                "smpack GUI", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    private void OnUnhandled(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        WriteCrash(e.Exception);

        if (MainWindow is not { IsVisible: true })
        {
            MessageBox.Show(e.Exception.Message, "smpack GUI", MessageBoxButton.OK, MessageBoxImage.Error);
            e.Handled = true;
            Shutdown(1);
            return;
        }

        if (MainWindow?.DataContext is ViewModels.MainViewModel vm)
            vm.ReportError(e.Exception);
        else
            MessageBox.Show(e.Exception.Message, "smpack GUI", MessageBoxButton.OK, MessageBoxImage.Error);

        e.Handled = true;
    }

    private static void WriteCrash(Exception? ex)
    {
        if (ex == null)
            return;

        try
        {
            Directory.CreateDirectory(AppSettings.LocalDir);
            File.AppendAllText(Path.Combine(AppSettings.LocalDir, "errors.log"), $"[{DateTime.Now:u}] {ex}\n\n");
        }
        catch
        {}
    }
}