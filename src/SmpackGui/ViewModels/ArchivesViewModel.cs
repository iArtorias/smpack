using System.ComponentModel;
using System.Data;
using System.IO;
using System.Windows.Data;
using System.Windows.Input;
using SmpackGui.Core;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Mvvm;
using SmpackGui.Services;
using SmpackGui.Views;

namespace SmpackGui.ViewModels;

/// <summary>Browse every game archive, inspect it, extract it, and rebuild/replace pieces of it.</summary>
public sealed class ArchivesViewModel : ObservableObject
{
    public const string AllKinds = "All types";
    private readonly MainViewModel m_Main;
    private List<GameFile> m_Files = [];
    private CancellationTokenSource? m_LoadCts;
    private bool m_Loaded;

    public ArchivesViewModel(MainViewModel main)
    {
        m_Main = main;

        RefreshCommand = new RelayCommand(() => 
        { 
            m_Loaded = false;
            EnsureLoaded();
        });

        ExtractCommand = new AsyncCommand(ExtractAsync, () => Selected != null);
        RevealCommand = new RelayCommand(() =>
        {
            if (Selected != null)
                Dialogs.OpenFolder(Selected.Path);
        }, () => Selected != null);

        RebuildWadCommand = new AsyncCommand(RebuildWadAsync, () => m_Main.HasGame);
        ReplaceItemCommand = new AsyncCommand(ReplaceItemAsync, () => Selected != null && CanReplaceItem);
        AudioReplaceCommand = new AsyncCommand(AudioReplaceAsync, () => Selected?.Kind == GameFileKind.AUDIOPACK);
        LodPatchCommand = new AsyncCommand(LodPatchAsync, () => Selected?.Kind == GameFileKind.LODPACK);
    }

    public ICommand RefreshCommand { get; }
    public ICommand ExtractCommand { get; }
    public ICommand RevealCommand { get; }
    public ICommand RebuildWadCommand { get; }
    public ICommand ReplaceItemCommand { get; }
    public ICommand AudioReplaceCommand { get; }
    public ICommand LodPatchCommand { get; }

    public ICollectionView? Files
    {
        get;
        private set;
    }
    public List<string> Kinds { get; } = [
        AllKinds,
        "WAD",
        "Texture pack", 
        "Geometry pack", 
        "Animation sets",
        "Shader pack", 
        "Audio pack", 
        "WYP stitching DB", 
        "Data compiler (DCB)"];

    private string m_Kind = AllKinds;
    public string Kind
    {
        get => m_Kind;

        set
        {
            if (Set(ref m_Kind, value))
                Files?.Refresh();
        } }

    private string m_Search = "";
    public string Search
    {
        get => m_Search;

        set
        {
            if (Set(ref m_Search, value))
                Files?.Refresh();
        } }

    public string Summary => m_Files.Count == 0 ? "" : $"{m_Files.Count:N0} files, {ImageUtil.HumanBytes(m_Files.Sum(f => f.Size))}";

    public void OnGameChanged()
    {
        m_Loaded = false;

        if (m_Main.Page == "archives")
            EnsureLoaded();
    }

    public void EnsureLoaded()
    {
        if (m_Loaded || m_Main.Game == null)
            return;

        m_Loaded = true;
        _ = LoadAsync();
    }

    private async Task LoadAsync()
    {
        var g = m_Main.Game!;

        try
        {
            m_Files = await Task.Run(() => g.EnumerateFiles().OrderBy(f => f.Kind).ThenBy(f => f.Name).ToList());
        }
        catch (Exception ex)
        {
            m_Loaded = false;
            m_Main.ReportError(ex);
            return;
        }

        Files = CollectionViewSource.GetDefaultView(m_Files);
        Files.Filter = o => o is GameFile f && (Kind == AllKinds || f.KindLabel == Kind) &&
                            (Search.Length == 0 || f.Name.Contains(Search, StringComparison.OrdinalIgnoreCase));

        OnPropertyChanged(nameof(Files));
        OnPropertyChanged(nameof(Summary));
    }

    // Selection.

    private GameFile? m_Selected;
    public GameFile? Selected
    {
        get => m_Selected;

        set
        {
            if (!Set(ref m_Selected, value))
                return;

            OnPropertyChanged(nameof(HasSelection));
            OnPropertyChanged(nameof(CanReplaceItem));
            OnPropertyChanged(nameof(ReplaceItemLabel));
            OnPropertyChanged(nameof(ReplaceHint));
            OnPropertyChanged(nameof(IsAudio));
            OnPropertyChanged(nameof(IsLod));
            OnPropertyChanged(nameof(IsWad));
            _ = LoadDetailsAsync();
        }
    }

