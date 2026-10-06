using System.Collections.ObjectModel;
using System.IO;
using System.Windows.Input;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Mods;
using SmpackGui.Mvvm;
using SmpackGui.Services;

namespace SmpackGui.ViewModels;

public sealed class PatchRow
{
    public required string Name
    {
        get;
        init;
    }
    public required bool IsTexpack
    {
        get;
        init;
    }
    public bool FilesPresent
    {
        get;
        init;
    }
    public string? OwnerProject
    {
        get;
        init;
    }
    public string KindLabel => IsTexpack ? "Texture patch" : "Geometry patch";
    public string Status => (FilesPresent ? "files present" : "files missing!") + (OwnerProject != null ? $", from '{OwnerProject}'" : "");
}

public sealed class BackupRow(Backup b)
{
    public Backup Backup { get; } = b;
    public string Name => Backup.Name;
    public string Folder => Path.GetFileName(Path.GetDirectoryName(Backup.BackupPath) ?? "");
    public string SizeLabel => ImageUtil.HumanBytes(Backup.Size);
    public string Created => Backup.Created.ToString("g");
}

/// <summary>What is currently changed in the game folder, and how to undo it.</summary>
public sealed class InstalledViewModel : ObservableObject
{
    private readonly MainViewModel m_Main;

    public InstalledViewModel(MainViewModel main)
    {
        m_Main = main;

        RefreshCommand = new RelayCommand(Refresh);
        RemovePatchCommand = new AsyncCommand(p => RemovePatchAsync(p as PatchRow));
        RestoreCommand = new AsyncCommand(p => RestoreAsync(p as BackupRow));
        RestoreAllCommand = new AsyncCommand(RestoreAllAsync, () => Backups.Count > 0 || Patches.Count > 0);
    }

    public ICommand RefreshCommand { get; }
    public ICommand RemovePatchCommand { get; }
    public ICommand RestoreCommand { get; }
    public ICommand RestoreAllCommand { get; }

    public ObservableCollection<PatchRow> Patches { get; } = [];
    public ObservableCollection<BackupRow> Backups { get; } = [];

    private string m_Status = "";
    public string Status
    {
        get => m_Status;
        private set => Set(ref m_Status, value);
    }

    public void OnGameChanged() => Refresh();

    private int m_RefreshVersion;

    public async void Refresh()
    {
        int version = ++m_RefreshVersion;
        var g = m_Main.Game;

        if (g == null)
        {
            Patches.Clear();
            Backups.Clear();
            Status = "No game folder selected.";
            return;
        }

        try
        {
            var owners = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

            foreach (var dir in m_Main.Settings.RecentProjects)
            {
                try
                {
                    if (!File.Exists(Path.Combine(dir, ModProject.FileName)))
                        continue;

                    var p = ModProject.Open(dir);

                    if (p.Data.Installed?.Texpack is {} tp)
                        owners["t:" + tp] = p.Data.Name;

                    if (p.Data.Installed?.Lodpack is {} lp)
                        owners["l:" + lp] = p.Data.Name;
                }
                catch (Exception)
                {
                    // Unreadable project.
                }
            }

            var lists = await m_Main.Builder.GetPatchListsAsync(g);
            var backups = await Task.Run(() => g.EnumerateBackups().OrderBy(b => b.Name).ToList());

            // A newer refresh started while this one waited. Let that one fill the lists.
            if (version != m_RefreshVersion)
                return;

            Patches.Clear();
            Backups.Clear();

            foreach (var n in lists.PatchTexpacks)
                Patches.Add(new PatchRow
                {
                    Name = n,
                    IsTexpack = true,
                    OwnerProject = owners.GetValueOrDefault("t:" + n),
                    FilesPresent = File.Exists(Path.Combine(g.WadDir, n + ".texpack")) && File.Exists(Path.Combine(g.WadDir, n + ".texpack.toc")),
                });

            foreach (var n in lists.PatchLodpacks)
                Patches.Add(new PatchRow
                {
                    Name = n,
                    IsTexpack = false,
                    OwnerProject = owners.GetValueOrDefault("l:" + n),
                    FilesPresent = File.Exists(Path.Combine(g.WadDir, n + ".lodpack")) && File.Exists(Path.Combine(g.WadDir, n + ".lodpack.toc")),
                });

            foreach (var b in backups)
                Backups.Add(new BackupRow(b));

            Status = Patches.Count == 0 && Backups.Count == 0
                ? "The game is unmodified."
                : $"{Patches.Count} registered patch pack(s), {Backups.Count} replaced file(s) with backups.";
        }
        catch (Exception ex)
        {
            if (version == m_RefreshVersion)
                Status = ex.Message;
        }

        CommandManager.InvalidateRequerySuggested();
    }

