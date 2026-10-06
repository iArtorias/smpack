using System.Security;
using SmpackGui.Core.Cli;

namespace SmpackGui.Core.Audio;

/// <summary>
/// WAV to Wwise WEM through Audiokinetic's WwiseConsole.
/// </summary>
public sealed class WwiseConverter(IActivityLog? log)
{
    public const string ProjectUrl = "https://www.audiokinetic.com/download/";
    public const string Conversion = "Vorbis Quality High";

    private readonly ToolRunner m_Runner = new(log);

    public string? ExePath
    {
        get;
        set;
    }
    public bool Available => ExePath != null && File.Exists(ExePath);

    public static string? Locate(string? configured)
    {
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured))
            return configured;

        var cands = new List<string>();
        var root = Environment.GetEnvironmentVariable("WWISEROOT");

        if (!string.IsNullOrEmpty(root))
            cands.Add(Path.Combine(root, "Authoring", "x64", "Release", "bin", "WwiseConsole.exe"));

        foreach (var pf in new[]
        {
            Environment.GetEnvironmentVariable("SystemDrive"),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
        })
        {
            if (string.IsNullOrWhiteSpace(pf))
                continue;

            var ak = Path.Combine(pf, "Audiokinetic");

            if (!Directory.Exists(ak))
                continue;

            try
            {
                foreach (var d in Directory.EnumerateDirectories(ak, "Wwise*").OrderByDescending(d => d, StringComparer.OrdinalIgnoreCase))
                    cands.Add(Path.Combine(d, "Authoring", "x64", "Release", "bin", "WwiseConsole.exe"));
            }

            catch (IOException)
            {}
            catch (UnauthorizedAccessException)
            {}
        }

        return cands.FirstOrDefault(File.Exists);
    }

    private async Task<string> EnsureProjectAsync(string work_root, CancellationToken ct)
    {
        var proj = Path.Combine(work_root, "project", "SmpackConvert", "SmpackConvert.wproj");

        if (File.Exists(proj))
            return proj;

        Directory.CreateDirectory(Path.Combine(work_root, "project"));
        var r = await m_Runner.RunAsync(ExePath!, ["create-new-project", proj, "--platform", "Windows"], ct: ct).ConfigureAwait(false);

        if (!File.Exists(proj))
            throw new SmpackException($"WwiseConsole could not create its scratch project: {r.ErrorMessage}", r);

        return proj;
    }

    /// <summary>Convert <paramref name="wav"/> to <paramref name="wem_out"/> Vorbis.</summary>
    public async Task ConvertAsync(string wav, string wem_out, string work_root, CancellationToken ct = default)
    {
        if (!Available)
            throw new InvalidOperationException("WwiseConsole.exe was not found. Install Wwise or set its path in Settings.");

        var proj = await EnsureProjectAsync(work_root, ct).ConfigureAwait(false);
        var job = Path.Combine(work_root, "jobs", Guid.NewGuid().ToString("N"));

        Directory.CreateDirectory(job);

        try
        {
            var src = Path.Combine(job, "input.wav");
            File.Copy(wav, src);

            var list = Path.Combine(job, "list.wsources");
            File.WriteAllText(list,
                $"""
                <?xml version="1.0" encoding="UTF-8"?>
                <ExternalSourcesList SchemaVersion="1" Root="{SecurityElement.Escape(job)}">
                  <Source Path="input.wav" Conversion="{Conversion}"/>
                </ExternalSourcesList>
                """);

            var out_dir = Path.Combine(job, "out");
            var r = await m_Runner.RunAsync(ExePath!, ["convert-external-source", proj, "--source-file", list, "--output", out_dir], ct: ct)
                .ConfigureAwait(false);

            var wem = Directory.Exists(out_dir) ? Directory.EnumerateFiles(out_dir, "*.wem", SearchOption.AllDirectories).FirstOrDefault() : null;

            if (wem == null)
                throw new SmpackException($"WwiseConsole produced no .wem: {r.ErrorMessage}", r);

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wem_out))!);
            File.Copy(wem, wem_out, overwrite: true);
        }
        finally
        {
            try
            {
                Directory.Delete(job, true);
            }
            catch
            {}
        }
    }
}