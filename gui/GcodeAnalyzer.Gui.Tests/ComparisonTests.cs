using System.Reflection;
using System.Text.Json;
using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class ComparisonTests
{
    private static string Fixture(string name) => Path.Combine(AppContext.BaseDirectory, "data", $"stage13-{name}.nc");

    private static async Task<(string Report, ComparisonData Data)> CompareAsync(string before, string after)
    {
        string? cli = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(cli);
        var result = await new AnalyzerRunner(cli).CompareAsync(Fixture(before), Fixture(after), "auto", null);
        Assert.True(result.Run.ExitCode == 0, result.Run.StdErr);
        return (result.Report, Assert.IsType<ComparisonData>(result.Comparison));
    }

    [Fact]
    public async Task ParsesComparisonJson()
    {
        var (report, data) = await CompareAsync("base", "shifted");
        Assert.Equal(1, data.Schema);
        Assert.Equal(0.01, data.Tolerance);
        Assert.Equal("stage13-base.nc", data.Before.File.Name);
        Assert.Equal("stage13-shifted.nc", data.After.File.Name);
        Assert.Equal(20, data.Before.Segments.Single(s => s.Line == 7).End[0]);
        Assert.Equal(20.1, data.After.Segments.Single(s => s.Line == 8).End[0]);
        var tool = Assert.Single(data.Trajectory);
        Assert.Equal(1, tool.DifferenceCount);
        Assert.Equal(new("modified", 7, 8), Assert.Single(tool.Differences));
        Assert.Contains(data.Summary, row => row.Label == "Кадры" && row.Difference == "0");
        Assert.Contains("изменена | 7 | 8", report);
    }

    [Fact]
    public void RejectsComparisonJsonWithInvalidLinePair()
    {
        const string json = """
            {
              "schema":1,"kind":"comparison","tolerance":0.01,
              "before":{"file":{"name":"a.nc","lines":4,"dialect":"fanuc-mill","machine":""},"segments":[],"warnings":[]},
              "after":{"file":{"name":"b.nc","lines":4,"dialect":"fanuc-mill","machine":""},"segments":[],"warnings":[]},
              "trajectory":[{"tool":"T1","presence":"both","differenceCount":1,
                "differences":[{"kind":"modified","oldLine":2,"newLine":null}]}],
              "summary":[],"format":[],"newWarnings":[],"resolvedWarnings":[]
            }
            """;
        Assert.Throws<JsonException>(() => ComparisonJson.Parse(json));
    }

    [Fact]
    public void BuildsTrajectoryOverlay()
    {
        Segment[] before = [ArcTessellatorTests.Move(line: 7, start: [0, 0, 0], end: [10, 0, 0])];
        Segment[] after = [ArcTessellatorTests.Move(line: 7, start: [0, 5, 0], end: [10, 5, 0]),
            ArcTessellatorTests.Move(line: 8, start: [10, 5, 0], end: [15, 5, 0])];
        var overlay = TrajectoryOverlay.Build(before, after);
        var edges = overlay.Scene.Views[(int)ViewPlane.XY];
        Assert.Equal(1, overlay.Scene.PreviousSegmentCount);
        Assert.Equal(3, edges.Length);
        Assert.Equal(new Point2(10, 0), edges[0].B);
        Assert.Equal(new Point2(15, 5), edges[2].B);
        Assert.Equal((0, 1), overlay.ForLines(7, 7));
        Assert.Equal((-1, 2), overlay.ForLines(8, 8));
        Assert.Equal((7, (int?)null), overlay.ForSegment(0));
        Assert.Equal(((int?)null, 7), overlay.ForSegment(1));

        var transform = new ViewTransform();
        transform.Fit(new(0, 0), new(15, 5), 200, 100, 10);
        using var bitmap = new Bitmap(200, 100);
        using var graphics = Graphics.FromImage(bitmap);
        using var renderer = new TrajectoryRenderer(edges, overlay.Scene.PreviousSegmentCount);
        renderer.Draw(graphics, transform, new HashSet<int>());
        var oldPixel = transform.ToScreen(new(5, 0));
        var newPixel = transform.ToScreen(new(5, 5));
        Assert.Equal(TrajectoryRenderer.PreviousColor.ToArgb(), bitmap.GetPixel((int)oldPixel.X, (int)oldPixel.Y).ToArgb());
        Assert.Equal(ToolPalette.ForTool(after[0].Tool).ToArgb(), bitmap.GetPixel((int)newPixel.X, (int)newPixel.Y).ToArgb());
    }

    [Fact]
    public async Task SelectsBothSourceLines()
    {
        var (report, data) = await CompareAsync("shifted", "inserted");
        var before = LineClassifier.Classify(File.ReadAllLines(Fixture("shifted")), data.Before.Segments, data.Before.Warnings);
        var after = LineClassifier.Classify(File.ReadAllLines(Fixture("inserted")), data.After.Segments, data.After.Warnings);
        var overlay = TrajectoryOverlay.Build(data.Before.Segments, data.After.Segments);
        TrajectoryIntegrationTests.InSta(() =>
        {
            using var form = new Form { Size = new(1100, 800) };
            using var workspace = new ComparisonWorkspace { Dock = DockStyle.Fill };
            form.Controls.Add(workspace);
            _ = form.Handle;
            workspace.CreateControl();
            workspace.SetComparison(data, report, overlay, before, after);
            var grid = (DataGridView)Assert.Single(workspace.Controls.Find("Расхождения траектории", true));
            var oldProgram = (ProgramView)Assert.Single(workspace.Controls.Find("beforeProgram", true));
            var newProgram = (ProgramView)Assert.Single(workspace.Controls.Find("afterProgram", true));
            var click = typeof(DataGridView).GetMethod("OnCellClick", BindingFlags.NonPublic | BindingFlags.Instance)!;
            click.Invoke(grid, [new DataGridViewCellEventArgs(1, 1)]);
            Assert.Equal(8, oldProgram.SelectedLine);
            Assert.Equal(7, newProgram.SelectedLine);
            click.Invoke(grid, [new DataGridViewCellEventArgs(1, 2)]);
            Assert.Equal(0, oldProgram.SelectedLine);
            Assert.Equal(8, newProgram.SelectedLine);
        });
    }
}
