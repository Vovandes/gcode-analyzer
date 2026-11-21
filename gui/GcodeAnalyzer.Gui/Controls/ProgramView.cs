using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

/// <summary>Рисуем только видимые строки. Выделение строк: мышь/Shift, копирование: Ctrl+C.</summary>
public class ProgramView : UserControl
{
    private readonly VScrollBar _vertical = new() { Dock = DockStyle.Right };
    private readonly HScrollBar _horizontal = new() { Dock = DockStyle.Bottom };
    private ProgramLine[] _lines = [];
    private int _selected = -1, _anchor = -1;
    private int _lineHeight = 20, _gutter = 65, _maxColumns;
    private float _characterWidth = 8;
    public event Action<int>? LineSelected;
    public int SelectedLine => _selected + 1;
    public int TopLine => _vertical.Value + 1;
    public int LastPaintedLines { get; private set; }

    public ProgramView()
    {
        DoubleBuffered = true;
        BackColor = Color.White;
        Font = new Font("Consolas", 10);
        TabStop = true;
        Controls.Add(_vertical);
        Controls.Add(_horizontal);
        _vertical.ValueChanged += (_, _) => Invalidate();
        _horizontal.ValueChanged += (_, _) => Invalidate();
        AccessibleName = "Программа: строки УП; Ctrl+C — копировать, Shift — выделить диапазон";
    }

    public void SetLines(ProgramLine[] lines)
    {
        _lines = lines;
        _selected = _anchor = -1;
        _vertical.Value = _horizontal.Value = 0;
        _maxColumns = lines.Length == 0 ? 0 : lines.Max(l => l.Text.Length);
        UpdateScrollbars();
        Invalidate();
    }

    private int PageLines => Math.Max(1, (ClientSize.Height - _horizontal.Height) / _lineHeight);
    private int ScrollLimit => Math.Max(0, _vertical.Maximum - _vertical.LargeChange + 1);

    private void UpdateScrollbars()
    {
        _lineHeight = Math.Max(1, Font.Height + 4);
        _characterWidth = TextRenderer.MeasureText(new string('M', 20), Font, Size.Empty,
            TextFormatFlags.NoPadding | TextFormatFlags.SingleLine).Width / 20f;
        _gutter = (int)(_characterWidth * Math.Max(6, (_lines.Length + 1).ToString().Length + 2));
        _vertical.LargeChange = PageLines;
        _vertical.Maximum = Math.Max(0, _lines.Length - 1);
        _vertical.Value = Math.Min(_vertical.Value, ScrollLimit);
        _horizontal.LargeChange = Math.Max(1, ClientSize.Width - _vertical.Width - _gutter);
        _horizontal.Maximum = Math.Max(0, (int)(_maxColumns * _characterWidth));
        _horizontal.Value = Math.Min(_horizontal.Value, Math.Max(0, _horizontal.Maximum - _horizontal.LargeChange + 1));
    }

    protected override void OnResize(EventArgs e) { base.OnResize(e); UpdateScrollbars(); Invalidate(); }

    public void SelectLine(int line, bool notify = false, bool extend = false)
    {
        if (_lines.Length == 0) return;
        _selected = Math.Clamp(line - 1, 0, _lines.Length - 1);
        if (!extend || _anchor < 0) _anchor = _selected;
        if (_selected < _vertical.Value || _selected >= _vertical.Value + PageLines)
            _vertical.Value = Math.Clamp(_selected - PageLines / 2, 0, ScrollLimit);
        Invalidate();
        if (notify) LineSelected?.Invoke(_selected + 1);
    }

    public void ClearSelection()
    {
        _selected = _anchor = -1;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        LastPaintedLines = 0;
        int width = ClientSize.Width - _vertical.Width;
        for (int row = 0, i = _vertical.Value; row <= PageLines && i < _lines.Length; row++, i++)
        {
            int y = row * _lineHeight;
            var line = _lines[i];
            var rect = new Rectangle(0, y, width, _lineHeight);
            using var background = new SolidBrush(SeverityStyle.Background(line.Severity));
            e.Graphics.FillRectangle(background, rect);
            using var gutter = new SolidBrush(Color.FromArgb(242, 244, 246));
            e.Graphics.FillRectangle(gutter, 0, y, _gutter, _lineHeight);
            TextRenderer.DrawText(e.Graphics, (i + 1).ToString(), Font, new Rectangle(0, y + 2, _gutter - 8, _lineHeight),
                Color.DimGray, TextFormatFlags.Right | TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
            var saved = e.Graphics.Save();
            e.Graphics.SetClip(new Rectangle(_gutter, y, Math.Max(0, width - _gutter), _lineHeight));
            foreach (var span in line.Spans)
            {
                TextRenderer.DrawText(e.Graphics, line.Text.Substring(span.Start, span.Length), Font,
                    new Point(_gutter + (int)(span.Start * _characterWidth) - _horizontal.Value, y + 2),
                    span.Comment ? Color.Gray : LineClassifier.Foreground(line.Kind),
                    TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine);
            }
            e.Graphics.Restore(saved);
            if (_selected >= 0 && i >= Math.Min(_anchor, _selected) && i <= Math.Max(_anchor, _selected))
            {
                using var pen = new Pen(Color.RoyalBlue, i == _selected ? 2 : 1);
                e.Graphics.DrawRectangle(pen, _gutter, y + 1, Math.Max(0, width - _gutter - 2), _lineHeight - 2);
            }
            LastPaintedLines++;
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        Focus();
        if (e.Button == MouseButtons.Left)
            SelectLine(_vertical.Value + e.Y / _lineHeight + 1, true, ModifierKeys.HasFlag(Keys.Shift));
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        _vertical.Value = Math.Clamp(_vertical.Value - e.Delta / 120 * 3, 0, ScrollLimit);
    }

    protected override bool IsInputKey(Keys keyData) =>
        (keyData & Keys.KeyCode) is Keys.Up or Keys.Down or Keys.PageUp or Keys.PageDown or Keys.Home or Keys.End || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (_lines.Length == 0) return;
        if (e.Control && e.KeyCode == Keys.C && _selected >= 0)
        {
            string text = string.Join(Environment.NewLine, _lines.Skip(Math.Min(_anchor, _selected))
                .Take(Math.Abs(_anchor - _selected) + 1).Select(l => l.Text));
            if (text.Length > 0) Clipboard.SetText(text);
            e.SuppressKeyPress = true;
            return;
        }
        if (e.Control && e.KeyCode == Keys.A)
        { _anchor = 0; _selected = _lines.Length - 1; Invalidate(); e.SuppressKeyPress = true; return; }
        int? line = e.KeyCode switch
        {
            Keys.Up => SelectedLine - 1, Keys.Down => SelectedLine + 1,
            Keys.PageUp => SelectedLine - PageLines, Keys.PageDown => SelectedLine + PageLines,
            Keys.Home => 1, Keys.End => _lines.Length, _ => null,
        };
        if (line is int target) { SelectLine(target, true, e.Shift); e.SuppressKeyPress = true; }
    }
}
