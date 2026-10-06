using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;
using SmpackGui.Views;

namespace SmpackGui.ViewModels;

public sealed class ProjectEntryViewModel : ObservableObject
{
    public ModTextureEntry Entry { get; }
    private readonly string m_Dds;

    public ProjectEntryViewModel(ModTextureEntry e, string dds)
    {
        Entry = e;
        m_Dds = dds;
        _ = LoadThumbAsync();
    }

    private BitmapSource? m_Thumb;
    public BitmapSource? Thumb
    {
        get => m_Thumb;
        private set => Set(ref m_Thumb, value);
    }

    public string Name => Entry.Name;
    public string Where => Entry.IsTexpack ? $"Texpack {Entry.Texpack}" + (Entry.Wads.Count > 0 ? $", low mips in {Entry.Wads.Count} WAD(s)" : "") : $"WAD {Entry.Wad}";
    public string SizeLabel => Entry.Width == Entry.OriginalWidth && Entry.Height == Entry.OriginalHeight
        ? $"{Entry.Width}x{Entry.Height}"
        : $"{Entry.OriginalWidth}x{Entry.OriginalHeight} -> {Entry.Width}x{Entry.Height}";
    public string Format => Core.Imaging.DxgiFormats.FriendlyAgc(Entry.Format);
    public string SourceLabel => Path.GetFileName(Entry.SourceImage);
    public bool SourceExists => File.Exists(Entry.SourceImage);

    private async Task LoadThumbAsync() => Thumb = await Task.Run(() => ImageUtil.Thumbnail(m_Dds, 96));
}

public sealed class ProjectAudioViewModel(ModAudioEntry e)
{
    public ModAudioEntry Entry { get; } = e;
    public string Title => $"Sound {Entry.FileId}";
    public string Where => $"Audio pack {Entry.Pack}";
    public string SourceLabel => Path.GetFileName(Entry.SourceFile);
    public string SizeLabel => ImageUtil.HumanBytes(Entry.Size);
}

public sealed class ProjectModelViewModel(ModModelEntry e)
{
    public ModModelEntry Entry { get; } = e;
    public string Title => Entry.Name;
    public string Where => $"WAD {Entry.Wad}, model #{Entry.MeshId}" + (Entry.AllLods ? ", every LOD" : ", only the LODs in the file");
    public string SourceLabel => Path.GetFileName(Entry.SourceFile);
    public bool SourceExists => File.Exists(Entry.SourceFile);
}

public sealed class ProjectViewModel : ObservableObject
{
    private readonly MainViewModel m_Main;

    public ProjectViewModel(MainViewModel main)
    {
        m_Main = main;
        NewCommand = new RelayCommand(() => CreateNew());
        OpenCommand = new RelayCommand(Open);

        OpenRecentCommand = new RelayCommand(p =>
        {
            if (p is string s)
                OpenPath(s);
        });

        CloseCommand = new RelayCommand(Close, () => Current != null);
        BuildCommand = new AsyncCommand(() => BuildAsync(install: false), CanBuild);
        BuildInstallCommand = new AsyncCommand(() => BuildAsync(install: true), CanBuild);
        UninstallCommand = new AsyncCommand(UninstallAsync, () => Current?.Data.Installed != null && m_Main.HasGame);
        PackageCommand = new RelayCommand(Package, () => Current != null && (Directory.Exists(Current.OutputDir) || Directory.Exists(Current.SoundOutputDir)));

        OpenFolderCommand = new RelayCommand(() =>
        {
            if (Current != null)
                Dialogs.OpenFolder(Current.Dir); 
        }, () => Current != null);

        OpenOutputCommand = new RelayCommand(() => 
        {
            if (Current != null)
                Dialogs.OpenFolder(Current.OutputDir);
        }, () => Current != null && Directory.Exists(Current.OutputDir));

        RemoveEntryCommand = new RelayCommand(p => RemoveEntry(p as ProjectEntryViewModel));
        ShowEntryCommand = new RelayCommand(p => ShowEntry(p as ProjectEntryViewModel));

        OpenSourceCommand = new RelayCommand(p =>
        {
            if (p is ProjectEntryViewModel e && e.SourceExists)
                Dialogs.OpenFileExternally(e.Entry.SourceImage);
        });

        RefreshSourceCommand = new AsyncCommand(p => ReapplyAsync(p as ProjectEntryViewModel));

        RemoveAudioCommand = new RelayCommand(p =>
        {
            if (p is not ProjectAudioViewModel a || Current == null)
                return;

            Current.RemoveAudio(a.Entry);
            Reload();
        });

        ShowAudioCommand = new RelayCommand(p =>
        {
            if (p is ProjectAudioViewModel a)
                m_Main.Audio.Reveal(a.Entry.Pack, a.Entry.FileId);
        });

        RemoveModelCommand = new RelayCommand(p =>
        {
            if (p is not ProjectModelViewModel m || Current == null)
                return;

            Current.RemoveModel(m.Entry);
            Reload();
        });

        RefreshModelCommand = new AsyncCommand(p => RefreshModelAsync(p as ProjectModelViewModel));
    }

