using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Windows.Data;
using System.Windows.Input;
using SmpackGui.Core;
using SmpackGui.Core.Audio;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

public sealed class AudioPackRow(AudioPack pack, long size, bool modified)
{
    public AudioPack Pack { get; } = pack;
    public string Name => Pack.Name;
    public long Size { get; } = size;
    public bool Modified { get; } = modified;
    public string SizeLabel => ImageUtil.HumanBytes(Size);
}

public sealed class AudioRow(AudioStream s) : ObservableObject
{
    public AudioStream Stream { get; } = s;
    public uint Id => Stream.FileId;
    public long Size => Stream.Size;
    public string SizeLabel => ImageUtil.HumanBytes(Stream.Size);

    private string m_Duration = "";
    public string Duration
    {
        get => m_Duration;
        set => Set(ref m_Duration, value);
    }

    private bool m_IsModded;
    public bool IsModded
    {
        get => m_IsModded;
        set => Set(ref m_IsModded, value);
    }
}

/// <summary>Audio page. Listen to the game's Wwise streams, export them, and replace them in a mod.</summary>
public sealed class AudioViewModel : ObservableObject
{
    private readonly MainViewModel m_Main;
    private CancellationTokenSource? m_PreviewCts;
    private (string Pack, uint Id)? m_PendingReveal;

    public AudioViewModel(MainViewModel main)
    {
        m_Main = main;

        var s = main.Settings;
        Converter = new AudioConverter(
            new Vgmstream(main.Log)
            {
                ExePath = Vgmstream.Locate(s.VgmstreamPath, AppSettings.ToolsDir)
            },
            new WwiseConverter(main.Log)
            {
                ExePath = WwiseConverter.Locate(s.WwisePath)
            },
            AppSettings.CacheDir)
        { 
            Mp3Kbps = s.Mp3Kbps
        };

        Player.Failed += msg => m_Main.Toast($"Playback failed: {msg}", ToastKind.ERROR);

        PackView = CollectionViewSource.GetDefaultView(Packs);
        PackView.Filter = o => o is AudioPackRow p && (PackSearch.Length == 0 || p.Name.Contains(PackSearch, StringComparison.OrdinalIgnoreCase));
        StreamView = CollectionViewSource.GetDefaultView(Streams);
        StreamView.Filter = o => o is AudioRow r && (StreamSearch.Length == 0 || r.Id.ToString().Contains(StreamSearch, StringComparison.Ordinal)) &&
                                 (!OnlyModded || r.IsModded);

        RefreshCommand = new RelayCommand(LoadPacks);

        PlayCommand = new RelayCommand(() =>
        {
            if (Player.HasMedia)
                Player.Toggle();
            else if (Selected != null)
                _ = PreviewAsync(Selected, autoplay: true);
        }, () => Selected != null && Converter.Vgmstream.Available);

        StopCommand = new RelayCommand(Player.Stop, () => Player.HasMedia);
        PlayModCommand = new AsyncCommand(PlayModAsync, () => Selected?.IsModded == true && Converter.Vgmstream.Available);
        ExportMp3Command = new AsyncCommand(() => ExportAsync(AudioExportFormat.MP3), () => Selected != null);
        ExportWavCommand = new AsyncCommand(() => ExportAsync(AudioExportFormat.WAV), () => Selected != null);
        ExportWemCommand = new AsyncCommand(() => ExportAsync(AudioExportFormat.WEM), () => Selected != null);
        ExportPackCommand = new AsyncCommand(ExportPackAsync, () => SelectedPack != null && Streams.Count > 0);
        ReplaceCommand = new AsyncCommand(() => ReplaceAsync(null), () => Selected != null);
        RevertCommand = new RelayCommand(Revert, () => Selected?.IsModded == true);

        CopyIdCommand = new RelayCommand(() =>
        {
            if (Selected != null)
                try
                {
                    System.Windows.Clipboard.SetText(Selected.Id.ToString());
                }
                catch
                {}
        }, () => Selected != null);

        DownloadVgmstreamCommand = new RelayCommand(() => m_Main.SettingsPage.DownloadVgmstreamCommand.Execute(null));
    }

