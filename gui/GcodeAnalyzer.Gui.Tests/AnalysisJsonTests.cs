using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class AnalysisJsonTests
{
    private static string Golden(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", name));

    [Fact]
    public void ParsesGoldenJsonWithArcAndTime()
    {
        var data = AnalysisJson.Parse(Golden("known-time.json"));
        Assert.Equal(1, data.Schema);
        Assert.Equal("known-time.nc", data.File.Name);
        Assert.Equal(13, data.File.Lines);
        Assert.Equal(5, data.Segments.Length);
        var arc = Assert.Single(data.Segments, s => s.Kind == "arc");
        Assert.Equal(9, arc.Line);
        Assert.Equal("G17", arc.Plane);
        Assert.Equal(new double?[] { 200, 0, 0 }, arc.Start);
        Assert.Equal(new double?[] { 210, 10, 0 }, arc.End);
        Assert.Equal(new double?[] { 200, 10, 0 }, arc.Center);
        Assert.False(arc.Cw);
        Assert.Equal(10, arc.Radius);
        Assert.Equal(600, arc.Feed);
        Assert.Equal(1000, arc.Spindle);
        Assert.Null(arc.B);
        // 0,6 + 10 + pi/2 + 0,5 + 0,3 секунды, округление JSON до 4 знаков.
        Assert.Equal(12.9708, data.Time.TotalSec);
        var tool = Assert.Single(data.Tools);
        Assert.Equal(data.Time.TotalSec, tool.Time.TotalSec);
        Assert.Equal(600, tool.FeedPerMin!.Min);
        Assert.Null(tool.FeedPerRev);
        Assert.Empty(data.Warnings);
    }

    [Fact]
    public void ParsesGoldenJsonWithBAndWarningLevels()
    {
        var data = AnalysisJson.Parse(Golden("axis-b-index.json"));
        Assert.Equal(18, data.Segments.Length);
        Assert.Contains(data.Segments, s => s.B == 90);
        Assert.Contains(data.Segments, s => s.B == 180);
        Assert.All(data.Segments[0].Start, Assert.Null);
        Assert.Null(data.Segments[2].End[2]);
        Assert.Equal(2, data.Warnings.Length);
        Assert.All(data.Warnings, w => { Assert.Equal("W012", w.Code); Assert.Equal("warning", w.Severity); });
        Assert.Equal("M11", data.Warnings[0].Text);
        Assert.Contains("неизвестный", data.Warnings[0].Message);
        Assert.Equal(180, data.Tools[0].Box[3]!.Max);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    public void RejectsUnsupportedSchema(int schema)
    {
        var root = JsonNode.Parse(Golden("known-time.json"))!;
        root["schema"] = schema;
        Assert.Throws<JsonException>(() => AnalysisJson.Parse(root.ToJsonString()));
    }

    [Theory]
    [InlineData("schema")]
    [InlineData("segments")]
    [InlineData("warnings")]
    [InlineData("tools")]
    [InlineData("time")]
    [InlineData("file")]
    public void RejectsMissingRequiredData(string key)
    {
        var root = JsonNode.Parse(Golden("known-time.json"))!.AsObject();
        root.Remove(key);
        Assert.Throws<JsonException>(() => AnalysisJson.Parse(root.ToJsonString()));
    }

    [Fact]
    public void RejectsInvalidCoordinatesAndSeverity()
    {
        var root = JsonNode.Parse(Golden("axis-b-index.json"))!;
        root["segments"]![0]!["start"] = new JsonArray(1, 2);
        Assert.Throws<JsonException>(() => AnalysisJson.Parse(root.ToJsonString()));
        root = JsonNode.Parse(Golden("axis-b-index.json"))!;
        root["warnings"]![0]!["severity"] = "unknown";
        Assert.Throws<JsonException>(() => AnalysisJson.Parse(root.ToJsonString()));
        Assert.Throws<JsonException>(() => AnalysisJson.Parse("null"));
        Assert.Throws<JsonException>(() => AnalysisJson.Parse("{"));
    }

    private static (AnalyzerRunner Runner, string Directory) RealRunner()
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(exe); // CLI должен быть собран: иначе сквозная проверка не состоялась.
        string directory = Path.Combine(Path.GetDirectoryName(exe)!, "stage9-tests", "тест папка " + Guid.NewGuid().ToString("N"));
        System.IO.Directory.CreateDirectory(directory);
        return (new AnalyzerRunner(exe), directory);
    }

    [Fact]
    public async Task AnalyzesJsonWithCyrillicPath()
    {
        var (runner, directory) = RealRunner();
        string input = Path.Combine(directory, "деталь 1.nc");
        string text = "G92 X10 Y0 Z0\nG54\nT1M6\nS1000 M3\nG3 X0 Y10 I-10 J0 F600\nM30\n";
        await File.WriteAllTextAsync(input, text, new UTF8Encoding(false));
        string jsonPath = Path.Combine(directory, "данные 1.json"), reportPath = Path.Combine(directory, "отчёт 1.txt");
        var run = await runner.RunAsync([input, "--out", reportPath, "--json", jsonPath, "--bom"]);
        Assert.Equal(0, run.ExitCode);
        byte[] bytes = await File.ReadAllBytesAsync(jsonPath);
        Assert.False(bytes.Take(3).SequenceEqual(new byte[] { 0xef, 0xbb, 0xbf }));
        var json = AnalysisJson.Parse(Encoding.UTF8.GetString(bytes));
        Assert.Equal("деталь 1.nc", json.File.Name);
        Assert.Equal("arc", Assert.Single(json.Segments).Kind);
        Assert.Contains("Время обработки", await File.ReadAllTextAsync(reportPath));
        var (memoryRun, report) = await runner.AnalyzeToMemoryAsync(input, "auto", "--rapid 12000");
        Assert.Equal(0, memoryRun.ExitCode);
        Assert.NotNull(memoryRun.Analysis);
        Assert.Equal(12000, memoryRun.Analysis.File.RapidFeed);
        Assert.Contains("деталь 1.nc", report);
        var saved = await runner.AnalyzeToFileAsync(input, reportPath, "auto", true, null);
        Assert.Equal(0, saved.ExitCode);
        Assert.NotNull(saved.Analysis);
    }

    [Fact]
    public async Task ParsesEffectiveFeedCycleAndEverySeverityFromCli()
    {
        var (runner, directory) = RealRunner();
        string input = Path.Combine(directory, "уровни.nc");
        await File.WriteAllTextAsync(input, "G92 X0 Y0 Z10\nG1 X1\nG123\n@\nG54 S1000 M3\nG95 G81 Z-5 R2 F0.1\nG80\nM30\n");
        var (run, _) = await runner.AnalyzeToMemoryAsync(input, "auto", null);
        Assert.Equal(0, run.ExitCode);
        Assert.NotNull(run.Analysis);
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W004" && w.Severity == "error");
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W012" && w.Severity == "warning");
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W013" && w.Severity == "info");
        var cycle = Assert.Single(run.Analysis.Segments, s => s.Kind == "cycle");
        Assert.Equal(100, cycle.Feed);
        Assert.NotNull(cycle.Cycle);
        Assert.Equal(2, cycle.Cycle.ZR);
        Assert.Equal(-5, cycle.Cycle.ZBottom);
    }

    [Fact]
    public async Task MissingJsonIsReportedAsError()
    {
        var (runner, _) = RealRunner();
        var (run, report) = await runner.AnalyzeToMemoryAsync("x.nc", "auto", "--version");
        Assert.Equal(-1, run.ExitCode);
        Assert.Contains("JSON", run.StdErr);
        Assert.Null(run.Analysis);
        Assert.Empty(report);
    }

    [Fact]
    public void JsonArgumentFollowsUserArguments()
    {
        var args = AnalyzerArguments.Build("деталь.nc", "отчёт.txt", "auto", false, "--json чужой.json", "данные.json");
        Assert.Equal(new[] { "--out", "отчёт.txt", "--json", "данные.json" }, args.TakeLast(4));
    }
}
