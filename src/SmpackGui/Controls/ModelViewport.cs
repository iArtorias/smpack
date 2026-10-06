using System.IO;
using System.Numerics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using SmpackGui.Core.Meshes;

namespace SmpackGui.Controls;

/// <summary>
/// Orbit camera 3D preview of a <see cref="PreviewModel"/>. Drag to rotate, right drag or
/// middle drag to pan, wheel to zoom, double click to reset.
/// </summary>
public sealed class ModelViewport : Border
{
    public static readonly DependencyProperty ModelProperty = DependencyProperty.Register(nameof(Model), typeof(PreviewModel), typeof(ModelViewport),
        new PropertyMetadata(null, (d, _) => ((ModelViewport)d).Rebuild()));

    public static readonly DependencyProperty ShowTexturesProperty = DependencyProperty.Register(nameof(ShowTextures), typeof(bool), typeof(ModelViewport),
        new PropertyMetadata(true, (d, _) => ((ModelViewport)d).Rebuild()));

    public PreviewModel? Model
    {
        get => (PreviewModel?)GetValue(ModelProperty);
        set => SetValue(ModelProperty, value);
    }
    public bool ShowTextures
    {
        get => (bool)GetValue(ShowTexturesProperty);
        set => SetValue(ShowTexturesProperty, value);
    }

    private readonly Viewport3D m_View = new()
    {
        ClipToBounds = true,
        IsHitTestVisible = false
    };
    private readonly PerspectiveCamera m_Camera = new()
    {
        FieldOfView = 40,
        NearPlaneDistance = 0.01,
        FarPlaneDistance = 10000
    };
    private readonly ModelVisual3D m_Content = new();
    private readonly Grid m_Host = new()
    { 
        Background = Brushes.Transparent
    };

    private Point3D m_Target;
    private double m_Radius = 1, m_Distance = 3, m_Yaw = 0.7, m_Pitch = 0.35;
    private Point m_Last;
    private bool m_Rotating, m_Panning;

    public ModelViewport()
    {
        m_View.Camera = m_Camera;

        var lights = new Model3DGroup();

        lights.Children.Add(new AmbientLight(Color.FromRgb(70, 74, 84)));
        lights.Children.Add(new DirectionalLight(Color.FromRgb(225, 220, 210), new Vector3D(-0.5, -0.8, -0.6)));
        lights.Children.Add(new DirectionalLight(Color.FromRgb(90, 100, 125), new Vector3D(0.7, 0.3, 0.8)));
        m_View.Children.Add(new ModelVisual3D { Content = lights });
        m_View.Children.Add(m_Content);
        m_Host.Children.Add(m_View);
        Child = m_Host;
        ClipToBounds = true;
        Focusable = true;

        m_Host.MouseDown += (_, e) =>
        {
            Focus();
            if (e.ClickCount == 2)
            {
                ResetView();
                return;
            }

            m_Last = e.GetPosition(this);
            m_Rotating = e.ChangedButton == MouseButton.Left;
            m_Panning = e.ChangedButton is MouseButton.Right or MouseButton.Middle;
            m_Host.CaptureMouse();
        };

        m_Host.MouseUp += (_, _) =>
        {
            m_Rotating = m_Panning = false;
            m_Host.ReleaseMouseCapture();
        };

        m_Host.MouseMove += (_, e) =>
        {
            if (!m_Rotating && !m_Panning)
                return;

            var p = e.GetPosition(this);
            var d = p - m_Last;

            m_Last = p;

            if (m_Rotating)
            {
                m_Yaw -= d.X * 0.01;
                m_Pitch = Math.Clamp(m_Pitch + d.Y * 0.01, -1.5, 1.5);
            }
            else
            {
                var (right, up, _) = Basis();
                double k = m_Distance * 0.0018;
                m_Target += right * (-d.X * k) + up * (d.Y * k);
            }

            UpdateCamera();
        };

        m_Host.MouseWheel += (_, e) =>
        {
            m_Distance = Math.Clamp(m_Distance * (e.Delta > 0 ? 0.88 : 1.14), m_Radius * 0.05, m_Radius * 40);
            UpdateCamera();
        };
    }

