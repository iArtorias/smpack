using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Windows.Data;
using System.Windows.Input;
using SmpackGui.Core;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Imaging;
using SmpackGui.Core.Meshes;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

public sealed class FormatChoice(MeshFormat format, string label, string hint)
{
    public MeshFormat Format { get; } = format;
    public string Label { get; } = label;
    public string Hint { get; } = hint;
}

/// <summary>Models page. Browse the meshes of a WAD, preview them in 3D, export to glTF or FBX.</summary>
public sealed class ModelsViewModel : ObservableObject
{
    private readonly MainViewModel m_Main;
    private readonly MeshService m_Meshes;
    private CancellationTokenSource? m_PreviewCts;
    private List<string> m_AllWads = [];

    public ModelsViewModel(MainViewModel main)
    {
        m_Main = main;

        m_Meshes = new MeshService(main.Cli);
        WadView = CollectionViewSource.GetDefaultView(Wads);
        WadView.Filter = o => o is string s && (WadSearch.Length == 0 || s.Contains(WadSearch, StringComparison.OrdinalIgnoreCase));
        ModelView = CollectionViewSource.GetDefaultView(Models);
        ModelView.Filter = o => o is MeshInfo m && (ModelSearch.Length == 0 || m.Name.Contains(ModelSearch, StringComparison.OrdinalIgnoreCase));

        RefreshCommand = new RelayCommand(LoadWads);
        ExportCommand = new AsyncCommand(() => ExportAsync(all: false), () => Selected != null && SelectedWad != null);
        ExportAllCommand = new AsyncCommand(() => ExportAsync(all: true), () => SelectedWad != null && Models.Count > 0);
        ReplaceCommand = new AsyncCommand(ReplaceAsync, () => Selected != null && SelectedWad != null);
        RevertCommand = new RelayCommand(Revert, () => SelectedModEntry != null);
    }

    public ICommand RefreshCommand { get; }
    public ICommand ExportCommand { get; }
    public ICommand ExportAllCommand { get; }
    public ICommand ReplaceCommand { get; }
    public ICommand RevertCommand { get; }

    public static IReadOnlyList<FormatChoice> FormatChoices { get; } =
    [
        new(MeshFormat.GLB, "glTF binary (.glb)", "One file with the geometry. Textures as PNG files next to it. Best for Blender."),
        new(MeshFormat.GLTF, "glTF (.gltf + .bin)", "Readable JSON plus a binary buffer, textures as PNG files."),
        new(MeshFormat.FBX, "FBX 7.4 binary (.fbx)", "For Autodesk tools, Blender and most engines. Textures as PNG files."),
    ];

    // WAD list.

    public ObservableCollection<string> Wads { get; } = [];
    public ICollectionView WadView { get; }

    private string m_WadSearch = "";
    public string WadSearch
    {
        get => m_WadSearch;

        set
        {
            if (Set(ref m_WadSearch, value ?? ""))
                WadView.Refresh();
        }
    }

    private string? m_SelectedWad;
    public string? SelectedWad
    {
        get => m_SelectedWad;

        set
        {
            if (!Set(ref m_SelectedWad, value))
                return;

            _ = LoadModelsAsync();
        }
    }

    private bool m_Loaded;

    public void OnGameChanged()
    {
        m_Loaded = false;
        Wads.Clear();
        Models.Clear();
        Preview = null;

        if (m_Main.Settings.LastModelsWad is {} w)
            m_PendingWad = w;
    }

    private string? m_PendingWad;

    public void EnsureLoaded()
    {
        if (m_Loaded)
            return;

        LoadWads();
    }

    private void LoadWads()
    {
        var g = m_Main.Game;
        Wads.Clear();

        if (g == null || !Directory.Exists(g.WadDir))
            return;

        m_AllWads = Directory.EnumerateFiles(g.WadDir, "*.wad").Select(Path.GetFileName).OfType<string>()
            .OrderBy(n => n, StringComparer.OrdinalIgnoreCase).ToList();

        foreach (var w in m_AllWads)
            Wads.Add(w);

        m_Loaded = true;

        if (m_PendingWad != null && Wads.Contains(m_PendingWad))
            SelectedWad = m_PendingWad;

        m_PendingWad = null;
        OnPropertyChanged(nameof(WadSummary));
    }

