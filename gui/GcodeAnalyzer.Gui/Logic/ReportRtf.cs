using System.Text;

namespace GcodeAnalyzer.Gui.Logic;

public static class ReportRtf
{
    private static readonly string?[] Colors = [null, "error", "warning", "info", "ok"];

    public static string Render(IEnumerable<ReportLine> lines)
    {
        var rtf = new StringBuilder(@"{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}{\f1 Consolas;}}{\colortbl ;");
        foreach (string? severity in Colors)
        {
            var color = SeverityStyle.Foreground(severity);
            rtf.Append($@"\red{color.R}\green{color.G}\blue{color.B};");
        }
        rtf.Append('}');
        foreach (var line in lines)
        {
            foreach (var span in line.Spans)
            {
                var style = span.Style;
                rtf.Append('{').Append(style.HasFlag(ReportTextStyle.Code) ? @"\f1" : @"\f0")
                    .Append(style.HasFlag(ReportTextStyle.Heading) ? @"\fs24" : @"\fs20")
                    .Append(@"\cf").Append(Math.Max(0, Array.IndexOf(Colors, span.Severity)) + 1);
                if (style.HasFlag(ReportTextStyle.Bold)) rtf.Append(@"\b");
                if (style.HasFlag(ReportTextStyle.Italic)) rtf.Append(@"\i");
                rtf.Append(' ');
                foreach (char ch in span.Text)
                {
                    if (ch is '\\' or '{' or '}') rtf.Append('\\').Append(ch);
                    else if (ch > 127) rtf.Append(@"\u").Append(unchecked((short)ch)).Append('?');
                    else if (ch == '\t') rtf.Append(@"\tab ");
                    else rtf.Append(ch);
                }
                rtf.Append('}');
            }
            rtf.Append(@"\par ");
        }
        return rtf.Append('}').ToString();
    }
}
