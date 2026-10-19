#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>

namespace gca {

/// Machining-time estimate: G0 / feed / canned cycles / dwell, total and per tool.
/// No acceleration model: length / feed.
class TimeAnalyzer final : public IAnalyzer {
public:
    explicit TimeAnalyzer(double rapidMmPerMin) : rapid_(rapidMmPerMin) {}
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    double rapid_;
    TimeBreakdown total_;
    std::map<int, TimeBreakdown> perTool_;
    std::vector<int> toolOrder_;
    std::size_t partial_ = 0;
    std::size_t noFeed_ = 0;
    std::vector<std::string> notes_;
};

} // namespace gca