    public string WadSummary => Wads.Count == 0 ? "" : $"{Wads.Count:N0} WAD files";

    // Models.

    public ObservableCollection<MeshInfo> Models { get; } = [];
    public ICollectionView ModelView { get; }

    private string m_ModelSearch = "";
    public string ModelSearch
    {
        get => m_ModelSearch;

        set
        {
            if (Set(ref m_ModelSearch, value ?? ""))
                ModelView.Refresh();
        }
    }

    private string m_ModelsInfo = "Pick a WAD to list its models.";
    public string ModelsInfo
    {
        get => m_ModelsInfo;
        private set => Set(ref m_ModelsInfo, value);
    }

    private bool m_LoadingModels;
    public bool LoadingModels
    {
        get => m_LoadingModels;
        private set => Set(ref m_LoadingModels, value);
    }

    private string WadPath(string wad) => GameInstall.Pristine(Path.Combine(m_Main.Game!.WadDir, wad));

    private async Task LoadModelsAsync()
    {
        Models.Clear();
        Selected = null;

        var wad = SelectedWad;

        if (wad == null || m_Main.Game == null)
            return;

        m_Main.Settings.LastModelsWad = wad;
        m_Main.Settings.Save();
        LoadingModels = true;
        ModelsInfo = "Reading models...";

        try
        {
            var list = await m_Meshes.ListAsync(WadPath(wad));

            if (wad != SelectedWad)
                return;

            // Shadow proxies and collider shells are never drawn by the game. Leave them out of the list.
            var shown = list.Models.Where(m => m.Visible).OrderBy(m => m.Name, StringComparer.OrdinalIgnoreCase).ToList();

            foreach (var m in shown)
                Models.Add(m);

            int hidden = list.Models.Count - shown.Count;
            ModelsInfo = shown.Count == 0
                ? (hidden > 0 ? $"Only {hidden} invisible helper mesh(es) (shadow or collider geometry) in this WAD." : "This WAD has no models.")
                : $"{shown.Count} model(s), {list.Lodpacks} geometry pack(s) found" + (hidden > 0 ? $", {hidden} helper mesh(es) hidden" : "");

            OnPropertyChanged(nameof(Models));
        }
        catch (Exception ex)
        {
            ModelsInfo = "Could not read the models.";
            m_Main.ReportError(ex);
        }
        finally
        {
            LoadingModels = false;
        }
    }

    private MeshInfo? m_Selected;
    public MeshInfo? Selected
    {
        get => m_Selected;

        set
        {
            if (!Set(ref m_Selected, value))
                return;

            m_Variants = value == null || value.Variants.Count == 0 ? [] : [.. value.Variants];
            m_Variant = value != null && value.DefaultVariant >= 0 && value.DefaultVariant < m_Variants.Count ? m_Variants[value.DefaultVariant] : null;

            OnPropertyChanged(nameof(HasSelection));
            OnPropertyChanged(nameof(SelectedDetails));
            OnPropertyChanged(nameof(Variants));
            OnPropertyChanged(nameof(HasVariants));
            OnPropertyChanged(nameof(Variant));
            OnPropertyChanged(nameof(HasHiddenParts));
            OnPropertyChanged(nameof(SelectedModEntry));
            OnPropertyChanged(nameof(ModStatus));

            if (value != null)
                _ = LoadPreviewAsync(value);
            else
                Preview = null;
        }
    }

    public bool HasSelection => Selected != null;

    private List<string> m_Variants = [];
    /// <summary>Visual configs of the selected model (e.g. 'Helwalker00').</summary>
    public List<string> Variants => m_Variants;
    public bool HasVariants => m_Variants.Count > 1;

    private string? m_Variant;
    public string? Variant
    {
        get => m_Variant;

        set
        {
            if (!Set(ref m_Variant, value) || Selected == null)
                return;

            _ = LoadPreviewAsync(Selected);
        }
    }

    public bool HasHiddenParts => (Selected?.HiddenParts ?? 0) > 0 || HasVariants;

    private bool m_ShowHiddenParts;
    /// <summary>Show damage states, wound decals and every variant at once.</summary>
    public bool ShowHiddenParts
    {
        get => m_ShowHiddenParts;

        set
        {
            if (!Set(ref m_ShowHiddenParts, value) || Selected == null)
                return;

            _ = LoadPreviewAsync(Selected);
        }
    }

