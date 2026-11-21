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
        string? jsonFile = null, string? machineFile = null)
    {
        var args = new List<string> { inputFile };
        AddOptions(args, outFile, machineId, bom, extraArguments, jsonFile, machineFile);
        return args;
    }

    public static List<string> BuildComparison(string before, string after, string outFile, string jsonFile,
        string? machineId, string? extraArguments, string? machineFile = null)
    {
        var args = new List<string> { "--compare", before, after };
        AddOptions(args, outFile, machineId, false, extraArguments, jsonFile, machineFile);
        return args;
    }

    private static void AddOptions(List<string> args, string outFile, string? machineId, bool bom, string? extraArguments,
        string? jsonFile, string? machineFile)
    {
        args.AddRange(ArgumentSplitter.Split(extraArguments));

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
