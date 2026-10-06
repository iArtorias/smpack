using System.Data;
using System.Text.Json.Nodes;
using SmpackGui.Core.Cli;

namespace SmpackGui.Core.Game;

public sealed class ExtractOptions
{
    public string? Filter
    {
        get;
        set;
    }
    public bool Raw
    {
        get;
        set;
    }
    public bool NoDds
    {
        get;
        set;
    }
    public bool Blocks
    {
        get;
        set;
    }
}

/// <summary>Generic archive operations (info, list, extract, rebuild) for every format smpack knows.</summary>
public sealed class ArchiveService(SmpackCli cli)
{
    public async Task<string> InfoAsync(string path, CancellationToken ct = default)
    {
        var r = await cli.RunAsync(["info", path], echo_stdout: false, ct: ct).ConfigureAwait(false);
        return r.Success ? r.StdOut.TrimEnd() : "error: " + r.ErrorMessage;
    }

    /// <summary>Contents of a file as a table (column set depends on the format).</summary>
    public async Task<DataTable> ListAsync(GameFile file, string? index_path, CancellationToken ct = default)
    {
        var path = file.Path;

        List<string> args = file.Kind == GameFileKind.TEXPACK
            ? ["tex", "list", path, "--json"]
            : ["list", path, "--json"];

        if (file.Kind == GameFileKind.TEXPACK && index_path != null && File.Exists(index_path))
        {
            args.Add("--index");
            args.Add(index_path);
        }

        var r = (await cli.RunAsync(args, echo_stdout: false, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        var doc = JsonNode.Parse(r.StdOut) as JsonObject ?? throw new InvalidDataException("unexpected smpack output");
        var arr = (doc["entries"] ?? doc["textures"]) as JsonArray ?? [];

        return ToTable(arr);
    }

    public static DataTable ToTable(JsonArray arr)
    {
        var t = new DataTable();

        foreach (var n in arr)
        {
            if (n is not JsonObject o)
                continue;

            foreach (var (k, v) in o)
            {
                if (t.Columns.Contains(k))
                    continue;

                var type = v is JsonValue jv && jv.TryGetValue<long>(out _) ? typeof(long)
                    : v is JsonValue jb && jb.TryGetValue<bool>(out _) ? typeof(bool) : typeof(string);
                t.Columns.Add(k, type);
            }
        }

        t.BeginLoadData();

        foreach (var n in arr)
        {
            if (n is not JsonObject o)
                continue;

            var row = t.NewRow();

            foreach (var (k, v) in o)
            {
                var col = t.Columns[k]!;

                if (v == null)
                    continue;

                if (col.DataType == typeof(long) && v is JsonValue a && a.TryGetValue<long>(out var l))
                    row[k] = l;
                else if (col.DataType == typeof(bool) && v is JsonValue b && b.TryGetValue<bool>(out var bb))
                    row[k] = bb;
                else
                    row[k] = v is JsonValue s && s.TryGetValue<string>(out var str) ? str : v.ToJsonString();
            }

            t.Rows.Add(row);
        }

        t.EndLoadData();
        return t;
    }

    public async Task<CliResult> ExtractAsync(string path, string out_dir, ExtractOptions o, CancellationToken ct = default)
    {
        var args = new List<string> { "extract", path, "-o", out_dir };

        if (!string.IsNullOrWhiteSpace(o.Filter))
        {
            args.Add("--filter");
            args.Add(o.Filter);
        }

        if (o.Raw)
            args.Add("--raw");

        if (o.NoDds)
            args.Add("--no-dds");

        if (o.Blocks)
            args.Add("--blocks");

        return (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
    }

    public async Task<string> WadPackAsync(string extracted_dir, string out_file, bool compress, CancellationToken ct = default)
    {
        var args = new List<string>
        { 
            "wad", 
            "pack",
            extracted_dir,
            "-o",
            out_file 
        };

        if (!compress)
            args.Add("--no-compress");

        (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return out_file;
    }

    public async Task<string> WadReplaceAsync(string wad, string selector, string file, string out_dir, CancellationToken ct = default)
    {
        (await cli.RunAsync(["wad", "replace", wad, selector, file, "-o", out_dir], ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return Path.Combine(out_dir, Path.GetFileName(wad).Replace(GameInstall.BackupSuffix, ""));
    }

    public async Task<List<string>> AnimReplaceAsync(string as_file, string wad, IEnumerable<(string Set, string Blob)> pairs, string out_dir,
        bool keep_identity, CancellationToken ct = default)
    {
        var args = new List<string> 
        { 
            "anim",
            "replace",
            as_file,
            wad, 
            "-o",
            out_dir
        };

        foreach (var (s, b) in pairs)
        {
            args.AddRange(["--set", s, "--blob", b]);
        }

        if (!keep_identity)
            args.Add("--no-identity");

        (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return [Path.Combine(out_dir, Path.GetFileName(as_file)), Path.Combine(out_dir, Path.GetFileName(wad))];
    }

    public async Task<string> ShaderReplaceAsync(string pack, string selector, string dxbc, string out_file, CancellationToken ct = default)
    {
        (await cli.RunAsync(["shader", "replace", pack, selector, dxbc, "-o", out_file], ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return out_file;
    }

    public async Task<List<string>> AudioReplaceAsync(string toc, IEnumerable<string> wems, string out_dir, bool add, CancellationToken ct = default)
    {
        var args = new List<string> 
        { 
            "audio",
            "replace",
            toc
        };

        args.AddRange(wems);
        args.AddRange(["-o", out_dir]);

        if (add)
            args.Add("--add");

        (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        var name = GameInstall.PackBase(Path.GetFileName(toc));
        return [.. Directory.EnumerateFiles(out_dir, name + ".*").Where(f => f.Contains(".audiopack"))];
    }

    public async Task<List<string>> LodPatchAsync(string pack, IEnumerable<string> blocks, string patch_name, string out_dir, CancellationToken ct = default)
    {
        var args = new List<string> 
        { 
            "lod",
            "patch",
            "--pack",
            pack
        };

        args.AddRange(blocks);
        args.AddRange(["--patch", patch_name, "-o", out_dir]);

        (await cli.RunAsync(args, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return [Path.Combine(out_dir, patch_name + ".lodpack"), Path.Combine(out_dir, patch_name + ".lodpack.toc")];
    }

    /// <summary>Build 'texindex.json', reporting 'scanned N/M wads' progress.</summary>
    public async Task BuildIndexAsync(string wad_dir, string out_file, IProgress<(int Done, int Total)>? progress, CancellationToken ct = default)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(out_file)!);
        int total = Directory.Exists(wad_dir) ? Directory.EnumerateFiles(wad_dir, "*.wad").Count() : 0;
        progress?.Report((0, total));
        var tmp = out_file + ".part";

        var r = await cli.RunAsync(["index", wad_dir, "-o", tmp], on_stderr: line =>
        {
            // 'scanned 50/1185 wads...'
            var m = System.Text.RegularExpressions.Regex.Match(line, @"scanned (\d+)/(\d+)");

            if (m.Success)
                progress?.Report((int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value)));
        }, ct: ct).ConfigureAwait(false);

        r.ThrowIfFailed();
        File.Move(tmp, out_file, overwrite: true);
        progress?.Report((total, total));
    }
}