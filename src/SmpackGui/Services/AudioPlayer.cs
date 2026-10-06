using System.Windows.Media;
using System.Windows.Threading;
using SmpackGui.Mvvm;

namespace SmpackGui.Services;

/// <summary>Small WAV player for the 'Audio' page.</summary>
public sealed class AudioPlayer : ObservableObject
{
    private readonly MediaPlayer m_Player = new();
    private readonly DispatcherTimer m_Timer;
    private bool m_Updating, m_Seeking;
    private string? m_File;

    public AudioPlayer()
    {
        m_Player.MediaOpened += (_, _) =>
        {
            Duration = m_Player.NaturalDuration.HasTimeSpan ? m_Player.NaturalDuration.TimeSpan.TotalSeconds : 0;
        };

        m_Player.MediaEnded += (_, _) =>
        {
            m_Player.Stop();
            IsPlaying = false;

            SetPosition(0);
        };

        m_Player.MediaFailed += (_, e) =>
        {
            IsPlaying = false;
            Failed?.Invoke(e.ErrorException?.Message ?? "playback failed");
        };

        m_Timer = new DispatcherTimer
        { 
            Interval = TimeSpan.FromMilliseconds(80)
        };

        m_Timer.Tick += (_, _) => 
        { 
            if (!m_Seeking)
                SetPosition(m_Player.Position.TotalSeconds);
        };

        Volume = 0.8;
    }

    public event Action<string>? Failed;

    private bool m_IsPlaying;
    public bool IsPlaying
    {
        get => m_IsPlaying;

        private set
        {
            if (!Set(ref m_IsPlaying, value))
                return;

            if (value)
                m_Timer.Start();
            else
                m_Timer.Stop();
        }
    }

    private double m_Duration;
    public double Duration
    {
        get => m_Duration;

        private set
        {
            if (Set(ref m_Duration, value))
                OnPropertyChanged(nameof(TimeLabel));
        }
    }

    private double m_Position;
    public double Position
    {
        get => m_Position;

        set
        {
            if (!Set(ref m_Position, value))
                return;

            OnPropertyChanged(nameof(TimeLabel));

            if (!m_Updating && !m_Seeking)
                m_Player.Position = TimeSpan.FromSeconds(value);
        }
    }

    private void SetPosition(double s)
    {
        m_Updating = true;
        Position = s;
        m_Updating = false;
    }

    public double Volume
    {
        get => m_Player.Volume;

        set
        { 
            m_Player.Volume = Math.Clamp(value, 0, 1);
            OnPropertyChanged();
        }
    }

    public string TimeLabel => $"{Format(Position)} / {Format(Duration)}";

    public static string Format(double s)
    {
        var t = TimeSpan.FromSeconds(Math.Max(0, s));
        return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss") : t.ToString(@"m\:ss");
    }

    public bool HasMedia => m_File != null;

    public void Load(string file, bool autoplay)
    {
        m_Player.Stop();
        m_File = file;
        m_Player.Open(new Uri(file));
        Duration = 0;

        SetPosition(0);
        OnPropertyChanged(nameof(HasMedia));

        if (autoplay)
            Play();
        else
            IsPlaying = false;
    }

    public void Play()
    {
        if (m_File == null)
            return;

        m_Player.Play();
        IsPlaying = true;
    }

    public void Pause()
    {
        m_Player.Pause();
        IsPlaying = false;
    }

    public void Toggle()
    {
        if (IsPlaying)
            Pause();
        else
            Play();
    }

    public void Stop()
    {
        m_Player.Stop();
        IsPlaying = false;
        SetPosition(0);
    }

    /// <summary>Release the file.</summary>
    public void Close()
    {
        Stop();

        m_Player.Close();
        m_File = null;
        Duration = 0;

        OnPropertyChanged(nameof(HasMedia));
    }

    public void BeginSeek() => m_Seeking = true;

    public void EndSeek()
    {
        m_Seeking = false;
        m_Player.Position = TimeSpan.FromSeconds(Position);
    }
}