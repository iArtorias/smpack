using System.Diagnostics;
using System.Text;

namespace SmpackGui.Core.Cli;

public sealed record CliResult(int ExitCode, string StdOut, string StdErr, TimeSpan Duration)
{
    public bool Success => ExitCode == 0;

    /// <summary>The 'error: ...' message smpack prints on failure, or the last stderr line.</summary>
    public string ErrorMessage
    {
        get
        {
            var lines = StdErr.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            var err = lines.LastOrDefault(l => l.StartsWith("error:", StringComparison.OrdinalIgnoreCase));

            if (err != null)
                return err[6..].Trim();

            return lines.LastOrDefault() ?? $"smpack exited with code {ExitCode}";
        }
    }

    public CliResult ThrowIfFailed()
    {
        if (!Success)
            throw new SmpackException(ErrorMessage, this);

        return this;
    }
}

public sealed class SmpackException(string message, CliResult? result = null) : Exception(message)
{
    public CliResult? Result { get; } = result;
}

public enum LogKind
{
    COMMAND,
    OUTPUT,
    ERROR,
    INFO,
    SUCCESS,
    WARNING
}

/// <summary>Anything that wants to see command activity in the GUI log panel.</summary>
public interface IActivityLog
{
    void Write(LogKind kind, string text);
}

/// <summary>Runs an external tool, streaming its output to an <see cref="IActivityLog"/>.</summary>
public class ToolRunner(IActivityLog? log)
{
    public IActivityLog? Log
    {
        get;
        set;
    } = log;

    public async Task<CliResult> RunAsync(string exe, IEnumerable<string> args, string? work_dir = null,
        Action<string>? on_stdout = null, Action<string>? on_stderr = null, bool echo_stdout = true,
        CancellationToken ct = default)
    {
        var arg_list = args.ToList();
        var psi = new ProcessStartInfo(exe)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
            WorkingDirectory = work_dir ?? Path.GetDirectoryName(exe) ?? Environment.CurrentDirectory,
        };

        foreach (var a in arg_list)
            psi.ArgumentList.Add(a);

        Log?.Write(LogKind.COMMAND, $"{Path.GetFileNameWithoutExtension(exe)} {string.Join(' ', arg_list.Select(Quote))}");

        var sw = Stopwatch.StartNew();
        var stdout = new StringBuilder();
        var stderr = new StringBuilder();

        using var p = new Process
        {
            StartInfo = psi,
            EnableRaisingEvents = true
        };

        var out_done = new TaskCompletionSource();
        var err_done = new TaskCompletionSource();

        p.OutputDataReceived += (_, e) =>
        {
            if (e.Data == null)
            {
                out_done.TrySetResult();
                return;
            }

            lock (stdout)
                stdout.AppendLine(e.Data);

            on_stdout?.Invoke(e.Data);

            if (echo_stdout && e.Data.Length > 0)
                Log?.Write(LogKind.OUTPUT, e.Data);
        };

        p.ErrorDataReceived += (_, e) =>
        {
            if (e.Data == null)
            {
                err_done.TrySetResult();
                return;
            }

            lock (stderr)
                stderr.AppendLine(e.Data);

            on_stderr?.Invoke(e.Data);

            if (e.Data.Length > 0 && !e.Data.TrimStart().StartsWith("scanned ", StringComparison.Ordinal))
                Log?.Write(e.Data.StartsWith("error", StringComparison.OrdinalIgnoreCase) || e.Data.TrimStart().StartsWith('!')
                    ? LogKind.ERROR : LogKind.OUTPUT, e.Data);
        };

        try
        {
            if (!p.Start())
                throw new SmpackException($"Could not start {exe}");
        }
        catch (System.ComponentModel.Win32Exception ex)
        {
            throw new SmpackException($"Could not start '{exe}': {ex.Message}");
        }

        p.BeginOutputReadLine();
        p.BeginErrorReadLine();

        using (ct.Register(() =>
        {
            try
            {
                if (!p.HasExited)
                    p.Kill(entireProcessTree: true);
            }
            catch
            {
                // Already gone.
            }
        }))
        {
            await p.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);

            // A child process that inherited the pipes can keep them open. Don't wait for it forever.
            try
            {
                await Task.WhenAll(out_done.Task, err_done.Task).WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                Log?.Write(LogKind.WARNING, $"{Path.GetFileName(exe)}: output streams did not close");
            }
        }

        ct.ThrowIfCancellationRequested();
        var result = new CliResult(p.ExitCode, stdout.ToString(), stderr.ToString(), sw.Elapsed);

        if (!result.Success)
            Log?.Write(LogKind.ERROR, $"failed: {result.ErrorMessage}");

        return result;
    }

    public static string Quote(string a) => a.Length == 0 || a.Any(c => char.IsWhiteSpace(c) || c == '"') ? $"\"{a.Replace("\"", "\\\"")}\"" : a;
}

/// <summary>Typed frontend for smpack.exe.</summary>
public sealed class SmpackCli(string exe_path, IActivityLog? log) : ToolRunner(log)
{
    public string ExePath
    {
        get;
        set;
    } = exe_path;

    public bool Exists => File.Exists(ExePath);

    public Task<CliResult> RunAsync(IEnumerable<string> args, Action<string>? on_stdout = null, Action<string>? on_stderr = null,
        bool echo_stdout = true, CancellationToken ct = default)
    {
        if (!File.Exists(ExePath))
            throw new SmpackException($"smpack.exe not found at '{ExePath}'. Set its location in 'Settings'.");

        return RunAsync(ExePath, args, null, on_stdout, on_stderr, echo_stdout, ct);
    }

    public async Task<Version?> GetVersionAsync(CancellationToken ct = default)
    {
        var r = await RunAsync(["--version"], echo_stdout: false, ct: ct).ConfigureAwait(false);
        var tok = r.StdOut.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();

        return Version.TryParse(tok, out var v) ? v : null;
    }

    /// <summary>Minimum smpack version this GUI relies on.</summary>
    public static readonly Version MinVersion = new(1, 3, 0);
}