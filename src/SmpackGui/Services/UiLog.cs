using System.Collections.ObjectModel;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using SmpackGui.Core.Cli;

namespace SmpackGui.Services;

public sealed record LogEntry(DateTime Time, LogKind Kind, string Text)
{
    public string TimeLabel => Time.ToString("HH:mm:ss");
    public string Prefix => Kind switch
    {
        LogKind.COMMAND => ">",
        LogKind.ERROR => "x",
        LogKind.SUCCESS => "+",
        LogKind.WARNING => "!",
        _ => " ",
    };
}

/// <summary>Thread safe activity log backing the console panel. Batches updates to keep the UI smooth.</summary>
public sealed class UiLog : IActivityLog
{
    private const int MAX_ENTRIES = 5000;
    private readonly Dispatcher m_Dispatcher;
    private readonly List<LogEntry> m_Pending = [];
    private readonly DispatcherTimer m_Flush;

    public ObservableCollection<LogEntry> Entries { get; } = [];
    public event Action<LogEntry>? Appended;

    public UiLog(Dispatcher dispatcher)
    {
        m_Dispatcher = dispatcher;
        m_Flush = new DispatcherTimer(TimeSpan.FromMilliseconds(80), DispatcherPriority.Background, (_, _) => Flush(), dispatcher);
        m_Flush.Start();
    }

    public void Write(LogKind kind, string text)
    {
        var e = new LogEntry(DateTime.Now, kind, text);

        lock (m_Pending)
            m_Pending.Add(e);
    }

    private void Flush()
    {
        List<LogEntry> batch;

        lock (m_Pending)
        {
            if (m_Pending.Count == 0)
                return;

            batch = [.. m_Pending];
            m_Pending.Clear();
        }

        // Trim before adding so a large batch does not grow the list past the limit first.
        int drop = Math.Min(Entries.Count, Math.Max(0, Entries.Count + batch.Count - MAX_ENTRIES));

        for (int i = 0; i < drop; i++)
            Entries.RemoveAt(0);

        foreach (var e in batch.Skip(Math.Max(0, batch.Count - MAX_ENTRIES)))
            Entries.Add(e);

        // One notification per batch. The window scrolls to the end once.
        Appended?.Invoke(batch[^1]);
    }

    public void Clear() => Entries.Clear();

    public string AsText()
    {
        var sb = new StringBuilder();

        foreach (var e in Entries)
            sb.Append(e.TimeLabel).Append(' ').Append(e.Prefix).Append(' ').AppendLine(e.Text);

        return sb.ToString();
    }

    public void CopyToClipboard()
    {
        try
        {
            Clipboard.SetText(AsText());
        }
        catch
        { 
            // Clipboard busy.
        }
    }
}