    private async Task RemovePatchAsync(PatchRow? p)
    {
        var g = m_Main.Game;

        if (p == null || g == null)
            return;

        if (!Dialogs.Confirm("Remove patch pack", $"Unregister '{p.Name}' from boot-options.json and delete its files?", "Remove", danger: true))
            return;

        await m_Main.RunAsync("Removing patch...", async (ct, _) =>
        {
            await m_Main.Builder.SetPatchAsync(g, p.Name, p.IsTexpack, enable: false, ct);
            var ext = p.IsTexpack ? ".texpack" : ".lodpack";

            foreach (var f in new[] { p.Name + ext, p.Name + ext + ".toc" })
            {
                var path = Path.Combine(g.WadDir, f);

                if (File.Exists(path))
                    File.Delete(path);
            }

            // If a project owns it, mark that project as no longer installed.
            if (m_Main.Project.Current is { Data.Installed: {} inst } cur)
            {
                if (p.IsTexpack && inst.Texpack == p.Name)
                    inst.Texpack = null;

                if (!p.IsTexpack && inst.Lodpack == p.Name)
                    inst.Lodpack = null;

                cur.Save();
            }
        });

        Refresh();
    }

    private async Task RestoreAsync(BackupRow? b)
    {
        if (b == null)
            return;

        if (!Dialogs.Confirm("Restore original", $"Put the original {b.Name} back? Mods that changed it stop working.", "Restore"))
            return;

        await m_Main.RunAsync("Restoring...", (_, _) =>
        {
            GameInstall.Restore(b.Backup, s => m_Main.Log.Write(LogKind.OUTPUT, "  " + s));
            return Task.CompletedTask;
        });

        Refresh();
    }

    private async Task RestoreAllAsync()
    {
        var g = m_Main.Game;

        if (g == null)
            return;

        if (!Dialogs.Confirm("Restore vanilla game",
                "Restore every backed up file (including boot-options.json) and delete all registered patch packs?\n\nYour mod projects are kept and can be installed again.",
                "Restore everything", danger: true))
            return;

        var ok = await m_Main.RunAsync("Restoring the original game...", async (ct, _) =>
        {
            foreach (var p in Patches.ToList())
            {
                try
                {
                    await m_Main.Builder.SetPatchAsync(g, p.Name, p.IsTexpack, enable: false, ct);
                }
                catch (SmpackException)
                {
                    // 'boot-options.json' is restored from its backup below.
                }

                var ext = p.IsTexpack ? ".texpack" : ".lodpack";
                foreach (var f in new[] { p.Name + ext, p.Name + ext + ".toc" })
                {
                    var path = Path.Combine(g.WadDir, f);

                    if (File.Exists(path) && !File.Exists(path + GameInstall.BackupSuffix))
                        File.Delete(path);
                }
            }

            foreach (var b in g.EnumerateBackups())
                GameInstall.Restore(b, s => m_Main.Log.Write(LogKind.OUTPUT, "  " + s));

            foreach (var dir in m_Main.Settings.RecentProjects)
            {
                try
                {
                    if (!File.Exists(Path.Combine(dir, ModProject.FileName)))
                        continue;

                    var p = ModProject.Open(dir);

                    if (p.Data.Installed == null)
                        continue;

                    p.Data.Installed = null;
                    p.Save();
                }
                catch (Exception ex)
                {
                    m_Main.Log.Write(LogKind.WARNING, $"{dir}: {ex.Message}");
                }
            }

            if (m_Main.Project.Current is {} cur)
                m_Main.Project.Load(ModProject.Open(cur.Dir));

            m_Main.Log.Write(LogKind.SUCCESS, "game restored to its original files");
        });

        Refresh();

        if (ok)
            m_Main.Toast("Game restored to vanilla", ToastKind.SUCCESS);
    }
}