    public bool HasSelection => Selected != null;
    public bool IsAudio => Selected?.Kind == GameFileKind.AUDIOPACK;
    public bool IsLod => Selected?.Kind == GameFileKind.LODPACK;
    public bool IsWad => Selected?.Kind is GameFileKind.WAD or GameFileKind.WYPDB or GameFileKind.DATA_COMPILER;

    private string m_Info = "";
    public string Info
    {
        get => m_Info;
        private set => Set(ref m_Info, value);
    }

    private DataView? m_Contents;
    public DataView? Contents
    {
        get => m_Contents;
        private set => Set(ref m_Contents, value);
    }

    private string m_ContentsFilter = "";
    public string ContentsFilter
    {
        get => m_ContentsFilter;

        set
        {
            if (!Set(ref m_ContentsFilter, value) || Contents == null)
                return;

            var cols = Contents.Table!.Columns.Cast<DataColumn>().Where(c => c.DataType == typeof(string)).Select(c => c.ColumnName).ToList();
            var esc = value.Replace("'", "''").Replace("[", "[[]").Replace("*", "[*]").Replace("%", "[%]");

            Contents.RowFilter = value.Length == 0 || cols.Count == 0 ? "" : string.Join(" OR ", cols.Select(c => $"[{c}] LIKE '%{esc}%'"));
            OnPropertyChanged(nameof(ContentsCount));
        }
    }

    public string ContentsCount => Contents == null ? "" : $"{Contents.Count:N0} entries";

    private DataRowView? m_SelectedRow;
    public DataRowView? SelectedRow
    {
        get => m_SelectedRow;
        set => Set(ref m_SelectedRow, value);
    }

    private bool m_LoadingDetails;
    public bool LoadingDetails
    {
        get => m_LoadingDetails;
        private set => Set(ref m_LoadingDetails, value);
    }

    private async Task LoadDetailsAsync()
    {
        m_LoadCts?.Cancel();

        var cts = m_LoadCts = new CancellationTokenSource();
        var f = Selected;

        Info = "";
        Contents = null;
        m_ContentsFilter = "";

        OnPropertyChanged(nameof(ContentsFilter));

        if (f == null)
            return;

        LoadingDetails = true;

        try
        {
            var info = await m_Main.Archives.InfoAsync(f.Path, cts.Token);

            if (cts.IsCancellationRequested)
                return;

            Info = info;

            if (f.Kind is GameFileKind.WYPDB or GameFileKind.DATA_COMPILER or GameFileKind.WAD or GameFileKind.TEXPACK or GameFileKind.LODPACK
                or GameFileKind.ANIM_SET or GameFileKind.SHADERPACK or GameFileKind.AUDIOPACK)
            {
                var idx = m_Main.Game != null ? AppSettings.IndexPathFor(m_Main.Game.Root) : null;
                var t = await m_Main.Archives.ListAsync(f, idx, cts.Token);

                if (cts.IsCancellationRequested)
                    return;

                Contents = t.DefaultView;
                OnPropertyChanged(nameof(ContentsCount));
            }
        }
        catch (OperationCanceledException)
        {}
        catch (Exception ex)
        {
            if (!cts.IsCancellationRequested)
                Info += $"\n\nerror: {ex.Message}";
        }
        finally
        {
            if (!cts.IsCancellationRequested)
                LoadingDetails = false;
        }
    }

    // Extract.

    private async Task ExtractAsync()
    {
        var f = Selected;

        if (f == null)
            return;

        var dlg = new ExtractDialog(f, Path.Combine(m_Main.ProjectsDir, "_extracted", f.BaseName))
        {
            Owner = System.Windows.Application.Current.MainWindow
        };

        if (dlg.ShowDialog() != true)
            return;

        var ok = await m_Main.RunAsync($"Extracting {f.Name}...", (ct, _) =>
            m_Main.Archives.ExtractAsync(f.Path, dlg.OutputDir, dlg.Options, ct));

        if (ok)
        {
            m_Main.Toast($"Extracted to {dlg.OutputDir}", ToastKind.SUCCESS);
            Dialogs.OpenFolder(dlg.OutputDir);
        }
    }

    // Rebuild and replace.

