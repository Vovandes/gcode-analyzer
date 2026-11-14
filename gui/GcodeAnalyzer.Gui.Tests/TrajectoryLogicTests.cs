using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class ProjectionTests
{
    [Fact]
    public void ProjectsKnownPointsAndRejectsOnlyUnknownVisibleAxes()
    {
        Assert.Equal(new Point2(2, 3), Projection.Project(new(2, 3, 5), ViewPlane.XY));
        Assert.Equal(new Point2(2, 5), Projection.Project(new(2, 3, 5), ViewPlane.XZ));
        Assert.Equal(new Point2(3, 5), Projection.Project(new(2, 3, 5), ViewPlane.YZ));
        Assert.Equal(new Point2(2, 3), Projection.Project(new(2, 3, null), ViewPlane.XY));
        Assert.Null(Projection.Project(new(2, 3, null), ViewPlane.XZ));
        Assert.Null(Projection.Project(new(double.NaN, 3, 5), ViewPlane.XY));
    }
}

public class ViewTransformTests
{
    [Fact]
    public void ZoomKeepsPointUnderCursorAndPanUsesPixels()
    {
        var t = new ViewTransform();
        t.Fit(new(-10, -20), new(30, 40), 1000, 700);
        Point2 cursor = new(175, 231), before = t.ToWorld(cursor);
        t.Zoom(cursor, 2.5);
        Assert.Equal(before.X, t.ToWorld(cursor).X, 10);
        Assert.Equal(before.Y, t.ToWorld(cursor).Y, 10);
        Point2 screen = t.ToScreen(before);
        t.Pan(10, -15);
        Assert.Equal(screen.X + 10, t.ToScreen(before).X, 10);
        Assert.Equal(screen.Y - 15, t.ToScreen(before).Y, 10);
    }

    [Fact]
    public void FitCentersBoundsAndHandlesZeroSize()
    {
        var t = new ViewTransform();
        t.Fit(new(0, 0), new(10, 20), 300, 300, 50);
        // Высота 200 px / 20 мм = 10 px/мм; середина (5,10) -> (150,150).
        Assert.Equal(10, t.Scale);
        Assert.Equal(new Point2(150, 150), t.ToScreen(new(5, 10)));
        t.Fit(new(2, 2), new(2, 2), 0, 0);
        Assert.True(double.IsFinite(t.Scale) && t.Scale > 0);
        t.Zoom(new(0, 0), 0);
        Assert.True(t.Scale > 0);
    }
}

public class ArcTessellatorTests
{
    internal static Segment Move(string kind = "linear", int line = 1, int tool = 1,
        double?[]? start = null, double?[]? end = null, string plane = "G17", bool cw = false) => new()
    {
        Kind = kind, Line = line, Tool = tool, Plane = plane,
        Start = start ?? [10, 0, 0], End = end ?? [0, 10, 0],
        Center = [0, 0, 0], Radius = 10, Cw = cw, Feed = 100, Spindle = 1000, B = null,
    };

    [Fact]
    public void QuarterCircleR10HasKnownEndpointsMidpointAndChordError()
    {
        var p = ArcTessellator.Tessellate(Move("arc"));
        Assert.Equal(new Point3(10, 0, 0), p[0]);
        Assert.Equal(new Point3(0, 10, 0), p[^1]);
        // 45 градусов: обе координаты 10/sqrt(2); 90 градусов / 5 = 18 отрезков.
        Assert.Equal(19, p.Length);
        Assert.Equal(10 / Math.Sqrt(2), p[9].X!.Value, 10);
        Assert.Equal(10 / Math.Sqrt(2), p[9].Y!.Value, 10);
        for (int i = 1; i < p.Length; i++)
        {
            double mx = (p[i - 1].X!.Value + p[i].X!.Value) / 2;
            double my = (p[i - 1].Y!.Value + p[i].Y!.Value) / 2;
            Assert.InRange(10 - Math.Sqrt(mx * mx + my * my), 0, .05);
        }
    }

    [Theory]
    [InlineData("G17")]
    [InlineData("G18")]
    [InlineData("G19")]
    public void FullCircleHelixPreservesPlaneOrientationAndThirdAxis(string plane)
    {
        var (u, v, w) = plane switch { "G18" => (2, 0, 1), "G19" => (1, 2, 0), _ => (0, 1, 2) };
        double?[] start = [0, 0, 0], end = [0, 0, 0];
        start[u] = end[u] = 10;
        end[w] = 8;
        var points = ArcTessellator.Tessellate(Move("arc", start: start, end: end, plane: plane, cw: true));
        Assert.Equal(Point3.From(start), points[0]);
        Assert.Equal(Point3.From(end), points[^1]);
        Assert.True(points[1].Axis(v) < 0);
        Assert.Equal(4, points[(points.Length - 1) / 2].Axis(w)!.Value, 10);
        Assert.Contains(points, p => p.Axis(u) < -9.9);
    }

    [Fact]
    public void PerpendicularViewIsLineAndUnknownThirdAxisIsPreserved()
    {
        var points = ArcTessellator.Tessellate(Move("arc"));
        Assert.All(points, p => Assert.Equal(0, Projection.Project(p, ViewPlane.XZ)!.Value.Y));
        var unknown = ArcTessellator.Tessellate(Move("arc", start: [10, 0, null], end: [0, 10, null]));
        Assert.All(unknown, p => Assert.Null(p.Z));
    }
}

