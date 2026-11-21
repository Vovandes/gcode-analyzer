using System.Globalization;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Собирает список аргументов для gcode-analyzer.exe.</summary>
public static class AnalyzerArguments
{
    /// <summary>Значение в списке станков, означающее «определить автоматически».</summary>
    public const string AutoMachine = "auto";

    /// <summary>
    /// Список аргументов: входной файл, дополнительные аргументы пользователя,
    /// затем --machine / --bom / --out. Наши опции стоят последними, поэтому побеждают.
    /// Каждый элемент списка — отдельный аргумент; кавычки не нужны:
    /// ProcessStartInfo.ArgumentList сам экранирует пробелы и кириллицу.
    /// </summary>
    public static List<string> Build(string inputFile, string outFile, string? machineId, bool bom, string? extraArguments,
        string? jsonFile = null, string? machineFile = null, bool fromComparison = false)
    {
        var args = new List<string> { inputFile };
        List<string> extra = ArgumentSplitter.Split(extraArguments);
        if (fromComparison)
        {
            // После успешного сравнения убираем только пару «допуск + число».
            for (int i = 0; i + 1 < extra.Count; i++)
                if (extra[i] == "--tolerance" && double.TryParse(extra[i + 1], NumberStyles.Float,
                    CultureInfo.InvariantCulture, out _))
                {
                    extra.RemoveRange(i, 2);
                    --i;
                }
        }
        AddOptions(args, outFile, machineId, bom, extra, jsonFile, machineFile);
        return args;
    }

    public static List<string> BuildComparison(string before, string after, string outFile, string jsonFile,
        string? machineId, string? extraArguments, string? machineFile = null)
    {
        var args = new List<string> { "--compare", before, after };
        AddOptions(args, outFile, machineId, false, ArgumentSplitter.Split(extraArguments), jsonFile, machineFile);
        return args;
    }

    private static void AddOptions(List<string> args, string outFile, string? machineId, bool bom, List<string> extraArguments,
        string? jsonFile, string? machineFile)
    {
        args.AddRange(extraArguments);

        if (!string.IsNullOrEmpty(machineId) && machineId != AutoMachine)
        {
            args.Add("--machine");
            args.Add(machineId);
        }
        if (bom) args.Add("--bom");
        if (!string.IsNullOrEmpty(machineFile))
        {
            args.Add("--machine-file");
            args.Add(machineFile);
        }

        args.Add("--out");
        args.Add(outFile);
        if (jsonFile != null)
        {
            args.Add("--json");
            args.Add(jsonFile);
        }
    }
}
