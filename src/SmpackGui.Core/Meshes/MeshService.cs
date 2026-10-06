using SmpackGui.Core.Cli;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Meshes;

public enum MeshFormat
{
    GLB,
    GLTF,
    FBX
}

public sealed class MeshInfo
{
    public int Id
    {
        get;
        set;
    }
    public string Name
    {
        get;
        set;
    } = "";
    public string Kind
    {
        get;
        set;
    } = "";
    public int Parts
    {
        get;
        set;
    }
    public int Prims
    {
        get;
        set;
    }
    public int Lods
    {
        get;
        set;
    }
    public long Vertices
    {
        get;
        set;
    }
    public long Triangles
    {
        get;
        set;
    }
    public int Materials
    {
        get;
        set;
    }
    public int Joints
    {
        get;
        set;
    }
    /// <summary>Returns 'false' for models made only of shadow proxies or collider shells.</summary>
    public bool Visible
    {
        get;
        set;
    } = true;
    public int StreamedPrims
    {
        get;
        set;
    }
    public int MissingPrims
    {
        get;
        set;
    }
    /// <summary>Parts on hidden joints (wounds, damage states) that are left out by default.</summary>
    public int HiddenParts
    {
        get;
        set;
    }
    /// <summary>Visual configs of the game object, e.g. 'Helwalker00' or 'seidr01_moderate'.</summary>
    public List<string> Variants
    {
        get;
        set;
    } = [];
    public int DefaultVariant
    { 
        get; 
        set;
    } = -1;

    public string KindLabel => Kind switch
    { 
        "model" => "Model",
        "static" => "Static prop",
        _ => "Mesh"
    };
}

public sealed class MeshList
{
    public string File
    {
        get;
        set;
    } = "";
    public int Lodpacks
    {
        get;
        set;
    }
    public List<MeshInfo> Models
    {
        get;
        set;
    } = [];
}

public sealed class MeshMaterial
{
    public string Name
    {
        get;
        set;
    } = "";
    public List<string> Textures
    {
        get;
        set;
    } = [];
    /// <summary>Texture name of the color map (empty when none).</summary>
    public string BaseColor
    {
        get;
        set;
    } = "";
    /// <summary>Separate cutout map that goes into the color PNG alpha (empty when none).</summary>
    public string Opacity
    {
        get;
        set;
    } = "";
    /// <summary>PNG file stem the model file links for the color map.</summary>
    public string BaseColorFile
    {
        get;
        set;
    } = "";
    public string Normal
    {
        get;
        set;
    } = "";
    public string NormalFile
    {
        get;
        set;
    } = "";
    /// <summary>'opaque', 'mask' or 'blend'.</summary>
    public string Alpha
    {
        get;
        set;
    } = "opaque";
}

public sealed class MeshExportItem
{
    public int Id
    {
        get;
        set;
    }
    public string Name
    {
        get;
        set;
    } = "";
    public string File
    {
        get;
        set;
    } = "";
    public long Vertices
    {
        get;
        set;
    }
    public long Triangles
    {
        get;
        set;
    }
    public int Variant
    {
        get;
        set;
    } = -1;
    public List<MeshMaterial> Materials
    {
        get;
        set;
    } = [];
}

public sealed class MeshSkipped
{
    public int Id
    {
        get;
        set;
    }
    public string Name
    {
        get;
        set;
    } = "";
    public string Reason
    {
        get;
        set;
    } = "";
}

public sealed class MeshExportResult
{
    public List<MeshSkipped> Skipped
    {
        get;
        set;
    } = [];
    public string Format
    {
        get;
        set;
    } = "";
    public string Output
    {
        get;
        set;
    } = "";
    public List<MeshExportItem> Models
    {
        get;
        set;
    } = [];
    public List<string> Warnings
    {
        get;
        set;
    } = [];
}

public sealed class MeshExportOptions
{
    public MeshFormat Format
    {
        get;
        set;
    } = MeshFormat.GLB;
    /// <summary>LOD index, or '-1' for every LOD as separate objects.</summary>
    public int Lod
    {
        get;
        set;
    }
    public bool Textures
    {
        get;
        set;
    } = true;
    public bool WorldPlacement
    {
        get;
        set;
    }
    /// <summary>Variant name, or null for the default one.</summary>
    public string? Variant
    {
        get;
        set;
    }
    /// <summary>Also export parts on hidden joints and every variant.</summary>
    public bool AllParts
    {
        get;
        set;
    }
}

public sealed class MeshImportResult
{
    public string Model
    {
        get;
        set;
    } = "";
    public int Prims
    {
        get;
        set;
    }
    public long Vertices
    {
        get;
        set;
    }
    public long Triangles
    {
        get;
        set;
    }
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
    public List<string> Warnings
    {
        get;
        set;
    } = [];
}

