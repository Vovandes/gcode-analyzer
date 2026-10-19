// Modal state machine: applies one block at a time and reports what happened
// (motion segments and tool events). All controller specifics come from Profile.
#pragma once

#include "gca/dialect.hpp"
#include "gca/lexer.hpp"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gca {

enum class Plane { XY, ZX, YZ };
enum class DistanceMode { Absolute, Incremental };
enum class FeedMode { PerMinute, PerRev };
enum class CompMode { Off, Left, Right };
enum class SpindleDir { Off, Cw, Ccw };

/// Axis position with a per-axis "known" flag (unknown after G28 and at program start).
struct Position {
    std::array<double, kAxisCount> v{};
    std::array<bool, kAxisCount> known{};
};

/// Canned-cycle parameters (modal while the cycle is active).
struct CycleParams {
    bool hasZ = false, hasR = false, hasQ = false, hasP = false;
    double z = 0.0, r = 0.0, q = 0.0, p = 0.0;
};

struct ModalState {
    // motion group: Rapid / Linear / ArcCw / ArcCcw (hasMotion == false until first G0..G3)
    bool hasMotion = false;
    GClass motion = GClass::Rapid;
    std::string cycleCode;                  ///< active canned cycle ("G81") or empty
    CycleParams cycle;
    bool cycleReturnInitial = true;         ///< G98 (default) / G99

    Plane plane = Plane::XY;
    DistanceMode dist = DistanceMode::Absolute;
    FeedMode feedMode = FeedMode::PerMinute;
    bool inch = false;

    bool wcsSelected = false;
    std::string wcs;                        ///< "G54", "G55", "G54.1 P3"
    int wcsP = -1;                          ///< P of G54.1 / G54 Pn, -1 if none

    CompMode comp = CompMode::Off;
    bool lengthComp = false;
    int h = -1;
    int d = -1;

    int activeTool = 0;                     ///< 0 = no tool change yet
    int preselectedTool = 0;

    SpindleDir spindle = SpindleDir::Off;
    bool hasS = false;
    double s = 0.0;
    bool hasF = false;
    double f = 0.0;
    std::set<std::string> coolant;          ///< active coolant M codes ("M8", "M15")

    Position pos;
    bool g91FromG28 = false;                ///< G91 set together with G28 and not re-set afterwards
    bool ended = false;                     ///< M2 / M30 seen

    std::map<int, std::string> groups;      ///< modal group -> current code (canonical)
};

enum class SegmentKind { Rapid, Linear, Arc, CycleHole, Dwell, Home };

struct ArcData {
    bool cw = false;
    Plane plane = Plane::XY;
    std::optional<double> i, j, k, r;
};

struct CycleData {
    std::string code;                       ///< "G81"
    double zInit = 0.0;                     ///< initial level
    double zR = 0.0;                        ///< R plane (absolute)
    double zBottom = 0.0;                   ///< hole bottom (absolute)
    double q = 0.0;
    double p = 0.0;
    bool hasQ = false;
    bool hasP = false;
    bool returnToInitial = true;            ///< G98
    bool zInitKnown = true;
};

struct Segment {
    SegmentKind kind = SegmentKind::Rapid;
    std::size_t lineNo = 0;
    Position from;
    Position to;
    std::array<bool, kAxisCount> axes{};    ///< axes mentioned in the block (Home: which axes go home)
    ArcData arc;
    CycleData cycle;
    double dwellSeconds = 0.0;

    // snapshot of the state while the segment is executed
    int tool = 0;
    std::string wcs;
    FeedMode feedMode = FeedMode::PerMinute;
    bool hasF = false;
    double feed = 0.0;
    bool hasS = false;
    double s = 0.0;
    SpindleDir spindle = SpindleDir::Off;
    CompMode comp = CompMode::Off;
};

struct ToolEvent {
    enum class Kind { Preselect, Change };
    Kind kind = Kind::Change;
    int tool = 0;
    int previousTool = 0;
    std::size_t lineNo = 0;
    bool tBeforeM6 = false;                 ///< "T1M6" (true) vs "M6T4" (false); false for M6 without T
    bool hadTWord = false;
    bool cycleActive = false;               ///< state at the moment of the change
    CompMode comp = CompMode::Off;
};

struct BlockEffect {
    std::vector<Segment> segments;
    std::vector<ToolEvent> toolEvents;
    bool moveWithoutMotionCode = false;     ///< axis words while no G0..G3 was ever given
    bool cycleIncomplete = false;           ///< cycle executed without Z or R
};

class ModalMachine {
public:
    explicit ModalMachine(const Profile& profile) : profile_(profile) {}

    [[nodiscard]] const ModalState& state() const { return state_; }
    /// Apply one block to the state and return what it caused.
    BlockEffect apply(const Block& block);

private:
    Segment makeSegment(SegmentKind kind, std::size_t lineNo, const Position& from) const;
    void executeCycle(const Block& block, const std::array<std::optional<double>, kAxisCount>& target,
                      BlockEffect& effect);

    Profile profile_;
    ModalState state_;
};

} // namespace gca
