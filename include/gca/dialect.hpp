// Dialect profile: everything the core needs to know about a controller.
// The core never hardcodes Fanuc: it asks IDialect / Profile.
#pragma once

#include <memory>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "gca/lexer.hpp"

namespace gca {

/// Description of one G/M code. `code` is the canonical key ("G54.1", "M6").
struct CodeInfo {
    std::string code;
    std::string description;
    bool machineSpecific = false;
    std::string source;     ///< profile that knows the code ("fanuc-mill", machine name, ...)
};

/// Canonical key of a code: letter + number without leading zeros ("G5.1", "M6").
[[nodiscard]] std::string canonicalCode(char letter, double value);
/// Display form with two-digit integer part ("G05.1", "M06", "G54.1").
[[nodiscard]] std::string displayCode(std::string_view canonical);

enum class GClass {
    Rapid, Linear, ArcCw, ArcCcw, Dwell,
    PlaneXY, PlaneZX, PlaneYZ,
    Inch, Metric, Absolute, Incremental, FeedPerMin, FeedPerRev,
    Wcs,
    Home,           ///< G28 / G30: return through an intermediate point to reference
    MachineCoords,  ///< G53
    SetPosition,    ///< G92
    CompOff, CompLeft, CompRight,
    ToolLenPlus, ToolLenMinus, ToolLenCancel,
    Cycle, CycleCancel, ReturnInitial, ReturnR,
    DataSetting,    ///< G10: axis words are data, not motion
    MacroCall,      ///< G65
    Transform, TransformCancel, RotaryAlign, RtcpOn, RtcpOff,
    TurningOn, TurningOff, SpindleLimit, SurfaceSpeed, FixedSpeed, UnsupportedFeed, UnmodeledMotion,
    Other
};

struct GInfo {
    GClass cls = GClass::Other;
    int group = 0;      ///< modal group, 0 = non-modal
};

enum class MClass {
    ToolChange, ProgramStop, OptionalStop, ProgramEnd,
    SpindleCw, SpindleCcw, SpindleStop, SpindleOrient,
    CoolantOn, CoolantOff, CoolantExtra,
    SubCall, SubReturn, TurningCancel, Other
};

/// Machine-specific overlay (codes whose meaning depends on the machine, not on the controller).
struct MachineProfile {
    std::string id;
    std::string name;
    std::string match;          ///< lower-case substring (spaces removed) searched in the header comment
    struct MCode {
        int code;
        MClass cls;
        std::string description;
    };
    std::vector<MCode> mCodes;
    struct Axis {
        std::optional<double> min, max, rapid, acceleration, home, home30;
        std::optional<double> rapidAcceleration;
    };
    std::array<Axis, 6> axes{};  ///< XYZB retained at their original indices, then A and C
    std::optional<double> feedMax, spindleMax;
    std::optional<double> toolChangeSec, bSecondsPer90, spindleStartSec, g28Time;
    double junctionAngle = 30.0;
    bool skipFirstChange = false;
    std::map<std::string, std::array<std::optional<double>, 3>> wcsOffsets;
    [[nodiscard]] bool hasMotionData() const {
        if (!wcsOffsets.empty()) return true;
        for (const auto& a : axes) if (a.min || a.max || a.rapid || a.acceleration || a.rapidAcceleration || a.home || a.home30) return true;
        return false;
    }
};

struct DefinitionInfo {
    int number = 0;
    std::string description;
    std::vector<std::string> required;
    std::vector<std::string> positive;
    bool measuring = false, requiresG94 = false, homeZ = false;
    std::string wcsAddress;       ///< AN / W; the caller's WCS is not restored
    bool writesWcs = false;
    std::string defect;
};

class IDialect {
public:
    virtual ~IDialect() = default;

    [[nodiscard]] virtual std::string_view id() const = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;

    [[nodiscard]] virtual std::optional<GInfo> g(double value) const = 0;
    [[nodiscard]] virtual std::optional<CodeInfo> describeG(double value) const = 0;
    [[nodiscard]] virtual std::optional<MClass> m(int value) const = 0;
    [[nodiscard]] virtual std::optional<CodeInfo> describeM(int value) const = 0;

    /// Index of a linear/rotary axis letter in the position vector, -1 if not an axis.
    [[nodiscard]] virtual int axisIndex(char letter) const = 0;
    [[nodiscard]] virtual bool isLinearAxis(int index) const = 0;
    [[nodiscard]] virtual bool isKnownAddress(char letter) const = 0;
    [[nodiscard]] virtual LexRules lexicalRules() const { return {}; }
    [[nodiscard]] virtual std::optional<DefinitionInfo> definition(int) const { return std::nullopt; }
    [[nodiscard]] virtual bool uncertainHomeIntermediate() const { return false; }
    [[nodiscard]] virtual bool feedDwellSeconds() const { return false; }
    [[nodiscard]] virtual std::string_view machineHeaderKey() const { return "generated for"; }
    [[nodiscard]] virtual bool requiresMinuteFeed(std::string_view) const { return false; }

    [[nodiscard]] virtual const std::vector<MachineProfile>& machines() const = 0;
};

constexpr int kAxisCount = 6;   // X Y Z B A C (the first four indices are stable)
constexpr int kAxisX = 0;
constexpr int kAxisY = 1;
constexpr int kAxisZ = 2;
constexpr int kAxisB = 3;
constexpr int kAxisA = 4;
constexpr int kAxisC = 5;
constexpr char kAxisLetters[kAxisCount + 1] = "XYZBAC";

/// Dialect + optional machine overlay: the object the rest of the code works with.
class Profile {
public:
    Profile(const IDialect& dialect, const MachineProfile* machine)
        : dialect_(&dialect), machine_(machine) {}

    [[nodiscard]] const IDialect& dialect() const { return *dialect_; }
    [[nodiscard]] const MachineProfile* machine() const { return machine_; }

    [[nodiscard]] std::optional<GInfo> g(double v) const { return dialect_->g(v); }
    [[nodiscard]] std::optional<MClass> m(int v) const;
    /// Description of a G/M code (machine overlay wins over the base dialect).
    [[nodiscard]] std::optional<CodeInfo> describe(char letter, double value) const;

private:
    const IDialect* dialect_;
    const MachineProfile* machine_;
};

/// Find a dialect by id ("fanuc-mill"); nullptr if unknown.
[[nodiscard]] const IDialect* findDialect(std::string_view id);
[[nodiscard]] const IDialect& gnc62Dialect();
[[nodiscard]] const IDialect* detectDialect(std::string_view text);
[[nodiscard]] std::vector<std::string> dialectIds();
/// Machine overlay by id ("skymaster-vl1100a"); nullptr if the dialect has no such machine.
[[nodiscard]] const MachineProfile* findMachine(const IDialect& dialect, std::string_view id);
/// Pick the machine overlay by the "Generated for ..." header text; nullptr if none matches.
[[nodiscard]] const MachineProfile* detectMachine(const IDialect& dialect, std::string_view headerText);

} // namespace gca
