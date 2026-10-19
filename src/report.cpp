#include "gca/report.hpp"

#include "gca/dialect.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace gca {

namespace {

std::string fixed(double v, int prec) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(prec) << v;
    std::string s = os.str();
    if (s == "-0" || (s.size() > 1 && s.find_first_not_of("-0.") == std::string::npos && s[0] == '-')) {
        s.erase(0, 1); // no negative zero
    }
    return s;
}

[[maybe_unused]] std::string num(double v) {
    std::string s = fixed(v, 3);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

void heading(std::ostream& out, const std::string& title) {
    out << "\n== " << title << " ==\n";
}

std::string pad(const std::string& s, std::size_t width) {
    // width in code points (UTF-8 aware enough for padding)
    std::size_t len = 0;
    for (const char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) ++len;
    }
    return len >= width ? s : s + std::string(width - len, ' ');
}

void writeCodes(std::ostream& out, const std::vector<CodeStat>& codes) {
    for (const auto& c : codes) {
        out << "  " << pad(displayCode(c.code), 7) << std::setw(6) << c.count << "  ";
        if (c.known) {
            out << c.description;
            if (c.machineSpecific) out << "  [машинно-специфичный: " << c.source << "]";
        } else {
            out << "?  (неизвестный код, первая строка " << c.firstLine << ")";
        }
        out << '\n';
    }
}

} // namespace

void writeTextReport(const Report& r, std::ostream& out) {
    out << "gcode-analyzer: отчёт\n";
    heading(out, "Файл");
    out << "  Файл:        " << r.file.fileName << '\n';
    out << "  Диалект:     " << r.file.dialectId << " (" << r.file.dialectName << ")\n";
    out << "  Станок:      " << (r.file.machineName.empty() ? "не указан в заголовке" : r.file.machineName);
    if (!r.file.machineProfile.empty()) out << "  [профиль станка: " << r.file.machineProfile << "]";
    out << '\n';
    out << "  Строк:       " << r.file.lines << " (кадров с содержимым: " << r.file.blocks << ")\n";
    out << "  Концы строк: CRLF " << r.file.crlfLines << ", LF " << r.file.lfLines << '\n';
    out << "  Ошибки лексера: " << r.file.lexerDiagnostics << '\n';

    heading(out, "Инструменты");
    out << "  Смен инструмента (M6): " << r.toolSequence.size()
        << ", предвыборов T: " << r.preselectSequence.size() << '\n';
    out << "  Порядок использования:";
    for (const auto& u : r.toolSequence) out << " T" << u.tool;
    out << '\n';
    for (const auto& t : r.tools) {
        out << "  T" << t.tool << ": смен " << t.changes;
        if (!t.hValues.empty()) {
            out << ", H";
            for (const int h : t.hValues) out << ' ' << h;
        }
        if (!t.dValues.empty()) {
            out << ", D";
            for (const int d : t.dValues) out << ' ' << d;
        }
        out << '\n';
        for (const auto& d : t.descriptions) out << "      " << d << '\n';
    }

    heading(out, "G-коды");
    writeCodes(out, r.gCodes);
    heading(out, "M-коды");
    writeCodes(out, r.mCodes);

    if (!r.diagnostics.empty()) {
        heading(out, "Диагностика лексера");
        for (const auto& d : r.diagnostics) out << "  " << d << '\n';
    }
}

std::string renderTextReport(const Report& report) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    writeTextReport(report, os);
    return os.str();
}

} // namespace gca
