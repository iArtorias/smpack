using System.Collections;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Threading;
using SmpackGui.Core;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Imaging;
using SmpackGui.Core.Models;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;
using SmpackGui.Views;

namespace SmpackGui.ViewModels;

public sealed class TextureRow(TextureItem item) : ObservableObject
{
    public TextureItem Item { get; } = item;
    private bool m_IsModded;
    public bool IsModded
    {
        get => m_IsModded;
        set => Set(ref m_IsModded, value);
    }
    public string Name => Item.Name;
    public string SizeLabel => Item.SizeLabel;
    public long Pixels => Item.Pixels;
    public string FormatLabel => Item.FormatLabel;
    public string Location => Item.Location;
    public int WadCount => Item.Wads.Count;
}

public sealed class TexturesViewModel : ObservableObject
{
    public const string AllPacks = "All locations";
    public const string WadOnly = "WAD resident only";
    public const string AllFormats = "All formats";

    private readonly MainViewModel m_Main;
    private List<TextureRow> m_Rows = [];
    private readonly Dictionary<string, TextureRow> m_ByKey = new();
    private readonly DispatcherTimer m_SearchDebounce;
    private CancellationTokenSource? m_PreviewCts;

    public TexturesViewModel(MainViewModel main)
    {
        m_Main = main;

        m_SearchDebounce = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(250)
        };

        m_SearchDebounce.Tick += (_, _) =>
        {
            m_SearchDebounce.Stop();
            View?.Refresh();
            OnPropertyChanged(nameof(CountLabel));
        };

        BuildIndexCommand = new AsyncCommand(BuildIndexAsync, () => m_Main.HasGame && !m_Main.IsBusy);
        ReplaceCommand = new AsyncCommand(() => ReplaceAsync(null), () => Selected != null);
        ReplaceFolderCommand = new AsyncCommand(ReplaceFromFolderAsync, () => IsIndexLoaded);
        ExportPngCommand = new AsyncCommand(() => ExportSelectedAsync(png: true), () => SelectedCount > 0);
        ExportDdsCommand = new AsyncCommand(() => ExportSelectedAsync(png: false), () => SelectedCount > 0);
        ExportViewCommand = new RelayCommand(ExportCurrentView, () => Preview.Bitmap != null);
        RevertCommand = new RelayCommand(Revert, () => Selected?.IsModded == true);

        CopyNameCommand = new RelayCommand(() =>
        {
            if (Selected != null)
                try
                {
                    Clipboard.SetText(Selected.Name);
                }
                catch
                {}
        }, () => Selected != null);

        CopyHashCommand = new RelayCommand(() =>
        {
            if (Selected != null)
                try
                {
                    Clipboard.SetText(Json.Hex(Selected.Item.ContentHash));
                }
                catch
                {}
        }, () => Selected != null);

        OpenCacheCommand = new AsyncCommand(OpenOriginalFolderAsync, () => Selected != null);

        ClearFiltersCommand = new RelayCommand(() =>
        {
            SearchText = "";
            SelectedPack = AllPacks;
            SelectedFormat = AllFormats;
            OnlyModded = false;
        });