    public ICommand RemoveModelCommand { get; }
    public ICommand RefreshModelCommand { get; }
    public ObservableCollection<ProjectModelViewModel> ModelEntries { get; } = [];
    public bool HasModels => ModelEntries.Count > 0;

    /// <summary>Copy the source file again after it was edited outside the app.</summary>
    private Task RefreshModelAsync(ProjectModelViewModel? m)
    {
        if (m == null || Current == null)
            return Task.CompletedTask;

        if (!m.SourceExists)
        {
            m_Main.Toast("Source model not found", ToastKind.WARNING);
            return Task.CompletedTask;
        }

        var e = m.Entry;
        Current.AddModel(e.Wad, e.MeshId, e.Name, e.SourceFile, e.AllLods);

        Reload();

        m_Main.Toast($"{e.Name} updated from {Path.GetFileName(e.SourceFile)}", ToastKind.SUCCESS);
        return Task.CompletedTask;
    }

    public ICommand RemoveAudioCommand { get; }
    public ICommand ShowAudioCommand { get; }
    public ObservableCollection<ProjectAudioViewModel> AudioEntries { get; } = [];
    public bool HasAudio => AudioEntries.Count > 0;
    public bool HasTextures => Entries.Count > 0;
    public bool IsEmpty => Current != null && Entries.Count == 0 && AudioEntries.Count == 0 && ModelEntries.Count == 0;

    public ICommand NewCommand { get; }
    public ICommand OpenCommand { get; }
    public ICommand OpenRecentCommand { get; }
    public ICommand CloseCommand { get; }
    public ICommand BuildCommand { get; }
    public ICommand BuildInstallCommand { get; }
    public ICommand UninstallCommand { get; }
    public ICommand PackageCommand { get; }
    public ICommand OpenFolderCommand { get; }
    public ICommand OpenOutputCommand { get; }
    public ICommand RemoveEntryCommand { get; }
    public ICommand ShowEntryCommand { get; }
    public ICommand OpenSourceCommand { get; }
    public ICommand RefreshSourceCommand { get; }

    private ModProject? m_Current;
    public ModProject? Current
    {
        get => m_Current;

        private set
        {
            Set(ref m_Current, value);
            OnPropertyChanged(nameof(HasProject));
            NotifyFields();
        }
    }

    public bool HasProject => Current != null;
    public ObservableCollection<ProjectEntryViewModel> Entries { get; } = [];
    public List<string> Recent => m_Main.Settings.RecentProjects.Where(p => File.Exists(Path.Combine(p, ModProject.FileName))).ToList();

    // Editable fields.

    public string Name
    {
        get => Current?.Data.Name ?? "";

        set
        {
            if (Current == null)
                return;

            Current.Data.Name = value;
            Current.Save();
            OnPropertyChanged();
            OnPropertyChanged(nameof(Title));
        }
    }

    public string PatchName
    {
        get => Current?.Data.PatchName ?? "";

        set
        {
            if (Current == null)
                return;

            Current.Data.PatchName = value.Trim();
            Current.Save();
            OnPropertyChanged();
            OnPropertyChanged(nameof(PatchNameError));
        }
    }

    public string? PatchNameError => Current == null ? null : ModProject.ValidatePatchName(Current.Data.PatchName);

    public string Author
    {
        get => Current?.Data.Author ?? "";

        set
        {
            if (Current == null)
                return;

            Current.Data.Author = value;
            Current.Save();
            OnPropertyChanged();
        }
    }

    public string Description
    {
        get => Current?.Data.Description ?? "";

        set
        {
            if (Current == null)
                return;

            Current.Data.Description = value;
            Current.Save();
            OnPropertyChanged();
        }
    }

    public bool InstallOnBuild
    {
        get => Current?.Data.Options.InstallOnBuild ?? true;

        set
        {
            if (Current == null)
                return;

            Current.Data.Options.InstallOnBuild = value;
            Current.Save();
            OnPropertyChanged();
        }
    }

