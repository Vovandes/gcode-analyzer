using System.Drawing;

namespace GcodeAnalyzer.Gui.Logic;

public enum LineKind { Other, Rapid, Work }
public readonly record struct TextSpan(int Start, int Length, bool Comment);
public sealed record ProgramLine(string Text, LineKind Kind, string? Severity, TextSpan[] Spans);

public static class LineClassifier
{
    public static ProgramLine[] Classify(string[] lines, Segment[] segments, WarningItem[] warnings)
    {
        var kinds = segments.GroupBy(s => s.Line).ToDictionary(g => g.Key, g =>
            g.Any(s => s.Kind is "linear" or "arc" or "cycle") ? LineKind.Work :
            g.Any(s => s.Kind is "rapid" or "home") ? LineKind.Rapid : LineKind.Other);
        var levels = warnings.GroupBy(w => w.Line).ToDictionary(g => g.Key,
            g => g.MaxBy(w => SeverityStyle.Rank(w.Severity))!.Severity);
        return lines.Select((text, i) => new ProgramLine(text, kinds.GetValueOrDefault(i + 1),
            levels.GetValueOrDefault(i + 1), SplitComments(text))).ToArray();
    }

    public static TextSpan[] SplitComments(string text)
    {
        var spans = new List<TextSpan>();
        int start = 0, depth = 0;
        for (int i = 0; i < text.Length; i++)
        {
            if (text[i] == ';' && depth == 0)
            {
                if (i > start) spans.Add(new(start, i - start, false));
                spans.Add(new(i, text.Length - i, true));
                return spans.ToArray();
            }
            if (text[i] == '(')
            {
                if (depth == 0) { if (i > start) spans.Add(new(start, i - start, false)); start = i; }
                depth++;
            }
            else if (text[i] == ')' && depth > 0 && --depth == 0)
            { spans.Add(new(start, i - start + 1, true)); start = i + 1; }
        }
        if (start < text.Length) spans.Add(new(start, text.Length - start, depth > 0));
        return spans.ToArray();
    }

    public static Color Foreground(LineKind kind) => kind switch
    { LineKind.Rapid => Color.FromArgb(145, 105, 0), LineKind.Work => Color.DarkGreen, _ => Color.Black };
}

public static class SeverityStyle
{
    public static int Rank(string? severity) => severity switch { "error" => 3, "warning" => 2, "info" => 1, _ => 0 };
    public static Color Background(string? severity) => severity switch
    { "error" => Color.LightCoral, "warning" => Color.FromArgb(255, 209, 140), "info" => Color.AliceBlue, _ => Color.White };
    public static Color Foreground(string? severity) => severity switch
    { "error" => Color.DarkRed, "warning" => Color.FromArgb(150, 75, 0), "info" => Color.SteelBlue, _ => Color.Black };
    public static string Label(string severity) => severity switch { "error" => "Ошибка", "warning" => "Предупреждение", _ => "Информация" };
}
