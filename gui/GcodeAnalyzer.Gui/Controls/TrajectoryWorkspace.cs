using System.Globalization;
using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

/// <summary>Связывает виды, программу и таблицу; геометрия остаётся в Logic.</summary>
public class TrajectoryWorkspace : UserControl
{
    private readonly AppSettings _settings;
    private readonly TrajectoryView _primary = new() { Dock = DockStyle.Fill };
    private readonly TrajectoryView _secondary = new() { Dock = DockStyle.Fill };
    private readonly ProgramView _program = new() { Dock = DockStyle.Fill };
    private readonly SplitContainer _views = new() { Dock = DockStyle.Fill, Size = new(600, 500), Orientation = Orientation.Horizontal };
    private readonly SplitContainer _programSplit = new() { Dock = DockStyle.Fill, Size = new(1000, 500) };
    private readonly SplitContainer _warningsSplit = new() { Dock = DockStyle.Fill, Size = new(1000, 650), Orientation = Orientation.Horizontal };
    private readonly FlowLayoutPanel _legend = new() { Dock = DockStyle.Bottom, AutoSize = true, AutoScroll = true, MaximumSize = new(0, 90) };
    private readonly DataGridView _warnings = new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, VirtualMode = true, AllowUserToAddRows = false,
        AllowUserToDeleteRows = false, RowHeadersVisible = false, MultiSelect = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, AutoGenerateColumns = false,
        BackgroundColor = Color.White,
    };
    private readonly Label _warningCount = new() { Dock = DockStyle.Top, Height = 24, Text = "Предупреждения: 0" };
    private readonly List<RadioButton> _viewButtons = [];
    private TrajectoryScene? _scene;
    private ProgramLine[] _lines = [];
    private WarningItem[] _warningItems = [];
    private int _mode, _selected = -1;
    public event Action<string>? StatusChanged;

    public TrajectoryWorkspace(AppSettings settings)
    {
        _settings = settings;
        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
        string[] titles = ["Сверху XY", "Спереди XZ", "Сбоку YZ", "Сверху + сбоку"];
        for (int i = 0; i < titles.Length; i++)
        {
            int mode = i;
            var button = new RadioButton { Text = titles[i], Appearance = Appearance.Button, AutoSize = true };
            button.CheckedChanged += (_, _) =>
            {
                if (button.Checked)
                {
                    if (_scene is not null) SaveLayout();
                    SetMode(mode);
                    if (mode == 3) SetRatio(_views, _settings.ViewsSplitRatio);
                }
            };
            _viewButtons.Add(button); toolbar.Controls.Add(button);
        }
        var fit = new Button { Text = "Показать всё", AutoSize = true };
        fit.Click += (_, _) => { _primary.FitAll(); _secondary.FitAll(); };
        var below = new CheckBox { Text = "Программа снизу", AutoSize = true, Checked = settings.ProgramBelow };
        below.CheckedChanged += (_, _) =>
        {
            SaveLayout();
            _programSplit.Orientation = below.Checked ? Orientation.Horizontal : Orientation.Vertical;
            ApplyRatios();
        };
        toolbar.Controls.AddRange([fit, below]);
        _views.Panel1.Controls.Add(_primary);
        _views.Panel2.Controls.Add(_secondary);
        var visual = new Panel { Dock = DockStyle.Fill };
        visual.Controls.Add(_views);
        visual.Controls.Add(_legend);
        _programSplit.Panel1.Controls.Add(visual);
        _programSplit.Panel2.Controls.Add(_program);
        _programSplit.Orientation = settings.ProgramBelow ? Orientation.Horizontal : Orientation.Vertical;
        _warningsSplit.Panel1.Controls.Add(_programSplit);
        var warningPanel = new Panel { Dock = DockStyle.Fill };
        warningPanel.Controls.Add(_warnings);
        warningPanel.Controls.Add(_warningCount);
        _warningsSplit.Panel2.Controls.Add(warningPanel);
        Controls.Add(_warningsSplit);
        Controls.Add(toolbar);

        foreach (var (name, title, width) in new[] { ("Line", "Строка", 70), ("Code", "Код", 80), ("Severity", "Уровень", 150), ("Message", "Сообщение", 600) })
            _warnings.Columns.Add(new DataGridViewTextBoxColumn { Name = name, HeaderText = title, Width = width,
                AutoSizeMode = name == "Message" ? DataGridViewAutoSizeColumnMode.Fill : DataGridViewAutoSizeColumnMode.None,
                SortMode = DataGridViewColumnSortMode.NotSortable });
        _warnings.CellValueNeeded += (_, e) =>
        {
            if (e.RowIndex < 0 || e.RowIndex >= _warningItems.Length) return;
            var w = _warningItems[e.RowIndex];
            e.Value = e.ColumnIndex switch { 0 => w.Line, 1 => w.Code, 2 => SeverityStyle.Label(w.Severity), _ => w.Message };
        };
        _warnings.CellFormatting += (_, e) =>
        {
            if (e.RowIndex < 0 || e.RowIndex >= _warningItems.Length) return;
            string severity = _warningItems[e.RowIndex].Severity;
            e.CellStyle.BackColor = SeverityStyle.Background(severity);
            e.CellStyle.ForeColor = SeverityStyle.Foreground(severity);
            e.CellStyle.SelectionBackColor = SeverityStyle.Background(severity);
            e.CellStyle.SelectionForeColor = Color.Black;
        };
        _warnings.CellClick += (_, e) => { if (e.RowIndex >= 0) GoToWarning(e.RowIndex); };
        _warnings.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter && _warnings.CurrentCell is { } cell)
            { GoToWarning(cell.RowIndex); e.SuppressKeyPress = true; }
        };
        _primary.SegmentSelected += SelectSegment;
        _secondary.SegmentSelected += SelectSegment;
        _program.LineSelected += SelectLine;
        _viewButtons[Math.Clamp(settings.ViewMode, 0, 3)].Checked = true;
        Load += (_, _) => ApplyRatios();
    }

    private void ApplyRatios()
    {
        SetRatio(_programSplit, _settings.ProgramSplitRatio);
        SetRatio(_warningsSplit, _settings.WarningsSplitRatio);
        SetRatio(_views, _settings.ViewsSplitRatio);
    }

    private static void SetRatio(SplitContainer split, double ratio)
    {
        int length = split.Orientation == Orientation.Vertical ? split.Width : split.Height;
        if (length > split.Panel1MinSize + split.Panel2MinSize + split.SplitterWidth)
            split.SplitterDistance = Math.Clamp((int)(length * (double.IsFinite(ratio) ? Math.Clamp(ratio, .15, .85) : .6)),
                split.Panel1MinSize, length - split.Panel2MinSize - split.SplitterWidth);
    }

    private static double Ratio(SplitContainer split) => (double)split.SplitterDistance /
        Math.Max(1, split.Orientation == Orientation.Vertical ? split.Width : split.Height);

    public void SaveLayout()
    {
        _settings.ViewMode = _mode;
        _settings.ProgramBelow = _programSplit.Orientation == Orientation.Horizontal;
        _settings.ProgramSplitRatio = Ratio(_programSplit);
        _settings.WarningsSplitRatio = Ratio(_warningsSplit);
        if (!_views.Panel2Collapsed) _settings.ViewsSplitRatio = Ratio(_views);
    }

    private void SetMode(int mode)
    {
        _mode = mode;
        _views.Panel2Collapsed = mode != 3;
        _primary.SetScene(_scene, mode == 3 ? ViewPlane.XY : (ViewPlane)mode);
        _secondary.SetScene(_scene, ViewPlane.YZ);
        _primary.SelectSegment(_selected); _secondary.SelectSegment(_selected);
    }

    public void SetAnalysis(TrajectoryScene scene, ProgramLine[] lines, WarningItem[] warnings)
    {
        _scene = scene; _lines = lines; _warningItems = warnings; _selected = -1;
        _program.SetLines(lines);
        foreach (Control control in _legend.Controls.Cast<Control>().ToArray()) control.Dispose();
        _legend.Controls.Clear();
        foreach (int tool in scene.Segments.Select(s => s.Tool).Distinct().Order())
        {
            var check = new CheckBox { Text = $"T{tool}", Tag = tool, Checked = true, AutoSize = true,
                ForeColor = ToolPalette.ForTool(tool), Margin = new(8, 3, 8, 3) };
            check.CheckedChanged += (_, _) => UpdateVisibility();
            _legend.Controls.Add(check);
        }
        _primary.SetHiddenTools([]); _secondary.SetHiddenTools([]);
        SetMode(_mode);
        _warnings.RowCount = warnings.Length;
        _warnings.Invalidate();
        _warningCount.Text = $"Предупреждения: {warnings.Length} (ошибок: {warnings.Count(w => w.Severity == "error")}) — клик или Enter: перейти к кадру";
    }

    public void ClearAnalysis()
    {
        _scene = null; _lines = []; _warningItems = []; _selected = -1;
        _program.SetLines([]); _warnings.RowCount = 0; _warningCount.Text = "Предупреждения: 0";
        _primary.SetScene(null, ViewPlane.XY); _secondary.SetScene(null, ViewPlane.YZ);
        foreach (Control control in _legend.Controls.Cast<Control>().ToArray()) control.Dispose();
        _legend.Controls.Clear();
    }

    private void UpdateVisibility()
    {
        int[] hidden = _legend.Controls.OfType<CheckBox>().Where(c => !c.Checked).Select(c => (int)c.Tag!).ToArray();
        _primary.SetHiddenTools(hidden); _secondary.SetHiddenTools(hidden);
    }

    private void GoToWarning(int row)
    { if (row >= 0 && row < _warningItems.Length) SelectLine(_warningItems[row].Line); }

    private void SelectLine(int line)
    {
        if (_scene is not null && _scene.SegmentByLine.TryGetValue(line, out int segment)) SelectSegment(segment);
        else
        {
            _selected = -1;
            _program.SelectLine(line);
            _primary.SelectSegment(-1); _secondary.SelectSegment(-1);
            StatusChanged?.Invoke($"Строка {line}: {Frame(line)} | нет сегмента перемещения");
        }
    }

    private string Frame(int line) => line > 0 && line <= _lines.Length ? _lines[line - 1].Text : "";

    private void SelectSegment(int index)
    {
        if (_scene is null || index < 0 || index >= _scene.Segments.Length) return;
        _selected = index;
        Segment s = _scene.Segments[index];
        foreach (var check in _legend.Controls.OfType<CheckBox>())
            if ((int)check.Tag! == s.Tool) check.Checked = true;
        _program.SelectLine(s.Line);
        _primary.SelectSegment(index); _secondary.SelectSegment(index);
        _primary.EnsureSelectedVisible(index); _secondary.EnsureSelectedVisible(index);
        static string Number(double? value) => value?.ToString("G8", CultureInfo.CurrentCulture) ?? "—";
        StatusChanged?.Invoke($"Строка {s.Line}: {Frame(s.Line)} | T{s.Tool} | F {Number(s.Feed)} мм/мин | S {Number(s.Spindle)} об/мин");
    }
}
