namespace GcodeAnalyzer.Gui.Logic;

/// <summary>Только визуализация уже рассчитанной ядром дуги, без анализа G-кодов.</summary>
public static class ArcTessellator
{
    public static Point3[] Tessellate(Segment s)
    {
        Point3 start = Point3.From(s.Start), end = Point3.From(s.End);
        if (s.Kind != "arc" || s.Center is null || s.Cw is null || s.Radius is not > 0)
            return [start, end];
        // G18 ориентирована Z,X; порядок совпадает с ядром.
        var (u, v, w) = s.Plane switch { "G18" => (2, 0, 1), "G19" => (1, 2, 0), _ => (0, 1, 2) };
        if (s.Start[u] is not double su || s.Start[v] is not double sv ||
            s.End[u] is not double eu || s.End[v] is not double ev ||
            s.Center[u] is not double cu || s.Center[v] is not double cv) return [];
        double radius = s.Radius.Value;
        double a = Math.Atan2(sv - cv, su - cu), b = Math.Atan2(ev - cv, eu - cu);
        double sweep = b - a;
        if (s.Cw.Value) { while (sweep >= -1e-10) sweep -= 2 * Math.PI; }
        else { while (sweep <= 1e-10) sweep += 2 * Math.PI; }
        // Максимум 5 градусов; дополнительно ограничиваем стрелу хорды 0,05 мм.
        double step = Math.Min(Math.PI / 36, 2 * Math.Acos(Math.Clamp(1 - .05 / radius, -1, 1)));
        int count = (int)Math.Clamp(Math.Ceiling(Math.Abs(sweep) / Math.Max(step, 1e-5)), 1, 1_000_000);
        var points = new Point3[count + 1];
        points[0] = start;
        points[^1] = end;
        for (int i = 1; i < count; i++)
        {
            double t = (double)i / count, angle = a + sweep * t;
            double?[] p = new double?[3];
            p[u] = cu + radius * Math.Cos(angle);
            p[v] = cv + radius * Math.Sin(angle);
            p[w] = s.Start[w] is double sw && s.End[w] is double ew ? sw + (ew - sw) * t : null;
            points[i] = Point3.From(p);
        }
        return points;
    }
}
