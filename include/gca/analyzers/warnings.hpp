#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>
#include <set>
#include <array>
#include <string_view>

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
constexpr const char* kFeedLimit = "W019";
constexpr const char* kSpindleLimit = "W020";
constexpr const char* kAxisLimit = "W021";
constexpr const char* kNoWcsOffset = "W022";
constexpr const char* kMeasureAddress = "W023";
constexpr const char* kMeasureFeedMode = "W024";
constexpr const char* kMeasureDefect = "W025";
constexpr const char* kMeasureEffects = "W026";
constexpr const char* kApproximate = "W027";
constexpr const char* kDialectHint = "W028";
constexpr const char* kInheritedFeed = "W029";
constexpr const char* kNoRetract = "W030";
constexpr const char* kEndCompensation = "W031";
constexpr const char* kMissingEnd = "W032";
constexpr const char* kEarlyEnd = "W033";
constexpr const char* kMixedArc = "W034";
constexpr const char* kArcPlane = "W035";
constexpr const char* kHelix = "W036";
} // namespace warn

enum class Severity { Error, Warning, Info };
struct WarningLevel {
    std::string_view code;
    Severity severity;
};
inline constexpr std::array<WarningLevel, 36> kWarningLevels{{
    {warn::kNoWcs, Severity::Warning}, {warn::kWcsZero, Severity::Warning},
    {warn::kWcsChange, Severity::Warning}, {warn::kNoSpindle, Severity::Error},
    {warn::kNoFeed, Severity::Error}, {warn::kG43NoH, Severity::Warning},
    {warn::kHNotT, Severity::Warning}, {warn::kChangeComp, Severity::Error},
    {warn::kCompRapid, Severity::Error}, {warn::kG91AfterG28, Severity::Error},
    {warn::kCycleActive, Severity::Error}, {warn::kUnknownCode, Severity::Warning},
    {warn::kUnparsed, Severity::Info}, {warn::kInch, Severity::Warning},
    {warn::kNoMotionCode, Severity::Info}, {warn::kCycleIncomplete, Severity::Error},
    {warn::kChangeNoTool, Severity::Error}, {warn::kArc, Severity::Error},
    {warn::kFeedLimit, Severity::Error}, {warn::kSpindleLimit, Severity::Error},
    {warn::kAxisLimit, Severity::Error}, {warn::kNoWcsOffset, Severity::Info},
    {warn::kMeasureAddress, Severity::Error}, {warn::kMeasureFeedMode, Severity::Error},
    {warn::kMeasureDefect, Severity::Warning}, {warn::kMeasureEffects, Severity::Info},
    {warn::kApproximate, Severity::Info}, {warn::kDialectHint, Severity::Info},
    {warn::kInheritedFeed, Severity::Warning}, {warn::kNoRetract, Severity::Error},
    {warn::kEndCompensation, Severity::Warning}, {warn::kMissingEnd, Severity::Error},
    {warn::kEarlyEnd, Severity::Warning}, {warn::kMixedArc, Severity::Error},
    {warn::kArcPlane, Severity::Error}, {warn::kHelix, Severity::Info}
}};
[[nodiscard]] constexpr Severity warningSeverity(std::string_view code) {
    for (const auto& level : kWarningLevels) if (level.code == code) return level.severity;
    return Severity::Warning;
}
[[nodiscard]] constexpr std::string_view severityName(Severity severity) {
    switch (severity) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Info: return "info";
    }
    return "warning";
}

class WarningsAnalyzer final : public IAnalyzer {
public:
    WarningsAnalyzer(const Program& program, double arcTolerance, std::optional<double> safeZ)
        : program_(program), arcTolerance_(arcTolerance), safeZ_(safeZ) {}
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    void emit(std::size_t line, const char* code, std::string message);
    void emitOncePerOp(std::size_t line, const char* code, std::string message);
    void checkToolChange(const ToolEvent& ev, const BlockContext& ctx);
    void checkZeroFeed(std::size_t line);
    void checkTermination(const BlockContext& ctx);
    void checkArc(const Segment& seg);
    void trackRetract(const Segment& seg, const Profile& profile);
    [[nodiscard]] std::string lineText(std::size_t line) const;

    const Program& program_;
    double arcTolerance_;
    std::optional<double> safeZ_;
    std::vector<Warning> warnings_;
    std::set<std::string> once_;
    std::map<std::string, std::pair<std::size_t, std::size_t>> unknown_;  ///< code -> (warning index, count)
    std::size_t op_ = 0;                ///< index of the current tool operation (increments at M6)
    std::size_t lastToolChange_ = 0;
    bool opHasMotion_ = false;
    bool zeroFeedReported_ = false;
    bool g91Reported_ = false;
    bool endChecked_ = false;
    bool defining_ = false, subReturn_ = false;
    bool endRadiusComp_ = false, endLengthComp_ = false;
    std::size_t lastSignificant_ = 0, lastExecutable_ = 0, firstM30_ = 0, programEndLine_ = 0;
    bool retractCommand_ = false, workRapidZ_ = false, retractIndeterminate_ = false;
    std::optional<double> retractZ_;
    std::pair<std::string, std::size_t> retractFrame_;
    std::map<std::pair<std::string, std::size_t>, Range> operationZ_;
};

} // namespace gca
