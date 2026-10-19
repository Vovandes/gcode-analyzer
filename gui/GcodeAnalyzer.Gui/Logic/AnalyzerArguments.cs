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
    public static List<string> Build(string inputFile, string outFile, string? machineId, bool bom, string? extraArguments)
    {
        var args = new List<string> { inputFile };
        args.AddRange(ArgumentSplitter.Split(extraArguments));

        if (!string.IsNullOrEmpty(machineId) && machineId != AutoMachine)
        {
            args.Add("--machine");
            args.Add(machineId);
        }
        if (bom) args.Add("--bom");

        args.Add("--out");
        args.Add(outFile);
        return args;
    }
}
