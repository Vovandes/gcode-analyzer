// Analyzer = observer of the modal machine. New analyzers are added to the pipeline
// without touching the core.
#pragma once

#include "gca/dialect.hpp"
#include "gca/lexer.hpp"
#include "gca/modal_state.hpp"
#include "gca/report.hpp"

#include <memory>
#include <vector>

namespace gca {

struct BlockContext {
    const Block& block;
    std::size_t index;              ///< index in Program::blocks
    const ModalState& before;
    const ModalState& after;
    const BlockEffect& effect;
    const Profile& profile;
};

class IAnalyzer {
public:
    virtual ~IAnalyzer() = default;
    virtual void onBlock(const BlockContext&) {}
    virtual void onSegment(const Segment&, const BlockContext&) {}
    virtual void onToolChange(const ToolEvent&, const BlockContext&) {}
    /// Write results into the report model.
    virtual void finish(Report& report) = 0;
};

using AnalyzerList = std::vector<std::unique_ptr<IAnalyzer>>;

} // namespace gca
