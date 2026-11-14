#pragma once
#include "gca/analyzers/analyzer.hpp"

namespace gca {
// Rates in mm/s, acceleration in mm/s^2, length in mm.
[[nodiscard]] double acceleratedSeconds(double length, double speed, double acceleration,
                                         double entry = 0, double exit = 0);
class MachineTimeAnalyzer final : public IAnalyzer {
public:
    MachineTimeAnalyzer(const MachineProfile* machine, double rapid) : machine_(machine), rapid_(rapid) {}
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& segment, const BlockContext& ctx) override;
    void onToolChange(const ToolEvent& event, const BlockContext& ctx) override;
    void finish(Report& report) override;
private:
    const MachineProfile* machine_;
    double rapid_;
    Report::MachineTime result_;
    std::vector<std::size_t> groups_;
    std::size_t group_ = 0, changes_ = 0;
};
} // namespace gca
