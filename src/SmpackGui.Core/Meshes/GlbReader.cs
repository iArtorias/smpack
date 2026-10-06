using System.Numerics;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace SmpackGui.Core.Meshes;

public sealed class PreviewPrimitive
{
    public required Vector3[] Positions
    {
        get;
        init;
    }
    public Vector3[]? Normals
    {
        get;
        init;
    }
    public Vector2[]? Uvs
    {
        get;
        init;
    }
    /// <summary>Every 'TEXCOORD_n' set. Null where absent.</summary>
    public Vector2[]?[] UvSets
    {
        get; 
        init;
    } = new Vector2[]?[4];
    public required int[] Indices
    {
        get;
        init;
    }
    public int Material
    {
        get;
        init;
    }
}

public sealed class PreviewNode
{
    public string Name
    {
        get;
        init; 
    } = "";
    public Matrix4x4 Transform
    {
        get;
        init;
    } = Matrix4x4.Identity;
    public List<PreviewPrimitive> Primitives
    {
        get;
    } = [];
}

public enum PreviewAlpha
{
    OPAQUE,
    MASK,
    BLEND
}

public sealed class PreviewModel
{
    public List<PreviewNode> Nodes { get; } = [];
    /// <summary>Base color image URI per material (empty when none).</summary>
    public List<string> MaterialImages { get; } = [];
    public List<string> MaterialNames { get; } = [];
    /// <summary>Base color alpha factor per material. Overlays are below 1.</summary>
    public List<float> MaterialAlpha { get; } = [];
    /// <summary>How each material uses alpha.</summary>
    public List<PreviewAlpha> MaterialAlphaMode { get; } = [];
    /// <summary>UV set the base color texture reads.</summary>
    public List<int> MaterialUv { get; } = [];
    /// <summary>Folder the image URIs are relative to.</summary>
    public string Directory
    {
        get;
        init;
    } = "";

    public (Vector3 Min, Vector3 Max) Bounds()
    {
        var mn = new Vector3(float.MaxValue);
        var mx = new Vector3(float.MinValue);

        foreach (var n in Nodes)
            foreach (var p in n.Primitives)
                foreach (var v in p.Positions)
                {
                    var w = Vector3.Transform(v, n.Transform);
                    mn = Vector3.Min(mn, w);
                    mx = Vector3.Max(mx, w);
                }

        return mn.X > mx.X ? (Vector3.Zero, Vector3.Zero) : (mn, mx);
    }

    public int TriangleCount => Nodes.Sum(n => n.Primitives.Sum(p => p.Indices.Length / 3));
}

