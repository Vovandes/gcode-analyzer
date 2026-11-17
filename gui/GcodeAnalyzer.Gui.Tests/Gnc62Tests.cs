using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class Gnc62Tests
{
    [Fact]
    public async Task KedeProfileSelectionUsesCliListAndReceivesWarnings()
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(exe);
        var runner = new AnalyzerRunner(exe);
        var listed = await runner.RunAsync(["--list-machines"]);
        Assert.Equal(0, listed.ExitCode);
        Assert.Contains("kede-uni800ut\tKEDE UNI 800 UT", listed.StdOut);
        var (run, report) = await runner.AnalyzeToMemoryAsync(
            Path.Combine(AppContext.BaseDirectory, "data", "gnc62-synthetic.nc"), "kede-uni800ut", null);
        Assert.Equal(0, run.ExitCode);
        Assert.NotNull(run.Analysis);
        Assert.Equal("gnc62", run.Analysis.File.Dialect);
        Assert.Equal("KEDE UNI 800 UT", run.Analysis.File.MachineProfile);
        Assert.Contains(run.Analysis.Tools, t => t.Name == "SyntheticProbe");
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W023" && w.Severity == "error");
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W025" && w.Severity == "warning");
        Assert.Contains(run.Analysis.Warnings, w => w.Code == "W026" && w.Severity == "info");
        Assert.Contains(run.Analysis.Limits, l => l.Parameter == "A" && l.Status == "error");
        Assert.Contains("Synthetic measurement", report);
        var rotation = Assert.Single(run.Analysis.Segments, s => s.A == 133);
        Assert.True(rotation.LimitViolation);
    }

    [Fact]
    public async Task MeasuringWarningDisplayUsesSharedSeverityColors()
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(exe);
        var (run, report) = await new AnalyzerRunner(exe).AnalyzeToMemoryAsync(
            Path.Combine(AppContext.BaseDirectory, "data", "gnc62-synthetic.nc"), "auto", null);
        Assert.NotNull(run.Analysis);
        var levels = ReportHighlighter.Levels(report, run.Analysis.Warnings);
        string[] lines = report.Split('\n');
        foreach (var (code, severity) in new[] { ("W023", "error"), ("W025", "warning"), ("W026", "info") })
        {
            int index = Array.FindIndex(lines, line => line.Contains("[" + code + "]"));
            Assert.True(index >= 0);
            Assert.Equal(severity, levels[index]);
            Assert.NotEqual(Color.Empty, SeverityStyle.Foreground(severity));
        }
    }

    [Fact]
    public void EmptyMachineLimitsHideTableHeaders() => TrajectoryIntegrationTests.InSta(() =>
    {
        using var host = new Form { ShowInTaskbar = false, Opacity = 0 };
        using var view = new MachineLimitsView { Dock = DockStyle.Fill };
        host.Controls.Add(view); host.Show();
        var table = Assert.Single(view.Controls.OfType<DataGridView>());
        view.ShowChecks([]);
        Assert.False(table.Visible);
        view.ShowChecks([new LimitCheck { Line = 1, Parameter = "A", ValueMin = 0, ValueMax = 133,
            LimitMin = -132, LimitMax = 132, Status = "error" }]);
        Assert.True(table.Visible);
    });

    [Fact]
    public void Gnc62CommentsDoNotColorSpindleSelectorsOrToolNames()
    {
        const string text = "T\"ЩУП@(2)\" S(2)300 M3S(2) @( comment )";
        var spans = LineClassifier.SplitComments(text, true);
        var comment = Assert.Single(spans, s => s.Comment);
        Assert.Equal("@( comment )", text.Substring(comment.Start, comment.Length));
        Assert.DoesNotContain(LineClassifier.SplitComments("S(2)300 M3S(2)", true), s => s.Comment);
    }
}
