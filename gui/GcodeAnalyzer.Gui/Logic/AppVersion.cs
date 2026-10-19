using System.Reflection;

namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Версия приложения (из файла VERSION через свойство Version проекта).</summary>
public static class AppVersion
{
    public static string Current { get; } = Read();

    private static string Read()
    {
        var attr = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>();
        string v = attr?.InformationalVersion ?? "0.0.0";
        int plus = v.IndexOf('+'); // отрезаем возможный суффикс «+хеш»
        return plus >= 0 ? v[..plus] : v;
    }
}
