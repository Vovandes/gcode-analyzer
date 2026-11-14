namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Файл проверяет CLI; окно хранит только выбор пользователя.</summary>
public sealed class MachineProfileSelection
{
    public string? FilePath { get; private set; }
    public string FileLabel => FilePath is null ? "" : "Файл: " + Path.GetFileName(FilePath);
    public void Open(string path) => FilePath = Path.GetFullPath(path);
    public string? SelectedFile(int selectedIndex, int machineCount) =>
        selectedIndex == machineCount + 1 ? FilePath : null;
}
