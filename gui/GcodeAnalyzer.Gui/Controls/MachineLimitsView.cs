using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

/// <summary>Виртуальная таблица показывает проверки ядра без пересчёта пределов.</summary>
public sealed class MachineLimitsView : UserControl
{
    private readonly Label _summary = new() { Dock = DockStyle.Top, Height = 28, Text = "Анализ не выполнен" };
    private readonly DataGridView _table = new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, VirtualMode = true, AllowUserToAddRows = false,
        AllowUserToDeleteRows = false, RowHeadersVisible = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, MultiSelect = false,
    };
    private LimitCheck[] _checks = [];
    public MachineLimitsView()
    {
        foreach (string name in new[] { "Строка", "Параметр", "Значение в УП", "Предел", "Статус" })
            _table.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = name, SortMode = DataGridViewColumnSortMode.NotSortable });
        _table.CellValueNeeded += (_, e) =>
        {
            if ((uint)e.RowIndex >= (uint)_checks.Length) return;
            var c = _checks[e.RowIndex];
            e.Value = e.ColumnIndex switch { 0 => c.Line, 1 => c.Parameter, 2 => MachineLimits.Value(c.ValueMin, c.ValueMax),
                3 => MachineLimits.Value(c.LimitMin, c.LimitMax), _ => MachineLimits.Status(c.Status) };
        };
        _table.CellFormatting += (_, e) =>
        {
            if ((uint)e.RowIndex >= (uint)_checks.Length || e.CellStyle is null) return;
            string? severity = MachineLimits.Severity(_checks[e.RowIndex].Status);
            e.CellStyle.ForeColor = SeverityStyle.Foreground(severity);
            e.CellStyle.BackColor = SeverityStyle.Background(severity);
            e.CellStyle.SelectionForeColor = SeverityStyle.Foreground(severity);
            e.CellStyle.SelectionBackColor = SeverityStyle.Background(severity);
        };
        Controls.Add(_table); Controls.Add(_summary);
    }
    public void ShowChecks(LimitCheck[] checks)
    {
        _checks = checks;
        _table.RowCount = checks.Length;
        _table.Visible = checks.Length > 0;
        _table.Invalidate();
        _summary.Text = checks.Length == 0 ? "Пределы не заданы или нет данных для проверки" :
            $"Проверок: {checks.Length}; нарушений: {checks.Count(c => c.Status == "error")}";
    }
}