    public void ResetView()
    {
        m_Yaw = 0.7;
        m_Pitch = 0.35;

        if (Model != null)
        {
            var (mn, mx) = Model.Bounds();

            m_Target = new Point3D((mn.X + mx.X) / 2, (mn.Y + mx.Y) / 2, (mn.Z + mx.Z) / 2);
            m_Radius = Math.Max(0.01, Vector3.Distance(mn, mx) / 2);
        }

        m_Distance = m_Radius / Math.Tan(m_Camera.FieldOfView * Math.PI / 360) * 1.15;
        m_Camera.NearPlaneDistance = m_Radius * 0.01;
        m_Camera.FarPlaneDistance = m_Radius * 200;

        UpdateCamera();
    }

    private (Vector3D Right, Vector3D Up, Vector3D Forward) Basis()
    {
        var fwd = new Vector3D(-Math.Cos(m_Pitch) * Math.Sin(m_Yaw), -Math.Sin(m_Pitch), -Math.Cos(m_Pitch) * Math.Cos(m_Yaw));
        var right = Vector3D.CrossProduct(fwd, new Vector3D(0, 1, 0));

        right.Normalize();

        var up = Vector3D.CrossProduct(right, fwd);
        return (right, up, fwd);
    }

    private void UpdateCamera()
    {
        var (_, up, fwd) = Basis();

        m_Camera.Position = m_Target - fwd * m_Distance;
        m_Camera.LookDirection = fwd;
        m_Camera.UpDirection = up;
    }

    private int m_BuildVersion;

    private async void Rebuild()
    {
        var m = Model;
        bool textures = ShowTextures;
        int version = ++m_BuildVersion;

        Model3DGroup group;
        try
        {
            // Freezable objects built on a worker thread can be used on the UI thread once frozen.
            group = m == null ? new Model3DGroup() : await Task.Run(() => BuildScene(m, textures));
        }
        catch (Exception)
        {
            group = new Model3DGroup();
        }

        if (version != m_BuildVersion)
            return;

        if (!group.IsFrozen)
            group.Freeze();

        m_Content.Content = group;

        // Keep the camera when only the texture switch changed.
        if (!ReferenceEquals(m, m_ShownModel))
            ResetView();

        m_ShownModel = m;
    }

    private PreviewModel? m_ShownModel;

    private static BitmapSource? LoadImage(string file, PreviewAlpha alpha)
    {
        var bmp = new BitmapImage();

        bmp.BeginInit();
        bmp.CacheOption = BitmapCacheOption.OnLoad;
        bmp.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
        bmp.UriSource = new Uri(file);
        bmp.DecodePixelWidth = 1024;
        bmp.EndInit();
        bmp.Freeze();

        if (alpha != PreviewAlpha.MASK)
            return bmp;

        // WPF has no alpha test. Snap the alpha to '0' or '255' so cutouts look like the game.
        var conv = new FormatConvertedBitmap(bmp, PixelFormats.Bgra32, null, 0);
        int w = conv.PixelWidth, h = conv.PixelHeight;
        var px = new byte[w * h * 4];
        conv.CopyPixels(px, w * 4, 0);

        for (int i = 3; i < px.Length; i += 4)
            px[i] = px[i] >= 128 ? (byte)255 : (byte)0;

        var out_bmp = BitmapSource.Create(w, h, 96, 96, PixelFormats.Bgra32, null, px, w * 4);
        out_bmp.Freeze();
        return out_bmp;
    }

