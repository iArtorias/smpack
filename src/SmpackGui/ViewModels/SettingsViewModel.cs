using System.IO;
using System.Windows.Input;
using SmpackGui.Core;
using SmpackGui.Core.Audio;
using SmpackGui.Core.Game;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

public sealed class SettingsViewModel : ObservableObject
{
    private readonly MainViewModel m_Main;

    public SettingsViewModel(MainViewModel main)
    {
        m_Main = main;

        BrowseGameCommand = new RelayCommand(() =>
        {
            var d = Dialogs.PickFolder("God of War Ragnarok folder", m_Main.Game?.Root);

            if (d != null && m_Main.TrySetGame(d))
            {
                Notify();
                m_Main.Toast("Game folder set", ToastKind.SUCCESS);
            }
        });

        UseGameCommand = new RelayCommand(p =>
        {
            if (p is string d && m_Main.TrySetGame(d))
                Notify();
        });

        BrowseSmpackCommand = new AsyncCommand(async () =>
        {
            var f = Dialogs.OpenFile("Locate smpack.exe", "smpack.exe|smpack.exe|Programs|*.exe");

            if (f == null)
                return;

            m_Main.Settings.SmpackPath = f;
            m_Main.Settings.Save();
            m_Main.Cli.ExePath = f;

            await m_Main.CheckSmpackAsync();
            Notify();
        });

        BrowseTexconvCommand = new RelayCommand(() =>
        {
            var f = Dialogs.OpenFile("Locate texconv.exe", "texconv.exe|texconv.exe|Programs|*.exe");

            if (f == null)
                return;

            m_Main.Settings.TexconvPath = f;
            m_Main.Settings.Save();
            m_Main.Texconv.ExePath = f;
            Notify();
        });

        DownloadTexconvCommand = new AsyncCommand(DownloadTexconvAsync);
        OpenTexconvPageCommand = new RelayCommand(() => Dialogs.OpenUrl(Texconv.ProjectUrl));
        BrowseProjectsCommand = new RelayCommand(() =>
        {
            var d = Dialogs.PickFolder("Folder for mod projects", m_Main.ProjectsDir);

            if (d == null)
                return;

            m_Main.Settings.ProjectsDir = d;
            m_Main.Settings.Save();
            Notify();
        });

        ClearCacheCommand = new RelayCommand(() =>
        {
            // The audio preview player holds a cached WAV open.
            m_Main.Audio.Player.Close();

            try
            {
                m_Main.Exporter.ClearCache();
                m_Main.Toast("Preview cache cleared");
            }
            catch (Exception ex)
            {
                m_Main.ReportError(ex);
            }

            Notify();
        });

        OpenCacheCommand = new RelayCommand(() =>
        {
            Directory.CreateDirectory(AppSettings.CacheDir);
            Dialogs.OpenFolder(AppSettings.CacheDir);
        });

        DownloadVgmstreamCommand = new AsyncCommand(DownloadVgmstreamAsync);

        BrowseVgmstreamCommand = new RelayCommand(() =>
        {
            var f = Dialogs.OpenFile("Locate vgmstream-cli.exe", "vgmstream-cli.exe|vgmstream-cli.exe|Programs|*.exe");

            if (f == null)
                return;

            m_Main.Settings.VgmstreamPath = f;
            m_Main.Settings.Save();
            m_Main.Audio.Converter.Vgmstream.ExePath = f;
            Notify();
        });

        BrowseWwiseCommand = new RelayCommand(() =>
        {
            var f = Dialogs.OpenFile("Locate WwiseConsole.exe", "WwiseConsole.exe|WwiseConsole.exe|Programs|*.exe");

            if (f == null)
                return;

            m_Main.Settings.WwisePath = f;
            m_Main.Settings.Save();
            m_Main.Audio.Converter.Wwise.ExePath = f;
            Notify();
        });

        OpenVgmstreamPageCommand = new RelayCommand(() => Dialogs.OpenUrl(Vgmstream.ProjectUrl));
        OpenWwisePageCommand = new RelayCommand(() => Dialogs.OpenUrl(WwiseConverter.ProjectUrl));

        OpenGameCommand = new RelayCommand(() =>
        {
            if (m_Main.Game != null)
                Dialogs.OpenFolder(m_Main.Game.WadDir); }, () => m_Main.HasGame);
    }

    public ICommand BrowseGameCommand { get; }
    public ICommand UseGameCommand { get; }
    public ICommand BrowseSmpackCommand { get; }
    public ICommand BrowseTexconvCommand { get; }
    public ICommand DownloadTexconvCommand { get; }
    public ICommand OpenTexconvPageCommand { get; }
    public ICommand BrowseProjectsCommand { get; }
    public ICommand ClearCacheCommand { get; }
    public ICommand OpenCacheCommand { get; }
    public ICommand OpenGameCommand { get; }
    public ICommand DownloadVgmstreamCommand { get; }
    public ICommand BrowseVgmstreamCommand { get; }
    public ICommand BrowseWwiseCommand { get; }
    public ICommand OpenVgmstreamPageCommand { get; }
    public ICommand OpenWwisePageCommand { get; }