    private MeshExportOptions ViewOptions(MeshExportOptions o)
    {
        o.Variant = Variant;
        o.AllParts = ShowHiddenParts;
        return o;
    }

    public string SelectedDetails
    {
        get
        {
            var m = Selected;

            if (m == null)
                return "";

            var parts = $"{m.Parts} part(s), {m.Prims} primitive(s), {m.Lods} LOD level(s), {m.Materials} material(s)" +
                        (m.Joints > 0 ? $", skeleton with {m.Joints} joints" : "");

            var stream = m.StreamedPrims == 0 ? "all geometry stored in the WAD"
                : m.MissingPrims == 0 ? $"{m.StreamedPrims} primitive(s) streamed from geometry packs"
                : $"{m.MissingPrims} of {m.StreamedPrims} streamed primitive(s) are in a geometry pack that is missing, so a coarser LOD is used";

            var looks = m.Variants.Count > 1 ? $"\n{m.Variants.Count} variants of this model. Counts are for the default one" : "";

            if (m.HiddenParts > 0)
                looks += $"{(looks.Length > 0 ? ", " : "\n")}{m.HiddenParts} part(s) hidden until the game shows them (damage, wounds etc)";

            return $"{m.KindLabel}: {m.Vertices:N0} vertices and {m.Triangles:N0} triangles at LOD 0\n{parts}\n{stream}{looks}";
        }
    }

    // Preview.

    private PreviewModel? m_Preview;
    public PreviewModel? Preview
    {
        get => m_Preview;
        private set => Set(ref m_Preview, value);
    }

    private bool m_PreviewBusy;
    public bool PreviewBusy
    {
        get => m_PreviewBusy;
        private set => Set(ref m_PreviewBusy, value);
    }

    private string m_PreviewInfo = "";
    public string PreviewInfo
    {
        get => m_PreviewInfo;
        private set => Set(ref m_PreviewInfo, value);
    }

    private bool m_PreviewTextures = true;
    public bool PreviewTextures
    {
        get => m_PreviewTextures;
        set => Set(ref m_PreviewTextures, value);
    }

    private async Task LoadPreviewAsync(MeshInfo m)
    {
        m_PreviewCts?.Cancel();

        var cts = m_PreviewCts = new CancellationTokenSource();
        var wad = SelectedWad!;

        PreviewBusy = true;
        PreviewInfo = "Building preview...";

        try
        {
            var opts = ViewOptions(new MeshExportOptions
            {
                Format = MeshFormat.GLB,
                Lod = 0
            });

            var view = opts.AllParts ? "all" : TextureExporter.SafeName(opts.Variant ?? "default");
            var dir = Path.Combine(AppSettings.CacheDir, "models", GameInstall.PackBase(wad), m.Id.ToString(), view);
            var res = await m_Meshes.ExportAsync(WadPath(wad), dir, opts, id: m.Id, ct: cts.Token);

            if (cts.IsCancellationRequested)
                return;

            if (res.Models.Count == 0)
            {
                Preview = null;
                PreviewInfo = res.Skipped.Count > 0 ? $"Nothing to show: {res.Skipped[0].Reason}." : "Nothing to show.";
                return;
            }

            var item = res.Models[0];

            // Textures are shared by every view of the model.
            var tex_root = Path.GetDirectoryName(dir)!;
            int tex_ok = await WriteTexturesAsync(res, tex_root, all: false, only_base_color: true, wad, cts.Token);
            var model = await Task.Run(() => GlbReader.Load(item.File, tex_root), cts.Token);

            if (cts.IsCancellationRequested)
                return;

            Preview = model;

            var mats = item.Materials.Count;
            PreviewInfo = $"{item.Vertices:N0} vertices, {item.Triangles:N0} triangles, {mats} material(s)" +
                          (tex_ok > 0 ? $", {tex_ok} texture(s)" : "") + (res.Warnings.Count > 0 ? $"\n{res.Warnings[0]}" : "");
        }
        catch (OperationCanceledException)
        {}
        catch (Exception ex)
        {
            if (!cts.IsCancellationRequested)
            {
                Preview = null;
                PreviewInfo = ex.Message;
                m_Main.Log.Write(LogKind.ERROR, $"{m.Name}: {ex.Message}");
            }
        }
        finally
        {
            if (m_PreviewCts == cts)
                PreviewBusy = false;
        }
    }

