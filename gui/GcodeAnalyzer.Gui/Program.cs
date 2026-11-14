namespace GcodeAnalyzer.Gui;

internal static class Program
{
    [STAThread] // WinForms (диалоги, drag-and-drop) требует однопоточного режима STA
    private static void Main()
    {
        ApplicationConfiguration.Initialize(); // стандартная настройка WinForms для .NET 10
        Application.Run(new MainForm());
    }
}
