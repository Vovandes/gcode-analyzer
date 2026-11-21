using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

public sealed class ComparisonWorkspace : UserControl
{
    private readonly TabControl _tabs = new() { Dock = DockStyle.Fill };
    private readonly DataGridView _summary = CreateGrid("Сводка сравнения");
    private readonly DataGridView _differences = CreateGrid("Расхождения траектории");
    private readonly Label _files = new() { Dock = DockStyle.Top, Height = 26, AutoEllipsis = true };
    private readonly Label _beforeLabel = new() { Dock = DockStyle.Top, Height = 25, AutoEllipsis = true };
    private readonly Label _afterLabel = new() { Dock = DockStyle.Top, Height = 25, AutoEllipsis = true };
    private readonly ProgramView _before = new() { Dock = DockStyle.Fill, Name = "beforeProgram" };
    private readonly ProgramView _after = new() { Dock = DockStyle.Fill, Name = "afterProgram" };
    private readonly TrajectoryView _view = new() { Dock = DockStyle.Fill };
    private readonly TextBox _report = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, WordWrap = false, ScrollBars = ScrollBars.Both,
        Font = new Font("Consolas", 10),
    };
    private readonly ComboBox _projection = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140 };
    private readonly Dictionary<int, PointDifference> _byOldLine = [];
    private readonly Dictionary<int, PointDifference> _byNewLine = [];
    private TrajectoryOverlay? _overlay;
    private (int? OldLine, int? NewLine) _selected;

    public ComparisonWorkspace()
    {
        AddColumns(_summary, ("Показатель", 30), ("Было", 24), ("Стало", 24), ("Разница", 22));
        AddColumns(_differences, ("Инструмент", 22), ("Траектория", 42), ("Было", 18), ("Стало", 18));
        var summaryTab = new TabPage("Сводка");
        summaryTab.Controls.Add(_summary);
        summaryTab.Controls.Add(_files);

        var overlayTab = new TabPage("Наложение 2D");
        var top = new SplitContainer { Dock = DockStyle.Fill, Size = new(1000, 360), SplitterDistance = 610 };
        var programs = new SplitContainer { Dock = DockStyle.Fill, Size = new(1000, 280), SplitterDistance = 500 };
        var split = new SplitContainer
        {
            Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, Size = new(1000, 640), SplitterDistance = 360,
        };
        top.Panel1.Controls.Add(_view);
        top.Panel2.Controls.Add(_differences);
        programs.Panel1.Controls.Add(_before);
        programs.Panel1.Controls.Add(_beforeLabel);
        programs.Panel2.Controls.Add(_after);
        programs.Panel2.Controls.Add(_afterLabel);
        split.Panel1.Controls.Add(top);
        split.Panel2.Controls.Add(programs);
        overlayTab.Controls.Add(split);
        overlayTab.Controls.Add(BuildToolbar());
        overlayTab.Enter += (_, _) => _view.FitAll();

        var reportTab = new TabPage("Отчёт сравнения");
        reportTab.Controls.Add(_report);
        _tabs.TabPages.AddRange([summaryTab, overlayTab, reportTab]);
        Controls.Add(_tabs);

        _differences.CellClick += (_, e) => SelectDifferenceRow(e.RowIndex);
        _differences.KeyDown += (_, e) =>
        {
            if (e.KeyCode != Keys.Enter || _differences.CurrentRow is not { } row) return;
            SelectDifferenceRow(row.Index);
            e.SuppressKeyPress = true;
        };
        _view.SegmentSelected += index =>
        {
            if (_overlay is null) return;
            var (oldLine, newLine) = _overlay.ForSegment(index);
            if (oldLine is int beforeLine) SelectProgramLine(beforeLine, previous: true);
            else if (newLine is int afterLine) SelectProgramLine(afterLine, previous: false);
        };
        _before.LineSelected += line => SelectProgramLine(line, previous: true);
        _after.LineSelected += line => SelectProgramLine(line, previous: false);
    }

    public void SetComparison(ComparisonData data, string report, TrajectoryOverlay overlay,
        ProgramLine[] before, ProgramLine[] after)
    {
        _beforeLabel.Text = $"Старая УП · {data.Before.File.Name}";
        _afterLabel.Text = $"Новая УП · {data.After.File.Name}";
        _files.Text = $"  {data.Before.File.Name} → {data.After.File.Name}  ·  Допуск XYZ {data.Tolerance:G} мм";
        _summary.Rows.Clear();
        foreach (var row in data.Summary)
        {
            var result = _summary.Rows[_summary.Rows.Add(row.Label, row.Before, row.After, row.Difference)].Cells[3];
            var color = row.Trend switch { "improved" => Color.DarkGreen, "worsened" => Color.Firebrick, _ => Color.Black };
            result.Style.ForeColor = result.Style.SelectionForeColor = color;
        }
        _summary.ClearSelection();

        _differences.Rows.Clear();
        _byOldLine.Clear();
        _byNewLine.Clear();
        foreach (var tool in data.Trajectory)
        {
            string status = tool.Presence switch
            {
                "added" => "инструмент добавлен",
                "removed" => "инструмент пропал",
                _ when tool.DifferenceCount == 0 => "совпадает",
                _ => $"Расхождений: {tool.DifferenceCount} (видно {tool.Differences.Length})",
            };
            _differences.Rows.Add(tool.Tool, status);
            foreach (var point in tool.Differences)
            {
                string kind = point.Kind switch { "inserted" => "вставлена", "deleted" => "удалена", _ => "изменена" };
                int index = _differences.Rows.Add(tool.Tool, kind, point.OldLine?.ToString() ?? "—", point.NewLine?.ToString() ?? "—");
                _differences.Rows[index].Tag = point;
                if (point.OldLine is int oldLine) _byOldLine.TryAdd(oldLine, point);
                if (point.NewLine is int newLine) _byNewLine.TryAdd(newLine, point);
            }
        }
        _differences.ClearSelection();
        _report.Text = ReportParser.NormalizeNewlines(report);
        _report.SelectionStart = 0;
        _before.SetLines(before);
        _after.SetLines(after);
        _overlay = overlay;
        _selected = default;
        SetProjection();
        _tabs.SelectedIndex = 0;
    }

    private Control BuildToolbar()
    {
        var tools = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(4) };
        _projection.Items.AddRange(["Сверху XY", "Спереди XZ", "Сбоку YZ"]);
        _projection.SelectedIndex = 0;
        _projection.SelectedIndexChanged += (_, _) => SetProjection();
        var fit = new Button { Text = "Показать всё", AutoSize = true };
        fit.Click += (_, _) => _view.FitAll();
        tools.Controls.AddRange([_projection, fit,
            new Label { Text = "Старая УП — серая", ForeColor = Color.DimGray, AutoSize = true, Anchor = AnchorStyles.Left },
            new Label { Text = "Новая УП — цвета инструментов", AutoSize = true, Anchor = AnchorStyles.Left }]);
        return tools;
    }

    private static DataGridView CreateGrid(string name) => new()
    {
        Name = name, AccessibleName = name, Dock = DockStyle.Fill, ReadOnly = true, AllowUserToAddRows = false,
        AllowUserToDeleteRows = false, RowHeadersVisible = false, MultiSelect = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, BackgroundColor = Color.White,
        AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
        DefaultCellStyle = new DataGridViewCellStyle
        {
            SelectionBackColor = Color.FromArgb(220, 234, 249), SelectionForeColor = Color.Black,
        },
    };

    private static void AddColumns(DataGridView grid, params (string Header, float Weight)[] columns)
    {
        foreach (var column in columns)
            grid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = column.Header, FillWeight = column.Weight, SortMode = DataGridViewColumnSortMode.NotSortable,
            });
    }

    private void SetProjection()
    {
        _view.SetScene(_overlay?.Scene, (ViewPlane)_projection.SelectedIndex);
        SelectGeometry();
    }

    private void SelectDifferenceRow(int row)
    {
        if (row >= 0 && _differences.Rows[row].Tag is PointDifference difference)
            SelectLines(difference.OldLine, difference.NewLine);
    }

    private void SelectProgramLine(int line, bool previous)
    {
        var map = previous ? _byOldLine : _byNewLine;
        if (map.TryGetValue(line, out var difference)) SelectLines(difference.OldLine, difference.NewLine);
        else if (previous) SelectLines(line, null);
        else SelectLines(null, line);
    }

    private void SelectLines(int? oldLine, int? newLine)
    {
        _selected = (oldLine, newLine);
        if (oldLine is int a) _before.SelectLine(a);
        else _before.ClearSelection();
        if (newLine is int b) _after.SelectLine(b);
        else _after.ClearSelection();
        SelectGeometry();
    }

    private void SelectGeometry()
    {
        if (_overlay is null) return;
        var (oldSegment, newSegment) = _overlay.ForLines(_selected.OldLine, _selected.NewLine);
        _view.SelectSegments(oldSegment, newSegment);
        _view.EnsureSelectedVisible(newSegment >= 0 ? newSegment : oldSegment);
    }
}
