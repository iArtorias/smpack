using System.Text.RegularExpressions;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Mods;

public sealed class ModTextureEntry
{
    /// <summary>File stem of the DDS/JSON pair inside the project's textures folder.</summary>
    public string Stem
    {
        get;
        set;
    } = "";
    public string Name
    {
        get;
        set;
    } = "";
    public string ContentHash
    {
        get;
        set;
    } = "";
    /// <summary>'texpack' or 'wad'.</summary>
    public string Source
    {
        get;
        set;
    } = "";
    public string? Texpack
    {
        get;
        set;
    }
    /// <summary>WAD that owns a WAD resident texture.</summary>
    public string? Wad
    { 
        get;
        set;
    }
    /// <summary>WADs holding low mip copies of a streamed texture.</summary>
    public List<string> Wads
    {
        get;
        set;
    } = [];
    public string SourceImage
    {
        get;
        set;
    } = "";
    public int Width
    {
        get;
        set;
    }
    public int Height
    {
        get;
        set;
    }
    public int OriginalWidth
    {
        get;
        set;
    }
    public int OriginalHeight
    {
        get;
        set;
    }
    public string Format
    {
        get;
        set;
    } = "";
    public DateTime Added
    {
        get;
        set;
    } = DateTime.Now;

    public bool IsTexpack => Source == "texpack";
    public string Label => IsTexpack ? Name : $"{Name}  ({Wad})";
}

public sealed class ModAudioEntry
{
    public uint FileId
    {
        get;
        set;
    }
    /// <summary>Audio pack base name, e.g. 'english(us)' or '150_niflheim1'.</summary>
    public string Pack
    {
        get;
        set;
    } = "";
    public string SourceFile
    {
        get;
        set;
    } = "";
    public long Size
    {
        get;
        set;
    }
    public DateTime Added
    {
        get;
        set;
    } = DateTime.Now;

    public string Label => $"{FileId}  ({Pack})";
}

/// <summary>A model whose geometry is replaced by an edited glTF.</summary>
public sealed class ModModelEntry
{
    /// <summary>File name of the glTF copy inside the project's models folder.</summary>
    public string File
    {
        get;
        set;
    } = "";
    public string Wad
    {
        get;
        set;
    } = "";
    /// <summary>WAD entry index of the MESH chunk (the ID 'mesh list' reports).</summary>
    public int MeshId
    {
        get;
        set;
    }
    public string Name
    {
        get;
        set;
    } = "";
    public string SourceFile
    {
        get;
        set;
    } = "";
    /// <summary>Also write the new mesh into the coarser LODs.</summary>
    public bool AllLods
    {
        get;
        set;
    } = true;
    public DateTime Added
    {
        get;
        set;
    } = DateTime.Now;

    public string Label => $"{Name}  ({Wad})";
}

public sealed class ModBuildOptions
{
    public bool InstallOnBuild
    {
        get;
        set;
    } = true;
    public bool SyncWadLowMips
    {
        get;
        set;
    } = true;
    public bool Compress
    {
        get;
        set;
    } = true;
}

public sealed class InstalledState
{
    public string? Texpack
    {
        get;
        set;
    }
    /// <summary>Patch lodpack registered for replaced streamed geometry.</summary>
    public string? Lodpack
    {
        get;
        set;
    }
    public List<string> Files
    {
        get;
        set;
    } = [];
    public List<string> Wads
    {
        get;
        set;
    } = [];
    /// <summary>Audio pack files written into 'exec\sound\pc_le'.</summary>
    public List<string> SoundFiles
    {
        get;
        set;
    } = [];
    public string GameRoot
    {
        get;
        set;
    } = "";
    public DateTime When
    {
        get;
        set;
    }
}

public sealed class ModProjectData
{
    public int Version
    {
        get;
        set;
    } = 1;
    public string Name
    {
        get;
        set;
    } = "";
    public string PatchName
    {
        get;
        set;
    } = "";
    public string Author
    {
        get;
        set;
    } = "";
    public string Description
    {
        get;
        set;
    } = "";
    public DateTime Created
    {
        get;
        set;
    } = DateTime.Now;
    public List<ModTextureEntry> Textures
    {
        get;
        set;
    } = [];
    public List<ModAudioEntry> Audio
    {
        get;
        set;
    } = [];
    public List<ModModelEntry> Models
    {
        get;
        set;
    } = [];
    public ModBuildOptions Options
    {
        get;
        set;
    } = new();
    public InstalledState? Installed
    {
        get;
        set;
    }
}

/// <summary>A texture mod on disk.</summary>
public sealed class ModProject
{
    public const string FileName = "smpack-mod.json";

    public string Dir { get; }
    public ModProjectData Data { get; }

    public string ProjectFile => Path.Combine(Dir, FileName);
    public string TexturesDir => Path.Combine(Dir, "textures");
    public string AudioDir => Path.Combine(Dir, "audio");
    public string ModelsDir => Path.Combine(Dir, "models");
    public string SoundOutputDir => Path.Combine(BuildDir, "sound");
    public string BuildDir => Path.Combine(Dir, "build");
    public string OutputDir => Path.Combine(BuildDir, "out");
    public string SourceWadDir => Path.Combine(BuildDir, "src");

    private ModProject(string dir, ModProjectData data)
    {
        Dir = dir;
        Data = data;
    }

    public static ModProject Create(string dir, string name)
    {
        Directory.CreateDirectory(dir);

        var p = new ModProject(Path.GetFullPath(dir), new ModProjectData
        {
            Name = name,
            PatchName = SuggestPatchName(name)
        });

        Directory.CreateDirectory(p.TexturesDir);

        p.Save();
        return p;
    }