        ShowModdedCommand = new RelayCommand(p =>
        {
            ShowModded = p as string == "mod";
        });
    }

    public TexturePreviewViewModel Preview { get; } = new();

    public ICommand BuildIndexCommand { get; }
    public ICommand ReplaceCommand { get; }
    public ICommand ReplaceFolderCommand { get; }
    public ICommand ExportPngCommand { get; }
    public ICommand ExportDdsCommand { get; }
    public ICommand ExportViewCommand { get; }
    public ICommand RevertCommand { get; }
    public ICommand CopyNameCommand { get; }
    public ICommand CopyHashCommand { get; }
    public ICommand OpenCacheCommand { get; }
    public ICommand ClearFiltersCommand { get; }
    public ICommand ShowModdedCommand { get; }

    // Index.

    private bool m_IsIndexLoaded;
    public bool IsIndexLoaded
    {
        get => m_IsIndexLoaded;
        private set => Set(ref m_IsIndexLoaded, value);
    }

    private string m_IndexInfo = "";
    public string IndexInfo
    {
        get => m_IndexInfo;
        private set => Set(ref m_IndexInfo, value);
    }

    public ICollectionView? View
    {
        get;
        private set;
    }

    public void OnGameChanged()
    {
        IsIndexLoaded = false;
        m_Rows = [];
        m_ByName.Clear();
        View = null;

        OnPropertyChanged(nameof(View));

        Selected = null;

        if (m_Main.Game == null)
            return;

        var idx = AppSettings.IndexPathFor(m_Main.Game.Root);

        if (File.Exists(idx))
            _ = LoadIndexAsync(idx);

        else IndexInfo = "Build the texture index once to browse textures by name.";
    }

    private async Task LoadIndexAsync(string path)
    {
        try
        {
            var items = await Task.Run(() => TextureCatalog.FromIndex(Json.Read<TexIndex>(path)));

            SetItems(items);

            IndexInfo = $"{items.Count:N0} textures, index from {File.GetLastWriteTime(path):g}";
            m_Main.Log.Write(LogKind.INFO, $"texture index loaded ({items.Count:N0} textures)");
        }
        catch (Exception ex)
        {
            IndexInfo = "Index could not be read. Rebuild it.";
            m_Main.Log.Write(LogKind.ERROR, $"index: {ex.Message}");
        }
    }

    private async Task BuildIndexAsync()
    {
        var game = m_Main.Game;

        if (game == null)
            return;

        var path = AppSettings.IndexPathFor(game.Root);

        var ok = await m_Main.RunAsync("Indexing textures...", async (ct, progress) =>
        {
            await m_Main.Archives.BuildIndexAsync(game.WadDir, path, new Progress<(int Done, int Total)>(p =>
            {
                if (p.Total > 0)
                {
                    ((IProgress<double>)progress).Report(p.Done / (double)p.Total);
                    m_Main.SetBusyText($"Indexing textures... {p.Done}/{p.Total} WADs");
                }
            }), ct);
        });

        if (ok)
        {
            await LoadIndexAsync(path);
            m_Main.Toast("Texture index ready", ToastKind.SUCCESS);
        }
    }

    private readonly Dictionary<string, TextureItem> m_ByName = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Texture by exact name, the full resolution streamed entry wins over WAD copies.</summary>
    public TextureItem? FindByName(string name) => m_ByName.GetValueOrDefault(name);

    private void SetItems(List<TextureItem> items)
    {
        m_Rows = items.Select(i => new TextureRow(i)).ToList();

        m_ByKey.Clear();
        m_ByName.Clear();

        foreach (var r in m_Rows)
        {
            m_ByKey[r.Item.Key] = r;

            if (!m_ByName.TryGetValue(r.Item.Name, out var have) || (have.Texpack == null && r.Item.Texpack != null))
                m_ByName[r.Item.Name] = r.Item;
        }

        Packs = [AllPacks, WadOnly, .. items.Where(i => i.Texpack != null).Select(i => i.Texpack!).Distinct().Order()];
        Formats = [AllFormats, .. items.Select(i => i.FormatLabel).Distinct().Order()];
        m_SelectedPack = AllPacks;
        m_SelectedFormat = AllFormats;

        OnPropertyChanged(nameof(Packs));
        OnPropertyChanged(nameof(Formats));
        OnPropertyChanged(nameof(SelectedPack));
        OnPropertyChanged(nameof(SelectedFormat));
        RefreshModded();
        RebuildView();

        IsIndexLoaded = true;
    }

    private void RebuildView()
    {
        var rows = m_Rows;
        if (IncludeUnnamed && m_Unnamed != null)
            rows = [.. rows, .. m_Unnamed];

        View = CollectionViewSource.GetDefaultView(rows);
        View.Filter = o => Filter((TextureRow)o);

        OnPropertyChanged(nameof(View));
        OnPropertyChanged(nameof(CountLabel));
    }

    public string CountLabel
    {
        get
        {
            if (View == null)
                return "";

            int shown = 0;
            foreach (var _ in View)
                shown++;

            int total = m_Rows.Count + (IncludeUnnamed ? m_Unnamed?.Count ?? 0 : 0);
            return shown == total ? $"{total:N0} textures" : $"{shown:N0} of {total:N0}";
        }
    }

    // Filters.

    public List<string> Packs
    {
        get;
        private set;
    } = [AllPacks];
    public List<string> Formats
    {
        get;
        private set;
    } = [AllFormats];

    private string m_SearchText = "";
    public string SearchText
    {
        get => m_SearchText;

        set
        {
            if (Set(ref m_SearchText, value))
            {
                m_SearchDebounce.Stop();
                m_SearchDebounce.Start();
            }
        }
    }

    private string m_SelectedPack = AllPacks;
    public string SelectedPack
    {
        get => m_SelectedPack;

        set
        {
            if (!Set(ref m_SelectedPack, value ?? AllPacks))
                return;

            OnPropertyChanged(nameof(IsPackSelected));

            if (IncludeUnnamed)
                _ = LoadUnnamedAsync();
            else
            { 
                View?.Refresh();
                OnPropertyChanged(nameof(CountLabel));
            }
        }
    }

    public bool IsPackSelected => SelectedPack != AllPacks && SelectedPack != WadOnly;

    private string m_SelectedFormat = AllFormats;
    public string SelectedFormat
    {
        get => m_SelectedFormat;

        set
        {
            if (Set(ref m_SelectedFormat, value ?? AllFormats))
            {
                View?.Refresh();
                OnPropertyChanged(nameof(CountLabel));
            }
        }
    }

    private bool m_OnlyModded;
    public bool OnlyModded
    {
        get => m_OnlyModded;

        set
        {
            if (Set(ref m_OnlyModded, value))
            {
                View?.Refresh();
                OnPropertyChanged(nameof(CountLabel));
            }
        }
    }

    private bool m_IncludeUnnamed;
    /// <summary>Also list textures of the selected texpack that no WAD names.</summary>
    public bool IncludeUnnamed
    {
        get => m_IncludeUnnamed;

        set
        {
            if (!Set(ref m_IncludeUnnamed, value))
                return;

            if (value)
                _ = LoadUnnamedAsync();
            else
                RebuildView();
        }
    }

    private List<TextureRow>? m_Unnamed;
    private string? m_UnnamedPack;

    private async Task LoadUnnamedAsync()
    {
        if (!IsPackSelected || m_Main.Game == null)
        {
            m_Unnamed = null;
            RebuildView();
            return;
        }

        if (m_UnnamedPack == SelectedPack && m_Unnamed != null)
        {
            RebuildView();
            return;
        }

        try
        {
            var pack = SelectedPack;
            var r = (await m_Main.Cli.RunAsync(["tex", "list", Path.Combine(m_Main.Game.WadDir, pack + ".texpack"), "--json"], echo_stdout: false))
                .ThrowIfFailed();
            var list = Json.Parse<TexList>(r.StdOut);

            m_Unnamed = TextureCatalog.Unnamed(list, m_Rows.Select(x => x.Item)).Select(i => new TextureRow(i)).ToList();

            foreach (var u in m_Unnamed)
                m_ByKey[u.Item.Key] = u;

            m_UnnamedPack = pack;

            RefreshModded();
            m_Main.Log.Write(LogKind.INFO, $"{pack}: {m_Unnamed.Count} texture(s) without a WAD name");
        }
        catch (Exception ex) 
        {
            m_Main.ReportError(ex);
            m_Unnamed = null;
        }

        RebuildView();
    }

    private bool Filter(TextureRow r)
    {
        var i = r.Item;

        if (OnlyModded && !r.IsModded)
            return false;

        if (SelectedPack == WadOnly)
        {
            if (i.Texpack != null)
                return false;
        }
        else if (SelectedPack != AllPacks && !string.Equals(i.Texpack, SelectedPack, StringComparison.OrdinalIgnoreCase))
            return false;

        if (SelectedFormat != AllFormats && i.FormatLabel != SelectedFormat)
            return false;

        return i.Matches(SearchText.Trim());
    }

    // Selection and preview.

    private TextureRow? m_Selected;
    public TextureRow? Selected
    {
        get => m_Selected;
        set

        {
            if (!Set(ref m_Selected, value))
                return;

            m_ShowModded = value?.IsModded == true;

            OnPropertyChanged(nameof(ShowModded));
            OnPropertyChanged(nameof(HasSelection));
            OnPropertyChanged(nameof(Details));
            OnPropertyChanged(nameof(ModEntries));

            _ = LoadPreviewAsync();
        }
    }

    public bool HasSelection => Selected != null;

    /// <summary>Multi selection from the grid.</summary>
    public IList SelectedItems
    {
        get;
        set;
    } = new List<object>();
    private int m_SelectedCount;
    public int SelectedCount
    {
        get => m_SelectedCount;
        set => Set(ref m_SelectedCount, value);
    }

    private bool m_ShowModded;
    public bool ShowModded
    {
        get => m_ShowModded;

        set
        {
            if (Set(ref m_ShowModded, value))
                _ = LoadPreviewAsync();
        }
    }

    private string? m_PreviewSource;
    public string? PreviewSource
    {
        get => m_PreviewSource;
        private set => Set(ref m_PreviewSource, value);
    }

    public List<ModTextureEntry> ModEntries
    {
        get
        {
            var p = m_Main.Project.Current;

            if (p == null || Selected == null)
                return [];

            var h = Json.Hex(Selected.Item.ContentHash);
            return p.Data.Textures.Where(t => t.ContentHash == h && t.Name == Selected.Item.Name).ToList();
        }
    }

    private async Task LoadPreviewAsync()
    {
        m_PreviewCts?.Cancel();

        var cts = m_PreviewCts = new CancellationTokenSource();
        var row = Selected;

        if (row == null)
        {
            Preview.Clear();
            PreviewSource = null;
            return;
        }

        try
        {
            string dds;
            if (ShowModded && ModEntries.FirstOrDefault() is {} me && m_Main.Project.Current is {} proj && File.Exists(proj.DdsPath(me)))
            {
                dds = proj.DdsPath(me);
                PreviewSource = "Your replacement";
            }
            else
            {
                Preview.IsLoading = true;
                Preview.Error = null;

                var e = await m_Main.Exporter.ExportAsync(row.Item, row.Item.Texpack == null ? row.Item.Wads.FirstOrDefault() : null, cts.Token);

                if (cts.IsCancellationRequested)
                    return;

                dds = e.DdsPath;
                PreviewSource = row.Item.LowMipsOnly ? "Original (WAD low mip copy, texpack not indexed)" : "Original";
                m_OriginalDetails = e.LoadSidecar();

                OnPropertyChanged(nameof(Details));
            }

            if (cts.IsCancellationRequested)
                return;

            await Preview.LoadAsync(dds);
        }
        catch (OperationCanceledException)
        {}
        catch (Exception ex)
        {
            if (!cts.IsCancellationRequested)
            {
                Preview.Clear();
                Preview.Error = ex.Message;
            }
        }
        finally
        {
            if (!cts.IsCancellationRequested)
                Preview.IsLoading = false;
        }
    }

    private Sidecar? m_OriginalDetails;

    public List<KeyValuePair<string, string>> Details
    {
        get
        {
            var r = Selected;

            if (r == null)
                return [];

            var i = r.Item;
            var d = new List<KeyValuePair<string, string>>
            {
                new("Size", $"{i.Width} x {i.Height}, {i.Mips} mips"),
                new("Format", $"{i.FormatLabel} ({DxgiFormats.Name(i.Dxgi)})"),
                new("Usage", DxgiFormats.UsageHint(i.Dxgi) is { Length: > 0 } h ? h : "-"),
                new("Storage", i.StorageLabel),
                new("Texpack", i.Texpack ?? "-"),
                new("WADs", i.Wads.Count == 0 ? "-" : string.Join("\n", i.Wads)),
                new("Content hash", Json.Hex(i.ContentHash)),
                new("Identifier", Json.Hex(i.TexIdentifier)),
            };

            if (m_OriginalDetails is { } sc)
            {
                if (sc.Depth > 1)
                {
                    d[0] = new("Size", $"{sc.Width} x {sc.Height} x {sc.Depth}, {sc.Mips} mips");
                    d.Insert(2, new("Layout", "3D volume (probe, lighting data)"));
                }
                else if (sc.Slices > 1) d.Insert(2, new("Layout", $"{sc.TexType}, {sc.Slices} slices"));
            }

            return d;
        }
    }

    // Modded markers.

    public void RefreshModded()
    {
        var p = m_Main.Project.Current;
        var keys = new HashSet<string>();

        if (p != null)
            foreach (var t in p.Data.Textures)
                keys.Add($"{Json.ParseHex(t.ContentHash):x16}|{t.Name}");

        foreach (var r in m_ByKey.Values)
            r.IsModded = keys.Contains($"{r.Item.ContentHash:x16}|{r.Item.Name}");

        OnPropertyChanged(nameof(ModEntries));

        if (OnlyModded)
            View?.Refresh();
    }

    // Replace.

    /// <summary>Replace the selected texture (or <paramref name="file"/> dropped onto the preview).</summary>
    public async Task ReplaceAsync(string? file)
    {
        var row = Selected;

        if (row == null)
            return;

        file ??= Dialogs.OpenFile($"Replace {row.Name}", Dialogs.ImageFilter);

        if (file == null)
            return;

        var project = m_Main.Project.EnsureProject();

        if (project == null)
            return;

        // Original export first so the dialog can show it.
        ExportedTexture? orig = null;
        await m_Main.RunAsync("Reading original...", async (ct, _) =>
            orig = await m_Main.Exporter.ExportAsync(row.Item, row.Item.Texpack == null ? row.Item.Wads.FirstOrDefault() : null, ct));

        if (orig == null)
            return;

        var dlg = new ReplaceDialog(row.Item, orig, file, m_Main.Texconv.Available, m_Main.Settings.Quality) { Owner = Application.Current.MainWindow };

        if (dlg.ShowDialog() != true || dlg.Request == null)
            return;

        m_Main.Settings.Quality = dlg.Request.Quality;
        m_Main.Settings.Save();

        ReplaceResult? res = null;
        await m_Main.RunAsync($"Converting {row.Name}...", async (ct, _) => res = await m_Main.Replacer.ReplaceAsync(project, dlg.Request, ct));

        if (res == null)
            return;

        foreach (var w in res.Warnings)
            m_Main.Log.Write(LogKind.WARNING, w);

        m_Main.Project.Reload();
        m_ShowModded = true;

        OnPropertyChanged(nameof(ShowModded));
        await LoadPreviewAsync();

        m_Main.Toast(res.Warnings.Count > 0 ? $"Replaced (with {res.Warnings.Count} warning(s), see log)" : $"{row.Name} added to '{project.Data.Name}'",
            res.Warnings.Count > 0 ? ToastKind.WARNING : ToastKind.SUCCESS);
    }

    /// <summary>Batch operation. Every image in a folder whose file name matches a texture name replaces it.</summary>
    private async Task ReplaceFromFolderAsync()
    {
        var dir = Dialogs.PickFolder("Folder with edited textures (file names are texture names)");

        if (dir == null)
            return;

        var by_name = new Dictionary<string, TextureRow>(StringComparer.OrdinalIgnoreCase);

        foreach (var r in m_Rows)
            by_name.TryAdd(r.Name, r);

        if (m_Unnamed != null)
            foreach (var r in m_Unnamed) by_name.TryAdd(r.Name, r);

        var jobs = new List<(TextureRow Row, string File)>();
        var unmatched = new List<string>();

        foreach (var f in Directory.EnumerateFiles(dir).Where(f => TextureReplacer.ImageExtensions.Contains(Path.GetExtension(f).ToLowerInvariant())))
        {
            var stem = Path.GetFileNameWithoutExtension(f);

            if (by_name.TryGetValue(stem, out var row))
                jobs.Add((row, f));
            else
                unmatched.Add(Path.GetFileName(f));
        }

        if (jobs.Count == 0)
        {
            Dialogs.Info("Nothing to import", "No file in that folder is named after a texture (e.g. TX_wolf00_head_d_862625252731E46F.png).");
            return;
        }

        if (!Dialogs.Confirm("Import folder", $"{jobs.Count} image(s) match texture names{(unmatched.Count > 0 ? $", {unmatched.Count} don't and will be skipped" : "")}.\n\n" +
                                               "Each is converted to the original format and size, with a full mip chain.", "Import"))
            return;

        var project = m_Main.Project.EnsureProject();

        if (project == null)
            return;

        int done = 0, failed = 0;
        await m_Main.RunAsync("Importing textures...", async (ct, progress) =>
        {
            foreach (var (row, file) in jobs)
            {
                ct.ThrowIfCancellationRequested();
                m_Main.SetBusyText($"Importing {row.Name}...");

                try
                {
                    var res = await m_Main.Replacer.ReplaceAsync(project, new ReplaceRequest
                    {
                        Texture = row.Item,
                        SourceFile = file,
                        Quality = m_Main.Settings.Quality,
                        AllWadCopies = true,
                    }, ct);

                    foreach (var w in res.Warnings)
                        m_Main.Log.Write(LogKind.WARNING, w);

                    done++;
                }
                catch (OperationCanceledException)
                {
                    throw;
                }

                catch (Exception ex)
                {
                    failed++;
                    m_Main.Log.Write(LogKind.ERROR, $"{row.Name}: {ex.Message}");
                }

                progress.Report((done + failed) / (double)jobs.Count);
            }
        });

        m_Main.Project.Reload();

        foreach (var u in unmatched)
            m_Main.Log.Write(LogKind.WARNING, $"skipped {u}: no texture with that name");

        m_Main.Toast($"Imported {done} texture(s){(failed > 0 ? $", {failed} failed (see log)" : "")}", failed > 0 ? ToastKind.WARNING : ToastKind.SUCCESS);
    }

    private void Revert()
    {
        var p = m_Main.Project.Current;

        if (p == null)
            return;

        foreach (var e in ModEntries)
            p.Remove(e);

        m_Main.Project.Reload();
        ShowModded = false;

        m_Main.Toast("Replacement removed from the project");
    }

    // Export.

    private async Task ExportSelectedAsync(bool png)
    {
        var rows = SelectedItems.OfType<TextureRow>().ToList();

        if (rows.Count == 0 && Selected != null)
            rows.Add(Selected);

        if (rows.Count == 0)
            return;

        var dir = Dialogs.PickFolder(png ? "Export PNG to..." : "Export DDS to...");

        if (dir == null)
            return;

        int n = 0;
        await m_Main.RunAsync($"Exporting {rows.Count} texture(s)...", async (ct, progress) =>
        {
            foreach (var r in rows)
            {
                ct.ThrowIfCancellationRequested();
                m_Main.SetBusyText($"Exporting {r.Name}...");

                try
                {
                    var e = await m_Main.Exporter.ExportAsync(r.Item, r.Item.Texpack == null ? r.Item.Wads.FirstOrDefault() : null, ct);
                    var stem = TextureExporter.SafeName(r.Name);

                    if (png)
                    {
                        bool bc5 = DxgiFormats.Family(r.Item.Dxgi) == DxgiFormats.BC5_UNORM;
                        await Task.Run(() => ImageUtil.DdsToPng(e.DdsPath, Path.Combine(dir, stem + ".png"), bc5), ct);
                    }
                    else
                    {
                        File.Copy(e.DdsPath, Path.Combine(dir, stem + ".dds"), true);
                        File.Copy(e.SidecarPath, Path.Combine(dir, stem + ".json"), true);
                    }

                    n++;
                }

                catch (OperationCanceledException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    m_Main.Log.Write(LogKind.ERROR, $"{r.Name}: {ex.Message}");
                }

                progress.Report(n / (double)rows.Count);
            }
        });

        if (n > 0)
        {
            m_Main.Log.Write(LogKind.SUCCESS, $"exported {n} texture(s) to {dir}");
            m_Main.Toast($"Exported {n} texture(s)", ToastKind.SUCCESS);
            Dialogs.OpenFolder(dir);
        }
    }

    private void ExportCurrentView()
    {
        var bmp = Preview.Bitmap;

        if (bmp == null || Selected == null)
            return;

        var name = $"{TextureExporter.SafeName(Selected.Name)}_mip{Preview.Mip}{(Preview.Channel is ChannelMode.RGBA ? "" : "_" + Preview.Channel)}.png";
        var path = Dialogs.SaveFile("Save preview as PNG", "PNG|*.png", name);

        if (path == null)
            return;

        ImageUtil.SavePng(bmp, path);
        m_Main.Toast("Saved " + Path.GetFileName(path), ToastKind.SUCCESS);
    }

    private async Task OpenOriginalFolderAsync()
    {
        var r = Selected;

        if (r == null)
            return;

        var e = await m_Main.Exporter.ExportAsync(r.Item, r.Item.Texpack == null ? r.Item.Wads.FirstOrDefault() : null);
        Dialogs.OpenFolder(e.DdsPath);
    }
}