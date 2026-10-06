using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Audio;

public sealed class AudioStream
{
    public uint FileId
    {
        get;
        set;
    }
    public long Size
    {
        get;
        set;
    }
    public int Part
    {
        get;
        set;
    }
    public long Offset
    {
        get;
        set;
    }
}

internal sealed class AudioListDoc
{
    public List<AudioStream> Entries
    {
        get;
        set;
    } = [];
}

/// <summary>A Wwise stream pack '<base>.audiopack.toc' plus '<base><n>.audiopack' parts.</summary>
public sealed class AudioPack
{
    public required string TocPath
    {
        get;
        init;
    }
    public string Name => GameInstall.PackBase(Path.GetFileName(TocPath));
    public string Dir => Path.GetDirectoryName(TocPath)!;
    public List<AudioStream> Streams
    {
        get;
        private set;
    } = [];

    public string PartPath(int part) => Path.Combine(Dir, $"{Name}.{part}.audiopack");

    /// <summary>Every pack in the game's sound folder.</summary>
    public static List<AudioPack> Enumerate(GameInstall game)
    {
        if (!Directory.Exists(game.SoundDir))
            return [];

        return Directory.EnumerateFiles(game.SoundDir, "*.audiopack.toc")
            .Where(f => File.Exists(Path.Combine(game.SoundDir, $"{GameInstall.PackBase(Path.GetFileName(f))}.0.audiopack")))
            .OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
            .Select(f => new AudioPack { TocPath = f })
            .ToList();
    }

    public async Task LoadAsync(SmpackCli cli, CancellationToken ct = default)
    {
        // The pristine table. A modded pack must still list and play the original streams.
        var toc = GameInstall.Pristine(TocPath);
        var args = toc == TocPath ? new List<string> { "list", TocPath, "--json" } : ["list", StageBackup(toc), "--json"];
        var r = (await cli.RunAsync(args, echo_stdout: false, ct: ct).ConfigureAwait(false)).ThrowIfFailed();

        Streams = Json.Parse<AudioListDoc>(r.StdOut).Entries;
    }

    /// <summary>smpack detects packs by name, give a backup .toc its real name in a temp folder.</summary>
    private string StageBackup(string backup)
    {
        var dir = Path.Combine(Path.GetTempPath(), "SmpackGui", "toc");
        Directory.CreateDirectory(dir);

        var dst = Path.Combine(dir, Path.GetFileName(TocPath));
        File.Copy(backup, dst, overwrite: true);
        return dst;
    }

    /// <summary>Raw .wem bytes of one stream from the original part when a modded copy is installed.</summary>
    public byte[] ReadWem(AudioStream s)
    {
        using var fs = File.OpenRead(GameInstall.Pristine(PartPath(s.Part)));
        fs.Seek(s.Offset, SeekOrigin.Begin);

        var buf = new byte[s.Size];
        fs.ReadExactly(buf);
        return buf;
    }
}