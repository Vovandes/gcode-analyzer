using System.Text.RegularExpressions;

namespace GcodeAnalyzer.Gui.Logic;

public static partial class ReportHighlighter
{
    [GeneratedRegex(@"^\s+\[(W\d+)\] строка (\d+):")]
    private static partial Regex WarningHeader();

    public static string[] Levels(string text, WarningItem[] warnings)
    {
        var levels = warnings.GroupBy(w => (w.Code, w.Line)).ToDictionary(g => g.Key,
            g => g.MaxBy(w => SeverityStyle.Rank(w.Severity))!.Severity);
        string[] lines = text.Replace("\r\n", "\n").Split('\n');
        var result = new string[lines.Length];
        bool inWarnings = false;
        bool inLimits = false;
        string active = "";
        for (int i = 0; i < lines.Length; i++)
        {
            if (lines[i].StartsWith("== ")) { inWarnings = lines[i] == "== Предупреждения =="; inLimits = lines[i] == "== Пределы станка =="; active = ""; }
            Match match = WarningHeader().Match(lines[i]);
            if (inWarnings && match.Success && int.TryParse(match.Groups[2].Value, out int line))
                active = levels.GetValueOrDefault((match.Groups[1].Value, line), "");
            else if (string.IsNullOrWhiteSpace(lines[i]) || !lines[i].StartsWith("    ")) active = "";
            result[i] = active;
            if (inLimits) result[i] = lines[i].StartsWith("  [error] ") ? "error" : lines[i].StartsWith("  [info] ") ? "info" : "";
        }
        return result;
    }

    public static string ToRtf(string text, WarningItem[] warnings)
    {
        string[] lines = text.Replace("\r\n", "\n").Split('\n');
        string[] levels = Levels(text, warnings);
        return ReportRtf.Render(lines.Select((line, i) => new ReportLine([new(line, ReportTextStyle.Code, levels[i])])));
    }
}
