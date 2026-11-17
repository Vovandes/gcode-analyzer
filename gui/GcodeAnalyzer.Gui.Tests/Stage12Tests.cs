using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class ToolDetailsViewTests
{
    private static AnalysisData Golden(string name) =>
        AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", name + ".json")));

    [Fact]
    public void ReadsDetailedMeasurementsAndTotalFromCliJson()
    {
        var rows = ToolDetailsTable.Rows(Golden("known-time"));
        Assert.Equal(2, rows.Length);
        Assert.Equal("T1", rows[0].Label);
        Assert.Null(rows[0].Tool!.DiameterMm);
        Assert.Equal(115.708, rows[0].Metrics.WorkingMm, 3);
        Assert.Equal(150, rows[0].Metrics.IdleMm);
        Assert.Equal(600, rows[0].Metrics.FeedMmPerMin!.Min);
        Assert.Equal(210, rows[0].Axis(0)!.Max);
        Assert.Equal("ИТОГО", rows[1].Label);
        Assert.True(rows[1].IsTotal);
        Assert.Equal(rows[0].Metrics.MotionTime.TotalSec, rows[1].Metrics.MotionTime.TotalSec);
    }

    [Fact]
    public void SortsNumbersAndPreservesTotalInBothDirections()
    {
        static ToolDetailsRow Row(int tool, double? diameter) =>
            new(new() { Tool = tool, DiameterMm = diameter, Time = new() }, new() { MotionTime = new() }, []);
        var total = new ToolDetailsRow(null, new() { MotionTime = new() }, []);
        ToolDetailsRow[] rows = [Row(10, 20), Row(2, 3), Row(4, null), total];
        Assert.Equal(new[] { "T2", "T4", "T10", "ИТОГО" }, ToolDetailsTable.Sort(rows, 0, false).Select(r => r.Label));
        Assert.Equal(new[] { "T10", "T2", "T4", "ИТОГО" }, ToolDetailsTable.Sort(rows, 1, true).Select(r => r.Label));
        Assert.Equal(new[] { "T2", "T10", "T4", "ИТОГО" }, ToolDetailsTable.Sort(rows, 1, false).Select(r => r.Label));
    }

    [Fact]
    public void FormRestoresToolsTabAndGridRendersColorAndBoldTotal() => TrajectoryIntegrationTests.InSta(() =>
    {
        using var form = new MainForm(new() { ActiveTab = "tools" });
        var tabs = Assert.Single(form.Controls.OfType<TabControl>());
        Assert.Equal("Инструменты", tabs.SelectedTab?.Text);
        var view = Assert.IsType<ToolDetailsView>(Assert.Single(tabs.SelectedTab!.Controls.Cast<Control>()));
        view.ShowTools(Golden("axis-b-index"));
        var grid = Assert.Single(view.Controls.OfType<DataGridView>());
        Assert.Equal(3, grid.Rows.Count);
        var styles = new Dictionary<int, DataGridViewCellStyle>();
        grid.CellFormatting += (_, e) =>
        {
            if (e.ColumnIndex == 0 && e.CellStyle is not null) styles[e.RowIndex] = e.CellStyle.Clone();
        };
        Assert.Equal("● T1", grid.Rows[0].Cells[0].FormattedValue);
        Assert.Equal("ИТОГО", grid.Rows[2].Cells[0].FormattedValue);
        Assert.Equal(ToolPalette.ForTool(1), styles[0].ForeColor);
        Assert.True(styles[2].Font?.Bold);
        Assert.All(grid.Columns.Cast<DataGridViewColumn>(), column => Assert.Equal(DataGridViewColumnSortMode.Programmatic, column.SortMode));
    });
}

public class ReportPresentationTests
{
    [Fact]
    public void OrdersBlocksAndShowsGreenNoneForEmptySections()
    {
        var analysis = Parse("known-time");
        var report = ReportPresentation.Build("", analysis);
        Assert.Equal(new[] { "Сводка", "Ошибки", "Предупреждения", "Пределы станка", "Время обработки (оценка)", "Инструменты", "Инструменты: подробно" },
            report.Blocks.Select(b => b.Title));
        foreach (var block in report.Blocks.Skip(1))
        {
            var span = Assert.Single(Assert.Single(block.Lines).Spans);
            Assert.Equal("нет", span.Text.Trim());
            Assert.Equal("ok", span.Severity);
            Assert.Equal(Color.DarkGreen, SeverityStyle.Foreground(span.Severity));
        }
        Assert.All(report.HeadingLines, i => Assert.True(report.Lines[i].Spans[0].Style.HasFlag(ReportTextStyle.Bold)));
    }

    [Fact]
    public void StylesCodesAndSourceAndKeepsProgramLineReferences()
    {
        var analysis = Parse("machine-stage11");
        var report = ReportPresentation.Build("", analysis);
        var block = Assert.Single(report.Blocks, b => b.Title == "Ошибки");
        var w = Assert.Single(analysis.Warnings, w => w.Code == "W019");
        var matching = block.Lines.Where(l => l.ProgramLine == w.Line).ToArray();
        var row = Assert.Single(matching, l => l.Spans.Any(s => s.Text == "[W019]"));
        Assert.All(row.Spans, span => Assert.Equal("error", span.Severity));
        Assert.True(row.Spans[0].Style.HasFlag(ReportTextStyle.Bold));
        Assert.Equal(ReportTextStyle.Bold | ReportTextStyle.Code, row.Spans[1].Style);
        Assert.Equal(ReportTextStyle.Code, row.Spans[2].Style);
        Assert.Equal(ReportTextStyle.Italic, row.Spans[3].Style);
        Assert.Contains(matching, l => l.Spans.Length == 1 && l.Spans[0].Text.Trim() == w.Text);
    }

    [Fact]
    public void RichTextKeepsCopyableTextAndBlockPositions() => TrajectoryIntegrationTests.InSta(() =>
    {
        var report = ReportPresentation.Build("== Инструменты: подробно ==\n  ИТОГО | 1,2\n", Parse("axis-b-index"));
        using var box = new RichTextBox { WordWrap = false, Rtf = ReportRtf.Render(report.Lines) };
        string[] lines = box.Text.Split('\n');
        foreach (var (heading, block) in report.HeadingLines.Zip(report.Blocks))
            Assert.Equal(block.Title, lines[heading]);
        Assert.Contains("ИТОГО | 1,2", box.Text);
        Assert.Contains("\\i", box.Rtf);
    });

    private static AnalysisData Parse(string golden) =>
        AnalysisJson.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "golden", golden + ".json")));
}
