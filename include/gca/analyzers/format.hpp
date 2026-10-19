#pragma once

#include "gca/analyzers/analyzer.hpp"

#include <map>
#include <optional>
#include <set>

namespace gca {

/// Builds the "postprocessor cheat-sheet" from what the program actually contains:
/// structure, block format, modality, comments, rotary axis, machine-specific codes.
/// Nothing is hardcoded: every statement is derived from the analyzed text.
class FormatAnalyzer final : public IAnalyzer {
public:
    void onBlock(const BlockContext& ctx) override;
    void onSegment(const Segment& seg, const BlockContext& ctx) override;
    void onToolChange(const ToolEvent& ev, const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    enum class Zone { Header, Setup, Cut, Teardown, End };

    struct Rec {
        Block block;
        bool cut = false;               ///< cutting move (G1/G2/G3/cycle) in this block
        bool hasM6 = false;
        bool preselect = false;
        int tool = 0;                   ///< tool of M6 / T
        int activeTool = 0;             ///< active tool after the block
        bool spindleOffBefore = true;   ///< spindle stopped before the block
        Zone zone = Zone::Header;
    };

    /// Counters that need the modal state before the block.
    struct Modality {
        std::size_t motionBlocks = 0;
        std::size_t motionCodeExplicitSame = 0;
        std::size_t motionCodeExplicitChange = 0;
        std::size_t motionCodeOmitted = 0;
        std::size_t cutBlocks = 0;
        std::size_t fSame = 0, fChange = 0, fOmitted = 0;
        std::map<char, std::size_t> axisRepeated;   ///< axis word equal to the current position
        std::map<char, std::size_t> axisChanged;
        std::map<std::string, std::pair<std::size_t, std::size_t>> gRepeat;   ///< code -> (repeated, total)
        std::size_t sRepeated = 0, sTotal = 0;
        std::size_t spindleRepeated = 0, spindleTotal = 0;
    };

    struct ArcStats {
        std::size_t total = 0;
        std::size_t byOffsets = 0;          ///< center by I/J/K
        std::size_t byRadius = 0;           ///< center by R
        std::size_t helical = 0;            ///< arc with movement along the third axis
        std::size_t fullCircle = 0;
        std::size_t helicalFullCircle = 0;
        std::size_t explicitZeroOffset = 0; ///< I0. / J0. written explicitly
        std::size_t failed = 0;
    };

    void computeZones();
    void buildStructure(Report& report);
    void buildBlockFormat(Report& report);
    void buildModality(Report& report);
    void buildComments(Report& report);
    void buildRotary(Report& report);
    void buildMachineSpecific(Report& report);

    [[nodiscard]] std::string abstractBlock(const Block& b, const std::string& commentLabel) const;
    [[nodiscard]] std::vector<std::string> abstractRange(std::size_t first, std::size_t last, std::size_t m6Index) const;
    [[nodiscard]] std::string nearestAbstract(std::size_t from, int step) const;

    std::vector<Rec> recs_;
    std::optional<Profile> profile_;
    Modality mod_;
    ArcStats arcs_;
};

} // namespace gca
