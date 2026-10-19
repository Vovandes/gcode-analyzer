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

std::string num(double v) {
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

std::string duration(double sec) {
    if (sec < 0.0) sec = 0.0;
    const long long total = static_cast<long long>(sec + 0.5);
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    std::ostringstream os;
    os << h << ':' << std::setw(2) << std::setfill('0') << m << ':' << std::setw(2) << std::setfill('0') << s;
    return os.str();
}

std::string timeCell(double sec) { return duration(sec) + " (" + fixed(sec, 1) + " с)"; }

[[maybe_unused]] std::string rangeText(const Range& r) {
    if (!r.has) return "-";
    return num(r.min) + " .. " + num(r.max);
}

void writeBox(std::ostream& out, const BBox& b, const std::string& indent) {
    if (!b.any()) {
        out << indent << "нет данных\n";
        return;
    }
    for (int a = 0; a < kAxisCount; ++a) {
        const Range& r = b.axis[static_cast<std::size_t>(a)];
        if (!r.has) continue;
        out << indent << kAxisLetters[a] << ": " << num(r.min) << " .. " << num(r.max);
        if (a != kAxisB) out << "  (размах " << num(r.max - r.min) << ')';
        out << '\n';
    }
}

std::string toolTitle(int tool) {
    return tool == 0 ? "(инструмент не выбран)" : "T" + std::to_string(tool);
}

void writeTimeLine(std::ostream& out, const std::string& label, const TimeBreakdown& t) {
    out << "  " << pad(label, 24) << " всего " << timeCell(t.totalSec()) << '\n';
    out << "      G0:      " << timeCell(t.rapidSec) << ", путь " << fixed(t.rapidMm, 1) << " мм, ходов " << t.rapidMoves << '\n';
    out << "      рабочие: " << timeCell(t.feedSec) << ", путь " << fixed(t.feedMm, 1) << " мм, ходов " << t.feedMoves << '\n';
    out << "      циклы:   " << timeCell(t.cycleSec) << ", путь " << fixed(t.cycleMm, 1) << " мм, отверстий " << t.cycleHoles << '\n';
    out << "      выдержки:" << ' ' << timeCell(t.dwellSec) << '\n';
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

    heading(out, "Время обработки (оценка)");
    out << "  Скорость G0: " << num(r.file.rapidFeed) << " мм/мин. Модели разгона и торможения нет.\n";
    out << "  Смена инструмента, M0/M1, G28 по времени не считаются: M6 " << r.toolSequence.size()
        << ", M0 " << r.programStops << ", M1 " << r.optionalStops << ", G28/G30 " << r.homeReturns << ".\n";
    writeTimeLine(out, "ИТОГО", r.time);
    for (const auto& t : r.tools) {
        if (t.time.totalSec() <= 0.0 && t.time.rapidMoves + t.time.feedMoves + t.time.cycleHoles == 0) continue;
        writeTimeLine(out, toolTitle(t.tool), t.time);
    }
    if (r.partialSegments > 0) {
        out << "  Ходов с неизвестной начальной точкой (длина по неизвестным осям не учтена): " << r.partialSegments << '\n';
    }
    if (r.segmentsWithoutFeed > 0) {
        out << "  Рабочих ходов без известной подачи (время не учтено): " << r.segmentsWithoutFeed << '\n';
    }
    for (const auto& n : r.timeNotes) out << "  " << n << '\n';

    heading(out, "Габариты траектории (в рабочей СК, без G28)");
    out << "  Все перемещения (с G0):\n";
    writeBox(out, r.box, "    ");
    out << "  Рабочие ходы (G1/G2/G3/циклы):\n";
    writeBox(out, r.workBox, "    ");
    for (const auto& t : r.tools) {
        if (!t.box.any()) continue;
        out << "  " << toolTitle(t.tool) << ", все перемещения:\n";
        writeBox(out, t.box, "    ");
        out << "  " << toolTitle(t.tool) << ", рабочие ходы:\n";
        writeBox(out, t.workBox, "    ");
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
