#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>

namespace gca {

/// Bounding boxes: all moves (with G0) and cutting moves only; total and per tool; B range.
/// G28/G53 positions are machine coordinates and never enter the box.
class BBoxAnalyzer final : public IAnalyzer {
public:
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    struct Boxes { BBox all; BBox work; };
    static void addPoint(BBox& box, const Position& p);

    Boxes total_;
    std::map<int, Boxes> perTool_;
    std::vector<int> toolOrder_;
};

} // namespace gca
