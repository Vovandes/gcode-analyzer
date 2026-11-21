using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using GcodeAnalyzer.Gui.Logic;
using PdfSharp.Pdf;
using PdfSharp.Pdf.IO;
using Xunit;

namespace GcodeAnalyzer.Gui.Tests;

public class PdfReportTests
{
    private static readonly DateTimeOffset Date = new(2026, 11, 21, 12, 0, 0, TimeSpan.FromHours(3));

    [Theory]
    [InlineData("sample-01-skymaster-vl1100a", 6)]
    [InlineData("sample-02-bridgeport-1300-480", 6)]
    public async Task CreatesPdfForBothSamples(string name, int expectedPages)
    {
        var (result, report) = await Runner().AnalyzeToMemoryAsync(Input("samples", name), "auto", null);
        Assert.True(result.ExitCode == 0, result.StdErr);
        var data = Assert.IsType<AnalysisData>(result.Analysis);
        string path = Output(name);
        new PdfReportWriter().Save(path, data, report, TrajectoryScene.Build(data.Segments), Date);

        using var pdf = Read(path);
        Assert.InRange(pdf.PageCount, expectedPages - 1, expectedPages + 1);
        var pages = pdf.Pages.Cast<PdfPage>().ToArray();
        Assert.All(pages, page =>
        {
            Assert.InRange(page.Width.Point, 594, 597);
            Assert.InRange(page.Height.Point, 840, 843);
        });
        var fonts = Fonts(pdf).ToArray();
        Assert.NotEmpty(fonts);
        Assert.All(fonts, font => AssertSubset(font, "Arial"));
        Assert.Contains(fonts, f => f.Elements.GetName("/BaseFont").Contains("Bold", StringComparison.OrdinalIgnoreCase));
        var images = pages.SelectMany(page => Resources(page, "/XObject"))
            .Where(image => image.Elements.GetName("/Subtype") == "/Image").ToArray();
        Assert.Equal(2, images.Length);
        Assert.All(images, image =>
        {
            Assert.True(image.Elements.GetInteger("/Width") * 25.4 / 177 >= 300);
            Assert.True(image.Elements.GetInteger("/Height") * 25.4 / 97 >= 300);
        });
        string body = ReadCyrillic(pdf);
        foreach (string heading in new[] { "Отчёт анализа УП", "Сводка", "Таблица по инструментам", "ИТОГО",
            "Предупреждения", "Пределы станка", "Вид сверху", "Вид сбоку", "Легенда", "Шпаргалка для постпроцессора" })
            Assert.Contains(heading, body);
    }

    [Fact]
    public async Task IncludesComparisonReport()
    {
        var runner = Runner();
        string before = Input("data", "stage13-base"), after = Input("data", "stage13-shifted");
        const string options = "--tolerance 0.005 --rapid 12000";
        var comparison = await runner.CompareAsync(before, after, "auto", options);
        Assert.True(comparison.Run.ExitCode == 0, comparison.Run.StdErr);
        var compared = Assert.IsType<ComparisonData>(comparison.Comparison);
        var (run, report) = await runner.AnalyzeToMemoryAsync(after, "auto", options, fromComparison: true);
        Assert.True(run.ExitCode == 0, run.StdErr);
        var data = Assert.IsType<AnalysisData>(run.Analysis);
        Assert.Equal(12000, data.File.RapidFeed);
        string path = Output("comparison");
        new PdfReportWriter().Save(path, data, report, TrajectoryOverlay.Build(compared.Before.Segments, compared.After.Segments).Scene,
            Date, new(compared.Before.File.Name, comparison.Report));

        using var pdf = Read(path);
        string body = ReadCyrillic(pdf);
        foreach (string part in new[] { "Старая УП", "Серый", "Сравнение УП", "Формат кадра", "Новые предупреждения", "изменена" })
            Assert.Contains(part, body);
    }

    [Theory]
    [InlineData(true, "Arial")]
    [InlineData(false, "Segoe UI")]
    public void SelectsInstalledFontAndEmbedsItsSubset(bool arialAvailable, string family)
    {
        var (data, report, scene) = FontFixture();
        var requested = new List<string>();
        var writer = new PdfReportWriter(name =>
        {
            requested.Add(name);
            return arialAvailable || name == "Segoe UI";
        });
        string path = Output(family);
        writer.Save(path, data, report, scene, Date);
        using var pdf = Read(path);
        Assert.Equal(arialAvailable ? ["Arial"] : new[] { "Arial", "Segoe UI" }, requested);
        Assert.All(Fonts(pdf), font => AssertSubset(font, family));
        Assert.Contains("Шпаргалка для постпроцессора", ReadCyrillic(pdf));
    }

    [Fact]
    public void ExplainsHowToFixMissingFonts()
    {
        var (data, report, scene) = FontFixture();
        using var target = new MemoryStream();
        var error = Assert.Throws<InvalidOperationException>(() => new PdfReportWriter(_ => false).Write(target, data, report, scene, Date));
        Assert.Contains("установите Arial или Segoe UI", error.Message);
        Assert.Equal(0, target.Length);
    }

