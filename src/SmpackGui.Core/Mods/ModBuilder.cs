using System.IO.Compression;
using System.Text;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Meshes;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Mods;

public sealed record BuildResult(List<string> Outputs, string? Texpack, List<string> Wads, List<string> SoundOutputs, string? Lodpack = null);

/// <summary>Build, install, uninstall, package a <see cref="ModProject"/> with smpack.</summary>
public sealed class ModBuilder(SmpackCli cli, IActivityLog? log)
{
    private void Info(string s) => log?.Write(LogKind.INFO, s);

    public async Task<BuildResult> BuildAsync(ModProject project, GameInstall game, CancellationToken ct = default)
    {
        var d = project.Data;
        var err = ModProject.ValidatePatchName(d.PatchName);

        if (err != null)
            throw new InvalidOperationException($"Patch name: {err}");

        if (project.IsEmpty)
            throw new InvalidOperationException("The project has no textures, sounds or models yet.");

        foreach (var m in d.Models)
            if (!File.Exists(project.ModelPath(m)))
                throw new InvalidOperationException($"{m.Name}: {project.ModelPath(m)} is missing. Replace the model again.");

        foreach (var t in d.Textures)
        {
            if (!File.Exists(project.DdsPath(t)) || !File.Exists(project.SidecarPath(t)))
                throw new InvalidOperationException($"{t.Stem}: DDS or sidecar missing in {project.TexturesDir}. Try adding the texture again.");
        }

        if (File.Exists(Path.Combine(game.WadDir, d.PatchName + ".texpack")) && d.Installed?.Texpack != d.PatchName &&
            !File.Exists(Path.Combine(game.WadDir, d.PatchName + ".texpack" + GameInstall.BackupSuffix)))
        {
            // A pack with this name exists and isn't ours. Don't silently shadow another mod.
            var lists = await GetPatchListsAsync(game, ct).ConfigureAwait(false);

            if (lists.PatchTexpacks.Contains(d.PatchName, StringComparer.OrdinalIgnoreCase))
                Info($"note: '{d.PatchName}' is already registered in 'boot-options.json'. It will be replaced.");
        }

        if (Directory.Exists(project.OutputDir))
            Directory.Delete(project.OutputDir, true);

        Directory.CreateDirectory(project.OutputDir);

        if (Directory.Exists(project.SourceWadDir))
            Directory.Delete(project.SourceWadDir, true);

        // Always build WAD edits from pristine originals so rebuilding never stacks edits.
        var src_wads = new List<string>();
        foreach (var w in project.WadsToRebuild(d.Options.SyncWadLowMips))
        {
            var pristine = GameInstall.Pristine(Path.Combine(game.WadDir, w));

            if (!File.Exists(pristine))
            {
                log?.Write(LogKind.WARNING, $"skipping {w}: not found in the game folder");
                continue;
            }

            Directory.CreateDirectory(project.SourceWadDir);

            var copy = Path.Combine(project.SourceWadDir, w);
            File.Copy(pristine, copy, overwrite: true);
            src_wads.Add(copy);
        }

        if (d.Textures.Count > 0)
        {
            var args = new List<string>
            {
                "tex",
                "import",
                project.TexturesDir,
                "--patch",
                d.PatchName,
                "-o",
                project.OutputDir,
                "--game",
                game.Root 
            };

            foreach (var w in src_wads)
            {
                args.Add("--wad");
                args.Add(w);
            }

            if (!d.Options.Compress)
                args.Add("--no-compress");

            Info($"building '{d.PatchName}' from {d.Textures.Count} texture(s){(src_wads.Count > 0 ? $" and {src_wads.Count} WAD(s)" : "")}...");
            (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        }

        // Models go on top of the texture edits. A WAD already in the output is the input of the next step.
        string? lodpack = null;
        if (d.Models.Count > 0)
        {
            var meshes = new MeshService(cli);
            foreach (var m in d.Models.OrderBy(m => m.Wad, StringComparer.OrdinalIgnoreCase).ThenBy(m => m.MeshId))
            {
                var input = Path.Combine(project.OutputDir, m.Wad);

                if (!File.Exists(input))
                {
                    var pristine = GameInstall.Pristine(Path.Combine(game.WadDir, m.Wad));

                    if (!File.Exists(pristine))
                        throw new InvalidOperationException($"{m.Name}: {m.Wad} was not found in the game folder.");

                    Directory.CreateDirectory(project.SourceWadDir);

                    input = Path.Combine(project.SourceWadDir, m.Wad);
                    File.Copy(pristine, input, overwrite: true);
                }

                Info($"importing {m.Name} into {m.Wad}...");

                var r = await meshes.ImportAsync(input, project.ModelPath(m), project.OutputDir, game.Root, d.PatchName, m.MeshId, append: true,
                    m.AllLods, d.Options.Compress, ct).ConfigureAwait(false);

                foreach (var w in r.Warnings)
                    log?.Write(LogKind.WARNING, $"{m.Name}: {w}");

                lodpack ??= r.Lodpack;
            }
        }

        // Sounds. Each touched audio pack is rewritten from its pristine original.
        if (Directory.Exists(project.SoundOutputDir))
            Directory.Delete(project.SoundOutputDir, true);

        foreach (var grp in d.Audio.GroupBy(a => a.Pack, StringComparer.OrdinalIgnoreCase))
        {
            var toc = Path.Combine(game.SoundDir, grp.Key + ".audiopack.toc");

            if (!File.Exists(toc))
            {
                log?.Write(LogKind.WARNING, $"skipping sounds for {grp.Key}: pack not found in the game folder");
                continue;
            }

            var files = grp.Select(project.WemPath).ToList();
            var missing = files.FirstOrDefault(f => !File.Exists(f));

            if (missing != null)
                throw new InvalidOperationException($"{missing} is missing. Replace that sound again.");

            Info($"building audio pack {grp.Key} with {files.Count} replaced sound(s)...");

            var args = new List<string> { "audio", "replace", toc };
            args.AddRange(files);
            args.AddRange(["-o", project.SoundOutputDir, "--pristine"]);

            (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        }

        var outputs = Directory.EnumerateFiles(project.OutputDir).OrderBy(f => f).ToList();
        var sounds = Directory.Exists(project.SoundOutputDir) ? Directory.EnumerateFiles(project.SoundOutputDir).OrderBy(f => f).ToList() : [];
        var tp = outputs.Any(f => f.EndsWith(".texpack", StringComparison.OrdinalIgnoreCase)) ? d.PatchName : null;
        var wads = outputs.Where(f => f.EndsWith(".wad", StringComparison.OrdinalIgnoreCase)).Select(Path.GetFileName).ToList();

        log?.Write(LogKind.SUCCESS, $"built {outputs.Count + sounds.Count} file(s) in {project.BuildDir}");

        if (lodpack == null && outputs.Any(f => f.EndsWith(".lodpack", StringComparison.OrdinalIgnoreCase)))
            lodpack = d.PatchName;

        return new BuildResult(outputs, tp, wads!, sounds, lodpack);
    }

    public async Task<PatchLists> GetPatchListsAsync(GameInstall game, CancellationToken ct = default)
    {
        var r = (await cli.RunAsync(["patch", "list", game.Root, "--json"], echo_stdout: false, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return Json.Parse<PatchLists>(r.StdOut);
    }

    public async Task SetPatchAsync(GameInstall game, string name, bool texpack, bool enable, CancellationToken ct = default)
    {
        var r = await cli.RunAsync(["patch", enable ? "add" : "remove", game.Root, texpack ? "--texpack" : "--lodpack", name], ct: ct)
            .ConfigureAwait(false);

        r.ThrowIfFailed();
    }

    public async Task InstallAsync(ModProject project, GameInstall game, BuildResult build, CancellationToken ct = default)
    {
        if (project.Data.Installed != null)
            await UninstallAsync(project, game, ct).ConfigureAwait(false);

        var installed = new InstalledState { GameRoot = game.Root, When = DateTime.Now };

        foreach (var f in build.Outputs)
        {
            var dst = GameInstall.InstallFile(f, game.WadDir, s => log?.Write(LogKind.OUTPUT, "  " + s));
            installed.Files.Add(Path.GetFileName(dst));
        }

        foreach (var f in build.SoundOutputs)
        {
            var dst = GameInstall.InstallFile(f, game.SoundDir, s => log?.Write(LogKind.OUTPUT, "  " + s));
            installed.SoundFiles.Add(Path.GetFileName(dst));
        }

        installed.Wads.AddRange(build.Wads);

        if (build.Texpack != null)
        {
            await SetPatchAsync(game, build.Texpack, texpack: true, enable: true, ct).ConfigureAwait(false);
            installed.Texpack = build.Texpack;
        }

        if (build.Lodpack != null)
        {
            await SetPatchAsync(game, build.Lodpack, texpack: false, enable: true, ct).ConfigureAwait(false);
            installed.Lodpack = build.Lodpack;
        }

        project.Data.Installed = installed;
        project.Save();

        log?.Write(LogKind.SUCCESS, $"installed '{project.Data.Name}' into {game.Root}");
    }

    public async Task UninstallAsync(ModProject project, GameInstall game, CancellationToken ct = default)
    {
        var st = project.Data.Installed;

        if (st == null)
            return;

        if (st.Texpack != null)
        {
            await SetPatchAsync(game, st.Texpack, texpack: true, enable: false, ct).ConfigureAwait(false);

            foreach (var ext in new[] { ".texpack", ".texpack.toc" })
            {
                var p = Path.Combine(game.WadDir, st.Texpack + ext);

                if (File.Exists(p + GameInstall.BackupSuffix))
                    File.Move(p + GameInstall.BackupSuffix, p, overwrite: true);
                else if (File.Exists(p))
                    File.Delete(p);
            }
        }

        if (st.Lodpack != null)
        {
            await SetPatchAsync(game, st.Lodpack, texpack: false, enable: false, ct).ConfigureAwait(false);

            foreach (var ext in new[] { ".lodpack", ".lodpack.toc" })
            {
                var p = Path.Combine(game.WadDir, st.Lodpack + ext);

                if (File.Exists(p + GameInstall.BackupSuffix))
                    File.Move(p + GameInstall.BackupSuffix, p, overwrite: true);
                else if (File.Exists(p))
                    File.Delete(p);
            }
        }

        foreach (var w in st.Wads)
        {
            var p = Path.Combine(game.WadDir, w);

            if (File.Exists(p + GameInstall.BackupSuffix))
            {
                File.Move(p + GameInstall.BackupSuffix, p, overwrite: true);
                log?.Write(LogKind.OUTPUT, $"  restored {w}");
            }
        }

        foreach (var f in st.SoundFiles)
        {
            var p = Path.Combine(game.SoundDir, f);

            if (File.Exists(p + GameInstall.BackupSuffix))
            {
                File.Move(p + GameInstall.BackupSuffix, p, overwrite: true);
                log?.Write(LogKind.OUTPUT, $"  restored {f}");
            }
        }

        project.Data.Installed = null;
        project.Save();

        log?.Write(LogKind.SUCCESS, $"uninstalled '{project.Data.Name}'");
    }

    /// <summary>Zip the build output with a short install guide, for sharing.</summary>
    public string Package(ModProject project, string zip_path)
    {
        var sounds = Directory.Exists(project.SoundOutputDir) ? Directory.EnumerateFiles(project.SoundOutputDir).ToList() : [];

        if ((!Directory.Exists(project.OutputDir) || !Directory.EnumerateFiles(project.OutputDir).Any()) && sounds.Count == 0)
            throw new InvalidOperationException("Build the mod first.");

        var d = project.Data;
        if (File.Exists(zip_path))
            File.Delete(zip_path);

        using var zip = ZipFile.Open(zip_path, ZipArchiveMode.Create);
        if (Directory.Exists(project.OutputDir))
            foreach (var f in Directory.EnumerateFiles(project.OutputDir))
                zip.CreateEntryFromFile(f, "exec/wad/pc_le/" + Path.GetFileName(f), CompressionLevel.Optimal);

        foreach (var f in sounds)
            zip.CreateEntryFromFile(f, "exec/sound/pc_le/" + Path.GetFileName(f), CompressionLevel.Fastest);

        var sb = new StringBuilder();
        sb.AppendLine(d.Name);

        if (!string.IsNullOrWhiteSpace(d.Author))
            sb.AppendLine($"by {d.Author}");

        if (!string.IsNullOrWhiteSpace(d.Description))
            sb.AppendLine().AppendLine(d.Description);

        sb.AppendLine().AppendLine("Install");
        sb.AppendLine("  1. Back up 'exec\\boot-options.json' and every .wad or audio pack this archive contains.");
        sb.AppendLine("  2. Copy the exec folder into the God of War Ragnarok folder.");

        if (d.Textures.Any(t => t.IsTexpack))
        {
            sb.AppendLine($"  3. Add \"{d.PatchName}\" to \"patch-texpacks\" in 'exec\\boot-options.json', e.g.");
            sb.AppendLine($"       \"patch-texpacks\": [\"{d.PatchName}\"],");
            sb.AppendLine($"     or run:  smpack patch add \"<game folder>\" --texpack {d.PatchName}");
        }

        if (Directory.Exists(project.OutputDir) && Directory.EnumerateFiles(project.OutputDir, "*.lodpack").Any())
        {
            sb.AppendLine($"  {(d.Textures.Any(t => t.IsTexpack) ? 4 : 3)}. Add \"{d.PatchName}\" to \"patch-lodpacks\" in 'exec\\boot-options.json', e.g.");
            sb.AppendLine($"       \"patch-lodpacks\": [\"{d.PatchName}\"],");
            sb.AppendLine($"     or run:  smpack patch add \"<game folder>\" --lodpack {d.PatchName}");
        }

        sb.AppendLine().AppendLine($"Built with smpack GUI on {DateTime.Now:yyyy-MM-dd}. Textures: {d.Textures.Count}. Sounds: {d.Audio.Count}. Models: {d.Models.Count}.");
        var e = zip.CreateEntry("README.txt");

        using (var w = new StreamWriter(e.Open(), new UTF8Encoding(false)))
            w.Write(sb.ToString());

        return zip_path;
    }
}