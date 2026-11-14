using System.Text;
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
        string active = "";
        for (int i = 0; i < lines.Length; i++)
        {
            if (lines[i].StartsWith("== ")) { inWarnings = lines[i] == "== Предупреждения =="; active = ""; }
            Match match = WarningHeader().Match(lines[i]);
            if (inWarnings && match.Success && int.TryParse(match.Groups[2].Value, out int line))
                active = levels.GetValueOrDefault((match.Groups[1].Value, line), "");
            else if (string.IsNullOrWhiteSpace(lines[i]) || !lines[i].StartsWith("    ")) active = "";
            result[i] = active;
        }
        return result;
    }

    public static string ToRtf(string text, WarningItem[] warnings)
    {
        string[] lines = text.Replace("\r\n", "\n").Split('\n');
        string[] levels = Levels(text, warnings);
        var rtf = new StringBuilder(@"{\rtf1\ansi\deff0{\fonttbl{\f0 Consolas;}}{\colortbl ;\red0\green0\blue0;\red139\green0\blue0;\red150\green75\blue0;\red70\green130\blue180;}\f0\fs20 ");
        for (int i = 0; i < lines.Length; i++)
        {
            rtf.Append(@"\cf").Append(levels[i] switch { "error" => 2, "warning" => 3, "info" => 4, _ => 1 }).Append(' ');
            foreach (char ch in lines[i])
            {
                if (ch is '\\' or '{' or '}') rtf.Append('\\').Append(ch);
                else if (ch > 127) rtf.Append(@"\u").Append(unchecked((short)ch)).Append('?');
                else if (ch == '\t') rtf.Append(@"\tab ");
                else rtf.Append(ch);
            }
            if (i + 1 < lines.Length) rtf.Append(@"\par ");
        }
        // RichEdit не включает завершающий абзац RTF в Text.
        return rtf.Append(@"\par }").ToString();
    }
}