    public AudioConverter Converter { get; }
    public AudioPlayer Player { get; } = new();

    public ICommand RefreshCommand { get; }
    public ICommand PlayCommand { get; }
    public ICommand StopCommand { get; }
    public ICommand PlayModCommand { get; }
    public ICommand ExportMp3Command { get; }
    public ICommand ExportWavCommand { get; }
    public ICommand ExportWemCommand { get; }
    public ICommand ExportPackCommand { get; }
    public ICommand ReplaceCommand { get; }
    public ICommand RevertCommand { get; }
    public ICommand CopyIdCommand { get; }
    public ICommand DownloadVgmstreamCommand { get; }

    public bool ToolsMissing => !Converter.Vgmstream.Available;

    public void NotifyTools()
    {
        OnPropertyChanged(nameof(ToolsMissing));
        CommandManager.InvalidateRequerySuggested();
    }

    // Packs.

    public ObservableCollection<AudioPackRow> Packs { get; } = [];
    public ICollectionView PackView { get; }

    private string m_PackSearch = "";
    public string PackSearch
    {
        get => m_PackSearch;

        set
        {
            if (Set(ref m_PackSearch, value ?? ""))
                PackView.Refresh();
        }
    }

    private bool m_Loaded;

    public void OnGameChanged()
    {
        m_Loaded = false;
        Packs.Clear();
        Streams.Clear();
        Player.Close();
    }

    public void EnsureLoaded()
    {
        if (!m_Loaded)
            LoadPacks();
    }

    private void LoadPacks()
    {
        Packs.Clear();

        var g = m_Main.Game;

        if (g == null)
            return;

        foreach (var p in AudioPack.Enumerate(g))
        {
            long size = 0;
            bool modified = File.Exists(p.TocPath + GameInstall.BackupSuffix);

            for (int i = 0; i < 64; i++)
            {
                var part = p.PartPath(i);

                if (!File.Exists(part))
                    break;

                size += new FileInfo(part).Length;
            }

            Packs.Add(new AudioPackRow(p, size, modified));
        }

        m_Loaded = true;

        OnPropertyChanged(nameof(PackSummary));
        var want = m_PendingReveal?.Pack ?? m_Main.Settings.LastAudioPack;

        if (want != null)
            SelectedPack = Packs.FirstOrDefault(p => p.Name.Equals(want, StringComparison.OrdinalIgnoreCase));
    }

    public string PackSummary => Packs.Count == 0 ? "" : $"{Packs.Count} audio packs, {ImageUtil.HumanBytes(Packs.Sum(p => p.Size))}";

    private AudioPackRow? m_SelectedPack;
    public AudioPackRow? SelectedPack
    {
        get => m_SelectedPack;

        set
        {
            if (!Set(ref m_SelectedPack, value))
                return;

            _ = LoadStreamsAsync();
        }
    }

    // Streams.

    public ObservableCollection<AudioRow> Streams { get; } = [];
    public ICollectionView StreamView { get; }

    private string m_StreamSearch = "";
    public string StreamSearch
    {
        get => m_StreamSearch;

        set
        {
            if (Set(ref m_StreamSearch, value ?? ""))
                StreamView.Refresh();
        }
    }

    private bool m_OnlyModded;
    public bool OnlyModded
    {
        get => m_OnlyModded;

        set
        {
            if (Set(ref m_OnlyModded, value))
                StreamView.Refresh();
        }
    }

    private string m_StreamsInfo = "Pick an audio pack.";
    public string StreamsInfo
    {
        get => m_StreamsInfo;
        private set => Set(ref m_StreamsInfo, value);
    }

