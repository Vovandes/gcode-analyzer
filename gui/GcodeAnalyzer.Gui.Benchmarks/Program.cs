using System.Diagnostics;
using GcodeAnalyzer.Gui;
using GcodeAnalyzer.Gui.Controls;
using GcodeAnalyzer.Gui.Logic;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        ApplicationConfiguration.Initialize();
        if (args.Length != 2) throw new ArgumentException("Usage: benchmark <synthetic.nc> <output-directory>");
        string input = Path.GetFullPath(args[0]), output = Path.GetFullPath(args[1]);
        Directory.CreateDirectory(output);
        string[] lines = File.ReadAllLines(input);
        Console.WriteLine($"Runtime={Environment.Version}; OS={Environment.OSVersion}; CPUs={Environment.ProcessorCount}; lines={lines.Length}");
        var clock = Stopwatch.StartNew();
        using var rich = new RichTextBox { Size = new(600, 700), Font = new Font("Consolas", 10), WordWrap = false };
        rich.CreateControl();
        rich.Text = string.Join('\n', lines);
        Console.WriteLine($"RichTextBox load={clock.Elapsed.TotalMilliseconds:F1} ms; chars={rich.TextLength}");
        clock.Restart();
        int colored = 0;
        for (int i = 0; i < lines.Length; i++)
        {
            int offset = rich.GetFirstCharIndexFromLine(i);
            rich.Select(offset, lines[i].Length);
            rich.SelectionColor = Color.DarkGreen;
            colored++;
            if (clock.ElapsedMilliseconds > 3000) break;
        }
        Console.WriteLine($"RichTextBox per-line formatting={colored}/{lines.Length} in {clock.Elapsed.TotalMilliseconds:F1} ms (3-second budget)");

        string exe = ExeLocator.Find(AppContext.BaseDirectory) ?? throw new FileNotFoundException("Build CLI first");
        // STA занят измерениями; продолжения CLI выполняются в пуле, без контекста WinForms.
        var result = Task.Run(() => new AnalyzerRunner(exe).AnalyzeToMemoryAsync(input, "auto", null)).GetAwaiter().GetResult();
        if (result.Run.ExitCode != 0 || result.Run.Analysis is null) throw new InvalidOperationException(result.Run.StdErr);
        var data = result.Run.Analysis;
        Console.WriteLine($"CLI segments={data.Segments.Length}; warnings={data.Warnings.Length}");
        clock.Restart();
        var classified = LineClassifier.Classify(lines, data.Segments, data.Warnings);
        Console.WriteLine($"Classification={clock.Elapsed.TotalMilliseconds:F1} ms");
        using var program = new ProgramView { Size = new(600, 700) };
        program.CreateControl();
        clock.Restart(); program.SetLines(classified);
        Console.WriteLine($"ProgramView load={clock.Elapsed.TotalMilliseconds:F1} ms");
        using var textBitmap = new Bitmap(600, 700);
        clock.Restart(); program.DrawToBitmap(textBitmap, new(0, 0, 600, 700));
        Console.WriteLine($"ProgramView first paint={clock.Elapsed.TotalMilliseconds:F1} ms; painted={program.LastPaintedLines}");
        program.SelectLine(lines.Length - 1);
        clock.Restart(); program.DrawToBitmap(textBitmap, new(0, 0, 600, 700));
        Console.WriteLine($"ProgramView last-page paint={clock.Elapsed.TotalMilliseconds:F1} ms; selected={program.SelectedLine}; top={program.TopLine}");
        textBitmap.Save(Path.Combine(output, "program.png"));
        clock.Restart(); var scene = TrajectoryScene.Build(data.Segments);
        Console.WriteLine($"Scene (3 projections + grids)={clock.Elapsed.TotalMilliseconds:F1} ms");
        foreach (ViewPlane plane in Enum.GetValues<ViewPlane>())
        {
            var edges = scene.Views[(int)plane];
            clock.Restart(); using var renderer = new TrajectoryRenderer(edges);
            Console.WriteLine($"{plane} GDI-path preparation={clock.Elapsed.TotalMilliseconds:F1} ms; edges={edges.Length}");
            var t = new ViewTransform();
            t.Fit(new(edges.Min(e => Math.Min(e.A.X, e.B.X)), edges.Min(e => Math.Min(e.A.Y, e.B.Y))),
                new(edges.Max(e => Math.Max(e.A.X, e.B.X)), edges.Max(e => Math.Max(e.A.Y, e.B.Y))), 1200, 800);
            using var bitmap = new Bitmap(1200, 800);
            using var g = Graphics.FromImage(bitmap);
            var timings = new List<double>();
            for (int i = 0; i < 7; i++)
            {
                g.Clear(Color.White); clock.Restart(); renderer.Draw(g, t, new HashSet<int>());
                timings.Add(clock.Elapsed.TotalMilliseconds);
            }
            Console.WriteLine($"{plane} uncached draw 1200x800 ms: {string.Join(", ", timings.Select(v => v.ToString("F1")))}; max={timings.Max():F1}");
            clock.Restart();
            int hit = scene.HitTests[(int)plane].Find(t.ToScreen(edges[100000].A), t);
            Console.WriteLine($"{plane} hit={hit}; {clock.Elapsed.TotalMilliseconds:F2} ms; candidates={scene.HitTests[(int)plane].LastCandidateCount}");
            bitmap.Save(Path.Combine(output, $"trajectory-{plane}.png"));
        }

        using var form = new MainForm(new AppSettings { ViewMode = 3 }) { ShowInTaskbar = false, Opacity = 0 };
        form.Show();
        Application.DoEvents();
        using var formBitmap = new Bitmap(form.Width, form.Height);
        form.DrawToBitmap(formBitmap, new(0, 0, form.Width, form.Height));
        formBitmap.Save(Path.Combine(output, "main-empty.png"));
        Console.WriteLine("MainForm invisible-window render: OK");
        var workspace = Descendants(form).OfType<TrajectoryWorkspace>().Single();
        string root = Directory.GetCurrentDirectory();
        foreach (string sample in Directory.GetFiles(Path.Combine(root, "samples"), "*.nc"))
        {
            var sampleResult = Task.Run(() => new AnalyzerRunner(exe).AnalyzeToMemoryAsync(sample, "auto", null)).GetAwaiter().GetResult();
            var sampleData = sampleResult.Run.Analysis ?? throw new InvalidOperationException(sampleResult.Run.StdErr);
            var sampleScene = TrajectoryScene.Build(sampleData.Segments);
            workspace.SetAnalysis(sampleScene, LineClassifier.Classify(File.ReadAllLines(sample), sampleData.Segments, sampleData.Warnings), sampleData.Warnings);
            foreach (var view in Descendants(workspace).OfType<TrajectoryView>()) view.FitAll();
            form.DrawToBitmap(formBitmap, new(0, 0, form.Width, form.Height));
            formBitmap.Save(Path.Combine(output, Path.GetFileNameWithoutExtension(sample) + ".png"));
            Console.WriteLine($"Sample snapshot: {Path.GetFileName(sample)}, segments={sampleData.Segments.Length}");
        }
    }

    private static IEnumerable<Control> Descendants(Control root)
    {
        foreach (Control child in root.Controls)
        { yield return child; foreach (Control nested in Descendants(child)) yield return nested; }
    }
}