public class HitTesterTests
{
    [Fact]
    public void NearestUsesFivePixelsZoomAndVisibilityIncludingZeroLength()
    {
        ProjectedEdge[] edges = [new(new(0, 0), new(100, 0), 7, 1, false, false),
            new(new(0, 3), new(100, 3), 8, 2, false, false), new(new(200, 0), new(200, 0), 9, 3, false, true)];
        var h = new HitTester(edges);
        var t = new ViewTransform();
        Assert.Equal(7, h.Find(new(50, 5), t));
        Assert.Equal(-1, h.Find(new(50, 5.01), t));
        Assert.Equal(8, h.Find(new(50, -3), t));
        Assert.Equal(7, h.Find(new(50, -3), t, hiddenTools: new HashSet<int> { 2 }));
        Assert.Equal(9, h.Find(new(200, 0), t));
        t.Zoom(new(0, 0), 10);
        Assert.Equal(-1, h.Find(new(500, 6), t));
    }

    [Fact]
    public void GridFindsBoundaryAndLongEdgesWithoutScanningAllSegments()
    {
        var edges = Enumerable.Range(0, 10000).Select(i =>
            new ProjectedEdge(new(i % 100 * 10, i / 100 * 10), new(i % 100 * 10 + 2, i / 100 * 10), i, 1, false, false)).ToArray();
        var h = new HitTester(edges);
        Assert.Equal(5050, h.Find(new(501, -500), new ViewTransform(), .5));
        Assert.InRange(h.LastCandidateCount, 1, 20);
        var longEdges = edges.Append(new(new(-10000, -10000), new(10000, 10000), 10001, 1, false, false)).ToArray();
        Assert.Equal(10001, new HitTester(longEdges).Find(new(-200, 200), new ViewTransform(), .01));
        Assert.Equal(-1, new HitTester([]).Find(new(0, 0), new ViewTransform()));
    }
}

public class ToolPaletteTests
{
    [Fact]
    public void ColorsAreStableAndFirstSixteenDistinct()
    {
        Assert.Equal(16, Enumerable.Range(0, 16).Select(ToolPalette.ForTool).Distinct().Count());
        Assert.Equal(ToolPalette.ForTool(6), ToolPalette.ForTool(6));
        Assert.Equal(ToolPalette.ForTool(6), ToolPalette.ForTool(22));
        Assert.Equal(ToolPalette.ForTool(-1), ToolPalette.ForTool(-1));
    }
}

public class LineClassifierTests
{
    [Fact]
    public void UsesJsonForModalMovesAndHighlightsInlineCommentsAndHighestSeverity()
    {
        string[] lines = ["G0 X0", "X10 (test) ; tail", "G1 X15", "X20", "(only)", "S2000"];
        Segment[] segments = [ArcTessellatorTests.Move("rapid", 1), ArcTessellatorTests.Move("rapid", 2),
            ArcTessellatorTests.Move("linear", 3), ArcTessellatorTests.Move("linear", 4)];
        WarningItem[] warnings = [new() { Line = 2, Code = "W012", Severity = "warning", Message = "w", Text = "" },
            new() { Line = 2, Code = "W004", Severity = "error", Message = "e", Text = "" }];
        var result = LineClassifier.Classify(lines, segments, warnings);
        Assert.Equal(LineKind.Rapid, result[1].Kind);
        Assert.Equal(LineKind.Work, result[3].Kind);
        Assert.Equal(LineKind.Other, result[5].Kind);
        Assert.Equal("error", result[1].Severity);
        Assert.Equal(2, result[1].Spans.Count(s => s.Comment));
        Assert.True(Assert.Single(result[4].Spans).Comment);
    }
}

public class TrajectorySceneTests
{
    [Fact]
    public void CyclesProjectToDrillPointAndDepthAndHomeIsNotInvented()
    {
        var cycle = new Segment { Kind = "cycle", Line = 3, Tool = 1, Plane = "G17", Start = [2, 3, 10],
            End = [2, 3, 10], Feed = 100, Spindle = 1000, B = null,
            Cycle = new() { Code = "G81", ZR = 2, ZBottom = -5, ZInit = 10, ReturnToInitial = true } };
        var scene = TrajectoryScene.Build([ArcTessellatorTests.Move("home"), cycle]);
        var xy = Assert.Single(scene.Views[(int)ViewPlane.XY]);
        Assert.Equal(new Point2(2, 3), xy.A);
        Assert.Equal(xy.A, xy.B);
        var yz = Assert.Single(scene.Views[(int)ViewPlane.YZ]);
        Assert.Equal(new Point2(3, 2), yz.A);
        Assert.Equal(new Point2(3, -5), yz.B);
        Assert.Equal(1, scene.SegmentByLine[3]);
    }
}

public class ReportHighlighterTests
{
    [Fact]
    public void HighlightsWarningsAndFrameTextOnlyWithinWarningSection()
    {
        var data = AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "axis-b-index.json")));
        string text = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "axis-b-index.txt"));
        var levels = ReportHighlighter.Levels(text, data.Warnings);
        Assert.Equal(4, levels.Count(l => l == "warning"));
        string rtf = ReportHighlighter.ToRtf(text, data.Warnings);
        Assert.StartsWith(@"{\rtf1", rtf);
        Assert.Contains(@"\cf3", rtf);
        Assert.Contains(@"\u", rtf);
    }
}
