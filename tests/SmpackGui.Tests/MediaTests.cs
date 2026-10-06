using SmpackGui.Core.Audio;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Meshes;
using SmpackGui.Core.Mods;

namespace SmpackGui.Tests;

/// <summary>Models and audio.</summary>
public static class MediaTests
{
    static int m_Fail, m_Pass;
    static void Check(bool ok, string what)
    {
        if (ok)
        {
            m_Pass++;
            Console.WriteLine($"ok   {what}");
        }
        else
        {
            m_Fail++;
            Console.WriteLine($"FAIL {what}");
        }
    }

    /// <summary>Models only. List, export, preview and import (the .lodpack must sit next to the WAD).</summary>
    public static async Task<int> ModelsOnlyAsync(string[] a)
    {
        var cli = new SmpackCli(a[1], new ConsoleLog());

        if (Directory.Exists(a[3]))
            Directory.Delete(a[3], true);

        Directory.CreateDirectory(a[3]);
        await ModelsAsync(cli, a[2], a[3]);

        Console.WriteLine($"{m_Pass} passed, {m_Fail} failed");
        return m_Fail == 0 ? 0 : 1;
    }

    static async Task ModelsAsync(SmpackCli cli, string wad, string work)
    {
        var ms = new MeshService(cli);
        var list = await ms.ListAsync(wad);
        Check(list.Models.Count > 100, $"mesh list: {list.Models.Count} models, {list.Lodpacks} lodpacks");

        var gate = list.Models.First(m => m.Name == "main_gate_0");
        Check(gate.Kind == "model" && gate.Triangles > 10000 && gate.Joints == 5, $"main_gate_0 {gate.Kind} {gate.Triangles} tris, {gate.Joints} joints");

        foreach (var fmt in new[] { MeshFormat.GLB, MeshFormat.GLTF, MeshFormat.FBX })
        {
            var res = await ms.ExportAsync(wad, Path.Combine(work, "models"), new MeshExportOptions { Format = fmt }, id: gate.Id);
            var f = res.Models[0].File;

            var payload = fmt == MeshFormat.GLTF ? Path.ChangeExtension(f, ".bin") : f;
            Check(File.Exists(f) && new FileInfo(payload).Length > 100_000, $"export {fmt}: {Path.GetFileName(f)} {new FileInfo(f).Length} bytes");

            if (fmt == MeshFormat.GLB)
            {
                var pm = GlbReader.Load(f);
                Check(pm.TriangleCount == gate.Triangles, $"glb preview: {pm.Nodes.Count} nodes, {pm.TriangleCount} tris (shadow proxies skipped)");
                Check(pm.MaterialImages.Any(u => u.StartsWith("textures/")), "glb materials link textures");

                var tex = MeshService.TexturesToWrite(res, false).ToList();
                Check(tex.Any(t => t.Normal) && tex.Any(t => !t.Normal), $"textures to write: {string.Join(", ", tex)}");
            }
        }

        var all = await ms.ExportAsync(wad, Path.Combine(work, "all"), new MeshExportOptions { Format = MeshFormat.GLB, Lod = -1 }, filter: "rock_pile");
        Check(all.Models.Count >= 2, $"filtered export of every LOD: {all.Models.Count} models");


        // Import the unchanged export back. Same triangle count, a patched WAD and a patch lodpack.
        var exp = await ms.ExportAsync(wad, Path.Combine(work, "rt"), new MeshExportOptions { Format = MeshFormat.GLB }, id: gate.Id);
        var out_dir = Path.Combine(work, "imported");

        var imp = await ms.ImportAsync(wad, exp.Models[0].File, out_dir, Path.GetDirectoryName(Path.GetFullPath(wad))!, "zz_test", gate.Id, append: false);
        Check(imp.Model == "main_gate_0" && imp.Prims > 0 && imp.Files.Any(f => f.EndsWith(".wad")), $"import: {imp.Prims} prims, {imp.Triangles} tris, lodpack {imp.Lodpack}");
        Check(imp.Lodpack == null || File.Exists(Path.Combine(out_dir, imp.Lodpack + ".lodpack")), "patch lodpack written");
    }

