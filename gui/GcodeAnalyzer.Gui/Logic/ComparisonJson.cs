using System.Text.Json;

namespace GcodeAnalyzer.Gui.Logic;

public static class ComparisonJson
{
    public static ComparisonData Parse(string json) =>
        Validate(JsonSerializer.Deserialize<ComparisonData>(json, AnalysisJson.Options));

    public static async Task<ComparisonData> ReadAsync(Stream json) =>
        Validate(await JsonSerializer.DeserializeAsync<ComparisonData>(json, AnalysisJson.Options).ConfigureAwait(false));

    private static ComparisonData Validate(ComparisonData? data)
    {
        if (data is null || data.Schema != 1 || data.Kind != "comparison")
            throw new JsonException("Не поддерживается схема JSON сравнения.");
        if (!double.IsFinite(data.Tolerance) || data.Tolerance < 0 || data.Summary is null ||
            data.Trajectory is null || data.Format is null || data.NewWarnings is null || data.ResolvedWarnings is null)
            throw new JsonException("В JSON отсутствуют обязательные данные сравнения.");

        foreach (var program in new[] { data.Before, data.After })
        {
            if (program?.File is null || program.Segments is null || program.Warnings is null)
                throw new JsonException("В JSON отсутствуют данные одной из программ.");
            AnalysisJson.ValidateSegments(program.Segments);
            AnalysisJson.ValidateWarnings(program.Warnings);
        }
        AnalysisJson.ValidateWarnings(data.NewWarnings);
        AnalysisJson.ValidateWarnings(data.ResolvedWarnings);

        if (data.Summary.Any(row => row is null || row.Trend is not ("neutral" or "improved" or "worsened")) ||
            data.Format.Any(group => group is null || group.Before is null || group.After is null))
            throw new JsonException("Некорректные строки сводки или формата кадра.");

        foreach (var tool in data.Trajectory)
        {
            if (tool is null || tool.Presence is not ("both" or "added" or "removed") || tool.Differences is null ||
                tool.DifferenceCount < tool.Differences.Length ||
                (tool.Presence != "both" && tool.DifferenceCount != 0))
                throw new JsonException("Некорректное описание сравнения инструмента.");
            foreach (var difference in tool.Differences)
            {
                string? expectedKind = (difference?.OldLine, difference?.NewLine) switch
                {
                    (int, int) => "modified",
                    (null, int) => "inserted",
                    (int, null) => "deleted",
                    _ => null,
                };
                if (difference is null || expectedKind is null || difference.Kind != expectedKind ||
                    !ValidLine(difference.OldLine, data.Before.File.Lines) ||
                    !ValidLine(difference.NewLine, data.After.File.Lines))
                    throw new JsonException("Некорректные строки расхождения траектории.");
            }
        }
        return data;
    }

    private static bool ValidLine(int? line, int count) => line is null || (line > 0 && line <= count);
}

public sealed class ComparisonData
{
    public required int Schema { get; init; }
    public required string Kind { get; init; }
    public required double Tolerance { get; init; }
    public required ComparisonProgram Before { get; init; }
    public required ComparisonProgram After { get; init; }
    public required ComparisonSummaryRow[] Summary { get; init; }
    public required ToolComparison[] Trajectory { get; init; }
    public required FrameFormatDifference[] Format { get; init; }
    public required WarningItem[] NewWarnings { get; init; }
    public required WarningItem[] ResolvedWarnings { get; init; }
}

public sealed record ComparisonProgram(AnalysisFile File, Segment[] Segments, WarningItem[] Warnings);
public sealed record ComparisonSummaryRow(string Label, string Before, string After, string Difference, string Trend);
public sealed record ToolComparison(string Tool, string Presence, int OldPoints, int NewPoints, int DifferenceCount,
    PointDifference[] Differences);
public sealed record PointDifference(string Kind, int? OldLine, int? NewLine);
public sealed record FrameFormatDifference(string Label, string[] Before, string[] After);
