namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Раздел отчёта: заголовок и позиция (индекс символа) его строки в тексте.</summary>
public record ReportSection(string Title, int Offset);

/// <summary>Разбор текстового отчёта по заголовкам вида «== Заголовок ==».</summary>
public static class ReportParser
{
    /// <summary>
    /// Приводит концы строк к CRLF: TextBox в WinForms показывает переносы только в таком виде.
    /// Разбирать и вычислять позиции нужно уже по нормализованному тексту.
    /// </summary>
    public static string NormalizeNewlines(string text) =>
        text.Replace("\r\n", "\n").Replace("\n", "\r\n");

    public static List<ReportSection> ParseSections(string text)
    {
        var sections = new List<ReportSection>();
        int pos = 0;
        while (pos < text.Length)
        {
            int lineEnd = text.IndexOf('\n', pos);
            int next = lineEnd < 0 ? text.Length : lineEnd + 1;
            string line = text.Substring(pos, (lineEnd < 0 ? text.Length : lineEnd) - pos).TrimEnd('\r');

            if (line.Length > 6 && line.StartsWith("== ") && line.EndsWith(" =="))
                sections.Add(new ReportSection(line[3..^3].Trim(), pos));

            pos = next;
        }
        return sections;
    }

    /// <summary>Значение строки «Станок:» из раздела «Файл»; пустая строка, если её нет.</summary>
    public static string FindMachine(string text)
    {
        const string key = "Станок:";
        foreach (string raw in text.Split('\n'))
        {
            string line = raw.Trim();
            if (line.StartsWith(key)) return line[key.Length..].Trim();
        }
        return "";
    }
}
