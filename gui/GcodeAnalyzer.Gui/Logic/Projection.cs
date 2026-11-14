namespace GcodeAnalyzer.Gui.Logic;

public enum ViewPlane { XY, XZ, YZ }
public readonly record struct Point2(double X, double Y);
public readonly record struct Point3(double? X, double? Y, double? Z)
{
    public static Point3 From(double?[] p) => new(p[0], p[1], p[2]);
    public double? Axis(int axis) => axis switch { 0 => X, 1 => Y, _ => Z };
}

public static class Projection
{
    public static Point2? Project(Point3 p, ViewPlane plane)
    {
        var (x, y) = plane switch
        {
            ViewPlane.XY => (p.X, p.Y), ViewPlane.XZ => (p.X, p.Z), _ => (p.Y, p.Z),
        };
        return x is double a && y is double b && double.IsFinite(a) && double.IsFinite(b)
            ? new Point2(a, b) : null;
    }

    public static string Axes(ViewPlane plane) => plane switch
    {
        ViewPlane.XY => "X / Y", ViewPlane.XZ => "X / Z", _ => "Y / Z",
    };
}
