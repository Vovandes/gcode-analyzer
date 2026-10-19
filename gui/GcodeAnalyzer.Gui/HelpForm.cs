using GcodeAnalyzer.Gui.Logic;

namespace GcodeAnalyzer.Gui;

/// <summary>Окно «Справка по аргументам»: таблица опций или, если таблицы нет, текст --help.</summary>
public class HelpForm : Form
{
    public HelpForm(List<CliOption>? options, string helpText)
    {
        Text = "Справка по аргументам";
        Size = new Size(900, 420);
        StartPosition = FormStartPosition.CenterParent;

        if (options is { Count: > 0 })
        {
            var list = new ListView { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, GridLines = true };
            list.Columns.Add("Опция", 180);
            list.Columns.Add("Значение", 100);
            list.Columns.Add("По умолчанию", 100);
            list.Columns.Add("Описание", 480);
            foreach (CliOption o in options)
            {
                string name = o.Alias.Length > 0 ? $"{o.Name}, {o.Alias}" : o.Name;
                list.Items.Add(new ListViewItem(new[] { name, o.Value, o.Default, o.Description }));
            }
            Controls.Add(list);
        }
        else
        {
            // Таблицы опций нет (старый CLI?) — показываем обычный текст справки
            Controls.Add(new TextBox
            {
                Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both,
                Font = new Font("Consolas", 10), Text = ReportParser.NormalizeNewlines(helpText),
            });
        }
    }
}
