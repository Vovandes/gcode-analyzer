using System.Text.Json;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Настройки окна: хранятся в %APPDATA%\GcodeAnalyzer\settings.json.</summary>
public class AppSettings
{
    public string ExePath { get; set; } = "";
    public string LastFolder { get; set; } = "";
    public bool SaveToFile { get; set; }
    public bool Bom { get; set; }
    public string ExtraArguments { get; set; } = "";

    public static string DefaultPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "GcodeAnalyzer", "settings.json");

    /// <summary>Читает настройки; при любой проблеме (нет файла, битый JSON) возвращает значения по умолчанию.</summary>
    public static AppSettings Load(string? path = null)
    {
        try
        {
            path ??= DefaultPath;
            if (File.Exists(path))
                return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path)) ?? new AppSettings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            // настройки не критичны: работаем с умолчаниями
        }
        return new AppSettings();
    }

    /// <summary>Сохраняет настройки; ошибки записи не мешают работе.</summary>
    public void Save(string? path = null)
    {
        try
        {
            path ??= DefaultPath;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