/// <summary>Reads the glTF 2.0 binary (.glb) files smpack writes, for the 3D preview.</summary>
public static class GlbReader
{
    /// <param name="texture_directory">Folder the image URIs are relative to, the GLB's folder by default.</param>
    public static PreviewModel Load(string path, string? texture_directory = null)
    {
        var data = File.ReadAllBytes(path);

        if (data.Length < 20 || BitConverter.ToUInt32(data, 0) != 0x46546C67)
            throw new InvalidDataException("Not a GLB file");

        int json_len = BitConverter.ToInt32(data, 12);

        if (json_len <= 0 || 20L + json_len > data.Length)
            throw new InvalidDataException("GLB JSON chunk is truncated");

        using var doc = JsonDocument.Parse(data.AsMemory(20, json_len));

        int bin_start = 20 + json_len + 8;
        var root = doc.RootElement;
        var views = root.GetProperty("bufferViews").EnumerateArray().ToArray();
        var accs = root.GetProperty("accessors").EnumerateArray().ToArray();
        var meshes = root.TryGetProperty("meshes", out var mm) ? mm.EnumerateArray().ToArray() : [];
        var model = new PreviewModel { Directory = texture_directory ?? Path.GetDirectoryName(Path.GetFullPath(path)) ?? "" };

        // Accessor location. Start offset, element count, stride. Only tightly packed float and integer data is expected.
        (int Offset, int Count, int Stride) Locate(int acc_index, int elem_bytes)
        {
            var a = accs[acc_index];
            var v = views[a.GetProperty("bufferView").GetInt32()];
            int off = bin_start + (v.TryGetProperty("byteOffset", out var bo) ? bo.GetInt32() : 0) +
                      (a.TryGetProperty("byteOffset", out var ao) ? ao.GetInt32() : 0);
            int count = a.GetProperty("count").GetInt32();
            int stride = v.TryGetProperty("byteStride", out var bs) && bs.GetInt32() > 0 ? bs.GetInt32() : elem_bytes;

            if (count < 0 || off < 0 || (long)off + (long)stride * Math.Max(0, count - 1) + elem_bytes > data.Length)
                throw new InvalidDataException("Accessor reads past the end of the file");

            return (off, count, stride);
        }

        T[] ReadStruct<T>(int acc_index) where T : unmanaged
        {
            int size = Marshal.SizeOf<T>();
            var (off, count, stride) = Locate(acc_index, size);

            if (stride == size)
                return MemoryMarshal.Cast<byte, T>(data.AsSpan(off, count * size)).ToArray();

            var res = new T[count];

            for (int i = 0; i < count; i++)
                res[i] = MemoryMarshal.Read<T>(data.AsSpan(off + i * stride, size));

            return res;
        }

        int[] ReadIndices(int acc_index)
        {
            var ct = accs[acc_index].GetProperty("componentType").GetInt32();

            switch (ct)
            {
                case 5125:
                    return MemoryMarshal.Cast<uint, int>(ReadStruct<uint>(acc_index)).ToArray();

                case 5123:
                    return Array.ConvertAll(ReadStruct<ushort>(acc_index), x => (int)x);

                case 5121:
                    return Array.ConvertAll(ReadStruct<byte>(acc_index), x => (int)x);

                default:
                    throw new InvalidDataException($"Unsupported index type {ct}");
            }
        }

        if (root.TryGetProperty("materials", out var mats))
        {
            var images = root.TryGetProperty("images", out var im) ? im.EnumerateArray().ToArray() : [];
            var textures = root.TryGetProperty("textures", out var tx) ? tx.EnumerateArray().ToArray() : [];

            foreach (var m in mats.EnumerateArray())
            {
                model.MaterialNames.Add(m.TryGetProperty("name", out var nm) ? nm.GetString() ?? "" : "");
                string uri = "";
                int uv_set = 0;
                float alpha = 1;

                if (m.TryGetProperty("pbrMetallicRoughness", out var pbr))
                {
                    if (pbr.TryGetProperty("baseColorTexture", out var bct))
                    {
                        var t = textures[bct.GetProperty("index").GetInt32()];
                        uri = images[t.GetProperty("source").GetInt32()].GetProperty("uri").GetString() ?? "";

                        if (bct.TryGetProperty("texCoord", out var tc))
                            uv_set = tc.GetInt32();
                    }

                    if (pbr.TryGetProperty("baseColorFactor", out var bcf))
                        alpha = bcf.EnumerateArray().Last().GetSingle();
                }

                var mode = m.TryGetProperty("alphaMode", out var am) ? am.GetString() : null;
                model.MaterialImages.Add(uri);
                model.MaterialUv.Add(uv_set);
                model.MaterialAlpha.Add(alpha);
                model.MaterialAlphaMode.Add(mode == "MASK" ? PreviewAlpha.MASK : mode == "BLEND" ? PreviewAlpha.BLEND : PreviewAlpha.OPAQUE);
            }
        }

        // World transforms through the node hierarchy. Rigid parts hang under skeleton joints.
        var node_arr = root.GetProperty("nodes").EnumerateArray().ToArray();
        var parent = new int[node_arr.Length];

        Array.Fill(parent, -1);

        for (int i = 0; i < node_arr.Length; i++)
            if (node_arr[i].TryGetProperty("children", out var ch))
                foreach (var c in ch.EnumerateArray())
                    parent[c.GetInt32()] = i;

        var local = new Matrix4x4[node_arr.Length];
        for (int i = 0; i < node_arr.Length; i++)
        {
            local[i] = Matrix4x4.Identity;
            if (node_arr[i].TryGetProperty("matrix", out var mat))
            {
                var f = mat.EnumerateArray().Select(e => e.GetSingle()).ToArray();

                // glTF stores column-major column-vector matrices. The same numbers as a row-major row-vector matrix.
                local[i] = new Matrix4x4(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10],
                    f[11], f[12], f[13], f[14], f[15]);
            }
        }

        var world = new Matrix4x4?[node_arr.Length];
        Matrix4x4 World(int i)
        {
            if (world[i] is {} w)
                return w;

            var m = local[i];

            // Walk up iteratively so deep skeletons cannot overflow the stack.
            for (int p = parent[i], guard = 0; p >= 0 && guard < node_arr.Length; p = parent[p], guard++)
                m *= local[p];

            world[i] = m;
            return m;
        }

        for (int ni = 0; ni < node_arr.Length; ni++)
        {
            var n = node_arr[ni];
            if (!n.TryGetProperty("mesh", out var mi))
                continue;

            // Skinned meshes are already in model space as bind pose, the node transform does not apply.
            var m4 = n.TryGetProperty("skin", out _) ? Matrix4x4.Identity : World(ni);
            var node = new PreviewNode
            {
                Name = n.TryGetProperty("name", out var nn) ? nn.GetString() ?? "" : "",
                Transform = m4
            };

            foreach (var p in meshes[mi.GetInt32()].GetProperty("primitives").EnumerateArray())
            {
                var at = p.GetProperty("attributes");
                var pos = ReadStruct<Vector3>(at.GetProperty("POSITION").GetInt32());
                Vector3[]? nrm = at.TryGetProperty("NORMAL", out var na) ? ReadStruct<Vector3>(na.GetInt32()) : null;
                var sets = new Vector2[]?[4];

                for (int k = 0; k < 4; k++)
                    if (at.TryGetProperty($"TEXCOORD_{k}", out var ua)) sets[k] = ReadStruct<Vector2>(ua.GetInt32());

                var idx = p.TryGetProperty("indices", out var ia) ? ReadIndices(ia.GetInt32()) : Enumerable.Range(0, pos.Length).ToArray();

                foreach (var i in idx)
                    if ((uint)i >= (uint)pos.Length) throw new InvalidDataException("Index out of range");

                node.Primitives.Add(new PreviewPrimitive
                {
                    Positions = pos,
                    Normals = nrm,
                    Uvs = sets[0],
                    UvSets = sets,
                    Indices = idx,
                    Material = p.TryGetProperty("material", out var pm) ? pm.GetInt32() : -1,
                });
            }

            model.Nodes.Add(node);
        }

        return model;
    }
}