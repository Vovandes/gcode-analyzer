#include "gca/analyzers/tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gca {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

/// "T12: DRILL D5" -> (12, "DRILL D5")
bool parseToolListEntry(const std::string& text, int& tool, std::string& desc) {
    const std::string t = trim(text);
    if (t.size() < 3 || (t[0] != 'T' && t[0] != 't')) return false;
    std::size_t i = 1;
    int n = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
        n = n * 10 + (t[i] - '0');
        ++i;
    }
    if (i == 1 || i >= t.size() || t[i] != ':') return false;
    tool = n;
    desc = trim(t.substr(i + 1));
    return true;
}

} // namespace

void ToolsAnalyzer::addUnique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

void ToolsAnalyzer::onBlock(const BlockContext& ctx) {
    for (const auto& c : ctx.block.comments) {
        int tool = 0;
        std::string desc;
        if (parseToolListEntry(c.text, tool, desc)) {
            addUnique(listDescriptions_[tool], desc);
        } else if (pendingTool_ >= 0) {
            addUnique(changeDescriptions_[pendingTool_], trim(c.text));
            pendingTool_ = -1;
        }
    }
    for (const auto& w : ctx.block.words) {
        if (!w.hasValue) continue;
        // H0 / D0 mean "cancel", not a tool link
        if (w.letter == 'H' && w.value > 0.5) h_[ctx.after.activeTool].insert(static_cast<int>(std::lround(w.value)));
        else if (w.letter == 'D' && w.value > 0.5) d_[ctx.after.activeTool].insert(static_cast<int>(std::lround(w.value)));
        else if (w.letter == 'M') {
            if (std::fabs(w.value) < 1e-9) ++programStops_;
            else if (std::fabs(w.value - 1.0) < 1e-9) ++optionalStops_;
        }
    }
}

void ToolsAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    if (seg.kind == SegmentKind::Home) ++homeReturns_;
    else pendingTool_ = -1; // description comment must come before the first motion
}

void ToolsAnalyzer::onToolChange(const ToolEvent& ev, const BlockContext&) {
    if (ev.kind == ToolEvent::Kind::Preselect) {
        preselects_.push_back(ev.tool);
        lastPreselect_ = ev.tool;
        return;
    }
    ToolUse u;
    u.tool = ev.tool;
    u.line = ev.lineNo;
    u.tBeforeM6 = ev.tBeforeM6;
    u.hadTWord = ev.hadTWord;
    u.preselectedBefore = lastPreselect_;
    sequence_.push_back(u);
    changeLines_[ev.tool].push_back(ev.lineNo);
    lastPreselect_ = 0;
    pendingTool_ = ev.tool;
}

void ToolsAnalyzer::finish(Report& report) {
    report.toolSequence = sequence_;
    report.preselectSequence = preselects_;
    report.optionalStops = optionalStops_;
    report.programStops = programStops_;
    report.homeReturns = homeReturns_;
    for (const auto& u : sequence_) {
        ToolReport& t = report.tool(u.tool);
        ++t.changes;
    }
    for (auto& t : report.tools) {
        if (const auto it = listDescriptions_.find(t.tool); it != listDescriptions_.end()) {
            for (const auto& s : it->second) addUnique(t.descriptions, s);
        }
        if (const auto it = changeDescriptions_.find(t.tool); it != changeDescriptions_.end()) {
            for (const auto& s : it->second) addUnique(t.descriptions, s);
        }
        if (const auto it = changeLines_.find(t.tool); it != changeLines_.end()) t.changeLines = it->second;
        if (const auto it = h_.find(t.tool); it != h_.end()) t.hValues = it->second;
        if (const auto it = d_.find(t.tool); it != d_.end()) t.dValues = it->second;
    }
}

} // namespace gca
