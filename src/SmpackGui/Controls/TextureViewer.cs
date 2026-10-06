using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace SmpackGui.Controls;

/// <summary>Zoom or pan image surface with a checkerboard, pixel hover events and file drop.</summary>
public sealed class TextureViewer : Grid
{
    private readonly Image m_Image = new() { Stretch = Stretch.None, SnapsToDevicePixels = false };
    private readonly Canvas m_Canvas = new() { ClipToBounds = true };
    private readonly ScaleTransform m_Scale = new();
    private readonly TranslateTransform m_Translate = new();
    private readonly TextBlock m_ZoomLabel;
    private readonly Border m_DropHint;
    private Point? m_DragStart;
    private Point m_DragOrigin;
    private bool m_Fit = true;

    public static readonly DependencyProperty SourceProperty = DependencyProperty.Register(nameof(Source), typeof(ImageSource), typeof(TextureViewer),
        new PropertyMetadata(null, (d, e) => ((TextureViewer)d).OnSourceChanged(e.OldValue as ImageSource, e.NewValue as ImageSource)));

    public static readonly DependencyProperty ShowCheckerProperty = DependencyProperty.Register(nameof(ShowChecker), typeof(bool), typeof(TextureViewer),
        new PropertyMetadata(true, (d, _) => ((TextureViewer)d).UpdateBackground()));

    public ImageSource? Source
    {
        get => (ImageSource?)GetValue(SourceProperty);
        set => SetValue(SourceProperty, value);
    }
    public bool ShowChecker
    {
        get => (bool)GetValue(ShowCheckerProperty);
        set => SetValue(ShowCheckerProperty, value);
    }

    /// <summary>Pixel under the cursor (image space), or null when outside.</summary>
    public event Action<int, int>? PixelHover;
    public event Action? PixelLeave;
    public event Action<string>? FileDropped;

    public double Zoom => m_Scale.ScaleX;

    public TextureViewer()
    {
        ClipToBounds = true;
        Focusable = true;
        AllowDrop = true;

        var tg = new TransformGroup();
        tg.Children.Add(m_Scale);
        tg.Children.Add(m_Translate);

        m_Image.RenderTransform = tg;
        m_Canvas.Children.Add(m_Image);
        Children.Add(m_Canvas);

        m_ZoomLabel = new TextBlock
        {
            Margin = new Thickness(10),
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Bottom,
            Foreground = new SolidColorBrush(Color.FromArgb(200, 230, 237, 245)),
            FontSize = 11,
            IsHitTestVisible = false,
            Background = new SolidColorBrush(Color.FromArgb(150, 14, 17, 22)),
            Padding = new Thickness(6, 2, 6, 2),
        };

        Children.Add(m_ZoomLabel);

        m_DropHint = new Border
        {
            Visibility = Visibility.Collapsed,
            IsHitTestVisible = false,
            Margin = new Thickness(12),
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(2),
            BorderBrush = (Brush)Application.Current.FindResource("Accent"),
            Background = new SolidColorBrush(Color.FromArgb(170, 14, 17, 22)),

            Child = new TextBlock
            {
                Text = "Drop to replace this texture",
                FontSize = 16,
                Foreground = (Brush)Application.Current.FindResource("Text"),
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
            },
        };

        Children.Add(m_DropHint);

        UpdateBackground();
        SizeChanged += (_, _) =>
        {
            if (m_Fit)
                FitToView();
        };
    }

    private void UpdateBackground() =>
        Background = ShowChecker ? (Brush)Application.Current.FindResource("Checker") : (Brush)Application.Current.FindResource("Bg0");

    private void OnSourceChanged(ImageSource? old, ImageSource? now)
    {
        m_Image.Source = now;

        if (now == null)
        { 
            m_ZoomLabel.Text = "";
            return;
        }

        bool size_changed = old == null || Math.Abs(old.Width - now.Width) > 0.5 || Math.Abs(old.Height - now.Height) > 0.5;

        if (size_changed && !m_Fit && old != null)
        {
            // Different mip. Keep the same on-screen size.
            double f = old.Width / now.Width;
            SetZoom(m_Scale.ScaleX * f, new Point(ActualWidth / 2, ActualHeight / 2), keep_fit: false);
        }
        else if (m_Fit || old == null)
            FitToView();

        UpdateLabel();
    }

    public void FitToView()
    {
        var s = Source;

        if (s == null || ActualWidth <= 0 || ActualHeight <= 0)
            return;

        double z = Math.Min((ActualWidth - 24) / s.Width, (ActualHeight - 24) / s.Height);
        z = Math.Max(0.01, Math.Min(z, 8));

        m_Scale.ScaleX = m_Scale.ScaleY = z;
        m_Translate.X = (ActualWidth - s.Width * z) / 2;
        m_Translate.Y = (ActualHeight - s.Height * z) / 2;
        m_Fit = true;

        UpdateScaling();
        UpdateLabel();
    }

    public void ActualSize()
    {
        var s = Source;

        if (s == null)
            return;

        m_Scale.ScaleX = m_Scale.ScaleY = 1;
        m_Translate.X = Math.Round((ActualWidth - s.Width) / 2);
        m_Translate.Y = Math.Round((ActualHeight - s.Height) / 2);
        m_Fit = false;

        UpdateScaling();
        UpdateLabel();
    }

