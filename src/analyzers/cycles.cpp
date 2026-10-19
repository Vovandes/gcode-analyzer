#include "gca/analyzers/cycles.hpp"

namespace gca {

void CyclesAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    if (seg.kind != SegmentKind::CycleHole) return;
    CycleSummary* s = nullptr;
    for (auto& c : summaries_) {
        if (c.code == seg.cycle.code && c.tool == seg.tool) { s = &c; break; }
    }
    if (s == nullptr) {
        summaries_.emplace_back();
        s = &summaries_.back();
        s->code = seg.cycle.code;
        s->tool = seg.tool;
    }
    ++s->holes;
    s->z.add(seg.cycle.zBottom);
    s->r.add(seg.cycle.zR);
    if (seg.cycle.hasQ) s->q.add(seg.cycle.q);
    if (seg.cycle.hasP) s->p.add(seg.cycle.p);
    if (seg.hasF) (seg.feedMode == FeedMode::PerRev ? s->feedPerRev : s->feedPerMin).add(seg.feed);
    if (seg.cycle.returnToInitial) ++s->returnInitial;
    else ++s->returnR;
}

void CyclesAnalyzer::finish(Report& report) {
    report.cycles = summaries_;
    for (const auto& c : summaries_) report.tool(c.tool).cycleHoles[c.code] += c.holes;
}

} // namespace gca
