#include "gca/comparison.hpp"

#include "gca/analyzer.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/text_util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <span>
#include <string_view>

namespace gca {
namespace {

constexpr std::size_t kShownDifferences = 20;
constexpr std::size_t kMaxBand = 32;
constexpr auto kMissing = "—";
using Points = std::span<const Segment* const>;

class Alignment {
public:
    Alignment(double tolerance, ToolComparison& result) : tolerance_(tolerance), result_(result) {}
    void compare(Points before, Points after);

private:
    struct Score {
        std::size_t edits = 0, matches = 0;
        [[nodiscard]] bool betterThan(Score other) const {
            return edits < other.edits || (edits == other.edits && matches > other.matches);
        }
    };
    enum class Step : unsigned char { Pair, Delete, Insert };

    [[nodiscard]] bool matches(const Segment& a, const Segment& b) const;
    [[nodiscard]] bool tryBand(Points before, Points after, std::size_t width);
    [[nodiscard]] std::vector<Score> scores(Points before, Points after, bool reverse) const;
    [[nodiscard]] std::size_t split(Points left, Points right, Points after) const;
    void compareOne(Points before, Points after);
    void record(const Segment* before, const Segment* after);

    double tolerance_;
    ToolComparison& result_;
};

bool Alignment::matches(const Segment& a, const Segment& b) const {
    if ((a.machineCoordinates && !a.workTo) != (b.machineCoordinates && !b.workTo)) return false;
    const Position& x = a.workTo ? *a.workTo : a.to;
    const Position& y = b.workTo ? *b.workTo : b.to;
    if (x.known != y.known) return false;
    for (std::size_t axis = 3; axis < x.v.size(); ++axis)
        if (x.known[axis] && std::abs(x.v[axis] - y.v[axis]) > 1e-9) return false;
    std::array<double, 3> delta{};
    std::size_t different = 0;
    double scale = 0;
    for (std::size_t axis = 0; axis < delta.size(); ++axis) {
        if (!x.known[axis]) continue;
        delta[axis] = std::abs(x.v[axis] - y.v[axis]);
        if (delta[axis] > tolerance_ * (1 + 1e-6)) return false;
        if (delta[axis] == 0.0) continue;
        ++different;
        scale = std::max({scale, std::abs(x.v[axis]), std::abs(y.v[axis])});
    }
    if (different == 0) return true;
    const double distance = different == 1 ? delta[0] + delta[1] + delta[2] : std::hypot(delta[0], delta[1], delta[2]);
    // Allow for subtraction rounding at the tolerance boundary.
    const double slack = std::min(tolerance_ * 1e-6, scale * std::numeric_limits<double>::epsilon());
    return distance <= tolerance_ + slack;
}

void Alignment::record(const Segment* before, const Segment* after) {
    ++result_.differenceCount;
    if (result_.differences.size() >= kShownDifferences) return;
    result_.differences.push_back({
        before && after ? PointChange::Modified : before ? PointChange::Deleted : PointChange::Inserted,
        before ? std::optional(before->lineNo) : std::nullopt,
        after ? std::optional(after->lineNo) : std::nullopt
    });
}

bool Alignment::tryBand(Points before, Points after, std::size_t width) {
    // An edit script with at most width edits can never leave this band.
    const std::size_t stride = 2 * width + 1;
    const auto index = [=](std::size_t i, std::size_t j) { return i * stride + (j + width - i); };
    std::vector<Step> trace((before.size() + 1) * stride);
    std::vector<Score> previous(after.size() + 1), current(after.size() + 1);
    std::size_t previousFirst = 0;
    std::size_t previousLast = std::min(width, after.size());
    for (std::size_t j = 1; j <= previousLast; ++j) {
        previous[j].edits = j;
        trace[index(0, j)] = Step::Insert;
    }
    for (std::size_t i = 1; i <= before.size(); ++i) {
        const std::size_t first = i > width ? i - width : 0;
        const std::size_t last = std::min(i + width, after.size());
        for (std::size_t j = first; j <= last; ++j) {
            Score best{std::numeric_limits<std::size_t>::max(), 0};
            Step step = Step::Pair;
            if (j > previousFirst && j - 1 <= previousLast) {
                const bool match = matches(*before[i - 1], *after[j - 1]);
                best = previous[j - 1];
                best.edits += !match;
                best.matches += match;
            }
            if (j >= previousFirst && j <= previousLast) {
                const Score deletion{previous[j].edits + 1, previous[j].matches};
                if (deletion.betterThan(best)) { best = deletion; step = Step::Delete; }
            }
            if (j > first) {
                const Score insertion{current[j - 1].edits + 1, current[j - 1].matches};
                if (insertion.betterThan(best)) { best = insertion; step = Step::Insert; }
            }
            current[j] = best;
            trace[index(i, j)] = step;
        }
        previous.swap(current);
        previousFirst = first;
        previousLast = last;
    }
    if (previous[after.size()].edits > width) return false;

    std::vector<std::pair<const Segment*, const Segment*>> edits;
    std::size_t i = before.size(), j = after.size();
    while (i || j) {
        switch (trace[index(i, j)]) {
        case Step::Pair:
            --i; --j;
            if (!matches(*before[i], *after[j])) edits.emplace_back(before[i], after[j]);
            break;
        case Step::Delete: edits.emplace_back(before[--i], nullptr); break;
        case Step::Insert: edits.emplace_back(nullptr, after[--j]); break;
        }
    }
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) record(it->first, it->second);
    return true;
}

std::vector<Alignment::Score> Alignment::scores(Points before, Points after, bool reverse) const {
    std::vector<Score> row(after.size() + 1);
    for (std::size_t j = 1; j < row.size(); ++j) row[j].edits = j;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const Segment& a = *before[reverse ? before.size() - 1 - i : i];
        Score diagonal = row[0];
        ++row[0].edits;
        for (std::size_t j = 0; j < after.size(); ++j) {
            const Segment& b = *after[reverse ? after.size() - 1 - j : j];
            const bool match = matches(a, b);
            Score best{diagonal.edits + !match, diagonal.matches + match};
            diagonal = row[j + 1];
            const Score deletion{diagonal.edits + 1, diagonal.matches};
            const Score insertion{row[j].edits + 1, row[j].matches};
            if (deletion.betterThan(best)) best = deletion;
            if (insertion.betterThan(best)) best = insertion;
            row[j + 1] = best;
        }
    }
    return row;
}

