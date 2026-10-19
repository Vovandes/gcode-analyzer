#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>
#include <set>

namespace gca {

/// Work coordinate systems: which are selected, how many moves run in each, per-tool usage.
class WcsAnalyzer final : public IAnalyzer {
public:
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    struct Use { std::size_t selections = 0; std::size_t firstLine = 0; std::size_t moves = 0; };
    std::map<std::string, Use> uses_;
    std::map<int, std::set<std::string>> perTool_;
    std::vector<int> toolOrder_;
    std::size_t movesWithoutWcs_ = 0;
    std::size_t firstMoveWithoutWcs_ = 0;
};

} // namespace gca