    public static async Task<int> RunAsync(string[] a)
    {
        var log = new ConsoleLog();
        var cli = new SmpackCli(a[1], log);

        string wad = a[2], toc = a[3], vgm = a[4], boot = a[5], work = a[6];

        if (Directory.Exists(work))
            Directory.Delete(work, true);

        Directory.CreateDirectory(work);

        await ModelsAsync(cli, wad, work);

        // Audio.
        var pack = new AudioPack { TocPath = toc };
        await pack.LoadAsync(cli);

        Check(pack.Streams.Count == 11, $"audio pack: {pack.Streams.Count} streams");

        var s0 = pack.Streams.OrderBy(s => s.Size).First();
        var wem = pack.ReadWem(s0);
        Check(wem.Length == s0.Size && wem[0] == 'R' && wem[1] == 'I', "read wem bytes");

        var conv = new AudioConverter(new Vgmstream(log) { ExePath = vgm }, new WwiseConverter(log), Path.Combine(work, "cache"));
        var wav = await conv.PreviewWavAsync("t", () => wem);
        var w = WavFile.Load(wav);
        Check(w.Channels <= 2 && w.Duration.TotalSeconds > 0.1, $"preview wav: {w.Channels} ch, {w.SampleRate} Hz, {w.Duration.TotalSeconds:0.00} s");

        var surround = pack.Streams.Select(s => (s, b: pack.ReadWem(s))).First(x => x.b[22] == 6);
        var sw = await conv.PreviewWavAsync("six", () => surround.b);

        Check(WavFile.Load(sw).Channels == 2, "5.1 stream folded to stereo for preview");
        await conv.ExportAsync(wem, Path.Combine(work, "x.wav"), AudioExportFormat.WAV);

        Check(File.Exists(Path.Combine(work, "x.wav")), "export wav");

        var r = MediaFoundation.Resample(w, 44100);
        Check(Math.Abs(r.Duration.TotalSeconds - w.Duration.TotalSeconds) < 0.01, "resample keeps duration");

        // Audio in a mod. Build, install, uninstall on a scratch game folder.
        var root = Path.Combine(work, "game");

        Directory.CreateDirectory(Path.Combine(root, "exec", "wad", "pc_le"));
        Directory.CreateDirectory(Path.Combine(root, "exec", "sound", "pc_le"));

        File.Copy(boot, Path.Combine(root, "exec", "boot-options.json"));

        var name = GameInstall.PackBase(Path.GetFileName(toc));

        File.Copy(toc, Path.Combine(root, "exec", "sound", "pc_le", Path.GetFileName(toc)));
        File.Copy(pack.PartPath(0), Path.Combine(root, "exec", "sound", "pc_le", $"{name}.0.audiopack"));

        var game = new GameInstall(root);
        Check(AudioPack.Enumerate(game).Count == 1, "audio packs enumerated");

        var orig_part = File.ReadAllBytes(Path.Combine(game.SoundDir, $"{name}.0.audiopack"));

        var proj = ModProject.Create(Path.Combine(work, "proj"), "Sound test");
        var big = pack.Streams.OrderBy(s => s.Size).Skip(1).First();

        var e = new ModAudioEntry { FileId = s0.FileId, Pack = name, SourceFile = "other.wem" };
        await conv.ToWemAsync(WriteTemp(work, pack.ReadWem(big)), proj.WemPath(e), null);

        e.Size = new FileInfo(proj.WemPath(e)).Length;
        proj.UpsertAudio(e);

        var builder = new ModBuilder(cli, log);
        var build = await builder.BuildAsync(proj, game);

        Check(build.SoundOutputs.Count == 2 && build.Outputs.Count == 0, $"build: {build.SoundOutputs.Count} sound file(s)");

        await builder.InstallAsync(proj, game, build);
        Check(File.Exists(Path.Combine(game.SoundDir, $"{name}.0.audiopack.smpack-orig")), "original audio pack backed up");

        var installed = new AudioPack { TocPath = Path.Combine(game.SoundDir, Path.GetFileName(toc)) };
        var listed = Json(await cli.RunAsync(["list", installed.TocPath, "--json"], echo_stdout: false));
        Check(listed.Contains($"\"size\": {big.Size}"), "installed pack holds the replacement size");

        var build2 = await builder.BuildAsync(proj, game);
        Check(File.ReadAllBytes(build2.SoundOutputs.First(f => f.EndsWith(".0.audiopack"))).AsSpan()
            .SequenceEqual(File.ReadAllBytes(build.SoundOutputs.First(f => f.EndsWith(".0.audiopack")))), "rebuild from pristine is identical");

        await builder.UninstallAsync(proj, game);
        Check(File.ReadAllBytes(Path.Combine(game.SoundDir, $"{name}.0.audiopack")).AsSpan().SequenceEqual(orig_part), "uninstall restores the pack");

        var zip = builder.Package(proj, Path.Combine(work, "sound.zip"));
        Check(new System.IO.Compression.ZipArchive(File.OpenRead(zip)).Entries.Any(z => z.FullName.StartsWith("exec/sound/pc_le/")), "package has the audio pack");

        Console.WriteLine($"\n{m_Pass} passed, {m_Fail} failed");
        return m_Fail == 0 ? 0 : 1;
    }

    static string WriteTemp(string work, byte[] b)
    {
        var p = Path.Combine(work, "repl.wem");
        File.WriteAllBytes(p, b);

        return p;
    }

    static string Json(CliResult r) => r.StdOut;
}