    [Fact]
    public void UsesPinnedPdfSharpVersion()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "GcodeAnalyzer.Gui.Tests.deps.json");
        using var dependencies = JsonDocument.Parse(File.ReadAllText(path));
        var names = dependencies.RootElement.GetProperty("libraries").EnumerateObject().Select(p => p.Name).ToArray();
        Assert.Equal("PDFsharp-MigraDoc-GDI/6.2.4", Assert.Single(names, n => n.StartsWith("PDFsharp-MigraDoc-GDI/")));
        Assert.Equal("PDFsharp-GDI/6.2.4", Assert.Single(names, n => n.StartsWith("PDFsharp-GDI/")));
    }

    private static (AnalysisData Data, string Report, TrajectoryScene Scene) FontFixture()
    {
        string input = Path.Combine(AppContext.BaseDirectory, "golden", "axis-b-index");
        var data = AnalysisJson.Parse(File.ReadAllText(input + ".json"));
        return (data, File.ReadAllText(input + ".txt"), TrajectoryScene.Build(data.Segments));
    }

    private static AnalyzerRunner Runner()
    {
        string? cli = ExeLocator.Find(AppContext.BaseDirectory);
        Assert.NotNull(cli);
        return new(cli);
    }

    private static string Input(string folder, string name) => Path.Combine(AppContext.BaseDirectory, folder, name + ".nc");

    private static string Output(string name) =>
        Path.Combine(Directory.CreateDirectory(Path.Combine(AppContext.BaseDirectory, "pdf-stage14")).FullName, name + ".pdf");

    private static PdfDocument Read(string path)
    {
        using var stream = File.OpenRead(path);
        Assert.True(stream.Length > 1000);
        byte[] header = new byte[4];
        stream.ReadExactly(header);
        Assert.Equal("%PDF", Encoding.ASCII.GetString(header));
        return PdfReader.Open(path, PdfDocumentOpenMode.Import);
    }

    private static IEnumerable<PdfDictionary> Resources(PdfPage page, string kind)
    {
        var resources = page.Elements.GetDictionary("/Resources")?.Elements.GetDictionary(kind);
        return resources is null ? [] : resources.Elements.Keys.Select(key => resources.Elements.GetDictionary(key)!);
    }

    private static IEnumerable<PdfDictionary> Fonts(PdfDocument pdf) => pdf.Pages.Cast<PdfPage>().SelectMany(page => Resources(page, "/Font"));

    private static void AssertSubset(PdfDictionary font, string family)
    {
        string name = font.Elements.GetName("/BaseFont");
        Assert.Matches($@"^/[A-Z]{{6}}\+{Regex.Escape(family)}(?:,Bold)?$", name);
        var descendant = font.Elements.GetArray("/DescendantFonts")?.Elements.GetDictionary(0) ?? font;
        var embedded = descendant.Elements.GetDictionary("/FontDescriptor")?.Elements.GetDictionary("/FontFile2");
        Assert.True(embedded?.Stream?.Length > 0, name);
    }

    private static string ReadCyrillic(PdfDocument pdf)
    {
        var text = new StringBuilder();
        foreach (var page in pdf.Pages)
        {
            var fonts = page.Elements.GetDictionary("/Resources")!.Elements.GetDictionary("/Font")!;
            Dictionary<int, string> glyphs = [];
            string commands = Encoding.Latin1.GetString(page.Contents.CreateSingleContent().Stream.UnfilteredValue);
            foreach (Match command in Regex.Matches(commands, @"(?<font>/F\d+)\s+[\d.]+\s+Tf|<(?<text>[0-9A-Fa-f]+)>\s*Tj"))
            {
                if (command.Groups["font"].Success)
                {
                    var unicode = fonts.Elements.GetDictionary(command.Groups["font"].Value)?.Elements.GetDictionary("/ToUnicode");
                    glyphs = unicode?.Stream is { } map ? ReadGlyphs(Encoding.ASCII.GetString(map.UnfilteredValue)) : [];
                    continue;
                }
                string encoded = command.Groups["text"].Value;
                for (int i = 0; i + 4 <= encoded.Length; i += 4)
                    text.Append(glyphs.GetValueOrDefault(Convert.ToInt32(encoded.Substring(i, 4), 16), "�"));
                text.Append(' ');
            }
            text.AppendLine();
        }
        return text.ToString();
    }

    private static Dictionary<int, string> ReadGlyphs(string cmap)
    {
        var glyphs = new Dictionary<int, string>();
        foreach (Match block in Regex.Matches(cmap, @"beginbf(?<kind>char|range)\s+(?<body>.*?)\s+endbf\k<kind>", RegexOptions.Singleline))
        {
            foreach (Match mapping in Regex.Matches(block.Groups["body"].Value, @"<([0-9A-Fa-f]{4})>\s*<([0-9A-Fa-f]+)>(?:[ \t]*<([0-9A-Fa-f]+)>)?"))
            {
                int start = Convert.ToInt32(mapping.Groups[1].Value, 16);
                if (block.Groups["kind"].Value == "char")
                    glyphs[start] = Encoding.BigEndianUnicode.GetString(Convert.FromHexString(mapping.Groups[2].Value));
                else if (mapping.Groups[3].Success)
                {
                    int end = Convert.ToInt32(mapping.Groups[2].Value, 16);
                    int unicode = Convert.ToInt32(mapping.Groups[3].Value, 16);
                    for (int glyph = start; glyph <= end; glyph++) glyphs[glyph] = ((char)(unicode + glyph - start)).ToString();
                }
            }
        }
        return glyphs;
    }
}
