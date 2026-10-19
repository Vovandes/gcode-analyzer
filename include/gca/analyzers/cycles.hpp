#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>

namespace gca {

/// Canned cycles G81..G89, G73, G76: holes per cycle and tool, parameters, G98/G99.
class CyclesAnalyzer final : public IAnalyzer {
public:
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    std::vector<CycleSummary> summaries_;
};

} // namespace gca
