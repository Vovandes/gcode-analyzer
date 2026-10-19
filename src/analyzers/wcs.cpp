#include "gca/analyzers/wcs.hpp"

#include <algorithm>

namespace gca {

void WcsAnalyzer::onBlock(const BlockContext& ctx) {
    for (const auto& w : ctx.block.words) {
        if (w.letter != 'G' || !w.hasValue) continue;
        const auto info = ctx.profile.g(w.value);
        if (!info || info->cls != GClass::Wcs) continue;
        Use& u = uses_[ctx.after.wcs];
        if (u.selections == 0) u.firstLine = ctx.block.lineNo;
        ++u.selections;
    }
}

void WcsAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    if (seg.kind == SegmentKind::Home || seg.kind == SegmentKind::Dwell) return;
    if (seg.wcs.empty()) {
        if (movesWithoutWcs_ == 0) firstMoveWithoutWcs_ = seg.lineNo;
        ++movesWithoutWcs_;
        return;
    }
    ++uses_[seg.wcs].moves;
    if (perTool_.find(seg.tool) == perTool_.end()) toolOrder_.push_back(seg.tool);
    perTool_[seg.tool].insert(seg.wcs);
}

void WcsAnalyzer::finish(Report& report) {
    for (const auto& [name, u] : uses_) {
        WcsUse w;
        w.wcs = name;
        w.selections = u.selections;
        w.firstLine = u.firstLine;
        w.moves = u.moves;
        report.wcsUsage.push_back(std::move(w));
    }
    std::sort(report.wcsUsage.begin(), report.wcsUsage.end(),
              [](const WcsUse& a, const WcsUse& b) { return a.firstLine < b.firstLine; });

    for (const int t : toolOrder_) report.tool(t).wcs = perTool_[t];

    std::set<std::string> all;
    for (const auto& [t, s] : perTool_) all.insert(s.begin(), s.end());
    if (all.size() == 1) {
        report.wcsNotes.push_back("Во всей программе используется одна СК: " + *all.begin() + ".");
    } else if (all.size() > 1) {
        std::string msg = "Разные СК у разных инструментов:";
        for (const int t : toolOrder_) {
            msg += " T" + std::to_string(t) + " (";
            bool first = true;
            for (const auto& s : perTool_[t]) {
                msg += (first ? "" : ", ") + s;
                first = false;
            }
            msg += ");";
        }
        report.wcsNotes.push_back(msg);
    }
    if (movesWithoutWcs_ > 0) {
        report.wcsNotes.push_back("Перемещений до назначения СК: " + std::to_string(movesWithoutWcs_) +
                                  " (первое в строке " + std::to_string(firstMoveWithoutWcs_) + ").");
    }
}

} // namespace gca
