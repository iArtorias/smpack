using SmpackGui.Core.Models;
using SmpackGui.Core.Textures;

namespace SmpackGui.Core;

public sealed class AppSettings
{
    public string? GameDir
    {
        get;
        set;
    }
    public string? SmpackPath
    {
        get;
        set;
    }
    public string? TexconvPath
    {
        get;
        set;
    }
    public string? ProjectsDir
    {
        get;
        set;
    }
    /// <summary>Keep the cache and new mod projects in '<game>\smpack' instead of the user profile.</summary>
    public bool StoreInGameFolder
    {
        get;
        set;
    } = true;
    public string? LastProject
    {
        get;
        set;
    }
    public List<string> RecentProjects
    {
        get;
        set;
    } = [];
    public BcQuality Quality
    {
        get;
        set;
    } = BcQuality.BALANCED;
    public bool ShowUnnamed
    {
        get;
        set;
    }
    public string? LastModelsWad
    {
        get;
        set;
    }
    public string? LastAudioPack
    {
        get;
        set;
    }
    public string? LastExportDir
    {
        get;
        set;
    }
    public string? VgmstreamPath
    {
        get;
        set;
    }
    public string? WwisePath
    {
        get;
        set;
    }
    public int Mp3Kbps
    {
        get;
        set;
    } = 320;
    public bool LogExpanded
    {
        get;
        set;
    } = true;
    public double WindowWidth
    {
        get;
        set;
    } = 1280;
    public double WindowHeight
    {
        get;
        set;
    } = 720;
    public bool WindowMaximized
    {
        get;
        set;
    }

    public static string AppDataDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SmpackGui");
    public static string LocalDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SmpackGui");
    /// <summary>'<game>\smpack' while the game folder is used for storage, else null.</summary>
    public static string? GameStorageRoot
    {
        get;
        set;
    }
    public static string CacheDir => GameStorageRoot != null ? Path.Combine(GameStorageRoot, "cache") : Path.Combine(LocalDir, "cache");
    public static string? GameModsDir => GameStorageRoot != null ? Path.Combine(GameStorageRoot, "mods") : null;

    /// <summary>Returns 'true' when files can be created in <paramref name="dir"/>.</summary>
    public static bool CanWrite(string dir)
    {
        try
        {
            Directory.CreateDirectory(dir);

            var probe = Path.Combine(dir, $".write-test-{Guid.NewGuid():N}");
            File.WriteAllText(probe, "");
            File.Delete(probe);
            return true;
        }
        catch
        {
            return false;
        }
    }
    public static string ToolsDir => Path.Combine(LocalDir, "tools");
    public static string SettingsFile => Path.Combine(AppDataDir, "settings.json");
    public static string DefaultProjectsDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "smpack mods");

    /// <summary>'texindex.json' for a given game folder (one per install).</summary>
    public static string IndexPathFor(string game_dir)
    {
        var key = Convert.ToHexString(System.Security.Cryptography.SHA1.HashData(
            System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(game_dir).ToLowerInvariant())))[..12];

        return Path.Combine(CacheDir, $"texindex_{key}.json");
    }

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(SettingsFile))
                return Json.Read<AppSettings>(SettingsFile);
        }
        catch
        {}

        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            Json.Write(SettingsFile, this);
        }
        catch
        {}
    }

    public void TouchRecent(string project_dir)
    {
        RecentProjects.RemoveAll(p => string.Equals(p, project_dir, StringComparison.OrdinalIgnoreCase));
        RecentProjects.Insert(0, project_dir);

        if (RecentProjects.Count > 10)
            RecentProjects.RemoveRange(10, RecentProjects.Count - 10);

        LastProject = project_dir;
    }

    /// <summary>Find smpack.exe. Configured path, next to the GUI, bundled tools dir, the game folder, 'PATH'.</summary>
    public string? LocateSmpack()
    {
        string exe = OperatingSystem.IsWindows() ? "smpack.exe" : "smpack";

        var cands = new List<string?>
        {
            SmpackPath,
            Path.Combine(AppContext.BaseDirectory, exe),
            Path.Combine(AppContext.BaseDirectory, "tools", exe)
        };

        if (GameDir != null)
            cands.Add(Path.Combine(GameDir, exe));

        foreach (var c in cands)
            if (!string.IsNullOrWhiteSpace(c) && File.Exists(c))
                return c;

        foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
        {
            try
            {
                var p = Path.Combine(dir.Trim(), exe);

                if (File.Exists(p))
                    return p;
            }
            catch (ArgumentException)
            {}
        }

        return null;
    }
}