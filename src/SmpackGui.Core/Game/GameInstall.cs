namespace SmpackGui.Core.Game;

public enum GameFileKind { WAD, TEXPACK, LODPACK, ANIM_SET, SHADERPACK, AUDIOPACK, WYPDB, DATA_COMPILER, OTHER }

public sealed class GameFile
{
    public required string Path
    { 
        get; 
        init;
    }
    public required GameFileKind Kind
    {
        get;
        init;
    }
    public long Size
    {
        get;
        init;
    }
    public DateTime Modified
    {
        get;
        init;
    }
    public bool HasBackup
    {
        get;
        init;
    }
    /// <summary>Companion file (e.g. the .toc of a texpack) if any.</summary>
    public string? Companion
    {
        get;
        init;
    }
    public string Name => System.IO.Path.GetFileName(Path);
    public string BaseName => GameInstall.PackBase(Name);

    public string KindLabel => Kind switch
    {
        GameFileKind.WAD => "WAD",
        GameFileKind.TEXPACK => "Texture pack",
        GameFileKind.LODPACK => "Geometry pack",
        GameFileKind.ANIM_SET => "Animation sets",
        GameFileKind.SHADERPACK => "Shader pack",
        GameFileKind.AUDIOPACK => "Audio pack",
        GameFileKind.WYPDB => "WYP stitching DB",
        GameFileKind.DATA_COMPILER => "Data compiler (DCB)",
        _ => "Other",
    };
}

public sealed class Backup
{
    public required string BackupPath
    {
        get;
        init;
    }
    public string OriginalPath => BackupPath[..^GameInstall.BackupSuffix.Length];
    public string Name => Path.GetFileName(OriginalPath);
    public long Size
    {
        get;
        init;
    }
    public DateTime Created
    {
        get;
        init;
    }
    public bool OriginalExists => File.Exists(OriginalPath);
}

/// <summary>Layout of a God of War Ragnarok install and safe file installation with '*.smpack-orig' backups.</summary>
public sealed class GameInstall
{
    public const string BackupSuffix = ".smpack-orig";

    public string Root { get; }
    public string ExecDir => Path.Combine(Root, "exec");
    public string WadDir => Path.Combine(ExecDir, "wad", "pc_le");
    public string SoundDir => Path.Combine(ExecDir, "sound", "pc_le");
    public string DcDir => Path.Combine(ExecDir, "dc", "pc_le");
    public string BootOptions => Path.Combine(ExecDir, "boot-options.json");
    public string Exe => Path.Combine(Root, "GoWR.exe");

    public GameInstall(string root) => Root = Path.GetFullPath(root);

    public static bool LooksValid(string? root, out string reason)
    {
        reason = "";
        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
        { 
            reason = "Folder does not exist.";
            return false;
        }
        if (!Directory.Exists(Path.Combine(root, "exec", "wad", "pc_le")))
        {
            reason = "No 'exec\\wad\\pc_le' folder here. Pick the folder that contains GoWR.exe.";
            return false;
        }

        if (!File.Exists(Path.Combine(root, "exec", "boot-options.json")))
        {
            reason = "'exec\\boot-options.json' is missing.";
            return false;
        }

        return true;
    }

    /// <summary>Walk up from any path inside the game to its root.</summary>
    public static string? FindRoot(string? start)
    {
        if (string.IsNullOrEmpty(start))
            return null;

        var d = new DirectoryInfo(start);

        for (int i = 0; i < 8 && d != null; i++, d = d.Parent)
            if (LooksValid(d.FullName, out _))
                return d.FullName;

        return null;
    }

    /// <summary>Treat 'foo.texpack.toc' as 'foo'.</summary>
    public static string PackBase(string file_name)
    {
        foreach (var suf in new[] { ".audiopack.toc", ".texpack.toc", ".lodpack.toc", ".texpack", ".lodpack", ".shaderpack", ".wad.bak",
                     ".wad", ".wypdb", ".as", ".dcb" })
            if (file_name.Length > suf.Length && file_name.EndsWith(suf, StringComparison.OrdinalIgnoreCase))
                return file_name[..^suf.Length];

        return Path.GetFileNameWithoutExtension(file_name);
    }

