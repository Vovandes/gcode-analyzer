using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

public class TrajectoryView : UserControl
{
    private TrajectoryScene? _scene;
    private TrajectoryRenderer? _renderer;
    private ViewPlane _plane;
    private Bitmap? _cache;
    private bool _dirty = true, _dragging;
    private Point _down, _last;
    private int _selected = -1;
    private readonly HashSet<int> _hidden = [];
    public ViewTransform Transform { get; } = new();
    public event Action<int>? SegmentSelected;

    public TrajectoryView()
    {
        DoubleBuffered = true;
        BackColor = Color.White;
        TabStop = true;
        SetStyle(ControlStyles.StandardDoubleClick, true);
        AccessibleName = "Траектория: колесо — масштаб, перетаскивание — сдвиг, двойной клик — показать всё";
    }

    public void SetScene(TrajectoryScene? scene, ViewPlane plane)
    {
        _scene = scene; _plane = plane; _selected = -1;
        _renderer?.Dispose();
        _renderer = scene is null ? null : new TrajectoryRenderer(scene.Views[(int)plane]);
        FitAll();
    }

    public void SetHiddenTools(IEnumerable<int> tools)
    { _hidden.Clear(); _hidden.UnionWith(tools); Redraw(); }

    public void SelectSegment(int index) { _selected = index; Invalidate(); }

    public void EnsureSelectedVisible(int index)
    {
        var edge = _renderer?.FirstEdge(index);
        if (edge is not { } e || e.SegmentIndex != index) return;
        Point2 p = Transform.ToScreen(new((e.A.X + e.B.X) / 2, (e.A.Y + e.B.Y) / 2));
        if (p.X < 10 || p.Y < 10 || p.X > Width - 10 || p.Y > Height - 10)
        { Transform.Pan(Width / 2.0 - p.X, Height / 2.0 - p.Y); Redraw(); }
    }

    public void FitAll()
    {
        var edges = _renderer?.Edges.Where(e => !_hidden.Contains(e.Tool)).ToArray() ?? [];
        Point2 min = new(0, 0), max = new(1, 1);
        if (edges.Length > 0)
        {
            min = new(edges.Min(e => Math.Min(e.A.X, e.B.X)), edges.Min(e => Math.Min(e.A.Y, e.B.Y)));
            max = new(edges.Max(e => Math.Max(e.A.X, e.B.X)), edges.Max(e => Math.Max(e.A.Y, e.B.Y)));
        }
        Transform.Fit(min, max, ClientSize.Width, ClientSize.Height, 24);
        Redraw();
    }

    private void Redraw() { _dirty = true; Invalidate(); }
    protected override void OnResize(EventArgs e) { base.OnResize(e); Redraw(); }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (Width < 1 || Height < 1) return;
        if (_cache is null || _cache.Size != ClientSize)
        { _cache?.Dispose(); _cache = new Bitmap(Width, Height); _dirty = true; }
        if (_dirty)
        {
            using Graphics g = Graphics.FromImage(_cache);
            g.Clear(BackColor);
            _renderer?.Draw(g, Transform, _hidden);
            DrawScale(g);
            _dirty = false;
        }
        e.Graphics.DrawImageUnscaled(_cache, 0, 0);
        _renderer?.DrawSelected(e.Graphics, Transform, _selected, _hidden);
    }

    private void DrawScale(Graphics graphics)
    {
        double raw = 100 / Transform.Scale;
        double power = Math.Pow(10, Math.Floor(Math.Log10(raw)));
        double length = (raw / power >= 5 ? 5 : raw / power >= 2 ? 2 : 1) * power;
        float pixels = (float)(length * Transform.Scale);
        using var pen = new Pen(Color.DimGray);
        graphics.DrawLine(pen, 15, Height - 20, 15 + pixels, Height - 20);
        graphics.DrawLine(pen, 15, Height - 24, 15, Height - 16);
        graphics.DrawLine(pen, 15 + pixels, Height - 24, 15 + pixels, Height - 16);
        TextRenderer.DrawText(graphics, $"{Projection.Axes(_plane)}, мм ↑ →   |   {length:G4} мм", Font,
            new Point(12, Height - 43), Color.DimGray, Color.White);
        if (_renderer is null || _renderer.Edges.Length == 0)
            TextRenderer.DrawText(graphics, "Нет известной геометрии для этого вида", Font, new Point(15, 15), Color.DimGray);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        Transform.Zoom(new(e.X, e.Y), Math.Pow(1.2, e.Delta / 120.0));
        Redraw();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e); Focus();
        if (e.Button is MouseButtons.Left or MouseButtons.Middle)
        { _down = _last = e.Location; _dragging = false; Capture = true; }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!Capture || e.Button is not (MouseButtons.Left or MouseButtons.Middle)) return;
        if (!_dragging && Math.Abs(e.X - _down.X) + Math.Abs(e.Y - _down.Y) > 3) _dragging = true;
        if (_dragging) { Transform.Pan(e.X - _last.X, e.Y - _last.Y); Redraw(); }
        _last = e.Location;
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (Capture && !_dragging && e.Button == MouseButtons.Left && _scene is not null)
        {
            int index = _scene.HitTests[(int)_plane].Find(new(e.X, e.Y), Transform, hiddenTools: _hidden);
            _selected = index;
            if (index >= 0) SegmentSelected?.Invoke(index);
            Invalidate();
        }
        Capture = false;
    }

    protected override void OnMouseDoubleClick(MouseEventArgs e) { base.OnMouseDoubleClick(e); FitAll(); }
    protected override void Dispose(bool disposing)
    { if (disposing) { _cache?.Dispose(); _renderer?.Dispose(); } base.Dispose(disposing); }
}