    /// <summary>
    /// Export the textures a model export references as PNG into '<dir>/textures', merging
    /// separate cutout maps into the color alpha. Returns how many were written, failures are logged.
    /// </summary>
    private async Task<int> WriteTexturesAsync(MeshExportResult res, string dir, bool all, bool only_base_color, string wad, CancellationToken ct)
    {
        int n = 0;
        var tex_dir = Path.Combine(dir, "textures");

        foreach (var t in MeshService.TexturesToWrite(res, all))
        {
            if (only_base_color && t.Normal)
                continue;

            ct.ThrowIfCancellationRequested();
            var png = Path.Combine(tex_dir, t.File + ".png");

            if (File.Exists(png))
            {
                n++;
                continue;
            }
            try
            {
                async Task<string> Dds(string name)
                {
                    var item = m_Main.Textures.FindByName(name) ?? new TextureItem
                    {
                        Name = name,
                        ContentHash = 0
                    };

                    if (item.Wads.Count == 0 && item.Texpack == null)
                        item.Wads.Add(wad);

                    return (await m_Main.Exporter.ExportAsync(item, ct: ct)).DdsPath;
                }

                var color = t.Texture != null ? await Dds(t.Texture) : null;
                var opacity = t.Opacity != null ? await Dds(t.Opacity) : null;

                await Task.Run(() =>
                {
                    if (opacity != null)
                    {
                        ImageUtil.DdsWithOpacityToPng(color, opacity, png);
                        return;
                    }

                    var hdr = DdsFile.LoadHeader(color!);
                    bool two_channel = hdr.Dxgi is DxgiFormats.BC5_UNORM or DxgiFormats.BC5_SNORM;
                    ImageUtil.DdsToPng(color!, png, reconstruct_z: t.Normal && two_channel);
                }, ct);

                n++;
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                m_Main.Log.Write(LogKind.WARNING, $"texture {t.File}: {ex.Message}");
            }
        }
        return n;
    }

    // Replace.

    public ModModelEntry? SelectedModEntry =>
        Selected != null && SelectedWad != null ? m_Main.Project.Current?.FindModel(SelectedWad, Selected.Id) : null;

    public string ModStatus => SelectedModEntry is { } e
        ? $"Replaced in your mod by {Path.GetFileName(e.SourceFile)}" + (e.AllLods ? "" : " (finest LOD only)")
        : "";

    public void RefreshModded()
    {
        OnPropertyChanged(nameof(SelectedModEntry));
        OnPropertyChanged(nameof(ModStatus));
    }

    private async Task ReplaceAsync()
    {
        var m = Selected;
        var wad = SelectedWad;

        if (m == null || wad == null || m_Main.Game == null)
            return;

        var file = Dialogs.OpenFile($"Replace {m.Name} with an edited model", "glTF model|*.glb;*.gltf|All files|*.*", m_Main.Settings.LastExportDir);

        if (file == null)
            return;

        var project = m_Main.Project.EnsureProject();

        if (project == null)
            return;

        bool all_lods = Dialogs.Confirm("Detail levels",
            "Use the new mesh for every detail level (LOD)?\n\n'Yes' keeps the model consistent at any distance. " +
            "'No' only replaces the levels present in the file, so the original appears again further away.", "Every LOD", "Only these levels");

        // Check the file against the model now, so problems show up before the build.
        MeshImportResult? res = null;
        var tmp = Path.Combine(Path.GetTempPath(), "SmpackGui", "meshcheck", Guid.NewGuid().ToString("N"));
        var ok = await m_Main.RunAsync($"Checking {Path.GetFileName(file)}...", async (ct, _) =>
        {
            try
            {
                res = await m_Meshes.ImportAsync(WadPath(wad), file, tmp, m_Main.Game.Root, "check", m.Id, append: false, all_lods, compress: false, ct);
            }
            finally
            {
                try
                {
                    if (Directory.Exists(tmp))
                        Directory.Delete(tmp, true);
                }
                catch (IOException)
                {}
                catch (UnauthorizedAccessException)
                {}
            }
        });

        if (!ok || res == null)
            return;

        foreach (var w in res.Warnings)
            m_Main.Log.Write(LogKind.WARNING, $"{m.Name}: {w}");

        project.AddModel(wad, m.Id, m.Name, file, all_lods);
        m_Main.Project.Reload();

        RefreshModded();

        m_Main.Log.Write(LogKind.SUCCESS, $"{m.Name}: {res.Prims} primitive(s), {res.Triangles:N0} triangles queued in \"{project.Data.Name}\"");
        m_Main.Toast(res.Warnings.Count > 0 ? $"{m.Name} replaced with {res.Warnings.Count} warning(s), see the log" : $"{m.Name} added to \"{project.Data.Name}\"",
            res.Warnings.Count > 0 ? ToastKind.WARNING : ToastKind.SUCCESS);
    }

