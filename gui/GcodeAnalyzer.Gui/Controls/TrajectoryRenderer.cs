using System.Drawing.Drawing2D;
using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui.Controls;

/// <summary>GDI-пути кэшируются по T и типу линии; один DrawPath на группу.</summary>
public sealed class TrajectoryRenderer : IDisposable
{
    private readonly List<(int Tool, bool Rapid, bool Violation, bool Previous, GraphicsPath Path)> _paths = [];
    private readonly ProjectedEdge[] _cycles;
    private readonly Dictionary<int, ProjectedEdge[]> _bySegment;
    private readonly int _previousSegmentCount;
    public static Color PreviousColor { get; } = Color.Gray;
    public ProjectedEdge[] Edges { get; }
    public ProjectedEdge? FirstEdge(int segment) => _bySegment.TryGetValue(segment, out var edges) && edges.Length > 0 ? edges[0] : null;

    public TrajectoryRenderer(ProjectedEdge[] edges, int previousSegmentCount = 0)
    {
        Edges = edges;
        _previousSegmentCount = previousSegmentCount;
        foreach (var group in edges.Where(e => e.A != e.B)
            .GroupBy(e => (e.Tool, e.Rapid, e.LimitViolation, Previous: e.SegmentIndex < previousSegmentCount)))
        {
            var points = new List<PointF>();
            var types = new List<byte>();
            foreach (var edge in group)
            {
                points.Add(new((float)edge.A.X, (float)edge.A.Y)); types.Add((byte)PathPointType.Start);
                points.Add(new((float)edge.B.X, (float)edge.B.Y)); types.Add((byte)PathPointType.Line);
            }
            _paths.Add((group.Key.Tool, group.Key.Rapid, group.Key.LimitViolation, group.Key.Previous,
                new GraphicsPath(points.ToArray(), types.ToArray())));
        }
        _cycles = edges.Where(e => e.Cycle).ToArray();
        _bySegment = edges.GroupBy(e => e.SegmentIndex).ToDictionary(g => g.Key, g => g.ToArray());
    }

    public void Draw(Graphics graphics, ViewTransform transform, ISet<int> hidden, float pixelScale = 1)
    {
        var saved = graphics.Save();
        graphics.SmoothingMode = SmoothingMode.None;
        using var matrix = new Matrix((float)transform.Scale, 0, 0, (float)-transform.Scale,
            (float)transform.OffsetX, (float)transform.OffsetY);
        graphics.Transform = matrix;
        foreach (var item in _paths)
        {
            if (hidden.Contains(item.Tool)) continue;
            using var pen = new Pen(item.Previous ? PreviousColor : ToolPalette.ForTool(item.Tool),
                (float)((item.Previous ? 2.8 : 1.3) * pixelScale / transform.Scale));
            if (item.Violation && !item.Previous)
            {
                using var outline = new Pen(SeverityStyle.Foreground("error"), (float)(4.5 * pixelScale / transform.Scale));
                graphics.DrawPath(outline, item.Path);
            }
            if (item.Rapid) pen.DashStyle = DashStyle.Dash;
            graphics.DrawPath(pen, item.Path);
        }
        graphics.Restore(saved);
        foreach (var e in _cycles)
        {
            if (hidden.Contains(e.Tool)) continue;
            Point2 p = transform.ToScreen(e.A);
            bool previous = e.SegmentIndex < _previousSegmentCount;
            var color = previous ? PreviousColor : e.LimitViolation ? SeverityStyle.Foreground("error") : ToolPalette.ForTool(e.Tool);
            float radius = 3 * pixelScale;
            using var pen = new Pen(color, (previous || e.LimitViolation ? 3 : 1.3f) * pixelScale);
            graphics.DrawEllipse(pen, (float)p.X - radius, (float)p.Y - radius, radius * 2, radius * 2);
        }
    }

    public void DrawSelected(Graphics graphics, ViewTransform transform, int segment, ISet<int> hidden)
    {
        if (!_bySegment.TryGetValue(segment, out var edges) || edges.Length == 0 || hidden.Contains(edges[0].Tool)) return;
        bool previous = segment < _previousSegmentCount;
        using var outline = new Pen(!previous && edges[0].LimitViolation ? SeverityStyle.Foreground("error") : Color.Black, 5);
        using var highlight = new Pen(previous ? Color.Gainsboro : Color.Magenta, 3);
        foreach (var edge in edges)
        {
            Point2 a = transform.ToScreen(edge.A), b = transform.ToScreen(edge.B);
            graphics.DrawLine(outline, (float)a.X, (float)a.Y, (float)b.X, (float)b.Y);
            graphics.DrawLine(highlight, (float)a.X, (float)a.Y, (float)b.X, (float)b.Y);
            if (edge.Cycle || edge.A == edge.B)
            { graphics.DrawEllipse(outline, (float)a.X - 4, (float)a.Y - 4, 8, 8); graphics.DrawEllipse(highlight, (float)a.X - 4, (float)a.Y - 4, 8, 8); }
        }
    }

    public void Dispose() { foreach (var item in _paths) item.Path.Dispose(); }
}
