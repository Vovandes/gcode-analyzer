namespace GcodeAnalyzer.Gui.Logic;

public readonly record struct ProjectedEdge(Point2 A, Point2 B, int SegmentIndex, int Tool, bool Rapid, bool Cycle);

/// <summary>Сетка в мм не перестраивается при zoom/pan. Длинные линии храним отдельно.</summary>
public sealed class HitTester
{
    private readonly ProjectedEdge[] _edges;
    private readonly Dictionary<(int, int), List<int>> _grid = new();
    private readonly List<int> _longEdges = [];
    private readonly double _cell;
    private readonly Point2 _origin;
    public int LastCandidateCount { get; private set; }

    public HitTester(ProjectedEdge[] edges)
    {
        _edges = edges;
        if (edges.Length == 0) { _cell = 1; return; }
        double minX = edges.Min(e => Math.Min(e.A.X, e.B.X)), minY = edges.Min(e => Math.Min(e.A.Y, e.B.Y));
        double maxX = edges.Max(e => Math.Max(e.A.X, e.B.X)), maxY = edges.Max(e => Math.Max(e.A.Y, e.B.Y));
        _origin = new(minX, minY);
        _cell = Math.Max(1e-6, Math.Max(maxX - minX, maxY - minY) / Math.Clamp(Math.Sqrt(edges.Length), 1, 512));
        for (int i = 0; i < edges.Length; i++)
        {
            var e = edges[i];
            var lo = Cell(new(Math.Min(e.A.X, e.B.X), Math.Min(e.A.Y, e.B.Y)));
            var hi = Cell(new(Math.Max(e.A.X, e.B.X), Math.Max(e.A.Y, e.B.Y)));
            if ((long)(hi.X - lo.X + 1) * (hi.Y - lo.Y + 1) > 4096) { _longEdges.Add(i); continue; }
            for (int x = lo.X; x <= hi.X; x++)
                for (int y = lo.Y; y <= hi.Y; y++)
                {
                    if (!_grid.TryGetValue((x, y), out var list)) _grid[(x, y)] = list = [];
                    list.Add(i);
                }
        }
    }

    private (int X, int Y) Cell(Point2 p) => ((int)Math.Clamp(Math.Floor((p.X - _origin.X) / _cell), -1e8, 1e8),
        (int)Math.Clamp(Math.Floor((p.Y - _origin.Y) / _cell), -1e8, 1e8));

    public int Find(Point2 screen, ViewTransform transform, double tolerancePixels = 5, ISet<int>? hiddenTools = null)
    {
        Point2 p = transform.ToWorld(screen);
        double tolerance = tolerancePixels / transform.Scale, best = tolerance * tolerance;
        var lo = Cell(new(p.X - tolerance, p.Y - tolerance));
        var hi = Cell(new(p.X + tolerance, p.Y + tolerance));
        var candidates = new HashSet<int>(_longEdges);
        // При очень далёком отдалении дешевле один проход, чем миллионы пустых ячеек.
        if ((long)(hi.X - lo.X + 1) * (hi.Y - lo.Y + 1) > 16384)
            candidates.UnionWith(Enumerable.Range(0, _edges.Length));
        else
            for (int x = lo.X; x <= hi.X; x++)
                for (int y = lo.Y; y <= hi.Y; y++)
                    if (_grid.TryGetValue((x, y), out var list)) candidates.UnionWith(list);
        LastCandidateCount = candidates.Count;
        int result = -1;
        foreach (int i in candidates)
        {
            var e = _edges[i];
            if (hiddenTools?.Contains(e.Tool) == true) continue;
            // Отметка отверстия имеет радиус 3 px, линия — обычный допуск 5 px.
            double distance = DistanceSquared(p, e.A, e.B);
            if (distance <= best && (distance < best || result < 0 || e.SegmentIndex < result))
            { best = distance; result = e.SegmentIndex; }
        }
        return result;
    }

    public static double DistanceSquared(Point2 p, Point2 a, Point2 b)
    {
        double dx = b.X - a.X, dy = b.Y - a.Y, length = dx * dx + dy * dy;
        double t = length == 0 ? 0 : Math.Clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length, 0, 1);
        double x = p.X - a.X - t * dx, y = p.Y - a.Y - t * dy;
        return x * x + y * y;
    }
}