std::size_t Alignment::split(Points left, Points right, Points after) const {
    const auto forward = scores(left, after, false);
    const auto backward = scores(right, after, true);
    Score best{std::numeric_limits<std::size_t>::max(), 0};
    std::size_t column = 0;
    for (std::size_t j = 0; j <= after.size(); ++j) {
        const Score a = forward[j], b = backward[after.size() - j];
        const Score score{a.edits + b.edits, a.matches + b.matches};
        if (score.betterThan(best)) { best = score; column = j; }
    }
    return column;
}

void Alignment::compareOne(Points before, Points after) {
    const bool oneBefore = before.size() == 1;
    const Segment* one = oneBefore ? before.front() : after.front();
    const Points many = oneBefore ? after : before;
    const auto paired = std::find_if(many.begin(), many.end(), [&](const Segment* p) { return matches(*one, *p); });
    const auto match = paired == many.end() ? many.begin() : paired;
    for (auto it = many.begin(); it != many.end(); ++it) {
        if (it == paired) continue;
        const Segment* another = it == match ? one : nullptr;
        if (oneBefore) record(another, *it);
        else record(*it, another);
    }
}

void Alignment::compare(Points before, Points after) {
    while (!before.empty() && !after.empty() && matches(*before.front(), *after.front())) {
        before = before.subspan(1);
        after = after.subspan(1);
    }
    while (!before.empty() && !after.empty() && matches(*before.back(), *after.back())) {
        before = before.first(before.size() - 1);
        after = after.first(after.size() - 1);
    }
    if (before.empty() || after.empty()) {
        for (const auto* point : before) record(point, nullptr);
        for (const auto* point : after) record(nullptr, point);
        return;
    }
    if (before.size() == 1 || after.size() == 1) {
        compareOne(before, after);
        return;
    }
    const std::size_t lengthGap = std::max(before.size(), after.size()) - std::min(before.size(), after.size());
    for (std::size_t width = 4; width <= kMaxBand; width *= 2) {
        if (width >= lengthGap && tryBand(before, after, width)) return;
    }
    // Hirschberg reconstruction keeps only two rows for wide differences.
    const std::size_t middle = before.size() / 2;
    const std::size_t column = split(before.first(middle), before.subspan(middle), after);
    compare(before.first(middle), after.first(column));
    compare(before.subspan(middle), after.subspan(column));
}