    private void SetZoom(double z, Point anchor, bool keep_fit)
    {
        z = Math.Clamp(z, 0.02, 64);

        double old = m_Scale.ScaleX;
        double ix = (anchor.X - m_Translate.X) / old, iy = (anchor.Y - m_Translate.Y) / old; // Keep the image point under the anchor fixed.

        m_Scale.ScaleX = m_Scale.ScaleY = z;
        m_Translate.X = anchor.X - ix * z;
        m_Translate.Y = anchor.Y - iy * z;
        m_Fit = keep_fit;

        UpdateScaling();
        UpdateLabel();
    }

    private void UpdateScaling() =>
        RenderOptions.SetBitmapScalingMode(m_Image, m_Scale.ScaleX >= 2 ? BitmapScalingMode.NearestNeighbor : BitmapScalingMode.HighQuality);

    private void UpdateLabel() => m_ZoomLabel.Text = Source == null ? "" : $"{m_Scale.ScaleX * 100:0}%{(m_Fit ? " (fit)" : "")}";

    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        base.OnMouseWheel(e);

        if (Source == null)
            return;

        double factor = e.Delta > 0 ? 1.25 : 1 / 1.25;
        SetZoom(m_Scale.ScaleX * factor, e.GetPosition(this), keep_fit: false);

        e.Handled = true;
    }

    protected override void OnMouseDown(MouseButtonEventArgs e)
    {
        base.OnMouseDown(e);
        Focus();

        if (e.ChangedButton == MouseButton.Left && e.ClickCount == 2)
        {
            if (m_Fit)
                ActualSize();
            else
                FitToView();

            e.Handled = true;
            return;
        }
        if (e.ChangedButton is MouseButton.Left or MouseButton.Middle)
        {
            m_DragStart = e.GetPosition(this);
            m_DragOrigin = new Point(m_Translate.X, m_Translate.Y);

            CaptureMouse();
            Cursor = Cursors.SizeAll;
        }
    }

    protected override void OnMouseUp(MouseButtonEventArgs e)
    {
        base.OnMouseUp(e);

        if (m_DragStart != null)
        {
            m_DragStart = null;
            ReleaseMouseCapture();
            Cursor = null;
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        var p = e.GetPosition(this);

        if (m_DragStart is {} s)
        {
            m_Translate.X = m_DragOrigin.X + (p.X - s.X);
            m_Translate.Y = m_DragOrigin.Y + (p.Y - s.Y);
            m_Fit = false;

            UpdateLabel();
        }

        var src = Source;

        if (src == null)
            return;

        int x = (int)Math.Floor((p.X - m_Translate.X) / m_Scale.ScaleX);
        int y = (int)Math.Floor((p.Y - m_Translate.Y) / m_Scale.ScaleY);

        if (x >= 0 && y >= 0 && x < src.Width && y < src.Height)
            PixelHover?.Invoke(x, y);

        else PixelLeave?.Invoke();
    }

    protected override void OnMouseLeave(MouseEventArgs e)
    {
        base.OnMouseLeave(e);
        PixelLeave?.Invoke();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);

        if (e.Key is Key.D0 or Key.NumPad0 or Key.F)
        { 
            FitToView();
            e.Handled = true;
        }
        else if (e.Key is Key.D1 or Key.NumPad1)
        {
            ActualSize();
            e.Handled = true;
        }
        else if (e.Key is Key.OemPlus or Key.Add)
        {
            SetZoom(m_Scale.ScaleX * 1.25, new Point(ActualWidth / 2, ActualHeight / 2), false);
            e.Handled = true;
        }
        else if (e.Key is Key.OemMinus or Key.Subtract)
        {
            SetZoom(m_Scale.ScaleX / 1.25, new Point(ActualWidth / 2, ActualHeight / 2), false);
            e.Handled = true;
        }
    }

    private static string? DroppedFile(DragEventArgs e) =>
        e.Data.GetDataPresent(DataFormats.FileDrop) && e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } f ? f[0] : null;

    protected override void OnDragEnter(DragEventArgs e)
    {
        base.OnDragEnter(e);

        if (DroppedFile(e) != null && FileDropped != null)
        {
            m_DropHint.Visibility = Visibility.Visible;
            e.Effects = DragDropEffects.Copy;
        }
        else
            e.Effects = DragDropEffects.None;

        e.Handled = true;
    }

    protected override void OnDragOver(DragEventArgs e)
    {
        base.OnDragOver(e);

        e.Effects = DroppedFile(e) != null && FileDropped != null ? DragDropEffects.Copy : DragDropEffects.None;
        e.Handled = true;
    }

    protected override void OnDragLeave(DragEventArgs e)
    {
        base.OnDragLeave(e);
        m_DropHint.Visibility = Visibility.Collapsed;
    }

    protected override void OnDrop(DragEventArgs e)
    {
        base.OnDrop(e);

        m_DropHint.Visibility = Visibility.Collapsed;

        if (DroppedFile(e) is {} f)
            FileDropped?.Invoke(f);

        e.Handled = true;
    }
}