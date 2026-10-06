using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Imaging;
using SmpackGui.Core.Models;
using SmpackGui.Core.Mods;
using SmpackGui.Core.Textures;

namespace SmpackGui.Tests;

sealed class ConsoleLog : IActivityLog
{
    public void Write(LogKind kind, string text) => Console.WriteLine($"   [{kind}] {text}");
}

public static class UnitTests
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

    // Args: 'integration <smpack> <game_root> <work_dir>'
    public static async Task<int> RunAsync(string[] args)
    {
        if (args.Length < 4 || args[0] != "integration")
        {
            Console.WriteLine("usage: integration <smpack> <game> <work>");
            return 2;
        }

        var log = new ConsoleLog();
        var cli = new SmpackCli(args[1], log);
        var game = new GameInstall(args[2]);
        var work = args[3];

        if (Directory.Exists(work))
            Directory.Delete(work, true);

        Directory.CreateDirectory(work);

        Check(GameInstall.LooksValid(game.Root, out var why), "game folder valid " + why);

        var ver = await cli.GetVersionAsync();
        Check(ver != null && ver >= SmpackCli.MinVersion, $"smpack version {ver}");

        // files
        var files = game.EnumerateFiles();

        Check(files.Any(f => f.Kind == GameFileKind.TEXPACK && f.Companion != null), "texpack listed with its toc");
        Check(files.Count(f => f.Kind == GameFileKind.TEXPACK) == 1, "texpack pair listed once");

        // Archives.
        var arch = new ArchiveService(cli);
        foreach (var f in files)
        {
            var t = await arch.ListAsync(f, null);
            Check(t.Rows.Count > 0, $"list {f.Name}: {t.Rows.Count} rows, cols {string.Join(',', t.Columns.Cast<System.Data.DataColumn>().Select(c => c.ColumnName))}");

            var info = await arch.InfoAsync(f.Path);
            Check(!info.StartsWith("error"), $"info {f.Name}");
        }

        // Index.
        var idx_path = Path.Combine(work, "texindex.json");
        int last_done = -1;
        await arch.BuildIndexAsync(game.WadDir, idx_path, new Progress<(int Done, int Total)>(p => last_done = p.Done));

        var index = Json.Read<TexIndex>(idx_path);
        var items = TextureCatalog.FromIndex(index);

        Check(items.Count == 132, $"catalog items {items.Count}");
        Check(items.Count(i => i.LowMipsOnly) + items.Count(i => !i.Streamed) == 132, $"streamed textures w/o indexed pack: {items.Count(i => i.LowMipsOnly)}, WAD resident: {items.Count(i => !i.Streamed)}");

        // Unnamed texpack textures.
        var tl = Json.Parse<TexList>((await cli.RunAsync(["tex", "list", Path.Combine(game.WadDir, "170_midgard10_postgame.texpack"), "--json"], echo_stdout: false)).StdOut);
        var unnamed = TextureCatalog.Unnamed(tl, items);
        Check(unnamed.Count == 109, $"unnamed texpack textures {unnamed.Count}");

        // Exporter.
        var exporter = new TextureExporter(cli, () => game, Path.Combine(work, "cache"));
        var tp_item = unnamed.First(u => u.Width == 1024 && u.Format.StartsWith("Bc1"));
        var e1 = await exporter.ExportAsync(tp_item);

        var dds1 = DdsFile.Load(e1.DdsPath);
        Check(dds1.Width == tp_item.Width && dds1.HasFullChain, $"texpack export {e1.Stem} {dds1.Width}x{dds1.Height} mips {dds1.MipCount}");

        var e1b = await exporter.ExportAsync(tp_item);
        Check(e1b.DdsPath == e1.DdsPath, "export cache hit");

        var dec = TextureDecoder.Decode(dds1, 0, 2);
        Check(dec.Width == 256, "decode mip 2");

        var wad_item = items.First(i => i.Name.Contains("wolf00_head_d"));
        var e2 = await exporter.ExportAsync(wad_item, wad_item.Wads[0]);

        var sc2 = e2.LoadSidecar();
        Check(sc2.IsWad && sc2.Streamed && sc2.Wad == "add_atreusplayable00.wad", $"wad export sidecar wad={sc2.Wad} {sc2.Width}x{sc2.Height}");

        // Project and replacer (DDS inputs, no texconv).
        var texconv = new Texconv(log);

        var proj = ModProject.Create(Path.Combine(work, "proj"), "Test Mod");
        Check(proj.Data.PatchName == "mod_test_mod", "patch name suggestion " + proj.Data.PatchName);

        var rep = new TextureReplacer(exporter, texconv, log);

        // Make a modified copy of the original texpack texture: invert BC1 endpoints -> different colours.
        var mod_dds = Path.Combine(work, "edited.dds");
        var bytes = File.ReadAllBytes(e1.DdsPath);

        for (int i = dds1.DataOffset; i + 8 <= bytes.Length; i += 8)
        {
            (bytes[i], bytes[i + 2]) = (bytes[i + 2], bytes[i]);
            (bytes[i + 1], bytes[i + 3]) = (bytes[i + 3], bytes[i + 1]);
        }

        File.WriteAllBytes(mod_dds, bytes);

        var r1 = await rep.ReplaceAsync(proj, new ReplaceRequest { Texture = tp_item, SourceFile = mod_dds });
        Check(r1.Entries.Count == 1 && r1.Entries[0].IsTexpack, "texpack replacement queued");

        // Wrong mip count must be refused without texconv.
        var bad = Path.Combine(work, "bad.dds");
        var hdr = (byte[])bytes.Clone();

        BitConverter.GetBytes(3).CopyTo(hdr, 28);
        File.WriteAllBytes(bad, hdr);

        try
        {
            await rep.ReplaceAsync(proj, new ReplaceRequest { Texture = tp_item, SourceFile = bad });
            Check(false, "bad mips refused");
        }
        catch (InvalidOperationException ex)
        {
            Check(ex.Message.Contains("mips"), "bad mips refused: " + ex.Message);
        }

        Check(proj.Data.Textures.Count == 1, "failed replace leaves project unchanged");

        // WAD low mip copy replacement (same size).
        var r2 = await rep.ReplaceAsync(proj, new ReplaceRequest { Texture = wad_item, SourceFile = e2.DdsPath });
        Check(r2.Entries.Count == 1 && !r2.Entries[0].IsTexpack && r2.Entries[0].Wad == "add_atreusplayable00.wad", "wad replacement queued");

        // Build, install, uninstall.
        var builder = new ModBuilder(cli, log);
        var build = await builder.BuildAsync(proj, game);
        Check(build.Texpack == "mod_test_mod" && build.Wads.Count == 1, $"build outputs: {string.Join(", ", build.Outputs.Select(Path.GetFileName))}");

        var orig_wad = File.ReadAllBytes(Path.Combine(game.WadDir, "add_atreusplayable00.wad"));
        await builder.InstallAsync(proj, game, build);

        var lists = await builder.GetPatchListsAsync(game);
        Check(lists.PatchTexpacks.Contains("mod_test_mod"), "patch registered");
        Check(File.Exists(Path.Combine(game.WadDir, "add_atreusplayable00.wad.smpack-orig")), "wad backed up");
        Check(File.Exists(Path.Combine(game.WadDir, "mod_test_mod.texpack")), "patch pack installed");

        // The patched texture decodes to the edited data.
        var ptl = Json.Parse<TexList>((await cli.RunAsync(["tex", "list", Path.Combine(game.WadDir, "mod_test_mod.texpack"), "--json"], echo_stdout: false)).StdOut);
        Check(ptl.Textures.Count == 1 && Json.ParseHex(ptl.Textures[0].ContentHash) == tp_item.ContentHash, "patch pack holds the texture");

        var check = Path.Combine(work, "check");
        await cli.RunAsync(["tex", "export", Path.Combine(game.WadDir, "mod_test_mod.texpack"), "-o", check]);

        var back = DdsFile.Load(Directory.GetFiles(check, "*.dds")[0]);
        Check(back.Data.AsSpan(back.DataOffset).SequenceEqual(bytes.AsSpan(dds1.DataOffset)), "round trip: patch pack texels matches edited DDS");

        // Rebuild from pristine after install still works (reads .smpack-orig).
        var build2 = await builder.BuildAsync(proj, game);
        await builder.InstallAsync(proj, game, build2);
        Check(proj.Data.Installed != null, "reinstall");

        await builder.UninstallAsync(proj, game);
        lists = await builder.GetPatchListsAsync(game);

        Check(!lists.PatchTexpacks.Contains("mod_test_mod"), "patch unregistered");
        Check(!File.Exists(Path.Combine(game.WadDir, "mod_test_mod.texpack")), "patch pack removed");
        Check(File.ReadAllBytes(Path.Combine(game.WadDir, "add_atreusplayable00.wad")).AsSpan().SequenceEqual(orig_wad), "wad restored byte-identical");
        Check(!File.Exists(Path.Combine(game.WadDir, "add_atreusplayable00.wad.smpack-orig")), "backup consumed");

        var zip = builder.Package(proj, Path.Combine(work, "mod.zip"));
        Check(File.Exists(zip), "package zip");

        // Reopen project.
        var re = ModProject.Open(proj.Dir);
        Check(re.Data.Textures.Count == 2, "project reload");

        Console.WriteLine($"\n{m_Pass} passed, {m_Fail} failed");
        return m_Fail == 0 ? 0 : 1;
    }
}