std::string toolKey(const Report& report, int tool) {
    if (const auto it = report.toolNames.find(tool); it != report.toolNames.end())
        return "T\"" + it->second + '"';
    return "T" + std::to_string(tool);
}

struct ToolInput {
    const ToolReport* details = nullptr;
    std::vector<const Segment*> points;
};
using Tools = std::map<std::string, ToolInput>;

Tools byTool(const Report& report) {
    Tools tools;
    for (const auto& tool : report.tools) tools[toolKey(report, tool.tool)].details = &tool;
    for (const auto& segment : report.segments)
        if (segment.kind != SegmentKind::Dwell && segment.kind != SegmentKind::Home)
            tools[toolKey(report, segment.tool)].points.push_back(&segment);
    return tools;
}

std::string joined(const std::vector<std::string>& values, std::string_view separator = ", ") {
    if (values.empty()) return kMissing;
    std::string text = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) text.append(separator).append(values[i]);
    return text;
}

std::string number(double value) { return trimmedNumber(value); }

std::string signedNumber(double value) {
    const std::string text = number(value);
    return value > 0 && text != "0" ? "+" + text : text;
}

std::string countDelta(std::size_t before, std::size_t after) {
    if (after > before) return "+" + std::to_string(after - before);
    if (after < before) return "-" + std::to_string(before - after);
    return "0";
}

ComparisonTrend lowerIsBetter(double delta) {
    if (number(delta) == "0") return ComparisonTrend::Neutral;
    return delta > 0 ? ComparisonTrend::Worsened : ComparisonTrend::Improved;
}

