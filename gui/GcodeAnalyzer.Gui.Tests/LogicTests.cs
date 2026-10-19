using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class ArgumentSplitterTests
{
    [Fact]
    public void SplitsBySpaces() =>
        Assert.Equal(new[] { "--rapid", "12000", "--dialect", "fanuc-mill" },
            ArgumentSplitter.Split("--rapid 12000   --dialect fanuc-mill"));

    [Fact]
    public void QuotesKeepSpacesAndCyrillic() =>
        Assert.Equal(new[] { "--out", @"C:\мои файлы\отчёт.txt", "x" },
            ArgumentSplitter.Split(@"--out ""C:\мои файлы\отчёт.txt"" x"));

    [Fact]
    public void EmptyQuotesGiveEmptyArgument() =>
        Assert.Equal(new[] { "a", "", "b" }, ArgumentSplitter.Split("a \"\" b"));

    [Fact]
    public void UnclosedQuoteTakesTheRest() =>
        Assert.Equal(new[] { "a", "b c" }, ArgumentSplitter.Split("a \"b c"));

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void EmptyInputGivesNoArguments(string? text) => Assert.Empty(ArgumentSplitter.Split(text));
}

public class AnalyzerArgumentsTests
{
    [Fact]
    public void MinimalList()
    {
        var args = AnalyzerArguments.Build(@"C:\УП\деталь 1.nc", @"C:\Temp\r.txt", "auto", false, null);
        Assert.Equal(new[] { @"C:\УП\деталь 1.nc", "--out", @"C:\Temp\r.txt" }, args);
    }

    [Fact]
    public void MachineBomAndExtraArguments()
    {
        var args = AnalyzerArguments.Build("in.nc", "out.txt", "skymaster-vl1100a", true, "--rapid 12000 --dialect \"fanuc mill\"");
        Assert.Equal(new[]
        {
            "in.nc", "--rapid", "12000", "--dialect", "fanuc mill",
            "--machine", "skymaster-vl1100a", "--bom", "--out", "out.txt",
        }, args);
    }

    [Fact]
    public void AutoMachineIsNotPassed() =>
        Assert.DoesNotContain("--machine", AnalyzerArguments.Build("in.nc", "o.txt", "auto", false, ""));
}

public class ExeLocatorTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "gca-gui-test-" + Guid.NewGuid().ToString("N"));

    public ExeLocatorTests() => Directory.CreateDirectory(_root);
    public void Dispose() => Directory.Delete(_root, true);

    private string Touch(params string[] parts)
    {
        string path = Path.Combine(new[] { _root }.Concat(parts).ToArray());
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "");
        return path;
    }

    [Fact]
    public void FindsExeNextToStartDirectory()
    {
        string exe = Touch("app", ExeLocator.ExeName);
        Assert.Equal(exe, ExeLocator.Find(Path.Combine(_root, "app")));
    }

    [Fact]
    public void FindsExeInBuildPresetConfigAboveStartDirectory()
    {
        string exe = Touch("build", "msvc", "Debug", ExeLocator.ExeName);
        string start = Path.Combine(_root, "gui", "GcodeAnalyzer.Gui", "bin", "Debug");
        Directory.CreateDirectory(start);
        Assert.Equal(exe, ExeLocator.Find(start));
    }

    [Fact]
    public void FindsExeDirectlyInBuildPreset()
    {
        string exe = Touch("build", "debug", ExeLocator.ExeName);
        Assert.Equal(exe, ExeLocator.Find(_root));
    }

    [Fact]
    public void ConfiguredPathWins()
    {
        Touch("build", "msvc", "Debug", ExeLocator.ExeName);
        string custom = Touch("custom", "my-analyzer.exe");
        Assert.Equal(custom, ExeLocator.Find(_root, custom));
    }

    [Fact]
    public void MissingConfiguredPathFallsBackToSearch()
    {
        string exe = Touch("build", "msvc", "Release", ExeLocator.ExeName);
        Assert.Equal(exe, ExeLocator.Find(_root, Path.Combine(_root, "нет-такого.exe")));
    }

    [Fact]
    public void NothingFoundInsideAnEmptyTree()
    {
        string empty = Path.Combine(_root, "empty");
        Directory.CreateDirectory(empty);
        // над временным каталогом могут оказаться чужие сборки: проверяем только, что внутри дерева ничего нет
        string? found = ExeLocator.Find(empty);
        Assert.True(found == null || !found.StartsWith(_root));
    }
}

public class ReportParserTests
{
    private static string Golden(string name) =>
        File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", name));

    [Fact]
    public void ParsesAllSectionsOfSample1()
    {
        string text = ReportParser.NormalizeNewlines(Golden("sample-01-skymaster-vl1100a.txt"));
        var sections = ReportParser.ParseSections(text);

        Assert.Equal(12, sections.Count);
        Assert.Equal("Файл", sections[0].Title);
        Assert.Equal("Инструменты", sections[1].Title);
        Assert.Contains(sections, s => s.Title.StartsWith("Время обработки"));
        Assert.Contains(sections, s => s.Title == "Предупреждения");
        Assert.Equal("Шпаргалка для постпроцессора", sections[^1].Title);
    }

    [Fact]
    public void OffsetsPointToHeadings()
    {
        string text = ReportParser.NormalizeNewlines(Golden("sample-01-skymaster-vl1100a.txt"));
        foreach (var s in ReportParser.ParseSections(text))
            Assert.StartsWith("== " + s.Title + " ==", text[s.Offset..]);
    }