    private bool m_LoadingStreams;
    public bool LoadingStreams
    {
        get => m_LoadingStreams;
        private set => Set(ref m_LoadingStreams, value);
    }

    private async Task LoadStreamsAsync()
    {
        Streams.Clear();
        Selected = null;
        Player.Close();

        var p = SelectedPack;

        if (p == null)
            return;

        m_Main.Settings.LastAudioPack = p.Name;
        m_Main.Settings.Save();
        LoadingStreams = true;
        StreamsInfo = "Reading the pack...";

        try
        {
            await p.Pack.LoadAsync(m_Main.Cli);

            if (p != SelectedPack)
                return;

            foreach (var s in p.Pack.Streams.OrderBy(s => s.FileId))
                Streams.Add(new AudioRow(s));

            RefreshModded();

            StreamsInfo = $"{Streams.Count:N0} sounds, named by their Wwise file id" + (p.Modified ? ". A modded copy is installed. This list shows the originals." : "");

            if (m_PendingReveal is { } r && r.Pack.Equals(p.Name, StringComparison.OrdinalIgnoreCase))
            {
                Selected = Streams.FirstOrDefault(x => x.Id == r.Id);
                m_PendingReveal = null;
            }
        }
        catch (Exception ex)
        {
            StreamsInfo = "Could not read this pack.";
            m_Main.ReportError(ex);
        }
        finally
        {
            LoadingStreams = false;
        }
    }

    public void RefreshModded()
    {
        var proj = m_Main.Project.Current;
        var pack = SelectedPack?.Name;

        foreach (var r in Streams)
            r.IsModded = proj != null && pack != null && proj.FindAudio(pack, r.Id) != null;
    }

    private AudioRow? m_Selected;
    public AudioRow? Selected
    {
        get => m_Selected;

        set
        {
            if (!Set(ref m_Selected, value))
                return;

            OnPropertyChanged(nameof(HasSelection));
            OnPropertyChanged(nameof(SelectedTitle));

            Info = "";
            Player.Close();

            if (value != null && Converter.Vgmstream.Available)
                _ = PreviewAsync(value, AutoPlay);
        }
    }

    public bool HasSelection => Selected != null;
    public string SelectedTitle => Selected == null ? "Select a sound" : $"Sound {Selected.Id}";

    private bool m_AutoPlay = true;
    public bool AutoPlay
    { 
        get => m_AutoPlay;
        set => Set(ref m_AutoPlay, value);
    }

    private string m_Info = "";
    public string Info
    {
        get => m_Info;
        private set => Set(ref m_Info, value);
    }

    private bool m_PreviewBusy;
    public bool PreviewBusy
    {
        get => m_PreviewBusy;
        private set => Set(ref m_PreviewBusy, value);
    }

    private string Key(AudioRow r) => $"{SelectedPack!.Name}_{r.Id}_{r.Size}";

    private async Task PreviewAsync(AudioRow row, bool autoplay)
    {
        var pack = SelectedPack;

        if (pack == null)
            return;

        m_PreviewCts?.Cancel();

        var cts = m_PreviewCts = new CancellationTokenSource();
        PreviewBusy = true;

        try
        {
            var wav = await Converter.PreviewWavAsync(Key(row), () => pack.Pack.ReadWem(row.Stream), cts.Token);

            if (cts.IsCancellationRequested || row != Selected)
                return;

            var w = await Task.Run(() => WavFile.Load(wav), cts.Token);
            row.Duration = AudioPlayer.Format(w.Duration.TotalSeconds);
            Info = $"{AudioPlayer.Format(w.Duration.TotalSeconds)} long, {w.SampleRate / 1000.0:0.#} kHz, {ImageUtil.HumanBytes(row.Size)} as .wem (Wwise Vorbis)";
            Player.Load(wav, autoplay);
        }
        catch (OperationCanceledException)
        {}
        catch (Exception ex)
        {
            if (!cts.IsCancellationRequested)
                Info = ex.Message;
        }
        finally
        {
            if (m_PreviewCts == cts)
                PreviewBusy = false;
        }
    }

