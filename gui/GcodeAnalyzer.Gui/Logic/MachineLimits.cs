using System.Globalization;

namespace GcodeAnalyzer.Gui.Logic;

public static class MachineLimits
{
    public static string Value(double? low, double? high) =>
        $"{low?.ToString("0.###", CultureInfo.InvariantCulture) ?? "?"} .. {high?.ToString("0.###", CultureInfo.InvariantCulture) ?? "?"}";
    public static string Status(string status) => status switch
    { "error" => "Нарушение", "info" => "Только размах", _ => "В пределах" };
    public static string? Severity(string status) => status switch { "error" => "error", "info" => "info", _ => null };
}
