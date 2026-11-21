using System.Text.Json;
using System.Text.Json.Serialization;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Читает данные ядра; геометрия и анализ в окне не пересчитываются.</summary>
public static class AnalysisJson
{
    internal static readonly JsonSerializerOptions Options = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    public static AnalysisData Parse(string json)
    {
        var data = JsonSerializer.Deserialize<AnalysisData>(json, Options)
            ?? throw new JsonException("Пустой результат анализа.");
        if (data.Schema != 1) throw new JsonException($"Не поддерживается схема JSON {data.Schema}.");
        if (data.File is null || data.Segments is null || data.Warnings is null || data.Tools is null || data.Time is null)
            throw new JsonException("В JSON отсутствуют обязательные данные анализа.");
        ValidateSegments(data.Segments);
        ValidateWarnings(data.Warnings);
        if (data.Limits is null || data.Limits.Any(c => c is null || c.Line < 1 || c.Status is not ("ok" or "error" or "info")))
            throw new JsonException("Некорректная проверка пределов.");
        return data;
    }

    internal static void ValidateSegments(Segment[] segments)
    {
        foreach (var segment in segments)
        {
            if (segment is null || segment.Line < 1 || segment.Start?.Length != 3 || segment.End?.Length != 3 ||
                segment.Plane is not ("G17" or "G18" or "G19") ||
                segment.Kind is not ("rapid" or "linear" or "arc" or "cycle" or "dwell" or "home"))
                throw new JsonException("Некорректный сегмент траектории.");
            if (segment.Kind == "arc" && (segment.Center?.Length != 3 || segment.Cw is null))
                throw new JsonException("В дуге отсутствуют центр или направление.");
            if ((segment.WorkStart is not null && segment.WorkStart.Length != 3) ||
                (segment.WorkEnd is not null && segment.WorkEnd.Length != 3))
                throw new JsonException("Некорректные координаты G53.");
        }
    }

    internal static void ValidateWarnings(WarningItem[] warnings)
    {
        foreach (var warning in warnings)
            if (warning is null || warning.Severity is not ("error" or "warning" or "info"))
                throw new JsonException("Неизвестный уровень предупреждения.");
    }
}

public sealed class AnalysisData
{
    public required int Schema { get; init; }
    public required AnalysisFile File { get; init; }
    public required Segment[] Segments { get; init; }
    public required WarningItem[] Warnings { get; init; }
    public required ToolInfo[] Tools { get; init; }
    public required TimeInfo Time { get; init; }
    public ToolTotalInfo? ToolTotals { get; init; }
    public LimitCheck[] Limits { get; init; } = [];
    public MachineTimeInfo? MachineTime { get; init; }
}

public sealed class AnalysisFile
{
    public required string Name { get; init; }
    public required int Lines { get; init; }
    public int Blocks { get; init; }
    public required string Dialect { get; init; }
    public required string Machine { get; init; }
    public string MachineProfile { get; init; } = "";
    public double RapidFeed { get; init; }
}

public sealed class Segment
{
    public required int Line { get; init; }
    public required string Kind { get; init; }
    public required int Tool { get; init; }
    public required string Plane { get; init; }
    public required double?[] Start { get; init; }
    public required double?[] End { get; init; }
    public double?[]? Center { get; init; }
    public bool? Cw { get; init; }
    public double? Radius { get; init; }
    public required double? Feed { get; init; }
    public required double? Spindle { get; init; }
    public required double? B { get; init; }
    public double? A { get; init; }
    public double? C { get; init; }
    public bool Approximate { get; init; }
    public bool SurfaceSpeed { get; init; }
    public int SpindleChannel { get; init; }
    public double? DwellSec { get; init; }
    public CycleInfo? Cycle { get; init; }
    public bool LimitViolation { get; init; }
    public bool MachineCoordinates { get; init; }
    public double?[]? WorkStart { get; init; }
    public double?[]? WorkEnd { get; init; }
}

public sealed class LimitCheck
{
    public required string Parameter { get; init; }
    public double? ValueMin { get; init; }
    public double? ValueMax { get; init; }
    public double? LimitMin { get; init; }
    public double? LimitMax { get; init; }
    public required string Status { get; init; }
    public int Line { get; init; }
}

public sealed class MachineTimeInfo
{
    public double TotalSec { get; init; }
    public bool Accelerations { get; init; }
    public bool ProfileApplied { get; init; }
    public required TimeInfo Motion { get; init; }
    public double ToolChangeSec { get; init; }
    public double HomeSec { get; init; }
    public double BSec { get; init; }
    public double SpindleSec { get; init; }
    public int ProgramStops { get; init; }
    public int OptionalStops { get; init; }
    public string[] Notes { get; init; } = [];
}

public sealed class CycleInfo
{
    public required string Code { get; init; }
    [JsonPropertyName("zR")]
    public double ZR { get; init; }
    public double ZBottom { get; init; }
    public double? ZInit { get; init; }
    public double? Q { get; init; }
    public double? P { get; init; }
    public bool ReturnToInitial { get; init; }
}

public sealed class WarningItem
{
    public required int Line { get; init; }
    public required string Code { get; init; }
    public required string Severity { get; init; }
    public required string Message { get; init; }
    public required string Text { get; init; }
}

public sealed class RangeInfo
{
    public double Min { get; init; }
    public double Max { get; init; }
}

public sealed class TimeInfo
{
    public double TotalSec { get; init; }
    public double RapidSec { get; init; }
    public double FeedSec { get; init; }
    public double CycleSec { get; init; }
    public double DwellSec { get; init; }
    public double RapidMm { get; init; }
    public double FeedMm { get; init; }
    public double CycleMm { get; init; }
    public int RapidMoves { get; init; }
    public int FeedMoves { get; init; }
    public int CycleHoles { get; init; }
}

public sealed class ToolInfo
{
    public string? Name { get; init; }
    public required int Tool { get; init; }
    public string[] Descriptions { get; init; } = [];
    public int Changes { get; init; }
    public int[] ChangeLines { get; init; } = [];
    public int[] HValues { get; init; } = [];
    public int[] DValues { get; init; } = [];
    public required TimeInfo Time { get; init; }
    public double? DiameterMm { get; init; }
    public ToolMetrics? Detail { get; init; }
    // Габариты идут в порядке X, Y, Z, B; null означает неизвестную ось.
    public RangeInfo?[] Box { get; init; } = [];
    public RangeInfo?[] WorkBox { get; init; } = [];
    public RangeInfo? FeedPerMin { get; init; }
    public RangeInfo? FeedPerRev { get; init; }
    public RangeInfo? Spindle { get; init; }
    public bool FractionalS { get; init; }
    public string[] SpindleDirs { get; init; } = [];
    public string[] Coolant { get; init; } = [];
    public string[] Wcs { get; init; } = [];
    public Dictionary<string, int> CycleHoles { get; init; } = [];
}

public sealed class ToolMetrics
{
    public required TimeInfo MotionTime { get; init; }
    public double WorkingMm { get; init; }
    public double IdleMm { get; init; }
    public RangeInfo? FeedMmPerMin { get; init; }
    public RangeInfo? Rpm { get; init; }
}

public sealed class ToolTotalInfo
{
    public required ToolMetrics Detail { get; init; }
    public RangeInfo?[] Box { get; init; } = [];
}