    public bool SyncWadLowMips
    {
        get => Current?.Data.Options.SyncWadLowMips ?? true;

        set
        {
            if (Current == null)
                return;

            Current.Data.Options.SyncWadLowMips = value;
            Current.Save();
            OnPropertyChanged();
            OnPropertyChanged(nameof(Summary));
        }
    }

    public bool Compress
    {
        get => Current?.Data.Options.Compress ?? true;

        set 
        {
            if (Current == null)
                return;

            Current.Data.Options.Compress = value;
            Current.Save();
            OnPropertyChanged();
        }
    }

    public string Title => Current?.Data.Name ?? "No mod project";

    public string Summary
    {
        get
        {
            if (Current == null)
                return "";

            var d = Current.Data;
            int tp = d.Textures.Count(t => t.IsTexpack), wad = d.Textures.Count - tp;
            var wads = Current.WadsToRebuild(d.Options.SyncWadLowMips).Count;
            var packs = d.Audio.Select(a => a.Pack).Distinct(StringComparer.OrdinalIgnoreCase).Count();
            var tex = $"{d.Textures.Count} texture(s): {tp} streamed into patch texpack \"{d.PatchName}\" and {wad} WAD resident, so {wads} WAD(s) will be rebuilt";

            if (d.Audio.Count > 0)
                tex += $". {d.Audio.Count} sound(s) in {packs} audio pack(s)";

            if (d.Models.Count > 0)
                tex += $". {d.Models.Count} model(s)";

            return tex + ".";
        }
    }

    public bool IsInstalled => Current?.Data.Installed != null;

    public string InstallStatus
    {
        get
        {
            var st = Current?.Data.Installed;

            if (st == null)
                return "Not installed";

            var stale = Current!.Data.Textures.Any(t => t.Added > st.When) || Current.Data.Audio.Any(a => a.Added > st.When) ||
                        Current.Data.Models.Any(m => m.Added > st.When);

            return $"Installed {st.When:g}" + (stale ? ", changed since. Rebuild to apply." : "");
        }
    }

    private void NotifyFields()
    {
        foreach (var p in new[] { nameof(Name), nameof(PatchName), nameof(PatchNameError), nameof(Author), nameof(Description), nameof(InstallOnBuild),
                     nameof(SyncWadLowMips), nameof(Compress), nameof(Title), nameof(Summary), nameof(IsInstalled), nameof(InstallStatus), nameof(Recent), nameof(HasAudio), nameof(HasTextures), nameof(HasModels), nameof(IsEmpty) })
            OnPropertyChanged(p);
    }

    // Lifecycle.

    public void Load(ModProject p)
    {
        Current = p;
        m_Main.Settings.TouchRecent(p.Dir);
        m_Main.Settings.Save();
        Reload();
    }

    /// <summary>Reread entries after the project changed (thumbnails, markers).</summary>
    public void Reload()
    {
        Entries.Clear();
        AudioEntries.Clear();
        ModelEntries.Clear();

        if (Current != null)
        {
            foreach (var m in Current.Data.Models.OrderBy(m => m.Name))
                ModelEntries.Add(new ProjectModelViewModel(m));

            foreach (var e in Current.Data.Textures.OrderBy(t => t.Name))
                Entries.Add(new ProjectEntryViewModel(e, Current.DdsPath(e)));

            foreach (var a in Current.Data.Audio.OrderBy(a => a.Pack).ThenBy(a => a.FileId))
                AudioEntries.Add(new ProjectAudioViewModel(a));
        }

        NotifyFields();
        m_Main.Textures.RefreshModded();
        m_Main.Audio?.RefreshModded();
        m_Main.Models?.RefreshModded();
    }

    /// <summary>Returns the open project, creating one if none is open.</summary>
    public ModProject? EnsureProject() => Current ?? CreateNew();

    private ModProject? CreateNew()
    {
        var name = InputDialog.Ask(Application.Current.MainWindow, "New mod", "Name of your mod:", "My mod");

        if (string.IsNullOrWhiteSpace(name))
            return null;

        var dir = Path.Combine(m_Main.ProjectsDir, TextureExporter.SafeName(name));

        for (int i = 2; Directory.Exists(dir); i++)
            dir = Path.Combine(m_Main.ProjectsDir, $"{TextureExporter.SafeName(name)} ({i})");

        var p = ModProject.Create(dir, name);
        Load(p);

        m_Main.Log.Write(LogKind.SUCCESS, $"created mod project in {dir}");
        m_Main.Toast($"Created mod '{name}'", ToastKind.SUCCESS);
        return p;
    }

