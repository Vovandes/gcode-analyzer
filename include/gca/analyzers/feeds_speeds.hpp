#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>

namespace gca {

/// Per tool: F ranges (G94 and G95 apart), S range, spindle direction, coolant codes.
class FeedsSpeedsAnalyzer final : public IAnalyzer {
public:
    void onBlock(const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    struct Acc {
        Range feedPerMin, feedPerRev, spindle;
        bool fractionalS = false;
        std::set<std::string> spindleDirs;
        std::set<std::string> coolant;
    };
    std::map<int, Acc> perTool_;
    std::vector<int> toolOrder_;
    Acc& acc(int tool);
};

} // namespace gca
