#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>

namespace gca {

/// Tool sequence (M6 changes vs T preselects), descriptions, H/D links.
class ToolsAnalyzer final : public IAnalyzer {
public:
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void onToolChange(const ToolEvent& ev, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    static void addUnique(std::vector<std::string>& v, const std::string& s);

    std::vector<ToolUse> sequence_;
    std::vector<int> preselects_;
    std::map<int, std::vector<std::string>> listDescriptions_;   ///< from "( Tn: ... )"
    std::map<int, std::vector<std::string>> changeDescriptions_; ///< first comment after a change
    std::map<int, std::vector<std::size_t>> changeLines_;
    std::map<int, std::set<int>> h_;
    std::map<int, std::set<int>> d_;
    int pendingTool_ = -1;      ///< change waiting for its description comment
    int lastPreselect_ = 0;
    std::size_t optionalStops_ = 0;
    std::size_t programStops_ = 0;
    std::size_t homeReturns_ = 0;
};

} // namespace gca
