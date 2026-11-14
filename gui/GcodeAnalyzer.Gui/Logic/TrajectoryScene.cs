namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Кэш трёх проекций готовится в рабочем потоке один раз на анализ.</summary>
public sealed class TrajectoryScene
{
    public Segment[] Segments { get; }
    public ProjectedEdge[][] Views { get; }
    public HitTester[] HitTests { get; }
    public Dictionary<int, int> SegmentByLine { get; }
    private TrajectoryScene(Segment[] segments, ProjectedEdge[][] views)
    {
        Segments = segments;
        Views = views;
        HitTests = views.Select(v => new HitTester(v)).ToArray();
        SegmentByLine = new();
        for (int i = 0; i < segments.Length; i++) SegmentByLine.TryAdd(segments[i].Line, i);
    }

    public static TrajectoryScene Build(Segment[] segments)
    {
        List<ProjectedEdge>[] views = [[], [], []];
        for (int index = 0; index < segments.Length; index++)
        {
            Segment s = segments[index];
            if (s.Kind is "home" or "dwell") continue;
            Point3[] points;
            if (s.Kind == "cycle" && s.Cycle is { } cycle)
                points = [new(s.End[0], s.End[1], cycle.ZR), new(s.End[0], s.End[1], cycle.ZBottom)];
            else points = ArcTessellator.Tessellate(s);
            foreach (ViewPlane plane in Enum.GetValues<ViewPlane>())
                for (int j = 1; j < points.Length; j++)
                    if (Projection.Project(points[j - 1], plane) is { } a && Projection.Project(points[j], plane) is { } b)
                        views[(int)plane].Add(new(a, b, index, s.Tool, s.Kind == "rapid", s.Kind == "cycle"));
        }
        return new(segments, views.Select(v => v.ToArray()).ToArray());
    }
}
