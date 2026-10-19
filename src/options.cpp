#include "gca/options.hpp"

#include <algorithm>

namespace gca {

const std::vector<OptionInfo>& cliOptions() {
    static const std::vector<OptionInfo> options = {
        {"--out", "-o", "<файл>", "", "записать отчёт в текстовый файл UTF-8 вместо вывода в консоль"},
        {"--bom", "", "", "", "добавить BOM (EF BB BF) в начало файла --out (для старого Блокнота и Excel)"},
        {"--dialect", "", "<id>", "fanuc-mill", "профиль стойки ЧПУ"},
        {"--machine", "", "<id|auto>", "auto", "профиль станка; auto - определить по комментарию «Generated for ...»"},
        {"--rapid", "", "<мм/мин>", "10000", "скорость ускоренного хода G0 для оценки времени"},
        {"--list-machines", "", "", "", "вывести профили станков выбранного диалекта (id и название через TAB)"},
        {"--list-options", "", "", "", "вывести таблицу опций в машиночитаемом виде (через TAB)"},
        {"--help", "-h", "", "", "показать эту справку"},
        {"--version", "", "", "", "показать версию"},
    };
    return options;
}

std::string helpText() {
    std::string s =
        "Использование: gcode-analyzer <программа.nc> [опции]\n"
        "               gcode-analyzer --help | --version | --list-options | --list-machines\n"
        "\n"
        "Опции:\n";
    std::vector<std::string> left;
    for (const auto& o : cliOptions()) {
        std::string l = "  " + o.name;
        if (!o.alias.empty()) l += ", " + o.alias;
        if (!o.value.empty()) l += " " + o.value;
        left.push_back(std::move(l));
    }
    // pad by characters, not bytes: Cyrillic is 2 bytes per letter in UTF-8
    const auto chars = [](const std::string& t) {
        std::size_t n = 0;
        for (const unsigned char c : t) {
            if ((c & 0xC0U) != 0x80U) ++n;
        }
        return n;
    };
    std::size_t charWidth = 0;
    for (const auto& l : left) charWidth = std::max(charWidth, chars(l));
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& o = cliOptions()[i];
        s += left[i];
        s.append(charWidth - chars(left[i]) + 2, ' ');
        s += o.description;
        if (!o.defaultValue.empty()) s += " (по умолчанию " + o.defaultValue + ")";
        s += '\n';
    }
    s += "\nКоды возврата: 0 - успех, 1 - ошибка ввода-вывода, 2 - ошибка аргументов\n";
    return s;
}

std::string optionsListText() {
    std::string s;
    for (const auto& o : cliOptions()) {
        s += o.name + '\t' + o.alias + '\t' + o.value + '\t' + o.defaultValue + '\t' + o.description + '\n';
    }
    return s;
}

} // namespace gca
