namespace GcodeAnalyzer.Gui.Logic;

public sealed class TrajectoryOverlay
{
    private readonly Dictionary<int, int> _afterByLine = [];
    public TrajectoryScene Scene { get; }

    private TrajectoryOverlay(TrajectoryScene scene)
    {
        Scene = scene;
        for (int i = scene.PreviousSegmentCount; i < scene.Segments.Length; i++)
            _afterByLine.TryAdd(scene.Segments[i].Line, i);
    }

    public static TrajectoryOverlay Build(Segment[] before, Segment[] after) =>
        new(TrajectoryScene.Build([.. before, .. after], before.Length));

    public (int OldSegment, int NewSegment) ForLines(int? oldLine, int? newLine)
    {
        int oldSegment = Scene.SegmentByLine.GetValueOrDefault(oldLine ?? 0, -1);
        if (oldSegment >= Scene.PreviousSegmentCount) oldSegment = -1;
        return (oldSegment, _afterByLine.GetValueOrDefault(newLine ?? 0, -1));
    }

    public (int? OldLine, int? NewLine) ForSegment(int index)
    {
        int line = Scene.Segments[index].Line;
        return index < Scene.PreviousSegmentCount ? (line, null) : (null, line);
    }
}
