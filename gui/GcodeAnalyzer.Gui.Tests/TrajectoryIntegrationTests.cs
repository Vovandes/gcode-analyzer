using System.Reflection;
using GcodeAnalyzer.Gui;
using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class TrajectoryIntegrationTests
{
    [Theory]
    [InlineData("sample-01-skymaster-vl1100a.nc", 4766, "Y-12.41")]
    [InlineData("sample-02-bridgeport-1300-480.nc", 3440, "X-83.362 Y76.738 Z9.89 I-1.333 J-0.111")]
    public async Task ClassifiesBothSamplesUsingRealCliAndBuildsAllViews(string file, int segments, string modalWork)
    {
        string? exe = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(exe);
        string path = Path.Combine(AppContext.BaseDirectory, "samples", file);
        var (run, _) = await new AnalyzerRunner(exe).AnalyzeToMemoryAsync(path, "auto", null);
        Assert.Equal(0, run.ExitCode);
        Assert.NotNull(run.Analysis);
        Assert.Equal(segments, run.Analysis.Segments.Length);
        var lines = LineClassifier.Classify(await File.ReadAllLinesAsync(path), run.Analysis.Segments, run.Analysis.Warnings);
        Assert.Contains(lines, l => l.Text == modalWork && l.Kind == LineKind.Work);
        Assert.Contains(lines, l => l.Text == "Z13.5" && l.Kind == LineKind.Rapid || l.Text == "Z11.95" && l.Kind == LineKind.Rapid);
        Assert.All(lines.Where(l => l.Text.StartsWith('(')), l => Assert.True(Assert.Single(l.Spans).Comment));
        foreach (Segment s in run.Analysis.Segments)
            if (s.Kind is "linear" or "arc" or "cycle") Assert.Equal(LineKind.Work, lines[s.Line - 1].Kind);
        var scene = TrajectoryScene.Build(run.Analysis.Segments);
        Assert.All(scene.Views, view => Assert.NotEmpty(view));
        Assert.All(scene.Views, view => Assert.All(view, e => Assert.InRange(e.SegmentIndex, 0, segments - 1)));
    }

    internal static void InSta(Action action)
    {
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try { Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException); action(); }
            catch (Exception e) { failure = e; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start(); thread.Join();
        if (failure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }

    private static void Invoke(Control control, string method, params object[] arguments) =>
        control.GetType().GetMethod(method, BindingFlags.NonPublic | BindingFlags.Instance)!.Invoke(control, arguments);

    private static IEnumerable<Control> Descendants(Control root)
    {
        foreach (Control child in root.Controls)
        { yield return child; foreach (var item in Descendants(child)) yield return item; }
    }

    [Fact]
    public void VirtualProgramRendersOnlyVisibleLinesAndSelectsLastFrame() => InSta(() =>
    {
        var lines = LineClassifier.Classify(Enumerable.Range(1, 100100).Select(i => $"X{i} (frame)").ToArray(), [], []);
        using var view = new ProgramView { Size = new(600, 400) };
        view.CreateControl(); view.SetLines(lines);
        view.SelectLine(100100);
        using var bitmap = new Bitmap(600, 400);
        view.DrawToBitmap(bitmap, new(0, 0, 600, 400));
        Assert.Equal(100100, view.SelectedLine);
        Assert.InRange(view.LastPaintedLines, 1, 30);
        Assert.InRange(view.TopLine, 100070, 100100);
    });

    [Fact]
    public void TrajectoryMouseZoomClickPanAndDoubleClickFit() => InSta(() =>
    {
        var scene = TrajectoryScene.Build([ArcTessellatorTests.Move(start: [0, 0, 0], end: [100, 0, 0])]);
        using var view = new TrajectoryView { Size = new(600, 400) };
        view.CreateControl(); view.SetScene(scene, ViewPlane.XY);
        double fitScale = view.Transform.Scale;
        Point2 before = view.Transform.ToWorld(new(100, 150));
        Invoke(view, "OnMouseWheel", new MouseEventArgs(MouseButtons.None, 0, 100, 150, 120));
        Assert.Equal(before.X, view.Transform.ToWorld(new(100, 150)).X, 10);
        Assert.Equal(before.Y, view.Transform.ToWorld(new(100, 150)).Y, 10);
        int selected = -1;
        view.SegmentSelected += i => selected = i;
        Point2 p = view.Transform.ToScreen(new(50, 0));
        Invoke(view, "OnMouseDown", new MouseEventArgs(MouseButtons.Left, 1, (int)p.X, (int)p.Y, 0));
        Invoke(view, "OnMouseUp", new MouseEventArgs(MouseButtons.Left, 1, (int)p.X, (int)p.Y, 0));
        Assert.Equal(0, selected);
        double x = view.Transform.OffsetX;
        Invoke(view, "OnMouseDown", new MouseEventArgs(MouseButtons.Middle, 1, 100, 100, 0));
        Invoke(view, "OnMouseMove", new MouseEventArgs(MouseButtons.Middle, 0, 120, 110, 0));
        Invoke(view, "OnMouseUp", new MouseEventArgs(MouseButtons.Middle, 1, 120, 110, 0));
        Assert.Equal(x + 20, view.Transform.OffsetX, 10);
        Invoke(view, "OnMouseDoubleClick", new MouseEventArgs(MouseButtons.Left, 2, 120, 110, 0));
        Assert.Equal(fitScale, view.Transform.Scale, 10);
    });

    [Fact]
    public void WarningClickAndEnterNavigateToFrameAndSegmentOrExplainMissingGeometry() => InSta(() =>
    {
        var settings = new AppSettings { ViewMode = 3 };
        using var host = new Form { Size = new(1100, 800), ShowInTaskbar = false, Opacity = 0 };
        using var workspace = new TrajectoryWorkspace(settings) { Size = new(1000, 700) };
        host.Controls.Add(workspace);
        _ = host.Handle;
        workspace.CreateControl();
        Segment[] segments = [ArcTessellatorTests.Move(line: 2)];
        WarningItem[] warnings = [new() { Line = 2, Code = "W004", Severity = "error", Message = "test", Text = "X0" },
            new() { Line = 1, Code = "W012", Severity = "warning", Message = "test", Text = "G123" }];
        workspace.SetAnalysis(TrajectoryScene.Build(segments), LineClassifier.Classify(["G123", "X0"], segments, warnings), warnings);
        string status = ""; workspace.StatusChanged += s => status = s;
        var table = Descendants(workspace).OfType<DataGridView>().Single();
        Invoke(table, "OnCellClick", new DataGridViewCellEventArgs(0, 0));
        Assert.Equal(2, Descendants(workspace).OfType<ProgramView>().Single().SelectedLine);
        Assert.Contains("T1", status);
        Assert.Contains("F 100", status);
        table.CurrentCell = table[0, 1];
        Invoke(table, "OnKeyDown", new KeyEventArgs(Keys.Enter));
        Assert.Equal(1, Descendants(workspace).OfType<ProgramView>().Single().SelectedLine);
        Assert.Contains("нет сегмента", status);
        workspace.SaveLayout();
        Assert.Equal(3, settings.ViewMode);
    });

    [Fact]
    public void ReportRtfPreservesTextAndSectionOffsets() => InSta(() =>
    {
        string text = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "axis-b-index.txt"));
        var data = AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", "axis-b-index.json")));
        using var report = new RichTextBox { Rtf = ReportHighlighter.ToRtf(text, data.Warnings) };
        Assert.Equal(text.Replace("\r\n", "\n"), report.Text);
        foreach (var section in ReportParser.ParseSections(report.Text))
            Assert.StartsWith("== " + section.Title, report.Text[section.Offset..]);
        int offset = report.Text.IndexOf("[W012]", StringComparison.Ordinal);
        report.Select(offset, 1);
        Assert.Equal(SeverityStyle.Foreground("warning").ToArgb(), report.SelectionColor.ToArgb());
    });

    [Fact]
    public void MainFormAndLayoutRenderWithoutReadingUserSettings() => InSta(() =>
    {
        using var form = new MainForm(new AppSettings { ReportFirst = true, ActiveTab = "report", ProgramBelow = true, ViewMode = 2 });
        form.CreateControl();
        var tabs = Descendants(form).OfType<TabControl>().Single();
        Assert.Equal("report", tabs.TabPages[0].Name);
        Assert.Equal("report", tabs.SelectedTab!.Name);
        using var bitmap = new Bitmap(form.Width, form.Height);
        form.DrawToBitmap(bitmap, new(0, 0, form.Width, form.Height));
    });

    [Fact]
    public void LayoutSettingsRoundTripAndOldSettingsKeepDefaults()
    {
        string root = FindRepository();
        string path = Path.Combine(root, "build", "stage10-tests", $"settings-{Guid.NewGuid():N}.json");
        new AppSettings { ViewMode = 3, ReportFirst = true, ActiveTab = "report", ProgramBelow = true,
            ProgramSplitRatio = .7, WarningsSplitRatio = .8, ViewsSplitRatio = .4, ReportSplitRatio = .3 }.Save(path);
        var restored = AppSettings.Load(path);
        Assert.Equal(3, restored.ViewMode); Assert.True(restored.ReportFirst); Assert.True(restored.ProgramBelow);
        Assert.Equal("report", restored.ActiveTab); Assert.Equal(.7, restored.ProgramSplitRatio);
        Assert.Equal(.8, restored.WarningsSplitRatio); Assert.Equal(.4, restored.ViewsSplitRatio);
        Assert.Equal(.3, restored.ReportSplitRatio);
        string old = Path.Combine(Path.GetDirectoryName(path)!, $"old-{Guid.NewGuid():N}.json");
        File.WriteAllText(old, "{\"ExePath\":\"analyzer.exe\"}");
        var defaults = AppSettings.Load(old);
        Assert.Equal("analyzer.exe", defaults.ExePath); Assert.Equal(0, defaults.ViewMode);
        Assert.Equal("trajectory", defaults.ActiveTab); Assert.Equal(.6, defaults.ProgramSplitRatio);
    }

    private static string FindRepository()
    {
        for (DirectoryInfo? dir = new(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "VERSION"))) return dir.FullName;
        throw new DirectoryNotFoundException("Repository root not found");
    }
}