    private void Open()
    {
        var f = Dialogs.OpenFile("Open mod project", $"smpack mod|{ModProject.FileName}", m_Main.ProjectsDir);

        if (f != null)
            OpenPath(f);
    }

    private void OpenPath(string path)
    {
        try
        {
            Load(ModProject.Open(path));
            m_Main.Page = "project";
        }
        catch (Exception ex)
        {
            m_Main.ReportError(ex);
        }
    }

    private void Close()
    {
        Current = null;
        m_Main.Settings.LastProject = null;
        m_Main.Settings.Save();
        Reload();
    }

    private bool CanBuild() => Current != null && !Current.IsEmpty && m_Main.HasGame && PatchNameError == null;

    // Build and install.

    private async Task BuildAsync(bool install)
    {
        var p = Current;
        var game = m_Main.Game;

        if (p == null || game == null)
            return;

        BuildResult? result = null;
        var ok = await m_Main.RunAsync(install ? "Building and installing..." : "Building...", async (ct, _) =>
        {
            result = await m_Main.Builder.BuildAsync(p, game, ct);

            if (install)
            {
                m_Main.SetBusyText("Installing into the game...");
                await m_Main.Builder.InstallAsync(p, game, result, ct);
            }
        });

        NotifyFields();

        if (!ok || result == null)
            return;

        m_Main.Toast(install ? $"'{p.Data.Name}' is installed. Launch the game to see it" : $"Built {result.Outputs.Count + result.SoundOutputs.Count} file(s)", ToastKind.SUCCESS);
    }

    private async Task UninstallAsync()
    {
        var p = Current;
        var game = m_Main.Game;

        if (p == null || game == null)
            return;

        if (!Dialogs.Confirm("Uninstall mod", $"Remove '{p.Data.Name}' from the game?\n\nThe patch pack is unregistered and deleted, and modified WADs and audio packs are restored from their backups.",
                "Uninstall", danger: true))
            return;

        await m_Main.RunAsync("Uninstalling...", (ct, _) => m_Main.Builder.UninstallAsync(p, game, ct));

        NotifyFields();
        m_Main.Installed.Refresh();
    }

    private void Package()
    {
        var p = Current;

        if (p == null)
            return;

        var zip = Dialogs.SaveFile("Save mod archive", "Zip archive|*.zip", TextureExporter.SafeName(p.Data.Name) + ".zip");

        if (zip == null)
            return;

        try
        {
            m_Main.Builder.Package(p, zip);
            m_Main.Toast("Saved " + Path.GetFileName(zip), ToastKind.SUCCESS);
            Dialogs.OpenFolder(zip);
        }
        catch (Exception ex)
        {
            m_Main.ReportError(ex);
        }
    }

    private void RemoveEntry(ProjectEntryViewModel? e)
    {
        if (e == null || Current == null)
            return;

        Current.Remove(e.Entry);
        Reload();
    }

    private void ShowEntry(ProjectEntryViewModel? e)
    {
        if (e == null)
            return;

        m_Main.Page = "textures";
        m_Main.Textures.SearchText = e.Entry.Name;
    }

    /// <summary>Rerun the conversion from the edited source image.</summary>
    private async Task ReapplyAsync(ProjectEntryViewModel? e)
    {
        if (e == null || Current == null || !e.SourceExists)
        {
            m_Main.Toast("Source image not found", ToastKind.WARNING);
            return;
        }

        var item = new TextureItem
        {
            Name = e.Entry.Name,
            ContentHash = Core.Models.Json.ParseHex(e.Entry.ContentHash),
            Width = e.Entry.OriginalWidth,
            Height = e.Entry.OriginalHeight,
            Format = e.Entry.Format,
            Texpack = e.Entry.Texpack,
            Streamed = e.Entry.IsTexpack,
        };

        item.Wads.AddRange(e.Entry.IsTexpack ? e.Entry.Wads : [e.Entry.Wad!]);
        var p = Current;

        await m_Main.RunAsync($"Reconverting {e.Name}...", async (ct, _) =>
        {
            var r = await m_Main.Replacer.ReplaceAsync(p, new ReplaceRequest
            {
                Texture = item,
                SourceFile = e.Entry.SourceImage,
                Width = e.Entry.Width,
                Height = e.Entry.Height,
                Quality = m_Main.Settings.Quality,
                AllWadCopies = false,
            }, ct);

            foreach (var w in r.Warnings)
                m_Main.Log.Write(LogKind.WARNING, w);
        });

        Reload();
    }
}