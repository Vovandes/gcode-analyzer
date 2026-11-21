using System.Drawing.Text;
using System.Globalization;
using MigraDoc.DocumentObjectModel;
using MigraDoc.DocumentObjectModel.Tables;
using MigraDoc.Rendering;
using PdfColor = MigraDoc.DocumentObjectModel.Color;

namespace GcodeAnalyzer.Gui.Logic;

public sealed record PdfComparison(string BeforeFileName, string Report);

public sealed class PdfReportWriter(Func<string, bool>? fontAvailable = null)
{
    private static readonly PdfColor Ink = new(30, 48, 64);
    private static readonly PdfColor Rule = new(211, 221, 228);
    private static readonly PdfColor Tint = new(236, 243, 247);
    private const string Missing = "—";

    public void Save(string path, AnalysisData data, string report, TrajectoryScene scene, DateTimeOffset date,
        PdfComparison? comparison = null)
    {
        string staged = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(path))!, $".gca-{Guid.NewGuid():N}.tmp");
        try
        {
            using (var output = new FileStream(staged, FileMode.CreateNew)) Write(output, data, report, scene, date, comparison);
            // Готовый отчёт заменяет файл только после успешной вёрстки и записи.
            File.Move(staged, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(staged)) File.Delete(staged);
        }
    }

    public void Write(Stream output, AnalysisData data, string report, TrajectoryScene scene, DateTimeOffset date,
        PdfComparison? comparison = null)
    {
        string font = SelectFont(fontAvailable ?? IsInstalled);
        var document = new Document();
        document.Info.Title = $"Отчёт анализа УП — {data.File.Name}";
        var normal = document.Styles[StyleNames.Normal]!;
        normal.Font.Name = font;
        normal.Font.Size = 9;
        normal.Font.Color = Ink;
        normal.ParagraphFormat.SpaceAfter = 2;

        var section = document.AddSection();
        section.PageSetup.PageFormat = PageFormat.A4;
        section.PageSetup.Orientation = MigraDoc.DocumentObjectModel.Orientation.Portrait;
        section.PageSetup.LeftMargin = Unit.FromMillimeter(16.5);
        section.PageSetup.RightMargin = Unit.FromMillimeter(16.5);
        section.PageSetup.TopMargin = Unit.FromMillimeter(14);
        section.PageSetup.BottomMargin = Unit.FromMillimeter(19);
        section.PageSetup.FooterDistance = Unit.FromMillimeter(8);
        AddFooter(section, data.File.Name);
        AddOverview(section, data, date, comparison);
        AddTools(section, data);
        AddDiagnostics(section, data);
        AddTrajectory(section, data, scene, comparison);
        AddCheatSheet(section, report);
        if (comparison is not null) AddComparison(section, comparison.Report);

        // PDFsharp по умолчанию встраивает подмножества использованных глифов TrueType.
        var renderer = new PdfDocumentRenderer { Document = document, Language = "ru-RU" };
        using var pdf = renderer.PdfDocument;
        renderer.RenderDocument();
        pdf.Save(output, closeStream: false);
    }

    private static string SelectFont(Func<string, bool> available)
    {
        if (available("Arial")) return "Arial";
        if (available("Segoe UI")) return "Segoe UI";
        throw new InvalidOperationException("Для отчёта PDF установите Arial или Segoe UI (обычное и полужирное начертания).");
    }

    private static bool IsInstalled(string name)
    {
        using var installed = new InstalledFontCollection();
        var families = installed.Families;
        try
        {
            return families.Any(f => f.Name.Equals(name, StringComparison.OrdinalIgnoreCase) &&
                f.IsStyleAvailable(FontStyle.Regular) && f.IsStyleAvailable(FontStyle.Bold));
        }
        finally
        {
            foreach (var family in families) family.Dispose();
        }
    }

    private static void AddFooter(Section section, string fileName)
    {
        var table = section.Footers.Primary.AddTable();
        table.AddColumn(Unit.FromMillimeter(154));
        table.AddColumn(Unit.FromMillimeter(23));
        table.Borders.Top.Color = Rule;
        table.Borders.Top.Width = 0.6;
        var row = table.AddRow();
        row.TopPadding = 4;
        row.Format.Font.Size = 8;
        row.Cells[0].AddParagraph(fileName);
        var number = row.Cells[1].AddParagraph("Стр. ");
        number.Format.Alignment = ParagraphAlignment.Right;
        number.AddPageField();
    }

    private static void AddOverview(Section section, AnalysisData data, DateTimeOffset date, PdfComparison? comparison)
    {
        Heading(section, "Отчёт анализа УП", 23);
        Heading(section, data.File.Name, 13);
        if (comparison is not null)
        {
            section.AddParagraph($"Старая УП: {comparison.BeforeFileName}");
            section.AddParagraph("Сводка, инструменты, диагностика и шпаргалка относятся к новой УП.");
        }
        section.AddParagraph($"Дата формирования: {date:dd.MM.yyyy HH:mm zzz}   •   GcodeAnalyzer {AppVersion.Current}");
        section.AddParagraph($"Станок: {(string.IsNullOrWhiteSpace(data.File.Machine) ? "не определён" : data.File.Machine)}");
        Heading(section, "Сводка");

        var table = Table(section, [89, 88], ["Показатель", "Значение"]);
        AddRow(table, ["Время без разгонов", Duration(data.Time.TotalSec)]);
        var machine = data.MachineTime;
        string model = machine?.Accelerations == true ? "с разгонами" : "оценка без ускорений";
        AddRow(table, [$"Время по модели станка ({model})", machine is null ? Missing : Duration(machine.TotalSec)]);
        AddRow(table, ["Кадры / строки", $"{data.File.Blocks} / {data.File.Lines}"]);
        string tools = string.Join(", ", data.Tools.Select(t => $"T{t.Tool}"));
        AddRow(table, ["Инструменты", tools.Length == 0 ? "Нет" : $"{data.Tools.Length} · {tools}"]);
    }

    private static void AddTools(Section section, AnalysisData data)
    {
        Heading(section, "Таблица по инструментам");
        var rows = ToolDetailsTable.Rows(data);
        if (rows.Length == 0)
        {
            section.AddParagraph("Нет подробных данных по инструментам.");
            return;
        }

        var time = Table(section, [3, 45, 16, 21, 18, 18, 18, 19, 19],
            ["", "Инструмент", "Ø, мм", "H / D", "Рабочие, с", "G0, с", "Циклы, с", "Выдержки, с", "Всего, с"]);
        foreach (var row in rows)
        {
            var motion = row.Metrics.MotionTime;
            string offsets = row.Tool is { } t ? $"{Offsets(t.HValues)} / {Offsets(t.DValues)}" : Missing;
            AddToolRow(time, row, Number(row.Tool?.DiameterMm), offsets, Number(motion.FeedSec), Number(motion.RapidSec),
                Number(motion.CycleSec), Number(motion.DwellSec), Number(motion.TotalSec));
        }

        Heading(section, "Длины, подачи и обороты", 10);
        var feeds = Table(section, [3, 45, 25, 25, 39, 40],
            ["", "Инструмент", "Рабочие, мм", "Холостые, мм", "F, мм/мин", "S, об/мин"]);
        foreach (var row in rows)
            AddToolRow(feeds, row, Number(row.Metrics.WorkingMm), Number(row.Metrics.IdleMm),
                Range(row.Metrics.FeedMmPerMin), Range(row.Metrics.Rpm));

        Heading(section, "Габариты траектории", 10);
        var axes = Table(section, [3, 45, 43, 43, 43], ["", "Инструмент", "X, мм", "Y, мм", "Z, мм"]);
        foreach (var row in rows) AddToolRow(axes, row, Range(row.Axis(0)), Range(row.Axis(1)), Range(row.Axis(2)));
    }

    private static void AddDiagnostics(Section section, AnalysisData data)
    {
        Heading(section, "Предупреждения", newPage: true);
        if (data.Warnings.Length == 0) section.AddParagraph("Предупреждений нет.");
        else
        {
            var warnings = Table(section, [31, 14, 17, 115], ["Уровень", "Строка", "Код", "Сообщение"]);
            foreach (var warning in data.Warnings)
            {
                var row = AddRow(warnings, [SeverityStyle.Label(warning.Severity), Line(warning.Line), warning.Code, warning.Message]);
                row.Format.Font.Color = FromDrawing(SeverityStyle.Foreground(warning.Severity));
            }
        }

        Heading(section, "Пределы станка");
        if (data.Limits.Length == 0) section.AddParagraph("Нет данных о пределах станка.");
        else
        {
            var limits = Table(section, [34, 44, 44, 40, 15], ["Параметр", "Фактически", "Предел", "Статус", "Строка"]);
            foreach (var check in data.Limits)
            {
                var row = AddRow(limits, [check.Parameter, MachineLimits.Value(check.ValueMin, check.ValueMax),
                    MachineLimits.Value(check.LimitMin, check.LimitMax), MachineLimits.Status(check.Status), Line(check.Line)]);
                if (MachineLimits.Severity(check.Status) is { } severity)
                    row.Format.Font.Color = FromDrawing(SeverityStyle.Foreground(severity));
            }
        }
    }

    private static void AddTrajectory(Section section, AnalysisData data, TrajectoryScene scene, PdfComparison? comparison)
    {
        Heading(section, "Траектория", newPage: true);
        AddView(section, scene, ViewPlane.XY, "Вид сверху · XY");
        AddView(section, scene, ViewPlane.XZ, "Вид сбоку · XZ");
        Heading(section, "Легенда", 10);
        var legend = section.AddParagraph("G0 — пунктир, рабочие ходы — сплошная линия, циклы — окружности. ");
        legend.AddFormattedText("Красная обводка — нарушение предела.").Color = FromDrawing(SeverityStyle.Foreground("error"));
        if (comparison is not null)
            legend.AddFormattedText(" Серый — старая УП.").Color = FromDrawing(Controls.TrajectoryRenderer.PreviousColor);

        var names = data.Tools.ToDictionary(t => t.Tool);
        var palette = section.AddParagraph();
        foreach (int tool in scene.Views.Take(2).SelectMany(v => v).Where(e => e.SegmentIndex >= scene.PreviousSegmentCount)
            .Select(e => e.Tool).Distinct().Order())
        {
            string label = names.TryGetValue(tool, out var info) && !string.IsNullOrEmpty(info.Name) ? $"T{tool} ({info.Name})" : $"T{tool}";
            palette.AddFormattedText($"■ {label}   ").Color = FromDrawing(ToolPalette.ForTool(tool));
        }
    }

    private static void AddView(Section section, TrajectoryScene scene, ViewPlane plane, string title)
    {
        Heading(section, title, 10);
        if (scene.Views[(int)plane].Length == 0)
        {
            section.AddParagraph("Нет известной геометрии для этого вида.");
            return;
        }
        var image = section.AddImage(PdfTrajectoryRenderer.Render(scene, plane));
        image.Width = Unit.FromMillimeter(PdfTrajectoryRenderer.WidthMm);
        image.LockAspectRatio = true;
    }

    private static void AddCheatSheet(Section section, string report)
    {
        const string title = "Шпаргалка для постпроцессора";
        Heading(section, title, newPage: true);
        var sections = ReportParser.ParseSections(report);
        int index = sections.FindIndex(s => s.Title == title);
        if (index < 0)
        {
            section.AddParagraph("В текстовом отчёте нет шпаргалки.");
            return;
        }
        int start = report.IndexOf('\n', sections[index].Offset) + 1;
        int end = index + 1 < sections.Count ? sections[index + 1].Offset : report.Length;
        AddReportLines(section, report[start..end]);
    }

    private static void AddComparison(Section section, string report)
    {
        const string title = "Сравнение УП";
        Heading(section, title, newPage: true);
        // Сохраняем все разделы и строки сравнения из CLI.
        var lines = report.Split('\n');
        AddReportLines(section, string.Join('\n', lines.Where(line => line.Trim() != $"== {title} ==")));
    }

    private static void AddReportLines(Section section, string text)
    {
        foreach (string raw in text.Trim('\r', '\n').Split('\n'))
        {
            string line = raw.Trim();
            if (line.StartsWith("== ") && line.EndsWith(" ==")) Heading(section, line[3..^3], 11);
            else if (line.StartsWith("--- ") && line.EndsWith(" ---")) Heading(section, line[4..^4], 11);
            else
            {
                var paragraph = section.AddParagraph(line);
                paragraph.Format.SpaceAfter = 0;
                paragraph.Format.LeftIndent = Unit.FromPoint((raw.Length - raw.TrimStart().Length) * 2);
            }
        }
    }

    private static void Heading(Section section, string text, int size = 15, bool newPage = false)
    {
        var paragraph = section.AddParagraph(text);
        paragraph.Format.Font.Size = size;
        paragraph.Format.Font.Bold = true;
        paragraph.Format.SpaceBefore = size <= 11 ? 6 : 8;
        paragraph.Format.SpaceAfter = 4;
        paragraph.Format.KeepWithNext = true;
        paragraph.Format.PageBreakBefore = newPage;
    }

    private static Table Table(Section section, int[] widths, string[] titles)
    {
        var table = section.AddTable();
        table.Rows.LeftIndent = 0;
        table.Borders.Bottom.Color = Rule;
        table.Borders.Bottom.Width = 0.4;
        table.Format.Font.Size = 8;
        table.Format.SpaceAfter = 0;
        foreach (int width in widths)
        {
            var column = table.AddColumn(Unit.FromMillimeter(width));
            column.LeftPadding = column.RightPadding = 3;
        }
        var header = AddRow(table, titles);
        header.HeadingFormat = true;
        header.Shading.Color = Tint;
        header.Format.Font.Bold = true;
        return table;
    }

    private static Row AddRow(Table table, string[] values)
    {
        var row = table.AddRow();
        row.TopPadding = row.BottomPadding = 2;
        for (int i = 0; i < values.Length; i++) row.Cells[i].AddParagraph(values[i]);
        return row;
    }

    private static void AddToolRow(Table table, ToolDetailsRow tool, params string[] values)
    {
        var row = AddRow(table, ["", tool.Label, .. values]);
        if (tool.Tool is { } item) row.Cells[0].Shading.Color = FromDrawing(ToolPalette.ForTool(item.Tool));
        else
        {
            row.Shading.Color = Tint;
            row.Format.Font.Bold = true;
        }
    }

    private static string Offsets(int[] offsets) => offsets.Length == 0 ? Missing : string.Join("/", offsets);
    private static string Line(int line) => line > 0 ? line.ToString(CultureInfo.InvariantCulture) : Missing;
    private static string Number(double? value) => value?.ToString("0.###", CultureInfo.InvariantCulture) ?? Missing;
    private static string Range(RangeInfo? range) => range is null ? Missing : $"{Number(range.Min)} .. {Number(range.Max)}";
    private static string Duration(double seconds) => $"{Number(seconds)} с";
    private static PdfColor FromDrawing(System.Drawing.Color color) => new(color.R, color.G, color.B);
}