    private static Model3DGroup BuildScene(PreviewModel m, bool show_textures)
    {
        var materials = new Dictionary<int, (Material Mat, bool Transparent)>();

        (Material Mat, bool Transparent) MaterialFor(int index)
        {
            if (materials.TryGetValue(index, out var found))
                return found;

            Brush b = new SolidColorBrush(Color.FromRgb(168, 172, 180));

            var mode = index >= 0 && index < m.MaterialAlphaMode.Count ? m.MaterialAlphaMode[index] : PreviewAlpha.OPAQUE;
            bool transparent = mode != PreviewAlpha.OPAQUE;

            if (show_textures && index >= 0 && index < m.MaterialImages.Count && m.MaterialImages[index].Length > 0)
            {
                var file = Path.Combine(m.Directory, m.MaterialImages[index].Replace('/', Path.DirectorySeparatorChar));

                if (File.Exists(file))
                {
                    try
                    {
                        if (LoadImage(file, mode) is { } img)
                            b = new ImageBrush(img) { ViewportUnits = BrushMappingMode.Absolute, TileMode = TileMode.Tile, Viewport = new Rect(0, 0, 1, 1) };
                    }
                    catch (Exception)
                    {
                        // Unreadable image. Keep the grey material. 
                    }
                }
            }

            if (index >= 0 && index < m.MaterialAlpha.Count && m.MaterialAlpha[index] < 1)
                b.Opacity = Math.Max(0.12, m.MaterialAlpha[index]);

            b.Freeze();

            var g = new MaterialGroup();

            g.Children.Add(new DiffuseMaterial(b));
            g.Children.Add(new SpecularMaterial(new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)), 30));
            g.Freeze();

            materials[index] = (g, transparent);
            return (g, transparent);
        }

        var opaque = new List<GeometryModel3D>();
        var cutout = new List<GeometryModel3D>();

        foreach (var n in m.Nodes)
        {
            var xf = n.Transform;
            bool identity = xf.IsIdentity;
            var nxf = Matrix4x4.Invert(xf, out var inv) ? Matrix4x4.Transpose(inv) : Matrix4x4.Identity;

            foreach (var p in n.Primitives)
            {
                var mesh = new MeshGeometry3D();
                var pos = new Point3D[p.Positions.Length];

                for (int i = 0; i < pos.Length; i++)
                {
                    var w = identity ? p.Positions[i] : Vector3.Transform(p.Positions[i], xf);
                    pos[i] = new Point3D(w.X, w.Y, w.Z);
                }

                mesh.Positions = new Point3DCollection(pos);

                if (p.Normals != null)
                {
                    var nrm = new Vector3D[p.Normals.Length];

                    for (int i = 0; i < nrm.Length; i++)
                    {
                        var w = identity ? p.Normals[i] : Vector3.TransformNormal(p.Normals[i], nxf);
                        nrm[i] = new Vector3D(w.X, w.Y, w.Z);
                    }

                    mesh.Normals = new Vector3DCollection(nrm);
                }

                var set = p.Material >= 0 && p.Material < m.MaterialUv.Count ? m.MaterialUv[p.Material] : 0;
                var uvs = (set > 0 && set < p.UvSets.Length ? p.UvSets[set] : null) ?? p.Uvs;

                if (uvs != null)
                {
                    var uv = new Point[uvs.Length];

                    for (int i = 0; i < uv.Length; i++)
                        uv[i] = new Point(uvs[i].X, uvs[i].Y);

                    mesh.TextureCoordinates = new PointCollection(uv);
                }

                mesh.TriangleIndices = new Int32Collection(p.Indices);
                mesh.Freeze();

                var (mat, transparent) = MaterialFor(p.Material);
                var gm = new GeometryModel3D(mesh, mat) { BackMaterial = mat };
                gm.Freeze();
                (transparent ? cutout : opaque).Add(gm);
            }
        }

        // WPF draws in order and has no sorting.
        var group = new Model3DGroup();

        foreach (var g in opaque)
            group.Children.Add(g);

        foreach (var g in cutout)
            group.Children.Add(g);

        group.Freeze();
        return group;
    }
}