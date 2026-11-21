using System.Drawing.Imaging;
using GcodeAnalyzer.Gui.Controls;

namespace GcodeAnalyzer.Gui.Logic;

internal static class PdfTrajectoryRenderer
{
    private const int Dpi = 300;
    public const int WidthMm = 177;
    public const int HeightMm = 97;

    public static string Render(TrajectoryScene scene, ViewPlane plane)
    {
        var edges = scene.Views[(int)plane];
        int width = (int)Math.Ceiling(WidthMm * Dpi / 25.4);
        int height = (int)Math.Ceiling(HeightMm * Dpi / 25.4);
        using var bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb);
        bitmap.SetResolution(Dpi, Dpi);
        using var graphics = Graphics.FromImage(bitmap);
        graphics.Clear(Color.White);
        using var border = new Pen(Color.LightGray, 2);
        graphics.DrawRectangle(border, 1, 1, width - 2, height - 2);

        var transform = new ViewTransform();
        Point2 min = new(edges.Min(e => Math.Min(e.A.X, e.B.X)), edges.Min(e => Math.Min(e.A.Y, e.B.Y)));
        Point2 max = new(edges.Max(e => Math.Max(e.A.X, e.B.X)), edges.Max(e => Math.Max(e.A.Y, e.B.Y)));
        transform.Fit(min, max, width, height, padding: 45);
        using var renderer = new TrajectoryRenderer(edges, scene.PreviousSegmentCount);
        renderer.Draw(graphics, transform, new HashSet<int>(), pixelScale: 2);

        using var png = new MemoryStream();
        bitmap.Save(png, ImageFormat.Png);
        return "base64:" + Convert.ToBase64String(png.GetBuffer(), 0, checked((int)png.Length));
    }
}