    private async Task PlayModAsync()
    {
        var row = Selected;
        var proj = m_Main.Project.Current;
        var pack = SelectedPack?.Name;

        if (row == null || proj == null || pack == null)
            return;

        var e = proj.FindAudio(pack, row.Id);

        if (e == null)
            return;

        var wem = proj.WemPath(e);

        if (!File.Exists(wem))
        {
            m_Main.Toast("The replacement .wem is missing. Replace the sound again.", ToastKind.WARNING);
            return;
        }

        var key = $"mod_{pack}_{row.Id}_{File.GetLastWriteTimeUtc(wem).Ticks}";
        var wav = await Converter.PreviewWavAsync(key, () => File.ReadAllBytes(wem));

        Player.Load(wav, autoplay: true);

        Info = $"Playing your replacement ({Path.GetFileName(e.SourceFile)})";
    }

    // Export.

    private async Task ExportAsync(AudioExportFormat fmt)
    {
        var row = Selected;
        var pack = SelectedPack;

        if (row == null || pack == null)
            return;

        if (fmt != AudioExportFormat.WEM && !RequireVgmstream())
            return;

        var ext = fmt switch 
        {
            AudioExportFormat.MP3 => "mp3",
            AudioExportFormat.WAV => "wav",
            _ => "wem"
        };

        var file = Dialogs.SaveFile($"Export sound {row.Id}", $"{ext.ToUpperInvariant()} file|*.{ext}", $"{pack.Name}_{row.Id}.{ext}", m_Main.Settings.LastExportDir);

        if (file == null)
            return;

        m_Main.Settings.LastExportDir = Path.GetDirectoryName(file);
        m_Main.Settings.Save();

        var ok = await m_Main.RunAsync($"Exporting {row.Id} as {ext.ToUpperInvariant()}...", (ct, _) =>
            Converter.ExportAsync(pack.Pack.ReadWem(row.Stream), file, fmt, ct));

        if (ok)
            m_Main.Toast($"Saved {Path.GetFileName(file)}", ToastKind.SUCCESS);
    }

    private async Task ExportPackAsync()
    {
        var pack = SelectedPack;

        if (pack == null || !RequireVgmstream())
            return;

        var rows = StreamView.Cast<AudioRow>().ToList();
        var dir = Dialogs.PickFolder($"Export {rows.Count} sound(s) of {pack.Name} as MP3", m_Main.Settings.LastExportDir);

        if (dir == null)
            return;

        dir = Path.Combine(dir, TextureExporter.SafeName(pack.Name));

        m_Main.Settings.LastExportDir = Path.GetDirectoryName(dir);
        m_Main.Settings.Save();

        int done = 0, failed = 0;
        await m_Main.RunAsync($"Exporting {pack.Name} as MP3...", async (ct, progress) =>
        {
            foreach (var r in rows)
            {
                ct.ThrowIfCancellationRequested();

                try
                {
                    await Converter.ExportAsync(pack.Pack.ReadWem(r.Stream), Path.Combine(dir, $"{r.Id}.mp3"), AudioExportFormat.MP3, ct);
                }
                catch (OperationCanceledException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    failed++;
                    m_Main.Log.Write(LogKind.WARNING, $"{r.Id}: {ex.Message}");
                }

                done++;
                progress.Report(done / (double)rows.Count);
                m_Main.SetBusyText($"Exporting {pack.Name} as MP3... {done}/{rows.Count}");
            }
        });

        if (done > 0)
        {
            m_Main.Toast($"Exported {done - failed} sound(s)" + (failed > 0 ? $", {failed} failed (see log)" : ""), failed > 0 ? ToastKind.WARNING : ToastKind.SUCCESS);
            Dialogs.OpenFolder(dir);
        }
    }