std::vector<std::string> uniqueWcs(const Report& report) {
    std::vector<std::string> result;
    for (const auto& wcs : report.wcsUsage) result.push_back(wcs.wcs);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> toolSequence(const Report& report) {
    std::vector<std::string> result;
    for (const auto& entry : report.toolSequence) result.push_back(toolKey(report, entry.tool));
    return result;
}

std::string listDelta(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    std::vector<std::string> changes;
    for (const auto& value : before)
        if (std::find(after.begin(), after.end(), value) == after.end()) changes.push_back("-" + value);
    for (const auto& value : after)
        if (std::find(before.begin(), before.end(), value) == before.end()) changes.push_back("+" + value);
    return joined(changes);
}

std::size_t warningCount(const Report& report, Severity severity) {
    return static_cast<std::size_t>(std::count_if(report.warnings.begin(), report.warnings.end(),
        [=](const Warning& warning) { return warningSeverity(warning.code) == severity; }));
}

void addSummary(const Report& before, const Report& after, ComparisonReport& comparison) {
    auto& rows = comparison.summary;
    const auto time = [&](const char* name, double a, double b) {
        rows.push_back({name, number(a), number(b), signedNumber(b - a), lowerIsBetter(b - a)});
    };
    time("Время без разгонов, с", before.time.totalSec(), after.time.totalSec());
    time("Время по модели станка, с", before.machineTime.totalSec(), after.machineTime.totalSec());
    rows.push_back({"Кадры", std::to_string(before.file.blocks), std::to_string(after.file.blocks),
                    countDelta(before.file.blocks, after.file.blocks)});
    const auto a = toolSequence(before), b = toolSequence(after);
    rows.push_back({"Порядок использования инструментов", joined(a, " "), joined(b, " "),
                    a == b ? "совпадает" : "изменился"});
    const auto wcsBefore = uniqueWcs(before), wcsAfter = uniqueWcs(after);
    rows.push_back({"Рабочие СК", joined(wcsBefore), joined(wcsAfter),
                    wcsBefore == wcsAfter ? "совпадают" : listDelta(wcsBefore, wcsAfter)});
    for (const auto& [level, label] : {std::pair{Severity::Error, "Ошибки"},
             std::pair{Severity::Warning, "Предупреждения"}, std::pair{Severity::Info, "Сообщения"}}) {
        const auto oldCount = warningCount(before, level), newCount = warningCount(after, level);
        const ComparisonTrend trend = oldCount == newCount ? ComparisonTrend::Neutral :
            newCount > oldCount ? ComparisonTrend::Worsened : ComparisonTrend::Improved;
        rows.push_back({label, std::to_string(oldCount), std::to_string(newCount),
                        countDelta(oldCount, newCount), trend});
    }
}

std::string rangeText(const Range& range) {
    if (!range.has) return kMissing;
    if (range.min == range.max) return number(range.min);
    return number(range.min) + " … " + number(range.max);
}

void addToolSummary(const std::string& tool, const ToolReport* before, const ToolReport* after,
                    std::vector<ComparisonRow>& rows) {
    const std::vector<std::string> oldWcs = before ? std::vector<std::string>(before->wcs.begin(), before->wcs.end())
                                                  : std::vector<std::string>{};
    const std::vector<std::string> newWcs = after ? std::vector<std::string>(after->wcs.begin(), after->wcs.end())
                                                : std::vector<std::string>{};
    if (!oldWcs.empty() || !newWcs.empty()) {
        rows.push_back({tool + " · Рабочие СК", joined(oldWcs), joined(newWcs),
                        oldWcs == newWcs ? "совпадают" : listDelta(oldWcs, newWcs)});
    }
    const Range missing;
    const auto add = [&](const char* label, Range ToolReport::* member) {
        const Range& a = before ? before->*member : missing;
        const Range& b = after ? after->*member : missing;
        if (!a.has && !b.has) return;
        const std::string delta = a.has && b.has ?
            signedNumber(b.min - a.min) + " / " + signedNumber(b.max - a.max) :
            b.has ? "появились" : "исчезли";
        rows.push_back({tool + " · " + label, rangeText(a), rangeText(b), delta});
    };
    add("F G94, мм/мин", &ToolReport::feedPerMin);
    add("F G95, мм/об", &ToolReport::feedPerRev);
    add("S, об/мин", &ToolReport::spindle);
}

void compareTools(const Report& before, const Report& after, ComparisonReport& comparison) {
    const auto a = byTool(before), b = byTool(after);
    std::set<std::string> keys;
    for (const auto& [key, input] : a) keys.insert(key);
    for (const auto& [key, input] : b) keys.insert(key);
    std::vector<std::string> added, removed;
    const ToolInput missing;
    const auto rangesStart = comparison.summary.size();
    for (const auto& key : keys) {
        const auto oldIt = a.find(key), newIt = b.find(key);
        const auto& oldTool = oldIt != a.end() ? oldIt->second : missing;
        const auto& newTool = newIt != b.end() ? newIt->second : missing;
        ToolComparison result;
        result.tool = key == "T0" ? "Без инструмента (T0)" : key;
        result.oldPoints = oldTool.points.size();
        result.newPoints = newTool.points.size();
        if (oldIt == a.end()) {
            result.presence = ToolPresence::Added;
            if (key != "T0") added.push_back(key);
        } else if (newIt == b.end()) {
            result.presence = ToolPresence::Removed;
            if (key != "T0") removed.push_back(key);
        } else {
            Alignment(comparison.tolerance, result).compare(oldTool.points, newTool.points);
        }
        comparison.trajectory.push_back(std::move(result));
        addToolSummary(key, oldTool.details, newTool.details, comparison.summary);
    }
    comparison.summary.insert(comparison.summary.begin() + static_cast<std::ptrdiff_t>(rangesStart), {
        {"Инструменты добавлены", kMissing, joined(added), added.empty() ? kMissing : "+" + std::to_string(added.size())},
        {"Инструменты пропали", joined(removed), kMissing, removed.empty() ? kMissing : "-" + std::to_string(removed.size())}
    });
}

void compareFormat(const Report& before, const Report& after, ComparisonReport& comparison) {
    std::vector<std::string> labels;
    for (const auto* report : {&before, &after})
        for (const auto& group : report->comparisonFormat)
            if (std::find(labels.begin(), labels.end(), group.title) == labels.end()) labels.push_back(group.title);
    const std::vector<std::string> empty;
    const auto facts = [&](const Report& report, const std::string& label) -> const std::vector<std::string>& {
        const auto it = std::find_if(report.comparisonFormat.begin(), report.comparisonFormat.end(),
            [&](const CheatSection& section) { return section.title == label; });
        return it == report.comparisonFormat.end() ? empty : it->lines;
    };
    for (const auto& label : labels) {
        const auto& a = facts(before, label);
        const auto& b = facts(after, label);
        const auto [firstA, firstB] = std::mismatch(a.begin(), a.end(), b.begin(), b.end());
        if (firstA == a.end() && firstB == b.end()) continue;
        const auto [lastA, lastB] = std::mismatch(a.rbegin(), std::make_reverse_iterator(firstA),
                                                 b.rbegin(), std::make_reverse_iterator(firstB));
        comparison.format.push_back({label, {firstA, lastA.base()}, {firstB, lastB.base()}});
    }
}

std::vector<Warning> unmatched(const std::vector<Warning>& before, const std::vector<Warning>& after) {
    using WarningKey = std::pair<std::string_view, std::string_view>;
    std::map<WarningKey, std::size_t> counts;
    for (const auto& w : before) ++counts[{w.code, w.message}];
    std::vector<Warning> result;
    for (const auto& w : after) {
        const auto it = counts.find({w.code, w.message});
        if (it != counts.end() && it->second > 0) --it->second;
        else result.push_back(w);
    }
    return result;
}

const char* pointChangeText(PointChange kind) {
    switch (kind) {
    case PointChange::Modified: return "изменена";
    case PointChange::Inserted: return "вставлена";
    case PointChange::Deleted: return "удалена";
    }
    return kMissing;
}

} // namespace

