namespace GcodeAnalyzer.Gui.Logic;

public sealed record ToolDetailsRow(ToolInfo? Tool, ToolMetrics Metrics, RangeInfo?[] Box)
{
    public bool IsTotal => Tool is null;
    public string Label => Tool is null ? "ИТОГО" : Tool.Name is { Length: > 0 } ? $"T{Tool.Tool} ({Tool.Name})" : $"T{Tool.Tool}";
    public RangeInfo? Axis(int index) => index < Box.Length ? Box[index] : null;
}

public sealed record ToolDetailsColumn(string Title, Func<ToolDetailsRow, object?> Value, string? Format = "0.###",
    Func<ToolDetailsRow, object?>? SortKey = null);

public static class ToolDetailsTable
{
    public static IReadOnlyList<ToolDetailsColumn> Columns { get; } =
    [
        new("T", r => r.Label, null, r => r.Tool?.Tool),
        new("⌀, мм", r => r.Tool?.DiameterMm),
        new("H", r => Offsets(r.Tool?.HValues), null, r => r.Tool?.HValues.FirstOrDefault()),
        new("D", r => Offsets(r.Tool?.DValues), null, r => r.Tool?.DValues.FirstOrDefault()),
        new("Рабочие, с", r => r.Metrics.MotionTime.FeedSec),
        new("G0, с", r => r.Metrics.MotionTime.RapidSec),
        new("Циклы, с", r => r.Metrics.MotionTime.CycleSec),
        new("Выдержки, с", r => r.Metrics.MotionTime.DwellSec),
        new("Всего, с", r => r.Metrics.MotionTime.TotalSec),
        new("Рабочие, мм", r => r.Metrics.WorkingMm),
        new("Холостые, мм", r => r.Metrics.IdleMm),
        new("F мин, мм/мин", r => r.Metrics.FeedMmPerMin?.Min),
        new("F макс, мм/мин", r => r.Metrics.FeedMmPerMin?.Max),
        new("S мин, об/мин", r => r.Metrics.Rpm?.Min),
        new("S макс, об/мин", r => r.Metrics.Rpm?.Max),
        new("X мин, мм", r => r.Axis(0)?.Min),
        new("X макс, мм", r => r.Axis(0)?.Max),
        new("Y мин, мм", r => r.Axis(1)?.Min),
        new("Y макс, мм", r => r.Axis(1)?.Max),
        new("Z мин, мм", r => r.Axis(2)?.Min),
        new("Z макс, мм", r => r.Axis(2)?.Max),
    ];

    public static ToolDetailsRow[] Rows(AnalysisData? analysis)
    {
        if (analysis?.ToolTotals is not ToolTotalInfo total) return [];
        return [.. analysis.Tools.Where(t => t.Detail is not null).Select(t => new ToolDetailsRow(t, t.Detail!, t.Box)),
            new(null, total.Detail, total.Box)];
    }

    public static ToolDetailsRow[] Sort(ToolDetailsRow[] rows, int column, bool descending)
    {
        var definition = Columns[column];
        var key = definition.SortKey ?? definition.Value;
        var tools = rows.Where(r => !r.IsTotal).OrderBy(r => key(r) is null);
        var ordered = descending ? tools.ThenByDescending(key) : tools.ThenBy(key);
        return [.. ordered, .. rows.Where(r => r.IsTotal)];
    }

    private static string? Offsets(int[]? offsets) => offsets is { Length: > 0 } ? string.Join("/", offsets) : null;
}
