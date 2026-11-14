namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Мировые координаты в мм; экранная ось Y направлена вниз.</summary>
public sealed class ViewTransform
{
    public double Scale { get; private set; } = 1;
    public double OffsetX { get; private set; }
    public double OffsetY { get; private set; }
    public Point2 ToScreen(Point2 p) => new(p.X * Scale + OffsetX, -p.Y * Scale + OffsetY);
    public Point2 ToWorld(Point2 p) => new((p.X - OffsetX) / Scale, (OffsetY - p.Y) / Scale);
    public void Pan(double dx, double dy) { OffsetX += dx; OffsetY += dy; }

    public void Zoom(Point2 cursor, double factor)
    {
        if (!double.IsFinite(factor) || factor <= 0) return;
        Point2 anchor = ToWorld(cursor);
        Scale = Math.Clamp(Scale * factor, 1e-6, 1e6);
        OffsetX = cursor.X - anchor.X * Scale;
        OffsetY = cursor.Y + anchor.Y * Scale;
    }

    public void Fit(Point2 min, Point2 max, double width, double height, double padding = 35)
    {
        double dx = Math.Max(1, max.X - min.X), dy = Math.Max(1, max.Y - min.Y);
        Scale = Math.Clamp(Math.Min(Math.Max(1, width - padding * 2) / dx,
            Math.Max(1, height - padding * 2) / dy), 1e-6, 1e6);
        OffsetX = width / 2 - (min.X + max.X) / 2 * Scale;
        OffsetY = height / 2 + (min.Y + max.Y) / 2 * Scale;
    }
}
