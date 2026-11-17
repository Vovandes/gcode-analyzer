namespace GcodeAnalyzer.Gui.Logic;

[Flags]
public enum ReportTextStyle { Plain = 0, Bold = 1, Italic = 2, Code = 4, Heading = 8 }

public sealed record ReportSpan(string Text, ReportTextStyle Style = ReportTextStyle.Plain, string? Severity = null);
public sealed record ReportLine(ReportSpan[] Spans, int? ProgramLine = null);
public sealed record ReportBlock(string Title, ReportLine[] Lines);

public sealed class ReportPresentation
{
    public IReadOnlyList<ReportBlock> Blocks { get; }
    public IReadOnlyList<ReportLine> Lines { get; }
    public IReadOnlyList<int> HeadingLines { get; }

    private ReportPresentation(List<ReportBlock> blocks)
    {
        Blocks = blocks;
        var lines = new List<ReportLine>();
        var headings = new List<int>();
        foreach (var block in blocks)
        {
            if (lines.Count > 0) lines.Add(Text(""));
            headings.Add(lines.Count);
            lines.Add(Text(block.Title, ReportTextStyle.Bold | ReportTextStyle.Heading));
            lines.AddRange(block.Lines);
        }
        Lines = lines;
        HeadingLines = headings;
    }

    public static ReportPresentation Build(string text, AnalysisData analysis)
    {
        var sections = ReadSections(text);
        var errors = analysis.Warnings.Where(w => w.Severity == "error").ToArray();
        var warnings = analysis.Warnings.Where(w => w.Severity == "warning").ToArray();
        var info = analysis.Warnings.Where(w => w.Severity == "info").ToArray();
        var fileDetails = sections.GetValueOrDefault("Файл", [])
            .Where(line => line.TrimStart().StartsWith("Строк:") || line.TrimStart().StartsWith("Концы строк:") ||
                line.TrimStart().StartsWith("Ошибки лексера:"))
            .Select(line => Text(line.TrimStart()));
        var blocks = new List<ReportBlock>
        {
            new("Сводка",
            [
                Text($"Файл: {analysis.File.Name}"),
                Text($"Диалект: {analysis.File.Dialect}"),
                Text($"Профиль станка: {OrDash(analysis.File.MachineProfile)}"),
                Text($"Станок: {OrDash(analysis.File.Machine)}"),
                .. fileDetails,
                new([
                    new($"Ошибок: {errors.Length}", ReportTextStyle.Bold, errors.Length == 0 ? "ok" : "error"),
                    new("   "),
                    new($"Предупреждений: {warnings.Length}", ReportTextStyle.Bold, warnings.Length == 0 ? "ok" : "warning"),
                    new($"   Информация: {info.Length}"),
                ]),
            ]),
            WarningBlock("Ошибки", errors),
            WarningBlock("Предупреждения", warnings),
            Section("Пределы станка", sections, ReportTextStyle.Code),
            Section("Время обработки (оценка)", sections, ReportTextStyle.Code),
            Section("Инструменты", sections),
            Section("Инструменты: подробно", sections, ReportTextStyle.Code),
        };
        if (info.Length > 0) blocks.Add(WarningBlock("Информация", info));
        sections.Remove("Файл");
        sections.Remove("Предупреждения");
        blocks.AddRange(sections.Select(s => new ReportBlock(s.Key, s.Value.Select(l => Text(l, ReportTextStyle.Code)).ToArray())));
        return new(blocks);
    }

    private static Dictionary<string, string[]> ReadSections(string text)
    {
        text = text.Replace("\r\n", "\n");
        var sections = ReportParser.ParseSections(text);
        var result = new Dictionary<string, string[]>();
        for (int i = 0; i < sections.Count; i++)
        {
            int start = text.IndexOf('\n', sections[i].Offset);
            int end = i + 1 < sections.Count ? sections[i + 1].Offset : text.Length;
            result[sections[i].Title] = start < 0 ? [] : text[(start + 1)..end].TrimEnd('\n', '\r').Split('\n');
        }
        return result;
    }

    private static ReportBlock Section(string title, Dictionary<string, string[]> sections, ReportTextStyle style = ReportTextStyle.Plain)
    {
        if (!sections.Remove(title, out var source) || source.All(string.IsNullOrWhiteSpace))
            return new(title, [None()]);
        var lines = source.Select(line =>
        {
            string entry = line.TrimStart();
            if (entry == "нет") return None();
            if (entry.StartsWith('['))
            {
                int end = entry.IndexOf(']');
                if (end > 0 && entry[1..end] is "error" or "info" or "ok")
                {
                    string severity = entry[1..end];
                    return new ReportLine([new("  " + entry[..(end + 1)], ReportTextStyle.Bold, severity),
                        new(entry[(end + 1)..], style, severity)]);
                }
            }
            if (entry.StartsWith("ИТОГО") || entry.StartsWith("T |")) return Text(line, style | ReportTextStyle.Bold);
            if (entry.StartsWith("Следующие строки") || entry.StartsWith("Время в секундах") ||
                entry.StartsWith("Смена инструмента,") || entry.StartsWith("Неполная оценка") ||
                entry.StartsWith("Точная модель") || entry.StartsWith("оценка без ускорений"))
                return Text(line, ReportTextStyle.Italic);
            return Text(line, style);
        }).ToArray();
        return new(title, lines);
    }

    private static ReportBlock WarningBlock(string title, WarningItem[] warnings)
    {
        if (warnings.Length == 0) return new(title, [None()]);
        var lines = new List<ReportLine>();
        foreach (var w in warnings)
        {
            lines.Add(new([
                new($"{SeverityStyle.Label(w.Severity)} ", ReportTextStyle.Bold, w.Severity),
                new($"[{w.Code}]", ReportTextStyle.Bold | ReportTextStyle.Code, w.Severity),
                new($"  строка {w.Line}", ReportTextStyle.Code, w.Severity),
                new($"  {w.Message}", ReportTextStyle.Italic, w.Severity),
            ], w.Line));
            if (!string.IsNullOrWhiteSpace(w.Text)) lines.Add(new([new("    " + w.Text, ReportTextStyle.Code, w.Severity)], w.Line));
        }
        return new(title, [.. lines]);
    }

    private static string OrDash(string value) => string.IsNullOrWhiteSpace(value) ? "—" : value;
    private static ReportLine None() => Text("  нет", ReportTextStyle.Italic, "ok");
    private static ReportLine Text(string text, ReportTextStyle style = ReportTextStyle.Plain, string? severity = null) =>
        new([new(text, style, severity)]);
}
