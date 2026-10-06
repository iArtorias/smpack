using System.Diagnostics;
using System.IO;
using System.Windows;
using Microsoft.Win32;
using SmpackGui.Views;

namespace SmpackGui.Services;

public static class Dialogs
{
    public const string ImageFilter =
        "Images|*.png;*.tga;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.dds;*.hdr;*.exr|DDS|*.dds|All files|*.*";

    private static Window? m_Owner => Application.Current?.MainWindow;

    public static string? OpenFile(string title, string filter, string? initial_dir = null)
    {
        var d = new OpenFileDialog 
        { 
            Title = title,
            Filter = filter,
            CheckFileExists = true 
        };

        if (initial_dir != null && Directory.Exists(initial_dir))
            d.InitialDirectory = initial_dir;

        return d.ShowDialog(m_Owner) == true ? d.FileName : null;
    }

    public static string[] OpenFiles(string title, string filter, string? initial_dir = null)
    {
        var d = new OpenFileDialog
        {
            Title = title,
            Filter = filter,
            CheckFileExists = true,
            Multiselect = true
        };

        if (initial_dir != null && Directory.Exists(initial_dir))
            d.InitialDirectory = initial_dir;

        return d.ShowDialog(m_Owner) == true ? d.FileNames : [];
    }

    public static string? SaveFile(string title, string filter, string file_name, string? initial_dir = null)
    {
        var d = new SaveFileDialog
        {
            Title = title,
            Filter = filter,
            FileName = file_name,
            OverwritePrompt = true
        };

        if (initial_dir != null && Directory.Exists(initial_dir))
            d.InitialDirectory = initial_dir;

        return d.ShowDialog(m_Owner) == true ? d.FileName : null;
    }

    public static string? PickFolder(string title, string? initial_dir = null)
    {
        var d = new OpenFolderDialog
        {
            Title = title,
            Multiselect = false
        };

        if (initial_dir != null && Directory.Exists(initial_dir))
            d.InitialDirectory = initial_dir;

        return d.ShowDialog(m_Owner) == true ? d.FolderName : null;
    }

    public static bool Confirm(string title, string message, string ok = "Continue", string cancel = "Cancel", bool danger = false) =>
        MessageDialog.Show(m_Owner, title, message, ok, cancel, danger ? MessageDialog.Kind.DANGER : MessageDialog.Kind.QUESTION);

    public static void Error(string title, string message) =>
        MessageDialog.Show(m_Owner, title, message, "OK", null, MessageDialog.Kind.ERROR);

    public static void Info(string title, string message) =>
        MessageDialog.Show(m_Owner, title, message, "OK", null, MessageDialog.Kind.INFO);

    public static void OpenFolder(string path)
    {
        try
        {
            if (File.Exists(path))
                Process.Start("explorer.exe", $"/select,\"{path}\"");

            else if (Directory.Exists(path))
                Process.Start(new ProcessStartInfo(path)
                { 
                    UseShellExecute = true
                });
        }
        catch
        {
            // Explorer unavailable.
        }
    }

    public static void OpenUrl(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo(url)
            { 
                UseShellExecute = true });
        }
        catch
        {}
    }

    public static void OpenFileExternally(string path)
    {
        try
        {
            Process.Start(new ProcessStartInfo(path)
            {
                UseShellExecute = true });
        }
        catch
        {}
    }
}