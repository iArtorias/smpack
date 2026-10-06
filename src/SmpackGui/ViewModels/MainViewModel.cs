using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using SmpackGui.Core;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

public enum ToastKind
{
    INFO,
    SUCCESS,
    WARNING,
    ERROR
}

public sealed class MainViewModel : ObservableObject
{
    public static readonly string[] KnownGameDirs =
    [
        @"C:\Program Files (x86)\Steam\steamapps\common\God of War Ragnarok",
        @"D:\SteamLibrary\steamapps\common\God of War Ragnarok",
        @"E:\SteamLibrary\steamapps\common\God of War Ragnarok",
        @"C:\Program Files\Epic Games\GodofWarRagnarok",
    ];

    public AppSettings Settings { get; }
    public UiLog Log { get; }
    public SmpackCli Cli { get; }
    public Texconv Texconv { get; }
    public TextureExporter Exporter { get; }
    public ArchiveService Archives { get; }
    public ModBuilder Builder { get; }
    public TextureReplacer Replacer { get; }

    public TexturesViewModel Textures { get; }
    public ProjectViewModel Project { get; }
    public ArchivesViewModel ArchivesPage { get; }
    public InstalledViewModel Installed { get; }
    public ModelsViewModel Models { get; }
    public AudioViewModel Audio { get; }
    public SettingsViewModel SettingsPage { get; }

    private readonly DispatcherTimer m_ToastTimer;

    public MainViewModel()
    {
        Settings = AppSettings.Load();

        Log = new UiLog(Application.Current.Dispatcher);
        Cli = new SmpackCli(Settings.LocateSmpack() ?? "smpack.exe", Log);

        Texconv = new Texconv(Log)
        {
            ExePath = Texconv.Locate(Settings.TexconvPath, AppSettings.ToolsDir)
        };

        Exporter = new TextureExporter(Cli, () => Game, AppSettings.CacheDir);
        Archives = new ArchiveService(Cli);
        Builder = new ModBuilder(Cli, Log);
        Replacer = new TextureReplacer(Exporter, Texconv, Log);

        Textures = new TexturesViewModel(this);
        Project = new ProjectViewModel(this);
        ArchivesPage = new ArchivesViewModel(this);
        Installed = new InstalledViewModel(this);
        Models = new ModelsViewModel(this);
        Audio = new AudioViewModel(this);
        SettingsPage = new SettingsViewModel(this);

        m_ToastTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(5)
        };

        m_ToastTimer.Tick += (_, _) =>
        {
            m_ToastTimer.Stop();
            ToastText = null;
        };

        CancelCommand = new RelayCommand(() => m_BusyCts?.Cancel(), () => IsBusy && m_BusyCts != null);
        NavigateCommand = new RelayCommand(p => Page = p as string ?? "textures");
        ToggleLogCommand = new RelayCommand(() => LogExpanded = !LogExpanded);
        ClearLogCommand = new RelayCommand(() => Log.Clear());

        CopyLogCommand = new RelayCommand(() =>
        {
            Log.CopyToClipboard();
            Toast("Log copied to clipboard");
        });

        DismissToastCommand = new RelayCommand(() => ToastText = null);

