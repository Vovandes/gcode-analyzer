using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

public sealed class ToolDetailsView : UserControl
{
    private readonly DataGridView _grid = new()
    {
        Dock = DockStyle.Fill, ReadOnly = true, AllowUserToAddRows = false, AllowUserToDeleteRows = false,
        RowHeadersVisible = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect, MultiSelect = false, VirtualMode = true,
        BackgroundColor = SystemColors.Window,
    };
    private readonly Font _bold;
    private ToolDetailsRow[] _rows = [];
    private int _sortColumn = -1;
    private bool _descending;

    public ToolDetailsView()
    {
        _bold = new Font(_grid.Font, FontStyle.Bold);
        foreach (var column in ToolDetailsTable.Columns)
        {
            _grid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = column.Title, SortMode = DataGridViewColumnSortMode.Programmatic,
                DefaultCellStyle = new DataGridViewCellStyle { Format = column.Format ?? "", NullValue = "—" },
            });
        }
        _grid.Columns[0].Frozen = true;
        _grid.CellValueNeeded += (_, e) =>
        {
            if ((uint)e.RowIndex < (uint)_rows.Length)
                e.Value = ToolDetailsTable.Columns[e.ColumnIndex].Value(_rows[e.RowIndex]);
        };
        _grid.CellFormatting += (_, e) =>
        {
            if ((uint)e.RowIndex >= (uint)_rows.Length || e.CellStyle is null) return;
            if (_rows[e.RowIndex].IsTotal) e.CellStyle.Font = _bold;
            else if (e.ColumnIndex == 0 && _rows[e.RowIndex].Tool is ToolInfo tool)
            {
                e.Value = "● " + e.Value;
                e.CellStyle.ForeColor = ToolPalette.ForTool(tool.Tool);
                e.CellStyle.SelectionForeColor = e.CellStyle.ForeColor;
                e.CellStyle.SelectionBackColor = SystemColors.ControlLight;
            }
        };
        _grid.ColumnHeaderMouseClick += (_, e) => SortBy(e.ColumnIndex);
        Controls.Add(_grid);
    }

    public void ShowTools(AnalysisData? analysis)
    {
        _rows = ToolDetailsTable.Rows(analysis);
        if (_sortColumn >= 0) _rows = ToolDetailsTable.Sort(_rows, _sortColumn, _descending);
        _grid.RowCount = _rows.Length;
        _grid.Invalidate();
    }

    private void SortBy(int column)
    {
        if (_sortColumn >= 0) _grid.Columns[_sortColumn].HeaderCell.SortGlyphDirection = SortOrder.None;
        _descending = _sortColumn == column && !_descending;
        _sortColumn = column;
        _rows = ToolDetailsTable.Sort(_rows, column, _descending);
        _grid.Columns[column].HeaderCell.SortGlyphDirection = _descending ? SortOrder.Descending : SortOrder.Ascending;
        _grid.ClearSelection();
        _grid.Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _bold.Dispose();
        base.Dispose(disposing);
    }
}