    private async Task RebuildWadAsync()
    {
        var dir = Dialogs.PickFolder("Extracted WAD folder (with manifest.json)", Path.Combine(m_Main.ProjectsDir, "_extracted"));

        if (dir == null)
            return;

        var manifest = Path.Combine(dir, "manifest.json");

        if (!File.Exists(manifest))
        {
            Dialogs.Error("Not an extracted WAD", "That folder has no manifest.json. Extract a WAD first.");
            return;
        }

        string source = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(manifest))?["source"]?.GetValue<string>() ?? Path.GetFileName(dir) + ".wad";
        source = source.Replace(GameInstall.BackupSuffix, "");

        var out_dir = m_Main.NewOutputDir(Path.GetFileNameWithoutExtension(source));
        var out_file = Path.Combine(out_dir, source);

        var ok = await m_Main.RunAsync($"Packing {source}...", (ct, _) => m_Main.Archives.WadPackAsync(dir, out_file, compress: true, ct));

        if (ok)
            OfferInstall([out_file], m_Main.Game!.WadDir);
    }

    public bool CanReplaceItem => Selected?.Kind is GameFileKind.WAD or GameFileKind.WYPDB or GameFileKind.DATA_COMPILER or GameFileKind.ANIM_SET or GameFileKind.SHADERPACK;

    public string ReplaceItemLabel => Selected?.Kind switch
    {
        GameFileKind.ANIM_SET => "Replace animation set...",
        GameFileKind.SHADERPACK => "Replace shader...",
        _ => "Replace chunk...",
    };

    public string ReplaceHint => Selected?.Kind switch
    {
        GameFileKind.ANIM_SET => "Select a set in the list, then pick an .anmset blob. The matching WAD's stubs are repointed automatically.",
        GameFileKind.SHADERPACK => "Select a shader, then pick signed DXIL or DXBC bytecode.",
        GameFileKind.WAD or GameFileKind.WYPDB or GameFileKind.DATA_COMPILER => "Select a chunk, then pick the replacement .bin. Sizes may change.",
        GameFileKind.AUDIOPACK => "Pick Wwise .wem files named by their decimal file ID.",
        GameFileKind.LODPACK => "Extract with 'blocks', edit blocks\\<hash>.bin, then build a patch lodpack.",
        GameFileKind.TEXPACK => "Use the Textures page to replace textures in this pack.",
        _ => "",
    };

    private async Task ReplaceItemAsync()
    {
        var f = Selected;
        var row = SelectedRow;
        var game = m_Main.Game;

        if (f == null || game == null)
            return;

        if (row == null || !row.Row.Table.Columns.Contains("index"))
        {
            m_Main.Toast("Select an entry in the contents list first.", ToastKind.WARNING);
            return;
        }

        long index = Convert.ToInt64(row["index"]);
        string label = row.Row.Table.Columns.Contains("name") ? $"#{index} {row["name"]}" : $"#{index}";
        var src = GameInstall.Pristine(f.Path);

        switch (f.Kind)
        {
            case GameFileKind.ANIM_SET:
            {
                var blob = Dialogs.OpenFile($"Replacement for {label}", "Anim set blob|*.anmset;*.bin|All files|*.*");

                if (blob == null)
                    return;

                var wad = GameInstall.Pristine(Path.Combine(game.WadDir, f.BaseName + ".wad"));

                if (!File.Exists(wad))
                {
                    Dialogs.Error("WAD missing", $"{f.BaseName}.wad was not found next to the .as file.");
                    return;
                }

                // smpack writes outputs under the input names, so stage pristine copies with the real names.
                var out_dir = m_Main.NewOutputDir(f.BaseName);
                var stage = Path.Combine(out_dir, "_src");

                Directory.CreateDirectory(stage);

                var as_copy = Path.Combine(stage, f.Name);
                var wad_copy = Path.Combine(stage, f.BaseName + ".wad");

                File.Copy(src, as_copy, true);
                File.Copy(wad, wad_copy, true);

                List<string>? outs = null;
                var ok = await m_Main.RunAsync("Replacing animation set...", async (ct, _) =>
                    outs = await m_Main.Archives.AnimReplaceAsync(as_copy, wad_copy, [($"#{index}", blob)], out_dir, keep_identity: true, ct));

                if (ok && outs != null)
                    OfferInstall(outs, game.WadDir);
                break;
            }

            case GameFileKind.SHADERPACK:
            {
                var bin = Dialogs.OpenFile($"Replacement for {label}", "Shader bytecode|*.dxbc;*.dxil;*.cso;*.bin|All files|*.*");

                if (bin == null)
                    return;

                var out_dir = m_Main.NewOutputDir(f.BaseName);
                var out_file = Path.Combine(out_dir, f.Name);

                var ok = await m_Main.RunAsync("Replacing shader...", (ct, _) => m_Main.Archives.ShaderReplaceAsync(src, $"#{index}", bin, out_file, ct));

                if (ok)
                {
                    m_Main.Log.Write(LogKind.WARNING, "D3D12 only loads validly signed DXIL. Sign edited shaders before installing.");
                    OfferInstall([out_file], Path.GetDirectoryName(f.Path)!);
                }
                break;
            }

            default:
            {
                var bin = Dialogs.OpenFile($"Replacement for chunk {label}", "Chunk data|*.bin|All files|*.*");

                if (bin == null)
                    return;

                var out_dir = m_Main.NewOutputDir(f.BaseName);
                var stage = Path.Combine(out_dir, "_src");

                Directory.CreateDirectory(stage);

                var copy = Path.Combine(stage, f.Name);
                File.Copy(src, copy, true);

                string? out_file = null;
                var ok = await m_Main.RunAsync("Replacing chunk...", async (ct, _) => out_file = await m_Main.Archives.WadReplaceAsync(copy, $"#{index}", bin, out_dir, ct));

                if (ok && out_file != null)
                    OfferInstall([out_file], Path.GetDirectoryName(f.Path)!);
                break;
            }
        }
    }

    private async Task AudioReplaceAsync()
    {
        var f = Selected;
        var game = m_Main.Game;

        if (f == null || game == null)
            return;

        var wems = Dialogs.OpenFiles("Wwise streams", "Wwise stream|*.wem|All files|*.*");

        if (wems.Length == 0)
            return;

        bool add = Dialogs.Confirm("New stream IDs", "Allow adding streams whose id is not in the pack yet?", "Allow adding", "Only replace existing");
        var out_dir = m_Main.NewOutputDir(f.BaseName);

        List<string>? outs = null;
        var ok = await m_Main.RunAsync("Rebuilding audio pack...", async (ct, _) => outs = await m_Main.Archives.AudioReplaceAsync(f.Path, wems, out_dir, add, ct));

        if (ok && outs != null)
            OfferInstall(outs, game.SoundDir);
    }

    private async Task LodPatchAsync()
    {
        var f = Selected;
        var game = m_Main.Game;

        if (f == null || game == null)
            return;

        var blocks = Dialogs.OpenFiles("Replacement blocks. File name = 16 digit block hash.", "Geometry block|*.bin");

        if (blocks.Length == 0)
            return;

        var name = InputDialog.Ask(System.Windows.Application.Current.MainWindow, "Patch lodpack", "Name of the patch pack (max 31 chars):",
            Core.Mods.ModProject.SuggestPatchName(f.BaseName + "_geo"));

        if (string.IsNullOrWhiteSpace(name)) 
            return;

        if (Core.Mods.ModProject.ValidatePatchName(name) is {} err)
        {
            Dialogs.Error("Invalid name", err);
            return;
        }

        var out_dir = m_Main.NewOutputDir(name);
        List<string>? outs = null;
        var ok = await m_Main.RunAsync("Building patch lodpack...", async (ct, _) =>
            outs = await m_Main.Archives.LodPatchAsync(GameInstall.Pristine(f.Path), blocks, name, out_dir, ct));

        if (ok && outs != null)
            OfferInstall(outs, game.WadDir, lod_patch: name);
    }

    /// <summary>Show where the result is and optionally copy it into the game (with backups).</summary>
    private void OfferInstall(List<string> files, string dest_dir, string? lod_patch = null)
    {
        var list = string.Join("\n", files.Select(Path.GetFileName));

        if (!Dialogs.Confirm("Done", $"Built:\n{list}\n\nInstall into the game now? Existing files are backed up as *.smpack-orig.", "Install", "Just show files"))
        {
            Dialogs.OpenFolder(files[0]);
            return;
        }

        _ = m_Main.RunAsync("Installing...", async (ct, _) =>
        {
            foreach (var f in files)
                GameInstall.InstallFile(f, dest_dir, s => m_Main.Log.Write(LogKind.OUTPUT, "  " + s));

            if (lod_patch != null)
                await m_Main.Builder.SetPatchAsync(m_Main.Game!, lod_patch, texpack: false, enable: true, ct);

            m_Main.Log.Write(LogKind.SUCCESS, $"installed {files.Count} file(s)");
            m_Main.Toast("Installed", ToastKind.SUCCESS);
            m_Loaded = false;
            EnsureLoaded();
        });
    }
}