    private bool RequireVgmstream()
    {
        if (Converter.Vgmstream.Available)
            return true;

        if (Dialogs.Confirm("vgmstream needed", "Playing and converting game sounds needs vgmstream. Download it now?", "Download"))
            DownloadVgmstreamCommand.Execute(null);

        return false;
    }

    // Replace.

    public async Task ReplaceAsync(string? file)
    {
        var row = Selected;
        var pack = SelectedPack;

        if (row == null || pack == null)
            return;

        file ??= Dialogs.OpenFile($"Replace sound {row.Id}",
            "Audio files|" + string.Join(";", AudioConverter.ImportExtensions.Select(e => "*" + e)) + "|All files|*.*", m_Main.Settings.LastExportDir);

        if (file == null)
            return;

        if (!file.EndsWith(".wem", StringComparison.OrdinalIgnoreCase) && !Converter.Wwise.Available)
        {
            Dialogs.Error("Wwise needed",
                "Turning MP3, WAV and other files into the game's .wem format needs Audiokinetic Wwise.\n\n" +
                "Install Wwise, or set the path to 'WwiseConsole.exe' in Settings, then try again.");
            return;
        }

        var proj = m_Main.Project.EnsureProject();

        if (proj == null)
            return;

        var entry = new ModAudioEntry
        {
            FileId = row.Id,
            Pack = pack.Name,
            SourceFile = file,
            Added = DateTime.Now
        };

        var ok = await m_Main.RunAsync($"Converting {Path.GetFileName(file)} for sound {row.Id}...", async (ct, _) =>
        {
            // Match the original sample rate so the game's timing stays right.
            int? rate = null;
            if (Converter.Vgmstream.Available && !file.EndsWith(".wem", StringComparison.OrdinalIgnoreCase))
            {
                var tmp = Path.Combine(Path.GetTempPath(), "SmpackGui", $"{row.Id}.wem");
                Directory.CreateDirectory(Path.GetDirectoryName(tmp)!);

                await File.WriteAllBytesAsync(tmp, pack.Pack.ReadWem(row.Stream), ct);

                try
                {
                    rate = (await Converter.Vgmstream.InfoAsync(tmp, ct)).SampleRate;
                }
                finally
                {
                    File.Delete(tmp);
                }
            }

            var dst = proj.WemPath(entry);
            await Converter.ToWemAsync(file, dst, rate, ct);

            entry.Size = new FileInfo(dst).Length;
            proj.UpsertAudio(entry);
        });

        if (!ok)
            return;

        m_Main.Project.Reload();

        RefreshModded();
        OnPropertyChanged(nameof(Selected));

        m_Main.Log.Write(LogKind.SUCCESS, $"sound {row.Id} in {pack.Name} replaced with {Path.GetFileName(file)} (in '{proj.Data.Name}')");
        m_Main.Toast($"Sound {row.Id} replaced. Build the mod on the 'My mod' page to hear it in game.", ToastKind.SUCCESS);
    }

    private void Revert()
    {
        var row = Selected;
        var proj = m_Main.Project.Current;
        var pack = SelectedPack?.Name;

        if (row == null || proj == null || pack == null)
            return;

        var e = proj.FindAudio(pack, row.Id);

        if (e == null)
            return;

        proj.RemoveAudio(e);

        m_Main.Project.Reload();
        RefreshModded();
    }

    /// <summary>Jump here from the 'My mod' page.</summary>
    public void Reveal(string pack, uint id)
    {
        m_PendingReveal = (pack, id);
        m_Main.Page = "audio";

        if (!m_Loaded)
        {
            LoadPacks();
            return;
        }

        var p = Packs.FirstOrDefault(x => x.Name.Equals(pack, StringComparison.OrdinalIgnoreCase));

        if (p == null)
            return;

        if (SelectedPack == p)
        {
            Selected = Streams.FirstOrDefault(x => x.Id == id);
            m_PendingReveal = null;
        }
        else
            SelectedPack = p;
    }
}