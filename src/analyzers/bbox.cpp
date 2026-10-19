#include "gca/analyzers/bbox.hpp"

#include "gca/motion.hpp"

#include <algorithm>

namespace gca {

void BBoxAnalyzer::addPoint(BBox& box, const Position& p) {
    for (int a = 0; a < kAxisCount; ++a) {
        const auto i = static_cast<std::size_t>(a);
        if (p.known[i]) box.add(a, p.v[i]);
    }
}

void BBoxAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    if (seg.kind == SegmentKind::Home || seg.kind == SegmentKind::Dwell) return;
    if (perTool_.find(seg.tool) == perTool_.end()) toolOrder_.push_back(seg.tool);
    Boxes& tool = perTool_[seg.tool];
    const bool cutting = seg.kind != SegmentKind::Rapid;

    const auto addAll = [&](const Position& p) {
        addPoint(total_.all, p);
        addPoint(tool.all, p);
        if (cutting) {
            addPoint(total_.work, p);
            addPoint(tool.work, p);
        }
    };
    const auto addAxis = [&](int axis, double value, bool allOnly) {
        total_.all.add(axis, value);
        tool.all.add(axis, value);
        if (cutting && !allOnly) {
            total_.work.add(axis, value);
            tool.work.add(axis, value);
        }
    };

    if (seg.kind == SegmentKind::CycleHole) {
        // the hole axis is visited from the initial level down to the bottom
        Position p = seg.to;
        p.v[kAxisZ] = seg.cycle.zBottom;
        p.known[kAxisZ] = true;
        addAll(p);
        addAxis(kAxisZ, seg.cycle.zR, false);
        addAxis(kAxisZ, seg.cycle.zInit, true);
        return;
    }

    if (seg.kind == SegmentKind::Arc) {
        addAll(seg.to);
        addPoint(total_.work, seg.from);
        addPoint(tool.work, seg.from);
        const ArcResult a = computeSegmentArc(seg);
        if (a.ok) {
            const PlaneAxes pa = planeAxes(seg.arc.plane);
            const auto u = static_cast<std::size_t>(pa.u);
            const auto v = static_cast<std::size_t>(pa.v);
            const Extents e = arcExtents(a, seg.from.v[u], seg.from.v[v], seg.to.v[u], seg.to.v[v]);
            addAxis(pa.u, e.uMin, false);
            addAxis(pa.u, e.uMax, false);
            addAxis(pa.v, e.vMin, false);
            addAxis(pa.v, e.vMax, false);
        }
        return;
    }

    addAll(seg.to);
    if (cutting) {
        // a cutting move starts where the previous move ended; keep the start point for completeness
        addPoint(total_.work, seg.from);
        addPoint(tool.work, seg.from);
    }
}

void BBoxAnalyzer::finish(Report& report) {
    report.box = total_.all;
    report.workBox = total_.work;
    for (const int t : toolOrder_) {
        ToolReport& tr = report.tool(t);
        tr.box = perTool_[t].all;
        tr.workBox = perTool_[t].work;
    }
}

} // namespace gca