    public static ModProject Open(string dir_or_file)
    {
        var dir = File.Exists(dir_or_file) ? Path.GetDirectoryName(Path.GetFullPath(dir_or_file))! : Path.GetFullPath(dir_or_file);
        var data = Json.Read<ModProjectData>(Path.Combine(dir, FileName));
        return new ModProject(dir, data);
    }

    public void Save() => Json.Write(ProjectFile, Data);

    public static string SuggestPatchName(string name)
    {
        var s = Regex.Replace(name.ToLowerInvariant(), "[^a-z0-9_]+", "_").Trim('_');

        if (s.Length == 0)
            s = "mod";

        s = "mod_" + s;
        return s.Length > 31 ? s[..31] : s;
    }

    /// <summary>Patch pack names are resolved by the engine. Max 31 chars, keep them simple.</summary>
    public static string? ValidatePatchName(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
            return "Enter a patch name.";

        if (name.Length > 31)
            return "At most 31 characters (engine limit).";

        if (!Regex.IsMatch(name, "^[A-Za-z0-9_\\-]+$"))
            return "Use letters, digits, '_' or '-' only.";

        return null;
    }

    public string DdsPath(ModTextureEntry e) => Path.Combine(TexturesDir, e.Stem + ".dds");
    public string SidecarPath(ModTextureEntry e) => Path.Combine(TexturesDir, e.Stem + ".json");

    public ModTextureEntry? Find(string stem) => Data.Textures.FirstOrDefault(t => t.Stem.Equals(stem, StringComparison.OrdinalIgnoreCase));

    public void Upsert(ModTextureEntry e)
    {
        var old = Find(e.Stem);

        if (old != null)
            Data.Textures.Remove(old);

        Data.Textures.Add(e);
        Save();
    }

    public void Remove(ModTextureEntry e)
    {
        Data.Textures.Remove(e);

        foreach (var f in new[] { DdsPath(e), SidecarPath(e) })
            if (File.Exists(f)) File.Delete(f);

        Save();
    }

    /// <summary>WAD file names the build must read.</summary>
    public IReadOnlyList<string> WadsToRebuild(bool sync_low_mips)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var t in Data.Textures)
        {
            if (!t.IsTexpack && t.Wad != null)
                set.Add(t.Wad);

            if (t.IsTexpack && sync_low_mips)
                foreach (var w in t.Wads) set.Add(w);
        }

        return [.. set];
    }

    public string WemPath(ModAudioEntry e) => Path.Combine(AudioDir, e.Pack, $"{e.FileId}.wem");

    public ModAudioEntry? FindAudio(string pack, uint id) =>
        Data.Audio.FirstOrDefault(a => a.FileId == id && a.Pack.Equals(pack, StringComparison.OrdinalIgnoreCase));

    public void UpsertAudio(ModAudioEntry e)
    {
        var old = FindAudio(e.Pack, e.FileId);

        if (old != null)
            Data.Audio.Remove(old);

        Data.Audio.Add(e);
        Save();
    }

    public void RemoveAudio(ModAudioEntry e)
    {
        Data.Audio.Remove(e);
        var f = WemPath(e);

        if (File.Exists(f))
            File.Delete(f);

        Save();
    }

    public string ModelPath(ModModelEntry e) => Path.Combine(ModelsDir, e.File);

    public ModModelEntry? FindModel(string wad, int mesh_id) =>
        Data.Models.FirstOrDefault(m => m.MeshId == mesh_id && m.Wad.Equals(wad, StringComparison.OrdinalIgnoreCase));

    /// <summary>Copy an edited glTF (and a .gltf's .bin and textures next to it) into the project.</summary>
    public ModModelEntry AddModel(string wad, int mesh_id, string name, string source, bool all_lods)
    {
        // Stage the files first. The source may live in the folder of the entry being replaced.
        var files = new List<string> { source };

        if (source.EndsWith(".gltf", StringComparison.OrdinalIgnoreCase))
            files.AddRange(Directory.EnumerateFiles(Path.GetDirectoryName(Path.GetFullPath(source))!, "*.bin"));

        var staged = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);

        foreach (var f in files)
            staged[Path.GetFileName(f)] = File.ReadAllBytes(f);

        var old = FindModel(wad, mesh_id);

        if (old != null)
            RemoveModel(old);

        var stem = $"{Path.GetFileNameWithoutExtension(wad)}_{mesh_id}_{Regex.Replace(name, "[^A-Za-z0-9_\\-]+", "_")}";
        var dir = Path.Combine(ModelsDir, stem);

        Directory.CreateDirectory(dir);

        foreach (var (n, bytes) in staged)
            File.WriteAllBytes(Path.Combine(dir, n), bytes);

        var e = new ModModelEntry
        {
            File = Path.Combine(stem, Path.GetFileName(source)),
            Wad = wad,
            MeshId = mesh_id,
            Name = name,
            SourceFile = source,
            AllLods = all_lods,
        };

        Data.Models.Add(e);
        Save();
        return e;
    }

    public void RemoveModel(ModModelEntry e)
    {
        Data.Models.Remove(e);

        var dir = Path.GetDirectoryName(ModelPath(e));
        if (dir != null && Directory.Exists(dir) && !string.Equals(Path.GetFullPath(dir), Path.GetFullPath(ModelsDir), StringComparison.OrdinalIgnoreCase))
            Directory.Delete(dir, true);

        Save();
    }

    public bool IsEmpty => Data.Textures.Count == 0 && Data.Audio.Count == 0 && Data.Models.Count == 0;

    public bool HasTexpackTextures => Data.Textures.Any(t => t.IsTexpack);
}