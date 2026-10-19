namespace GcodeAnalyzer.Gui;

internal static class Program
{
    [STAThread] // WinForms (диалоги, drag-and-drop) требует однопоточного режима STA
    private static void Main()
    {
        ApplicationConfiguration.Initialize(); // стандартная настройка WinForms для .NET 8
        Application.Run(new MainForm());
    }
}
