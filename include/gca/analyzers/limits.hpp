#pragma once
#include "gca/analyzers/analyzer.hpp"
#include <set>
#include <tuple>

namespace gca {
class LimitsAnalyzer final : public IAnalyzer {
public:
    LimitsAnalyzer(const Program& program, const MachineProfile& machine) : program_(program), machine_(machine) {}
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& segment, const BlockContext& ctx) override;
    void finish(Report& report) override;
private:
    void scalar(std::string parameter, double value, std::optional<double> maximum, const char* code, std::size_t line);
    void warning(std::size_t line, const char* code, const std::string& message);
    struct Span { Range range; std::vector<std::size_t> segments; std::size_t line = 0; };
    const Program& program_;
    const MachineProfile& machine_;
    std::vector<Report::LimitCheck> checks_;
    std::vector<Warning> warnings_;
    std::set<std::pair<std::size_t, std::string>> emitted_, checked_;
    std::map<std::tuple<std::string, std::size_t, std::size_t>, Span> spans_;
    std::set<std::size_t> bad_;
    std::size_t index_ = 0;
};
} // namespace gca
