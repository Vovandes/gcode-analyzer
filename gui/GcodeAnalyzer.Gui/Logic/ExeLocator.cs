namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Ищет gcode-analyzer.exe.</summary>
public static class ExeLocator
{
    public const string ExeName = "gcode-analyzer.exe";

    /// <summary>
    /// Порядок поиска: путь из настроек (если файл существует; ручной выбор важнее автопоиска) →
    /// рядом с exe GUI → вверх по родительским каталогам: сам каталог, build\*\ и build\*\{Debug,Release}\.
    /// Возвращает null, если ничего не найдено.
    /// </summary>
    public static string? Find(string startDirectory, string? configuredPath = null)
    {
        if (!string.IsNullOrWhiteSpace(configuredPath) && File.Exists(configuredPath))
            return configuredPath;

        // Поднимаемся от каталога GUI к корню диска
        for (var dir = new DirectoryInfo(startDirectory); dir != null; dir = dir.Parent)
        {
            string direct = Path.Combine(dir.FullName, ExeName);
            if (File.Exists(direct)) return direct;

            string build = Path.Combine(dir.FullName, "build");
            if (!Directory.Exists(build)) continue;

            foreach (string preset in Directory.GetDirectories(build))
            {
                string inPreset = Path.Combine(preset, ExeName);
                if (File.Exists(inPreset)) return inPreset;
                foreach (string config in new[] { "Debug", "Release" })
                {
                    string inConfig = Path.Combine(preset, config, ExeName);
                    if (File.Exists(inConfig)) return inConfig;
                }
            }
        }
        return null;
    }
}
