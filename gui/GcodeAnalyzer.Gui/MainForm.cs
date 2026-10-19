using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui;

/// <summary>Главное окно. Форма тонкая: только элементы управления и вызовы классов из Logic.</summary>
public class MainForm : Form
{
    private readonly AppSettings _settings = AppSettings.Load();

    // Элементы управления
    private readonly Button _openButton = new() { Text = "Открыть…", AutoSize = true };
    private readonly Label _fileLabel = new() { Text = "Файл не выбран", AutoSize = true, Anchor = AnchorStyles.Left };
    private readonly RadioButton _showRadio = new() { Text = "Показать в окне", AutoSize = true, Checked = true };
    private readonly RadioButton _saveRadio = new() { Text = "Сохранить в .txt", AutoSize = true };
    private readonly CheckBox _bomCheck = new() { Text = "BOM для Блокнота", AutoSize = true };
    private readonly ComboBox _machineCombo = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 220 };
    private readonly TextBox _extraBox = new() { Dock = DockStyle.Fill };
    private readonly TextBox _exeBox = new() { Dock = DockStyle.Fill };
    private readonly Button _exeButton = new() { Text = "…", AutoSize = true };
    private readonly Button _runButton = new() { Text = "Анализировать", AutoSize = true };
    private readonly Button _saveAsButton = new() { Text = "Сохранить как txt", AutoSize = true, Enabled = false };
    private readonly Button _helpButton = new() { Text = "Справка по аргументам", AutoSize = true };
    private readonly Label _machineLabel = new() { Text = "Станок: —", AutoSize = true, Anchor = AnchorStyles.Left };
    private readonly ListBox _sectionList = new() { Dock = DockStyle.Fill };
    private readonly TextBox _reportBox = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both,
        WordWrap = false, Font = new Font("Consolas", 10), HideSelection = false, // текст можно выделять и копировать
    };
    private readonly TextBox _errorBox = new()
    {
        Dock = DockStyle.Bottom, Multiline = true, ReadOnly = true, Height = 60, ScrollBars = ScrollBars.Vertical,
        ForeColor = Color.Firebrick, Visible = false,
    };
    private readonly Label _status = new() { Text = "Готово", AutoSize = true };

    // Состояние
    private string? _inputFile;
    private List<ReportSection> _sections = new();
    private List<MachineInfo> _machines = new();
    private bool _busy;

    public MainForm()
    {
        Text = "GcodeAnalyzer " + AppVersion.Current;
        Size = new Size(1100, 750);
        AllowDrop = true; // разрешаем перетаскивать файлы на окно

        BuildLayout();

        _openButton.Click += (_, _) => OpenFileDialogAndRun();
        _runButton.Click += async (_, _) => await RunAnalysisAsync();
        _saveAsButton.Click += async (_, _) => await SaveAsTxtAsync();
        _helpButton.Click += async (_, _) => await ShowHelpAsync();
        _exeButton.Click += (_, _) => ChooseExe();
        _sectionList.SelectedIndexChanged += (_, _) => JumpToSection();
        _saveRadio.CheckedChanged += (_, _) => _bomCheck.Enabled = _saveRadio.Checked;
        DragEnter += OnDragEnter;
        DragDrop += OnDragDrop;
        Shown += async (_, _) => await OnShownAsync();
        FormClosing += (_, _) => SaveSettings();

        // Восстанавливаем сохранённые настройки
        _exeBox.Text = _settings.ExePath;
        _extraBox.Text = _settings.ExtraArguments;
        _saveRadio.Checked = _settings.SaveToFile;
        _showRadio.Checked = !_settings.SaveToFile;
        _bomCheck.Checked = _settings.Bom;
        _bomCheck.Enabled = _saveRadio.Checked;
    }

    // ---------- Разметка окна ----------

    private void BuildLayout()
    {
        // Верхняя панель: две строки элементов
        var row1 = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = false, Padding = new Padding(6, 6, 6, 0) };
        row1.Controls.AddRange(new Control[] { _openButton, _fileLabel });

        var row2 = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = true, Padding = new Padding(6, 2, 6, 0) };
        row2.Controls.AddRange(new Control[]
        {
            _showRadio, _saveRadio, _bomCheck,
            new Label { Text = "Профиль станка:", AutoSize = true, Margin = new Padding(20, 6, 0, 0) }, _machineCombo,
            _runButton, _saveAsButton, _helpButton,
        });

        // Строки с растягиваемым полем ввода: таблица из 3 колонок (подпись, поле, кнопка)
        var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 3, Padding = new Padding(6, 2, 6, 2) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.Controls.Add(new Label { Text = "Дополнительные аргументы:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0);
        grid.Controls.Add(_extraBox, 1, 0);
        grid.Controls.Add(new Label { Text = "Путь к gcode-analyzer.exe:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 1);
        grid.Controls.Add(_exeBox, 1, 1);
        grid.Controls.Add(_exeButton, 2, 1);

        var infoRow = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6, 0, 6, 0) };
        infoRow.Controls.AddRange(new Control[] { _machineLabel });

        // Центр: слева список разделов, справа текст отчёта
        var split = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 230 };
        split.Panel1.Controls.Add(_sectionList);
        split.Panel2.Controls.Add(_reportBox);

        var statusBar = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, Padding = new Padding(6, 2, 6, 2) };
        statusBar.Controls.Add(_status);

        // Порядок добавления важен: Dock-элементы раскладываются с последнего к первому
        Controls.Add(split);
        Controls.Add(_errorBox);
        Controls.Add(statusBar);
        Controls.Add(infoRow);
        Controls.Add(grid);
        Controls.Add(row2);
        Controls.Add(row1);
    }

    // ---------- Запуск и поиск анализатора ----------

    private async Task OnShownAsync()
    {
        // Список станков запрашиваем у самого CLI — в GUI ничего не зашито
        _machineCombo.Items.Add("Авто");
        _machineCombo.SelectedIndex = 0;
        AnalyzerRunner? runner = CreateRunner(showMessage: false);
        if (runner == null)
        {
            ShowError("Не найден gcode-analyzer.exe. Соберите CLI (cmake --build --preset msvc-debug) или укажите путь кнопкой «…».");
            return;
        }
        RunResult r = await runner.RunAsync(new[] { "--list-machines" });
        if (r.ExitCode != 0) return;
        _machines = CliTables.ParseMachines(r.StdOut);
        foreach (MachineInfo m in _machines) _machineCombo.Items.Add(m.Name);
    }

    /// <summary>Находит exe (настройки → рядом → build\*) и создаёт запускалку; null, если не найден.</summary>
    private AnalyzerRunner? CreateRunner(bool showMessage)
    {
        string? exe = ExeLocator.Find(ExeLocator.AppDirectory(), _exeBox.Text.Trim());
        if (exe == null)
        {
            if (showMessage)
                ShowError("Не найден gcode-analyzer.exe. Соберите CLI (cmake --build --preset msvc-debug) или укажите путь кнопкой «…».");
            return null;
        }
        if (string.IsNullOrWhiteSpace(_exeBox.Text)) _exeBox.Text = exe;
        return new AnalyzerRunner(exe);
    }

    private void ChooseExe()
    {
        using var dialog = new OpenFileDialog { Filter = "gcode-analyzer.exe|gcode-analyzer.exe|Программы (*.exe)|*.exe" };
        if (dialog.ShowDialog(this) == DialogResult.OK) _exeBox.Text = dialog.FileName;
    }

    /// <summary>Выбранный в списке id станка; "auto", если выбрано «Авто».</summary>
    private string SelectedMachineId =>
        _machineCombo.SelectedIndex > 0 && _machineCombo.SelectedIndex <= _machines.Count
            ? _machines[_machineCombo.SelectedIndex - 1].Id
            : AnalyzerArguments.AutoMachine;

    // ---------- Открытие файла ----------

    private void OpenFileDialogAndRun()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Управляющие программы|*.nc;*.tap;*.txt|Все файлы|*.*",
            InitialDirectory = Directory.Exists(_settings.LastFolder) ? _settings.LastFolder : "",
        };
        if (dialog.ShowDialog(this) == DialogResult.OK) SetInputFileAndRun(dialog.FileName);
    }

    // Drag-and-drop: DragEnter говорит, принимаем ли мы перетаскиваемое
    private void OnDragEnter(object? sender, DragEventArgs e)
    {
        e.Effect = e.Data?.GetDataPresent(DataFormats.FileDrop) == true ? DragDropEffects.Copy : DragDropEffects.None;
    }

    // DragDrop срабатывает при отпускании; FileDrop даёт массив полных путей
    private void OnDragDrop(object? sender, DragEventArgs e)
    {
        if (e.Data?.GetData(DataFormats.FileDrop) is string[] files && files.Length > 0)
            SetInputFileAndRun(files[0]);
    }

    private void SetInputFileAndRun(string path)
    {
        _inputFile = path;
        _fileLabel.Text = path;
        _settings.LastFolder = Path.GetDirectoryName(path) ?? "";
        // В режиме «Показать» анализ стартует сразу; в режиме «Сохранить» ждём кнопку
        if (_showRadio.Checked) _ = RunAnalysisAsync();
    }

    // ---------- Анализ ----------

    private async Task RunAnalysisAsync()
    {
        if (_busy) return;
        if (_inputFile == null) { ShowError("Сначала откройте файл УП."); return; }
        AnalyzerRunner? runner = CreateRunner(showMessage: true);
        if (runner == null) return;

        SetBusy(true);
        try
        {
            if (_saveRadio.Checked)
            {
                // Режим «Сохранить в .txt»: просим путь и отдаём его в --out
                string? target = AskSavePath();
                if (target == null) return;
                RunResult run = await runner.AnalyzeToFileAsync(_inputFile, target, SelectedMachineId, _bomCheck.Checked, _extraBox.Text);
                ShowRunResult(run, $"Отчёт сохранён: {target}");
            }
            else
            {
                // Режим «Показать»: отчёт через временный файл, читаем как UTF-8
                (RunResult run, string report) = await runner.AnalyzeToMemoryAsync(_inputFile, SelectedMachineId, _extraBox.Text);
                ShowRunResult(run, "Готово");
                if (run.ExitCode == 0) ShowReport(report);
            }
        }
        finally
        {
            SetBusy(false);
        }
    }

    /// <summary>Повторный запуск с --out в файл, выбранный пользователем (после просмотра).</summary>
    private async Task SaveAsTxtAsync()
    {
        if (_busy || _inputFile == null) return;
        AnalyzerRunner? runner = CreateRunner(showMessage: true);
        string? target = AskSavePath();
        if (runner == null || target == null) return;

        SetBusy(true);
        try
        {
            RunResult run = await runner.AnalyzeToFileAsync(_inputFile, target, SelectedMachineId, _bomCheck.Checked, _extraBox.Text);
            ShowRunResult(run, $"Отчёт сохранён: {target}");
        }
        finally
        {
            SetBusy(false);
        }
    }

    private string? AskSavePath()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "Текстовый файл (*.txt)|*.txt",
            FileName = Path.GetFileNameWithoutExtension(_inputFile!) + "-отчёт.txt",
            InitialDirectory = Directory.Exists(_settings.LastFolder) ? _settings.LastFolder : "",
        };
        return dialog.ShowDialog(this) == DialogResult.OK ? dialog.FileName : null;
    }

    private async Task ShowHelpAsync()
    {
        AnalyzerRunner? runner = CreateRunner(showMessage: true);
        if (runner == null) return;
        RunResult table = await runner.RunAsync(new[] { "--list-options" });
        List<CliOption>? options = table.ExitCode == 0 ? CliTables.ParseOptions(table.StdOut) : null;
        string help = "";
        if (options == null || options.Count == 0) help = (await runner.RunAsync(new[] { "--help" })).StdOut;
        using var form = new HelpForm(options, help);
        form.ShowDialog(this);
    }

    // ---------- Показ результата ----------

    private void ShowRunResult(RunResult run, string successText)
    {
        _errorBox.Visible = run.ExitCode != 0;
        if (run.ExitCode == 0)
        {
            _status.Text = $"{successText} (код возврата 0)";
            return;
        }
        string reason = run.ExitCode switch { 1 => "ошибка ввода-вывода", 2 => "ошибка аргументов", _ => "ошибка запуска" };
        _status.Text = $"Ошибка: код возврата {run.ExitCode} ({reason})";
        _errorBox.Text = ReportParser.NormalizeNewlines(run.StdErr.Trim());
    }

    private void ShowReport(string report)
    {
        string text = ReportParser.NormalizeNewlines(report);
        _reportBox.Text = text;
        _sections = ReportParser.ParseSections(text);
        _sectionList.Items.Clear();
        foreach (ReportSection s in _sections) _sectionList.Items.Add(s.Title);
        string machine = ReportParser.FindMachine(text);
        _machineLabel.Text = "Станок: " + (machine.Length > 0 ? machine : "—");
        _saveAsButton.Enabled = true;
    }

    // Щелчок по разделу: ставим курсор на заголовок и прокручиваем к нему
    private void JumpToSection()
    {
        int i = _sectionList.SelectedIndex;
        if (i < 0 || i >= _sections.Count) return;
        _reportBox.SelectionStart = _sections[i].Offset;
        _reportBox.SelectionLength = 0;
        _reportBox.ScrollToCaret();
    }

    private void ShowError(string message)
    {
        _errorBox.Text = message;
        _errorBox.Visible = true;
        _status.Text = "Ошибка";
    }

    /// <summary>На время работы кнопки неактивны, в статусе «Анализ…».</summary>
    private void SetBusy(bool busy)
    {
        _busy = busy;
        foreach (Control c in new Control[] { _openButton, _runButton, _saveAsButton, _helpButton, _exeButton })
            c.Enabled = !busy;
        if (!busy) _saveAsButton.Enabled = _reportBox.TextLength > 0;
        if (busy) { _status.Text = "Анализ…"; _errorBox.Visible = false; }
        UseWaitCursor = busy;
    }

    private void SaveSettings()
    {
        _settings.ExePath = _exeBox.Text.Trim();
        _settings.ExtraArguments = _extraBox.Text;
        _settings.SaveToFile = _saveRadio.Checked;
        _settings.Bom = _bomCheck.Checked;
        _settings.Save();
    }
}