ComparisonReport compareReports(const Report& before, const Report& after, double tolerance) {
    if (!std::isfinite(tolerance) || tolerance < 0)
        throw OptionError("Допуск сравнения должен быть конечным неотрицательным числом");
    ComparisonReport comparison;
    comparison.tolerance = tolerance;
    addSummary(before, after, comparison);
    compareTools(before, after, comparison);
    compareFormat(before, after, comparison);
    comparison.newWarnings = unmatched(before.warnings, after.warnings);
    comparison.resolvedWarnings = unmatched(after.warnings, before.warnings);
    return comparison;
}

void writeTextComparison(const Report& before, const Report& after, const ComparisonReport& comparison,
                         std::ostream& output) {
    std::ostream out(output.rdbuf());
    out.imbue(std::locale::classic());
    out << std::setprecision(12);
    out << "== Сравнение УП ==\nБыло: " << before.file.fileName << "\nСтало: " << after.file.fileName
        << "\nДопуск XYZ, мм: " << comparison.tolerance << "\n\n== Сводка ==\nПоказатель | Было | Стало | Разница\n";
    for (const auto& row : comparison.summary)
        out << row.label << " | " << row.before << " | " << row.after << " | " << row.difference << '\n';
    out << "\n== Траектория ==\n";
    if (comparison.trajectory.empty()) out << "Нет данных о траектории.\n";
    for (const auto& tool : comparison.trajectory) {
        out << tool.tool << ": ";
        if (tool.presence == ToolPresence::Added) out << "инструмент добавлен\n";
        else if (tool.presence == ToolPresence::Removed) out << "инструмент пропал\n";
        else if (tool.differenceCount == 0) out << "совпадает по точкам (" << tool.oldPoints << ")\n";
        else {
            out << "расхождений " << tool.differenceCount << " (показано " << tool.differences.size()
                << ", точек было / стало: " << tool.oldPoints << " / " << tool.newPoints << ")\n"
                << "  Точка | Строка старой УП | Строка новой УП\n";
            for (const auto& d : tool.differences)
                out << "  " << pointChangeText(d.kind) << " | " << (d.oldLine ? std::to_string(*d.oldLine) : kMissing)
                    << " | " << (d.newLine ? std::to_string(*d.newLine) : kMissing) << '\n';
        }
    }
    out << "\n== Формат кадра ==\n";
    if (comparison.format.empty()) out << "Совпадает.\n";
    for (const auto& section : comparison.format) {
        out << section.label << "\n  Было:\n";
        if (section.before.empty()) out << "    " << kMissing << '\n';
        for (const auto& line : section.before) out << "    " << line << '\n';
        out << "  Стало:\n";
        if (section.after.empty()) out << "    " << kMissing << '\n';
        for (const auto& line : section.after) out << "    " << line << '\n';
    }
    const auto warnings = [&](const char* heading, const std::vector<Warning>& entries) {
        out << "\n== " << heading << " ==\n";
        if (entries.empty()) out << "Нет.\n";
        for (const auto& w : entries)
            out << "  [" << severityName(warningSeverity(w.code)) << "] " << w.code
                << ", строка " << w.line << ": " << w.message << '\n' << "    " << w.text << '\n';
    };
    warnings("Новые предупреждения", comparison.newWarnings);
    warnings("Исчезнувшие предупреждения", comparison.resolvedWarnings);
    output.setstate(out.rdstate());
}

} // namespace gca
