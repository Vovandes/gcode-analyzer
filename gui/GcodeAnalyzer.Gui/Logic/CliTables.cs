namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Одна опция CLI из вывода --list-options.</summary>
public record CliOption(string Name, string Alias, string Value, string Default, string Description);

/// <summary>Профиль станка из вывода --list-machines.</summary>
public record MachineInfo(string Id, string Name);

/// <summary>Разбор машиночитаемых таблиц CLI (строки с полями через TAB, UTF-8).</summary>
public static class CliTables
{
    public static List<CliOption> ParseOptions(string text)
    {
        var result = new List<CliOption>();
        foreach (string line in Lines(text))
        {
            string[] f = line.Split('\t');
            if (f.Length < 5) continue; // чужая или повреждённая строка
            result.Add(new CliOption(f[0], f[1], f[2], f[3], f[4]));
        }
        return result;
    }

    public static List<MachineInfo> ParseMachines(string text)
    {
        var result = new List<MachineInfo>();
        foreach (string line in Lines(text))
        {
            string[] f = line.Split('\t');
            if (f.Length < 2) continue;
            result.Add(new MachineInfo(f[0], f[1]));
        }
        return result;
    }

    private static IEnumerable<string> Lines(string text) =>
        text.Split('\n').Select(l => l.TrimEnd('\r')).Where(l => l.Length > 0);
}
