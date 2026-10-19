#include "gca/analyzers/time.hpp"

#include "gca/motion.hpp"

#include <algorithm>

namespace gca {

namespace {

void add(TimeBreakdown& t, const Segment& seg, const SegmentMetrics& m) {
    switch (seg.kind) {
    case SegmentKind::Rapid:
        t.rapidSec += m.rapidSec;
        t.rapidMm += m.rapidMm;
        if (m.rapidMm > 0.0) ++t.rapidMoves;
        break;
    case SegmentKind::Linear:
    case SegmentKind::Arc:
        t.feedSec += m.feedSec;
        t.feedMm += m.feedMm;
        if (m.feedMm > 0.0) ++t.feedMoves;
        break;
    case SegmentKind::CycleHole:
        t.cycleSec += m.rapidSec + m.feedSec + m.dwellSec;
        t.cycleMm += m.rapidMm + m.feedMm;
        ++t.cycleHoles;
        break;
    case SegmentKind::Dwell:
        t.dwellSec += m.dwellSec;
        break;
    case SegmentKind::Home:
        break;
    }
}

} // namespace

void TimeAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    const SegmentMetrics m = measureSegment(seg, rapid_);
    if (m.partial) ++partial_;
    if (m.noFeed) ++noFeed_;
    if (m.arcError && notes_.size() < 20) {
        notes_.push_back("строка " + std::to_string(seg.lineNo) + ": дуга пропущена (" + m.arcMessage + ")");
    }
    add(total_, seg, m);
    if (perTool_.find(seg.tool) == perTool_.end()) toolOrder_.push_back(seg.tool);
    add(perTool_[seg.tool], seg, m);
}

void TimeAnalyzer::finish(Report& report) {
    report.time = total_;
    report.partialSegments = partial_;
    report.segmentsWithoutFeed = noFeed_;
    report.timeNotes = notes_;
    for (const int t : toolOrder_) report.tool(t).time = perTool_[t];
}

} // namespace gca
