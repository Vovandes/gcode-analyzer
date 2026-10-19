#include "gca/report.hpp"

#include "gca/dialect.hpp"
#include "gca/text_util.hpp"

#include <algorithm>
#include <map>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace gca {

namespace {

std::string fixed(double v, int prec) { return fixedNumber(v, prec); }

std::string num(double v) { return trimmedNumber(v); }

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

std::string rangeText(const Range& r) {
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

    heading(out, "Рабочие системы координат");
    if (r.wcsUsage.empty()) out << "  СК не назначались\n";
    for (const auto& w : r.wcsUsage) {
        out << "  " << pad(w.wcs.empty() ? "(не назначена)" : w.wcs, 12) << " назначений " << w.selections
            << ", перемещений " << w.moves;
        if (w.firstLine != 0) out << ", первое назначение в строке " << w.firstLine;
        out << '\n';
    }
    for (const auto& n : r.wcsNotes) out << "  " << n << '\n';

    heading(out, "Подачи и обороты по инструментам");
    for (const auto& t : r.tools) {
        if (!t.feedPerMin.has && !t.feedPerRev.has && !t.spindle.has && t.spindleDirs.empty() && t.coolant.empty()) continue;
        out << "  " << toolTitle(t.tool) << ":\n";
        out << "      F (G94, мм/мин): " << rangeText(t.feedPerMin) << '\n';
        out << "      F (G95, мм/об):  " << rangeText(t.feedPerRev) << '\n';
        out << "      S (об/мин):      " << rangeText(t.spindle);
        if (t.fractionalS) out << "  (есть дробные значения S)";
        out << '\n';
        out << "      шпиндель:        ";
        if (t.spindleDirs.empty()) out << "-";
        for (const auto& d : t.spindleDirs) out << displayCode(d) << ' ';
        out << '\n';
        out << "      охлаждение:      ";
        if (t.coolant.empty()) out << "-";
        for (const auto& c : t.coolant) out << displayCode(c) << ' ';
        out << '\n';
        if (!t.wcs.empty()) {
            out << "      СК:              ";
            for (const auto& w : t.wcs) out << w << "; ";
            out << '\n';
        }
    }

    heading(out, "Циклы сверления");
    if (r.cycles.empty()) out << "  циклы не используются\n";
    for (const auto& c : r.cycles) {
        out << "  " << displayCode(c.code) << ", " << toolTitle(c.tool) << ": отверстий " << c.holes
            << ", Z " << rangeText(c.z) << ", R " << rangeText(c.r);
        if (c.q.has) out << ", Q " << rangeText(c.q);
        if (c.p.has) out << ", P " << rangeText(c.p);
        if (c.feedPerRev.has) out << ", F (G95) " << rangeText(c.feedPerRev);
        if (c.feedPerMin.has) out << ", F (G94) " << rangeText(c.feedPerMin);
        out << ", возврат G98: " << c.returnInitial << ", G99: " << c.returnR << '\n';
    }
    {
        std::size_t g80 = 0;
        for (const auto& g : r.gCodes) if (g.code == "G80") g80 = g.count;
        out << "  Отмен цикла G80: " << g80 << '\n';
    }

    heading(out, "G-коды");
    writeCodes(out, r.gCodes);
    heading(out, "M-коды");
    writeCodes(out, r.mCodes);

    heading(out, "Машинно-специфичные и неизвестные коды");
    {
        bool any = false;
        const auto list = [&](const std::vector<CodeStat>& codes) {
            for (const auto& c : codes) {
                if (!c.machineSpecific && c.known) continue;
                any = true;
                out << "  " << pad(displayCode(c.code), 7) << std::setw(6) << c.count << "  ";
                if (c.known) out << c.description << "  [" << c.source << "]";
                else out << "?  неизвестный код, кандидат для онлайн-поиска (первая строка " << c.firstLine << ")";
                out << '\n';
            }
        };
        list(r.gCodes);
        list(r.mCodes);
        if (!any) out << "  нет\n";
    }

    heading(out, "Предупреждения");
    if (r.warnings.empty()) out << "  нет\n";
    {
        std::map<std::string, std::size_t> perCode;
        for (const auto& w : r.warnings) ++perCode[w.code];
        if (!perCode.empty()) {
            out << "  Всего: " << r.warnings.size() << " (";
            bool first = true;
            for (const auto& [code, n] : perCode) {
                out << (first ? "" : ", ") << code << ": " << n;
                first = false;
            }
            out << ")\n";
        }
    }
    for (const auto& w : r.warnings) {
        out << "  [" << w.code << "] строка " << w.line << ": " << w.message << '\n';
        if (!w.text.empty()) out << "      " << w.text << '\n';
    }

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
