using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace SmpackGui.Services;

internal static class Native
{
    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    /// <summary>Dark title bar and rounded corners (Win11) and a caption color matching the theme.</summary>
    public static void ApplyDarkChrome(Window w)
    {
        void Apply()
        {
            var hwnd = new WindowInteropHelper(w).Handle;

            if (hwnd == IntPtr.Zero)
                return;

            int on = 1;
            if (DwmSetWindowAttribute(hwnd, 20, ref on, sizeof(int)) != 0)
                DwmSetWindowAttribute(hwnd, 19, ref on, sizeof(int));

            int caption = 0x00181410;
            DwmSetWindowAttribute(hwnd, 35, ref caption, sizeof(int));

            int round = 2;
            DwmSetWindowAttribute(hwnd, 33, ref round, sizeof(int));
        }

        if (new WindowInteropHelper(w).Handle != IntPtr.Zero)
            Apply();
        else
            w.SourceInitialized += (_, _) => Apply();
    }
}