        AsyncCommand.ErrorHandler = ReportError;
    }

    // Game.

    private GameInstall? m_Game;
    public GameInstall? Game
    {
        get => m_Game;

        private set
        {
            if (Set(ref m_Game, value))
            {
                OnPropertyChanged(nameof(HasGame));
                OnPropertyChanged(nameof(GameLabel));
            }
        }
    }

    public bool HasGame => Game != null;
    public string GameLabel => Game?.Root ?? "No game folder selected";

    public bool TrySetGame(string? dir, bool announce = true)
    {
        var root = GameInstall.FindRoot(dir);

        if (root == null)
        {
            if (announce && dir != null)
            {
                GameInstall.LooksValid(dir, out var why);
                Toast($"Not a God of War Ragnarok folder: {why}", ToastKind.ERROR);
            }

            return false;
        }

        Game = new GameInstall(root);

        Settings.GameDir = root;
        Settings.Save();

        ApplyStorage();

        // A smpack.exe dropped into the game folder is a valid fallback.
        if (!Cli.Exists)
        {
            var s = Settings.LocateSmpack();

            if (s != null)
                Cli.ExePath = s;
        }

        Log.Write(LogKind.INFO, $"game folder: {root}");

        Textures.OnGameChanged();
        ArchivesPage.OnGameChanged();
        Installed.OnGameChanged();
        Models.OnGameChanged();
        Audio.OnGameChanged();
        SettingsPage.Notify();
        return true;
    }

    public async Task InitializeAsync()
    {
        Log.Write(LogKind.INFO, "smpack GUI started");

        await CheckSmpackAsync();

        if (!TrySetGame(Settings.GameDir, announce: false))
            foreach (var d in KnownGameDirs)
                if (TrySetGame(d, announce: false))
                    break;

        if (!HasGame)
            Page = "settings";

        if (Settings.LastProject != null && File.Exists(Path.Combine(Settings.LastProject, ModProject.FileName)))
        {
            try
            {
                Project.Load(ModProject.Open(Settings.LastProject));
            }
            catch (Exception ex)
            {
                Log.Write(LogKind.WARNING, $"could not reopen last project: {ex.Message}");
            }
        }

        if (!Texconv.Available)
            Log.Write(LogKind.WARNING, "texconv.exe not found. PNG and TGA import need it (in 'Settings' -> 'Tools', download texconv).");

        SettingsPage.Notify();
    }

    private string m_SmpackStatus = "";
    public string SmpackStatus
    {
        get => m_SmpackStatus;
        set => Set(ref m_SmpackStatus, value);
    }
    private bool m_SmpackOk;
    public bool SmpackOk
    {
        get => m_SmpackOk;
        set => Set(ref m_SmpackOk, value);
    }

    public async Task CheckSmpackAsync()
    {
        if (!Cli.Exists)
        {
            SmpackOk = false;
            SmpackStatus = "smpack.exe not found";
            Log.Write(LogKind.ERROR, "smpack.exe not found. Set its location in 'Settings'.");
            return;
        }
        try
        {
            var v = await Cli.GetVersionAsync();
            SmpackOk = v != null && v >= SmpackCli.MinVersion;
            SmpackStatus = v == null ? "unknown version" : $"smpack {v.ToString(3)}";

            if (!SmpackOk)
                Log.Write(LogKind.WARNING, $"{Cli.ExePath} is {SmpackStatus}. This GUI needs {SmpackCli.MinVersion.ToString(3)}+ (use the bundled smpack.exe).");
            else
                Log.Write(LogKind.INFO, $"using {Cli.ExePath} ({SmpackStatus})");
        }
        catch (Exception ex)
        {
            SmpackOk = false;
            SmpackStatus = "failed to start";
            Log.Write(LogKind.ERROR, ex.Message);
        }
    }

    // Navigation.

    private string m_Page = "textures";
    public string Page
    {
        get => m_Page;

        set
        {
            if (!Set(ref m_Page, value))
                return;

            OnPropertyChanged(nameof(CurrentPage));

            if (value == "installed")
                Installed.Refresh();

            if (value == "archives")
                ArchivesPage.EnsureLoaded();

            if (value == "models")
                Models.EnsureLoaded();

            if (value == "audio")
                Audio.EnsureLoaded();
        }
    }

    public object CurrentPage => Page switch
    {
        "project" => Project,
        "archives" => ArchivesPage,
        "installed" => Installed,
        "models" => Models,
        "audio" => Audio,
        "settings" => SettingsPage,
        _ => Textures,
    };

    public ICommand NavigateCommand { get; }

    // Busy and progress.

    private bool m_IsBusy;
    public bool IsBusy
    {
        get => m_IsBusy;
        private set => Set(ref m_IsBusy, value);
    }
    private string m_BusyText = "";
    public string BusyText
    {
        get => m_BusyText;
        private set => Set(ref m_BusyText, value);
    }
    private double m_Progress;
    public double Progress
    {
        get => m_Progress;
        private set => Set(ref m_Progress, value);
    }
    private bool m_Indeterminate = true;
    public bool Indeterminate
    {
        get => m_Indeterminate;
        private set => Set(ref m_Indeterminate, value);
    }
    private CancellationTokenSource? m_BusyCts;
    public ICommand CancelCommand { get; }

    /// <summary>Run a long operation with the status bar spinner. Returns 'false' if it failed or was cancelled.</summary>
    public async Task<bool> RunAsync(string text, Func<CancellationToken, IProgress<double>, Task> work)
    {
        if (IsBusy)
        {
            Toast("Another operation is still running.", ToastKind.WARNING);
            return false;
        }

        m_BusyCts = new CancellationTokenSource();

        IsBusy = true;
        BusyText = text;
        Indeterminate = true;
        Progress = 0;

        var progress = new Progress<double>(p => 
        { 
            Indeterminate = false;
            Progress = Math.Clamp(p, 0, 1) * 100;
        });

        try
        {
            await work(m_BusyCts.Token, progress);
            return true;
        }
        catch (OperationCanceledException)
        {
            Log.Write(LogKind.WARNING, "cancelled");
            Toast("Cancelled", ToastKind.WARNING);
            return false;
        }
        catch (Exception ex)
        {
            ReportError(ex);
            return false;
        }
        finally
        {
            IsBusy = false;
            BusyText = "";
            m_BusyCts.Dispose();
            m_BusyCts = null;

            CommandManager.InvalidateRequerySuggested();
        }
    }

    public void SetBusyText(string text) => Application.Current.Dispatcher.Invoke(() => BusyText = text);

    public void ReportError(Exception ex)
    {
        var root = ex is AggregateException ag ? ag.InnerExceptions[0] : ex;
        var msg = root.Message;

        if (root is System.Windows.Markup.XamlParseException && root.InnerException != null)
            msg += ": " + root.InnerException.Message;

        try
        {
            Directory.CreateDirectory(AppSettings.LocalDir);
            File.AppendAllText(Path.Combine(AppSettings.LocalDir, "errors.log"), $"[{DateTime.Now:u}] {ex}\n\n");
        }
        catch
        {}

        Log.Write(LogKind.ERROR, msg);
        Toast(msg, ToastKind.ERROR);
    }

    // Toast.

    private string? m_ToastText;
    public string? ToastText
    {
        get => m_ToastText;
        private set => Set(ref m_ToastText, value);
    }
    private ToastKind m_ToastKind;
    public ToastKind ToastKind
    {
        get => m_ToastKind;
        private set => Set(ref m_ToastKind, value);
    }
    public ICommand DismissToastCommand { get; }

    public void Toast(string text, ToastKind kind = ToastKind.INFO)
    {
        Application.Current.Dispatcher.Invoke(() =>
        {
            ToastKind = kind;
            ToastText = text;
            m_ToastTimer.Stop();
            m_ToastTimer.Interval = TimeSpan.FromSeconds(kind == ToastKind.ERROR ? 9 : 4.5);
            m_ToastTimer.Start();
        });
    }

    // Log panel.

    public bool LogExpanded
    {
        get => Settings.LogExpanded;

        set
        {
            Settings.LogExpanded = value;
            OnPropertyChanged();
        }
    }

    public ICommand ToggleLogCommand { get; }
    public ICommand ClearLogCommand { get; }
    public ICommand CopyLogCommand { get; }

    public string ProjectsDir
    {
        get
        {
            var d = Settings.ProjectsDir;

            if (string.IsNullOrWhiteSpace(d))
                d = AppSettings.GameModsDir ?? AppSettings.DefaultProjectsDir;

            Directory.CreateDirectory(d);
            return d;
        }
    }

    /// <summary>Point the cache (and the default mod folder) at '<game>\smpack' or the user profile.</summary>
    public void ApplyStorage()
    {
        string? root = null;
        if (Settings.StoreInGameFolder && Game != null)
        {
            var candidate = Path.Combine(Game.Root, "smpack");

            if (AppSettings.CanWrite(candidate))
                root = candidate;
            else
                Log.Write(LogKind.WARNING, $"can't write to {candidate}. Keeping the cache in {AppSettings.LocalDir}");
        }

        // Carry the texture index over so switching locations doesn't force a rebuild.
        string? old_index = Game != null ? AppSettings.IndexPathFor(Game.Root) : null;
        AppSettings.GameStorageRoot = root;

        if (Game != null && old_index != null)
        {
            var new_index = AppSettings.IndexPathFor(Game.Root);

            try
            {
                if (!File.Exists(new_index) && File.Exists(old_index))
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(new_index)!);
                    File.Copy(old_index, new_index);
                }
                else if (!File.Exists(new_index))
                {
                    // First run after the switch. Pick up an index from the profile cache.
                    var legacy = Path.Combine(AppSettings.LocalDir, "cache", Path.GetFileName(new_index));

                    if (File.Exists(legacy))
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(new_index)!);
                        File.Copy(legacy, new_index);
                    }
                }
            }
            catch (IOException)
            {}
            catch (UnauthorizedAccessException)
            {}
        }

        Exporter.CacheRoot = AppSettings.CacheDir;
        Audio.Converter.CacheRoot = AppSettings.CacheDir;

        Log.Write(LogKind.INFO, $"cache: {AppSettings.CacheDir}, mods: {ProjectsDir}");
        SettingsPage?.Notify();
    }

    public string NewOutputDir(string label)
    {
        var d = Path.Combine(ProjectsDir, "_output", $"{TextureExporter.SafeName(label)}_{DateTime.Now:yyyyMMdd_HHmmss}");
        Directory.CreateDirectory(d);
        return d;
    }
}