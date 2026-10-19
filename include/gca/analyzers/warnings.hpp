#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>
#include <set>

namespace gca {

/// Warning codes (stable identifiers, listed in the report).
namespace warn {
constexpr const char* kNoWcs = "W001";            ///< motion before any work coordinate system
constexpr const char* kWcsZero = "W002";          ///< G54.1 P0 / zero offset instead of G54
constexpr const char* kWcsChange = "W003";        ///< WCS changed inside one tool operation
constexpr const char* kNoSpindle = "W004";        ///< cutting move with spindle off / no S
constexpr const char* kNoFeed = "W005";           ///< G1/G2/G3/cycle without F
constexpr const char* kG43NoH = "W006";           ///< G43 without H
constexpr const char* kHNotT = "W007";            ///< H differs from the tool number
constexpr const char* kChangeComp = "W008";       ///< tool change with G41/G42 active
constexpr const char* kCompRapid = "W009";        ///< G41/G42 active during G0
constexpr const char* kG91AfterG28 = "W010";      ///< G91 left active after G28
constexpr const char* kCycleActive = "W011";      ///< cycle not cancelled by G80 before tool change / end
constexpr const char* kUnknownCode = "W012";      ///< unknown G/M code
constexpr const char* kUnparsed = "W013";         ///< unparsed text / unknown address
constexpr const char* kInch = "W014";             ///< G20
constexpr const char* kNoMotionCode = "W015";     ///< axis words without a motion code
constexpr const char* kCycleIncomplete = "W016";  ///< cycle without Z or R
constexpr const char* kChangeNoTool = "W017";     ///< M6 without a tool number
constexpr const char* kArc = "W018";              ///< arc geometry problem
} // namespace warn

class WarningsAnalyzer final : public IAnalyzer {
public:
    explicit WarningsAnalyzer(const Program& program) : program_(program) {}
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void onToolChange(const ToolEvent& ev, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    void emit(std::size_t line, const char* code, std::string message);
    void emitOncePerOp(std::size_t line, const char* code, std::string message);
    [[nodiscard]] std::string lineText(std::size_t line) const;

    const Program& program_;
    std::vector<Warning> warnings_;
    std::set<std::string> once_;
    std::map<std::string, std::pair<std::size_t, std::size_t>> unknown_;  ///< code -> (warning index, count)
    std::size_t op_ = 0;                ///< index of the current tool operation (increments at M6)
    bool opHasMotion_ = false;
    bool g91Reported_ = false;
    bool endChecked_ = false;
};

} // namespace gca