    public static GameFileKind KindOf(string name)
    {
        var n = name.ToLowerInvariant();

        if (n.EndsWith(".wad"))
            return GameFileKind.WAD;

        if (n.EndsWith(".texpack") || n.EndsWith(".texpack.toc"))
            return GameFileKind.TEXPACK;

        if (n.EndsWith(".lodpack") || n.EndsWith(".lodpack.toc"))
            return GameFileKind.LODPACK;

        if (n.EndsWith(".as"))
            return GameFileKind.ANIM_SET;

        if (n.EndsWith(".shaderpack"))
            return GameFileKind.SHADERPACK;

        if (n.EndsWith(".audiopack.toc") || n.EndsWith(".audiopack"))
            return GameFileKind.AUDIOPACK;

        if (n.EndsWith(".wypdb"))
            return GameFileKind.WYPDB;

        if (n.EndsWith(".dcb"))
            return GameFileKind.DATA_COMPILER;

        return GameFileKind.OTHER;
    }

    /// <summary>All moddable files. Pack pairs are listed once as the payload and companion toc.</summary>
    public List<GameFile> EnumerateFiles()
    {
        var list = new List<GameFile>();

        void Add(string dir, SearchOption opt = SearchOption.TopDirectoryOnly)
        {
            if (!Directory.Exists(dir))
                return;

            foreach (var f in Directory.EnumerateFiles(dir, "*", opt))
            {
                var name = Path.GetFileName(f);

                if (name.EndsWith(BackupSuffix, StringComparison.OrdinalIgnoreCase) || name.EndsWith(".tmp"))
                    continue;

                var kind = KindOf(name);

                if (kind == GameFileKind.OTHER)
                    continue;

                string? companion = null;
                var lower = name.ToLowerInvariant();

                if (lower.EndsWith(".texpack.toc") || lower.EndsWith(".lodpack.toc"))
                {
                    // Listed with its payload unless the payload is missing.
                    if (File.Exists(f[..^4]))
                        continue;
                }
                else if (lower.EndsWith(".texpack") || lower.EndsWith(".lodpack"))
                {
                    companion = File.Exists(f + ".toc") ? f + ".toc" : null;
                }
                else if (lower.EndsWith(".audiopack"))
                {
                    continue; // Parts are reached through the .toc
                }

                var fi = new FileInfo(f);
                list.Add(new GameFile
                {
                    Path = f,
                    Kind = kind,
                    Size = fi.Length,
                    Modified = fi.LastWriteTime,
                    Companion = companion,
                    HasBackup = File.Exists(f + BackupSuffix),
                });
            }
        }

        Add(WadDir);
        Add(SoundDir);
        Add(DcDir);
        return list;
    }

    public List<Backup> EnumerateBackups()
    {
        var res = new List<Backup>();

        foreach (var dir in new[]
        {
            ExecDir,
            WadDir,
            SoundDir,
            DcDir
        })
        {
            if (!Directory.Exists(dir))
                continue;

            foreach (var f in Directory.EnumerateFiles(dir, "*" + BackupSuffix))
            {
                var fi = new FileInfo(f);
                res.Add(new Backup
                {
                    BackupPath = f,
                    Size = fi.Length,
                    Created = fi.CreationTime
                });
            }
        }
        return res;
    }

    /// <summary>Path of the untouched original of a game file (its backup if one exists).</summary>
    public static string Pristine(string path) => File.Exists(path + BackupSuffix) ? path + BackupSuffix : path;

    /// <summary>Copy <paramref name="source"/> into <paramref name="dest_dir"/>, backing up an existing file once.</summary>
    public static string InstallFile(string source, string dest_dir, Action<string>? log = null)
    {
        Directory.CreateDirectory(dest_dir);
        var dst = Path.Combine(dest_dir, Path.GetFileName(source));

        if (File.Exists(dst) && !File.Exists(dst + BackupSuffix))
        {
            File.Copy(dst, dst + BackupSuffix);
            log?.Invoke($"backup  {Path.GetFileName(dst)}{BackupSuffix}");
        }

        var tmp = dst + ".smpack-tmp";
        File.Copy(source, tmp, overwrite: true);
        File.Move(tmp, dst, overwrite: true);
        log?.Invoke($"install {dst}");

        return dst;
    }

    /// <summary>Put the original back. The backup is removed so the next install makes a fresh one.</summary>
    public static void Restore(Backup b, Action<string>? log = null)
    {
        File.Move(b.BackupPath, b.OriginalPath, overwrite: true);
        log?.Invoke($"restored {b.Name}");
    }
}