    private void Revert()
    {
        var e = SelectedModEntry;
        var p = m_Main.Project.Current;

        if (e == null || p == null)
            return;

        p.RemoveModel(e);
        m_Main.Project.Reload();

        RefreshModded();

        m_Main.Toast("Model replacement removed from the project");
    }

    // Export.

    private FormatChoice m_Format = FormatChoices[0];
    public FormatChoice Format
    {
        get => m_Format;
        set => Set(ref m_Format, value ?? FormatChoices[0]);
    }

    public IReadOnlyList<string> LodChoices { get; } = ["LOD 0 (finest)", "LOD 1", "LOD 2", "LOD 3", "Every LOD"];

    private int m_LodIndex;
    public int LodIndex
    {
        get => m_LodIndex;
        set => Set(ref m_LodIndex, Math.Clamp(value, 0, LodChoices.Count - 1));
    }

    private bool m_ExportTextures = true;
    public bool ExportTextures
    {
        get => m_ExportTextures;
        set => Set(ref m_ExportTextures, value);
    }

    private bool m_AllTextures;
    public bool AllTextures
    {
        get => m_AllTextures;
        set => Set(ref m_AllTextures, value);
    }

    private bool m_WorldPlacement;
    public bool WorldPlacement
    {
        get => m_WorldPlacement;
        set => Set(ref m_WorldPlacement, value);
    }

    private async Task ExportAsync(bool all)
    {
        var wad = SelectedWad;

        if (wad == null || m_Main.Game == null)
            return;

        var target = all ? null : Selected;

        if (!all && target == null)
            return;

        var label = all ? GameInstall.PackBase(wad) : target!.Name;
        var out_dir = Dialogs.PickFolder("Export models to", m_Main.Settings.LastExportDir ?? m_Main.ProjectsDir) is {} picked
            ? Path.Combine(picked, TextureExporter.SafeName(label))
            : null;

        if (out_dir == null)
            return;

        m_Main.Settings.LastExportDir = Path.GetDirectoryName(out_dir);
        m_Main.Settings.Save();

        var opts = ViewOptions(new MeshExportOptions
        {
            Format = Format.Format,
            Lod = LodIndex == LodChoices.Count - 1 ? -1 : LodIndex,
            Textures = ExportTextures,
            WorldPlacement = WorldPlacement,
        });

        // A variant only applies to the selected model.
        if (all)
            opts.Variant = null;

        MeshExportResult? res = null;
        int tex = 0;
        var ok = await m_Main.RunAsync(all ? $"Exporting every model in {wad}..." : $"Exporting {label}...", async (ct, progress) =>
        {
            var filter = all && ModelSearch.Length > 0 ? ModelSearch : null;
            res = await m_Meshes.ExportAsync(WadPath(wad), out_dir, opts, id: target?.Id, filter: filter, ct: ct);

            foreach (var w in res.Warnings) m_Main.Log.Write(LogKind.WARNING, w);
            if (ExportTextures)
            {
                m_Main.SetBusyText("Writing textures...");
                tex = await WriteTexturesAsync(res, out_dir, AllTextures, only_base_color: false, wad, ct);
            }
        });

        if (!ok || res == null)
            return;

        m_Main.Log.Write(LogKind.SUCCESS, $"exported {res.Models.Count} model(s){(tex > 0 ? $" and {tex} texture(s)" : "")} to {out_dir}");
        m_Main.Toast($"Exported {res.Models.Count} model(s)" + (tex > 0 ? $" with {tex} texture(s)" : ""), ToastKind.SUCCESS);
        Dialogs.OpenFolder(out_dir);
    }
}