/// <summary>One PNG a model export links. A texture, optionally with a cutout map merged into its alpha.</summary>
public sealed record MeshTexture(string File, string? Texture, string? Opacity, bool Normal);

/// <summary>smpack 'mesh list' and 'mesh export' (glTF 2.0 and FBX).</summary>
public sealed class MeshService(SmpackCli cli)
{
    public async Task<MeshList> ListAsync(string wad, CancellationToken ct = default)
    {
        var r = (await cli.RunAsync(["mesh", "list", wad, "--json"], echo_stdout: false, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return Json.Parse<MeshList>(r.StdOut);
    }

    /// <summary>Export one model (<paramref name="id"/>) or every model whose name contains <paramref name="filter"/>.</summary>
    public async Task<MeshExportResult> ExportAsync(string wad, string out_dir, MeshExportOptions o, int? id = null, string? filter = null,
        bool quiet = false, CancellationToken ct = default)
    {
        var args = new List<string>
        {
            "mesh",
            "export",
            wad,
            "-o", 
            out_dir,
            "--json",
            "--as",
            o.Format switch
            { 
                MeshFormat.FBX => "fbx",
                MeshFormat.GLTF => "gltf",
                _ => "glb"
            },
            "--lod",
            o.Lod < 0 ? "all" : o.Lod.ToString(System.Globalization.CultureInfo.InvariantCulture),
        };

        if (id != null)
        {
            args.Add("--id");
            args.Add(id.Value.ToString());
        }
        else if (!string.IsNullOrWhiteSpace(filter))
        {
            args.Add("--filter");
            args.Add(filter);
        }

        if (!o.Textures)
            args.Add("--no-textures");

        if (o.WorldPlacement)
            args.Add("--world");

        if (o.AllParts)
            args.Add("--all-parts");
        else if (!string.IsNullOrEmpty(o.Variant))
        {
            args.Add("--variant");
            args.Add(o.Variant);
        }

        var r = await cli.RunAsync(args, echo_stdout: false, ct: ct).ConfigureAwait(false);

        if (!r.Success && string.IsNullOrWhiteSpace(r.StdOut))
            r.ThrowIfFailed();

        var res = Json.Parse<MeshExportResult>(r.StdOut);

        if (res.Models.Count == 0 && res.Skipped.Count == 0)
            r.ThrowIfFailed();

        return res;
    }

    /// <summary>
    /// Import an edited glTF back into a model. Writes the patched WAD and a patch lodpack for
    /// streamed geometry into <paramref name="out_dir"/>.
    /// </summary>
    public async Task<MeshImportResult> ImportAsync(string wad, string gltf, string out_dir, string game_root, string patch, int? id,
        bool append, bool all_lods = true, bool compress = true, CancellationToken ct = default)
    {
        var args = new List<string> 
        { 
            "mesh",
            "import",
            wad,
            gltf,
            "-o",
            out_dir,
            "--game",
            game_root,
            "--patch",
            patch,
            "--json",
            "--lods", 
            all_lods ? "all" : "same" 
        };

        if (id != null)
        {
            args.Add("--id");
            args.Add(id.Value.ToString());
        }

        if (append)
            args.Add("--append");

        if (!compress)
            args.Add("--no-compress");

        var r = (await cli.RunAsync(args, echo_stdout: false, ct: ct).ConfigureAwait(false)).ThrowIfFailed();
        return Json.Parse<MeshImportResult>(r.StdOut);
    }

    /// <summary>PNG files an export links. With <paramref name="all"/>, every texture of the materials as well.</summary>
    public static IEnumerable<MeshTexture> TexturesToWrite(MeshExportResult res, bool all)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var m in res.Models)
            foreach (var mat in m.Materials)
            {
                var color = mat.BaseColorFile.Length > 0 ? mat.BaseColorFile : mat.BaseColor;

                if (color.Length > 0 && seen.Add(color))
                    yield return new MeshTexture(color, mat.BaseColor.Length > 0 ? mat.BaseColor : null, mat.Opacity.Length > 0 ? mat.Opacity : null, false);

                var normal = mat.NormalFile.Length > 0 ? mat.NormalFile : mat.Normal;

                if (mat.Normal.Length > 0 && seen.Add(normal))
                    yield return new MeshTexture(normal, mat.Normal, null, true);

                if (!all)
                    continue;

                foreach (var t in mat.Textures)
                    if (seen.Add(t)) yield return new MeshTexture(t, t, null, IsNormalName(t));
            }
    }

    /// <summary>'..._0n_HASH' or '..._n_HASH'. A two channel normal map.</summary>
    public static bool IsNormalName(string name)
    {
        var parts = name.Split('_');

        if (parts.Length < 3)
            return false;

        var role = parts[^2];
        return role.TrimStart('0', '1', '2', '3', '4', '5', '6', '7', '8', '9') == "n";
    }
}