    public string VgmstreamPath => m_Main.Audio.Converter.Vgmstream.ExePath ?? "";
    public bool VgmstreamOk => m_Main.Audio.Converter.Vgmstream.Available;
    public string VgmstreamStatus => VgmstreamOk ? "ready" : "not installed. Needed to play and convert game sounds";
    public string WwisePath => m_Main.Audio.Converter.Wwise.ExePath ?? "";
    public bool WwiseOk => m_Main.Audio.Converter.Wwise.Available;
    public string WwiseStatus => WwiseOk ? "found" : "not found. Needed to turn MP3 or WAV into .wem";

    public IReadOnlyList<int> BitrateChoices { get; } = [128, 160, 192, 256, 320];

    public int Mp3Kbps
    {
        get => m_Main.Settings.Mp3Kbps;

        set
        {
            m_Main.Settings.Mp3Kbps = value;
            m_Main.Settings.Save();
            m_Main.Audio.Converter.Mp3Kbps = value;

            OnPropertyChanged();
        }
    }

    private async Task DownloadVgmstreamAsync()
    {
        var ok = await m_Main.RunAsync("Downloading vgmstream...", async (ct, progress) =>
        {
            var path = await Vgmstream.DownloadAsync(AppSettings.ToolsDir, progress, ct);

            m_Main.Audio.Converter.Vgmstream.ExePath = path;
            m_Main.Settings.VgmstreamPath = path;
            m_Main.Settings.Save();
        });

        Notify();
        m_Main.Audio.NotifyTools();

        if (ok)
            m_Main.Toast("vgmstream installed", ToastKind.SUCCESS);
    }

    public MainViewModel Main => m_Main;
    public string GameDir => m_Main.Game?.Root ?? "";
    public bool HasGame => m_Main.HasGame;
    public string SmpackPath => m_Main.Cli.ExePath;
    public string SmpackStatus => m_Main.SmpackStatus;
    public bool SmpackOk => m_Main.SmpackOk;
    public string TexconvPath => m_Main.Texconv.ExePath ?? "";
    public bool TexconvOk => m_Main.Texconv.Available;
    public string TexconvStatus => TexconvOk ? "ready" : "not installed. Needed to import PNG, TGA or JPG";
    public string ProjectsDir => m_Main.ProjectsDir;
    public string CacheDir => AppSettings.CacheDir;

    public bool StoreInGameFolder
    {
        get => m_Main.Settings.StoreInGameFolder;

        set
        {
            m_Main.Settings.StoreInGameFolder = value;
            m_Main.Settings.Save();
            m_Main.ApplyStorage();
            m_Main.Textures.OnGameChanged();

            OnPropertyChanged();
            Notify();
        }
    }
    private string m_CacheSize = "";
    /// <summary>Size of the preview caches, measured in the background.</summary>
    public string CacheSize => m_CacheSize;

    private int m_SizeVersion;

    private async void RefreshCacheSize()
    {
        int v = ++m_SizeVersion;

        try
        {
            var size = await Task.Run(m_Main.Exporter.CacheSize);

            if (v != m_SizeVersion)
                return;

            m_CacheSize = ImageUtil.HumanBytes(size);
        }
        catch (Exception)
        {
            m_CacheSize = "?";
        }

        OnPropertyChanged(nameof(CacheSize));
    }

    /// <summary>Known install locations that exist on this PC.</summary>
    public List<string> Detected => MainViewModel.KnownGameDirs.Where(d => GameInstall.LooksValid(d, out _)).ToList();

    public BcQuality Quality
    {
        get => m_Main.Settings.Quality;

        set
        {
            m_Main.Settings.Quality = value;
            m_Main.Settings.Save();

            OnPropertyChanged();
        }
    }

    private double m_DownloadProgress;
    public double DownloadProgress
    {
        get => m_DownloadProgress;
        set => Set(ref m_DownloadProgress, value);
    }

    private async Task DownloadTexconvAsync()
    {
        var ok = await m_Main.RunAsync("Downloading texconv (Microsoft DirectXTex)...", async (ct, progress) =>
        {
            var path = await Texconv.DownloadAsync(AppSettings.ToolsDir, progress, ct);

            m_Main.Texconv.ExePath = path;
            m_Main.Settings.TexconvPath = path;
            m_Main.Settings.Save();
        });

        Notify();

        if (ok)
            m_Main.Toast("texconv installed", ToastKind.SUCCESS);
    }

    public void Notify()
    {
        if (m_Main.Audio == null)
            return;

        foreach (var p in new[] { nameof(GameDir), nameof(HasGame), nameof(SmpackPath), nameof(SmpackStatus), nameof(SmpackOk), nameof(TexconvPath),
                     nameof(TexconvOk), nameof(TexconvStatus), nameof(ProjectsDir), nameof(CacheDir), nameof(StoreInGameFolder), nameof(Detected), nameof(VgmstreamPath), nameof(VgmstreamOk), nameof(VgmstreamStatus),
                     nameof(WwisePath), nameof(WwiseOk), nameof(WwiseStatus) })
            OnPropertyChanged(p);

        RefreshCacheSize();
    }
}