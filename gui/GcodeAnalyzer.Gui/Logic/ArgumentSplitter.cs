using System.Text;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>
/// Разбивает строку в стиле командной строки на аргументы: "--rapid 12000 --out "C:\мои файлы\r.txt"".
/// Пробелы разделяют аргументы, двойные кавычки позволяют включить пробелы внутрь аргумента.
/// </summary>
public static class ArgumentSplitter
{
    public static List<string> Split(string? text)
    {
        var result = new List<string>();
        if (string.IsNullOrWhiteSpace(text)) return result;

        var current = new StringBuilder();
        bool inQuotes = false;
        bool hasToken = false; // нужен, чтобы пустые кавычки "" давали пустой аргумент

        foreach (char c in text)
        {
            if (c == '"')
            {
                inQuotes = !inQuotes;
                hasToken = true;
            }
            else if (char.IsWhiteSpace(c) && !inQuotes)
            {
                if (hasToken) result.Add(current.ToString());
                current.Clear();
                hasToken = false;
            }
            else
            {
                current.Append(c);
                hasToken = true;
            }
        }
        if (hasToken) result.Add(current.ToString()); // последний аргумент (в т.ч. с незакрытой кавычкой)
        return result;
    }
}
