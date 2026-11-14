using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class MachineProfileTests
{
    [Fact]
    public void MachineFileSelectionUsesFileOnlyForSelectedItem()
    {
        var selection = new MachineProfileSelection();
        Assert.Null(selection.SelectedFile(0, 2));
        string file = Path.Combine(AppContext.BaseDirectory, "профиль с пробелом.ini");
        selection.Open(file);
        Assert.Equal("Файл: профиль с пробелом.ini", selection.FileLabel);
        Assert.Equal(file, selection.SelectedFile(3, 2));
        Assert.Null(selection.SelectedFile(0, 2)); Assert.Null(selection.SelectedFile(1, 2));
        var args = AnalyzerArguments.Build("a.nc", "r.txt", "auto", false, null, "r.json", selection.SelectedFile(3, 2));
        int index = args.IndexOf("--machine-file"); Assert.True(index > 0); Assert.Equal(file, args[index + 1]);
    }

    [Fact]
    public void ParsesLimitsAndMachineTimeGolden()
    {
        var data = AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "machine-stage11.json")));
        Assert.Contains(data.Warnings, w => w.Code == "W019" && w.Severity == "error");
        Assert.Contains(data.Warnings, w => w.Code == "W020" && w.Severity == "error");
        Assert.Contains(data.Warnings, w => w.Code == "W021" && w.Severity == "error");
        Assert.Contains(data.Limits, l => l.Parameter == "X" && l.ValueMax == 110 && l.LimitMax == 100 && l.Status == "error");
        Assert.NotNull(data.MachineTime); Assert.True(data.MachineTime.Accelerations);
        Assert.Equal(7, data.MachineTime.ToolChangeSec); Assert.Equal(2, data.MachineTime.BSec);
        Assert.Equal(1, data.MachineTime.ProgramStops); Assert.Equal(1, data.MachineTime.OptionalStops);
        Assert.True(data.MachineTime.TotalSec > data.Time.TotalSec);
    }

    [Fact]
    public async Task SelectsFileAndReceivesLimitsFromRealCli()
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory); Assert.NotNull(exe);
        var runner = new AnalyzerRunner(exe);
        string input = Path.Combine(AppContext.BaseDirectory, "data", "machine-stage11.nc");
        string profile = Path.Combine(AppContext.BaseDirectory, "data", "machine-stage11.ini");
        var (run, report) = await runner.AnalyzeToMemoryAsync(input, "auto", null, profile);
        Assert.Equal(0, run.ExitCode); Assert.NotNull(run.Analysis);
        Assert.Equal("Тестовый станок этапа 11", run.Analysis.File.MachineProfile);
        Assert.NotEmpty(run.Analysis.Limits); Assert.Contains("== Пределы станка ==", report);
        var (invalid, _) = await runner.AnalyzeToMemoryAsync(input, "auto", null, profile + ".missing");
        Assert.Equal(1, invalid.ExitCode); Assert.Contains("профиль", invalid.StdErr);
        var list = await runner.RunAsync(["--list-machines"]);
        Assert.Equal(0, list.ExitCode); Assert.Contains("kede-template", list.StdOut);
    }

    [Fact]
    public void LimitsDisplayUsesSharedErrorColorsAndDoesNotChangeReportText() => TrajectoryIntegrationTests.InSta(() =>
    {
        var data = AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "machine-stage11.json")));
        string text = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "machine-stage11.txt"));
        string[] lines = text.Split('\n');
        string[] levels = ReportHighlighter.Levels(text, data.Warnings);
        Assert.All(Enumerable.Range(0, lines.Length).Where(i => lines[i].StartsWith("  [error] ")), i => Assert.Equal("error", levels[i]));
        using var rich = new RichTextBox { Rtf = ReportHighlighter.ToRtf(text, data.Warnings) };
        Assert.Equal(text, rich.Text);
        using var host = new Form { Size = new(1000, 500), ShowInTaskbar = false, Opacity = 0 };
        using var view = new MachineLimitsView { Dock = DockStyle.Fill };
        host.Controls.Add(view); _ = host.Handle; host.Show(); view.CreateControl(); view.ShowChecks(data.Limits);
        host.PerformLayout(); view.PerformLayout();
        var table = Assert.Single(view.Controls.OfType<DataGridView>());
        Assert.Equal(data.Limits.Length, table.RowCount);
        int row = Array.FindIndex(data.Limits, l => l.Status == "error"); Assert.True(row >= 0);
        Assert.Equal("Нарушение", table.Rows[row].Cells[4].FormattedValue);
        using var image = new Bitmap(1000, 500); view.DrawToBitmap(image, new(0, 0, 1000, 500));
        string? cli = ExeLocator.Find(AppContext.BaseDirectory); Assert.NotNull(cli);
        string preview = Path.Combine(Path.GetDirectoryName(cli)!, "stage11-preview"); Directory.CreateDirectory(preview);
        image.Save(Path.Combine(preview, "machine-limits.png"));
        var formatting = new DataGridViewCellFormattingEventArgs(4, row, "Нарушение", typeof(string), new DataGridViewCellStyle());
        typeof(DataGridView).GetMethod("OnCellFormatting", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic,
            null, [typeof(DataGridViewCellFormattingEventArgs)], null)!
            .Invoke(table, [formatting]);
        Assert.Equal(SeverityStyle.Foreground("error"), formatting.CellStyle!.ForeColor);
        Assert.Equal(SeverityStyle.Background("error"), formatting.CellStyle.BackColor);
    });

    [Fact]
    public void LimitTrajectoryOutlineUsesSameRedAsErrors()
    {
        ProjectedEdge[] edges = [new(new(0, 0), new(100, 0), 0, 1, false, false, true)];
        using var renderer = new TrajectoryRenderer(edges);
        var transform = new ViewTransform(); transform.Fit(new(0, 0), new(100, 0), 400, 200);
        using var bitmap = new Bitmap(400, 200);
        using var graphics = Graphics.FromImage(bitmap); graphics.Clear(Color.White);
        renderer.Draw(graphics, transform, new HashSet<int>());
        int red = 0;
        for (int y = 0; y < bitmap.Height; y++) for (int x = 0; x < bitmap.Width; x++)
            if (bitmap.GetPixel(x, y).ToArgb() == SeverityStyle.Foreground("error").ToArgb()) red++;
        Assert.True(red > 100);
    }

    [Fact]
    public void ProjectsMachineMoveUsingWorkingCoordinatesFromCore()
    {
        var s = new Segment { Line = 1, Kind = "rapid", Tool = 1, Plane = "G17", Feed = null, Spindle = null, B = null,
            Start = [10, 20, 30], End = [80, 40, 20], MachineCoordinates = true,
            WorkStart = [0, 0, 0], WorkEnd = [70, 20, -10], LimitViolation = true };
        var scene = TrajectoryScene.Build([s]);
        var xy = Assert.Single(scene.Views[(int)ViewPlane.XY]);
        Assert.Equal(new Point2(0, 0), xy.A); Assert.Equal(new Point2(70, 20), xy.B); Assert.True(xy.LimitViolation);
    }

    [Fact]
    public async Task PortableCliLoadsMachinesBesideExeFromAnotherWorkingDirectory()
    {
        string? cli = ExeLocator.Find(AppContext.BaseDirectory); Assert.NotNull(cli);
        string root = Path.Combine(Path.GetDirectoryName(cli)!, "stage11-portable", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "machines"));
        string isolated = Path.Combine(root, "gcode-analyzer.exe"); File.Copy(cli, isolated);
        await File.WriteAllTextAsync(Path.Combine(root, "machines", "portable-only.ini"), "name=Переносной профиль\n");
        var start = new System.Diagnostics.ProcessStartInfo(isolated) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = System.Text.Encoding.UTF8,
            WorkingDirectory = AppContext.BaseDirectory };
        start.ArgumentList.Add("--list-machines");
        using var process = System.Diagnostics.Process.Start(start); Assert.NotNull(process);
        Task<string> output = process.StandardOutput.ReadToEndAsync(), error = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        Assert.Equal("", await error); Assert.Equal(0, process.ExitCode);
        Assert.Contains("portable-only\tПереносной профиль", await output);
    }
}
