using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Результат запуска gcode-analyzer.exe.</summary>
/// <param name="ExitCode">0 — успех, 1 — ошибка ввода-вывода, 2 — ошибка аргументов, -1 — не удалось запустить.</param>
public record RunResult(int ExitCode, string StdOut, string StdErr)
{
    public AnalysisData? Analysis { get; init; }
}

/// <summary>Запускает gcode-analyzer.exe дочерним процессом. Анализ не дублируется в GUI.</summary>
public class AnalyzerRunner
{
    private readonly string _exePath;

    public AnalyzerRunner(string exePath) => _exePath = exePath;

    /// <summary>Запуск с произвольными аргументами; окно в это время не блокируется (async/await).</summary>
    public async Task<RunResult> RunAsync(IEnumerable<string> arguments)
    {
        var startInfo = new ProcessStartInfo(_exePath)
        {
            UseShellExecute = false,   // нужно для перенаправления потоков
            CreateNoWindow = true,     // без чёрного окна консоли
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        // ArgumentList сам экранирует пробелы и кавычки: строку аргументов вручную не склеиваем
        foreach (string a in arguments) startInfo.ArgumentList.Add(a);

        try
        {
            using var process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("Процесс не запустился");

            // Читаем оба потока параллельно, иначе процесс может зависнуть на заполненном буфере
            Task<string> stdout = process.StandardOutput.ReadToEndAsync();
            Task<string> stderr = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            return new RunResult(process.ExitCode, await stdout, await stderr);
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException)
        {
            return new RunResult(-1, "", $"Не удалось запустить {_exePath}: {e.Message}");
        }
    }

    /// <summary>
    /// Анализ файла: отчёт пишется во временный файл (--out) и читается как UTF-8.
    /// Так кодировка консоли не влияет на результат. Временный файл удаляется.
    /// </summary>
    public async Task<(RunResult Run, string Report)> AnalyzeToMemoryAsync(
        string inputFile, string? machineId, string? extraArguments)
    {
        string tempFile = Path.Combine(Path.GetTempPath(), $"gca-{Guid.NewGuid():N}.txt");
        string jsonFile = Path.Combine(Path.GetTempPath(), $"gca-{Guid.NewGuid():N}.json");
        try
        {
            var args = AnalyzerArguments.Build(inputFile, tempFile, machineId, bom: false, extraArguments, jsonFile);
            RunResult run = await ReadAnalysisAsync(await RunAsync(args), jsonFile);
            string report = run.ExitCode == 0 && File.Exists(tempFile)
                ? await File.ReadAllTextAsync(tempFile, Encoding.UTF8)
                : "";
            return (run, report);
        }
        finally
        {
            if (File.Exists(tempFile)) File.Delete(tempFile);
            if (File.Exists(jsonFile)) File.Delete(jsonFile);
        }
    }

    /// <summary>Анализ с записью отчёта прямо в выбранный пользователем .txt.</summary>
    public async Task<RunResult> AnalyzeToFileAsync(
        string inputFile, string outFile, string? machineId, bool bom, string? extraArguments)
    {
        string jsonFile = Path.Combine(Path.GetTempPath(), $"gca-{Guid.NewGuid():N}.json");
        try
        {
            var args = AnalyzerArguments.Build(inputFile, outFile, machineId, bom, extraArguments, jsonFile);
            return await ReadAnalysisAsync(await RunAsync(args), jsonFile);
        }
        finally
        {
            if (File.Exists(jsonFile)) File.Delete(jsonFile);
        }
    }

    private static async Task<RunResult> ReadAnalysisAsync(RunResult run, string jsonFile)
    {
        if (run.ExitCode != 0) return run;
        try
        {
            return run with { Analysis = AnalysisJson.Parse(await File.ReadAllTextAsync(jsonFile, Encoding.UTF8)) };
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return run with { ExitCode = -1, StdErr = $"Не удалось прочитать JSON анализа: {e.Message}" };
        }
    }
}