    [Fact]
    public void WorksWithLfAndCrlf()
    {
        string lf = Golden("sample-01-skymaster-vl1100a.txt").Replace("\r\n", "\n");
        string crlf = lf.Replace("\n", "\r\n");
        Assert.Equal(ReportParser.ParseSections(lf).Select(s => s.Title), ReportParser.ParseSections(crlf).Select(s => s.Title));
    }

    [Fact]
    public void FindsMachine()
    {
        Assert.Contains("Sky Master VL1100A", ReportParser.FindMachine(Golden("sample-01-skymaster-vl1100a.txt")));
        Assert.Contains("Bridgeport", ReportParser.FindMachine(Golden("sample-02-bridgeport-1300-480.txt")));
        Assert.Equal("", ReportParser.FindMachine("нет такой строки"));
    }

    [Fact]
    public void TextWithoutSectionsGivesEmptyList() =>
        Assert.Empty(ReportParser.ParseSections("просто текст\n== не раздел"));
}

public class CliTablesTests
{
    [Fact]
    public void ParsesOptions()
    {
        string text = "--out\t-o\t<файл>\t\tзаписать отчёт\r\n--bom\t\t\t\tBOM\r\nплохая строка\r\n";
        var options = CliTables.ParseOptions(text);
        Assert.Equal(2, options.Count);
        Assert.Equal(new CliOption("--out", "-o", "<файл>", "", "записать отчёт"), options[0]);
        Assert.Equal("", options[1].Alias);
        Assert.Equal("BOM", options[1].Description);
    }

    [Fact]
    public void ParsesMachines()
    {
        var machines = CliTables.ParseMachines("skymaster-vl1100a\tSky Master VL1100A\nbridgeport-1300-480\tBridgeport 1300/480\n");
        Assert.Equal(2, machines.Count);
        Assert.Equal(new MachineInfo("skymaster-vl1100a", "Sky Master VL1100A"), machines[0]);
    }

    [Fact]
    public void EmptyTextGivesEmptyLists()
    {
        Assert.Empty(CliTables.ParseOptions(""));
        Assert.Empty(CliTables.ParseMachines(""));
    }
}

public class AppSettingsTests
{
    [Fact]
    public void SaveAndLoadRoundTrip()
    {
        string path = Path.Combine(Path.GetTempPath(), "gca-settings-" + Guid.NewGuid().ToString("N"), "settings.json");
        try
        {
            new AppSettings { ExePath = @"C:\тест\a.exe", ExtraArguments = "--rapid 1", SaveToFile = true, Bom = true }.Save(path);
            var loaded = AppSettings.Load(path);
            Assert.Equal(@"C:\тест\a.exe", loaded.ExePath);
            Assert.Equal("--rapid 1", loaded.ExtraArguments);
            Assert.True(loaded.SaveToFile);
            Assert.True(loaded.Bom);
        }
        finally
        {
            Directory.Delete(Path.GetDirectoryName(path)!, true);
        }
    }

    [Fact]
    public void MissingOrBrokenFileGivesDefaults()
    {
        Assert.Equal("", AppSettings.Load(Path.Combine(Path.GetTempPath(), "no-such-" + Guid.NewGuid().ToString("N") + ".json")).ExePath);
        string broken = Path.GetTempFileName();
        File.WriteAllText(broken, "{ не json");
        Assert.Equal("", AppSettings.Load(broken).ExePath);
        File.Delete(broken);
    }
}

/// <summary>Сквозной тест с настоящим gcode-analyzer.exe; если CLI не собран, тест тихо пропускается.</summary>
public class AnalyzerRunnerTests
{
    private static AnalyzerRunner? CreateRunner()
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory);
        return exe == null ? null : new AnalyzerRunner(exe);
    }

    [Fact]
    public async Task ListMachinesAndOptionsWork()
    {
        var runner = CreateRunner();
        if (runner == null) return;
        var machines = CliTables.ParseMachines((await runner.RunAsync(new[] { "--list-machines" })).StdOut);
        Assert.Contains(machines, m => m.Id == "skymaster-vl1100a");
        var options = CliTables.ParseOptions((await runner.RunAsync(new[] { "--list-options" })).StdOut);
        Assert.Contains(options, o => o.Name == "--rapid" && o.Default == "10000");
    }

    [Fact]
    public async Task AnalyzesFileWithCyrillicPathViaTempFile()
    {
        var runner = CreateRunner();
        if (runner == null) return;
        string dir = Path.Combine(Path.GetTempPath(), "gca-руки-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            string input = Path.Combine(dir, "деталь 1.nc");
            File.WriteAllText(input, "O\nG54\nG0 X10. Y10.\nG1 X20. F100\nM30\n");
            var (run, report) = await runner.AnalyzeToMemoryAsync(input, "auto", "--rapid 12000");
            Assert.Equal(0, run.ExitCode);
            Assert.Contains("деталь 1.nc", report);
            Assert.Contains("Скорость G0: 12000 мм/мин", report);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public async Task BadArgumentGivesExitCode2AndRussianMessage()
    {
        var runner = CreateRunner();
        if (runner == null) return;
        var (run, report) = await runner.AnalyzeToMemoryAsync("x.nc", "auto", "--rapid abc");
        Assert.Equal(2, run.ExitCode);
        Assert.Contains("положительным", run.StdErr);
        Assert.Equal("", report);
    }

    [Fact]
    public async Task MissingExeGivesStartError()
    {
        var run = await new AnalyzerRunner(@"C:\нет\такого.exe").RunAsync(new[] { "--help" });
        Assert.Equal(-1, run.ExitCode);
        Assert.NotEmpty(run.StdErr);
    }
}
