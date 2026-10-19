// Report model (plain data) and the text writer. Analyzers fill the model in finish();
// formatting lives only in the writers, so JSON/Markdown can be added later.
#pragma once

#include "gca/dialect.hpp"

#include <array>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gca {

struct Range {
    bool has = false;
    double min = 0.0;
    double max = 0.0;
    void add(double v) {
        if (!has) { has = true; min = max = v; return; }
        if (v < min) min = v;
        if (v > max) max = v;
    }
};

struct BBox {
    std::array<Range, kAxisCount> axis;
    void add(int axisIndex, double v) { axis[static_cast<std::size_t>(axisIndex)].add(v); }
    [[nodiscard]] bool any() const {
        for (const auto& a : axis) if (a.has) return true;
        return false;
    }
};

struct TimeBreakdown {
    double rapidSec = 0.0;
    double feedSec = 0.0;
    double cycleSec = 0.0;
    double dwellSec = 0.0;
    double rapidMm = 0.0;
    double feedMm = 0.0;
    double cycleMm = 0.0;
    std::size_t rapidMoves = 0;
    std::size_t feedMoves = 0;
    std::size_t cycleHoles = 0;
    [[nodiscard]] double totalSec() const { return rapidSec + feedSec + cycleSec + dwellSec; }
};

struct ToolUse {
    int tool = 0;
    std::size_t line = 0;
    bool tBeforeM6 = false;         ///< "T1M6"
    bool hadTWord = false;
    int preselectedBefore = 0;      ///< last preselected T before this change (0 = none)
};

struct ToolReport {
    int tool = 0;
    std::vector<std::string> descriptions;  ///< from TOOL LIST and from the comment after the change
    std::size_t changes = 0;
    std::vector<std::size_t> changeLines;
    std::set<int> hValues;
    std::set<int> dValues;
    TimeBreakdown time;
    BBox box;                       ///< all moves with known coordinates
    BBox workBox;                   ///< G1/G2/G3/cycles only
    Range feedPerMin;               ///< G94
    Range feedPerRev;               ///< G95
    Range spindle;
    bool fractionalS = false;
    std::set<std::string> spindleDirs;
    std::set<std::string> coolant;
    std::map<std::string, std::size_t> cycleHoles;
    std::set<std::string> wcs;
};

struct CodeStat {
    std::string code;               ///< canonical
    std::size_t count = 0;
    std::size_t firstLine = 0;
    std::string description;
    bool known = false;
    bool machineSpecific = false;
    std::string source;
};

struct WcsUse {
    std::string wcs;
    std::size_t selections = 0;
    std::size_t firstLine = 0;
    std::size_t moves = 0;          ///< motion segments executed in this WCS
};

struct CycleSummary {
    std::string code;
    int tool = 0;
    std::size_t holes = 0;
    Range z, r, q, p;
    Range feedPerRev, feedPerMin;
    std::size_t returnInitial = 0;  ///< G98
    std::size_t returnR = 0;        ///< G99
};

struct Warning {
    std::size_t line = 0;
    std::string text;               ///< block text
    std::string code;               ///< "W001"
    std::string message;
};

struct CheatSection {
    std::string title;
    std::vector<std::string> lines;
};

struct FileInfo {
    std::string fileName;
    std::string dialectId;
    std::string dialectName;
    std::string machineName;        ///< from "Generated for ..." comment, may be empty
    std::string machineProfile;     ///< matched machine overlay, may be empty
    std::size_t lines = 0;
    std::size_t blocks = 0;         ///< non-blank
    std::size_t crlfLines = 0;
    std::size_t lfLines = 0;
    std::size_t lexerDiagnostics = 0;
    double rapidFeed = 0.0;
};

struct Report {
    FileInfo file;

    std::vector<ToolUse> toolSequence;
    std::vector<int> preselectSequence;
    std::vector<ToolReport> tools;          ///< order of first use
    std::size_t optionalStops = 0;          ///< M1
    std::size_t programStops = 0;           ///< M0
    std::size_t homeReturns = 0;            ///< G28/G30 blocks

    TimeBreakdown time;
    std::size_t partialSegments = 0;        ///< moves whose start was unknown (length not fully counted)
    std::size_t segmentsWithoutFeed = 0;    ///< cutting moves with unknown/zero feed (time not counted)
    std::vector<std::string> timeNotes;     ///< arc errors etc.
    BBox box;
    BBox workBox;

    std::vector<WcsUse> wcsUsage;
    std::vector<std::string> wcsNotes;      ///< informational messages

    std::vector<CodeStat> gCodes;
    std::vector<CodeStat> mCodes;

    std::vector<CycleSummary> cycles;
    std::vector<Warning> warnings;
    std::vector<std::string> diagnostics;   ///< lexer diagnostics

    std::vector<CheatSection> cheatSheet;

    /// Get or create the per-tool entry (creation order = first-use order).
    ToolReport& tool(int number);
};

/// Text writer (UTF-8, '\n' line endings).
void writeTextReport(const Report& report, std::ostream& out);
[[nodiscard]] std::string renderTextReport(const Report& report);

} // namespace gca
