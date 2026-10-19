#include "gca/analyzer.hpp"

namespace gca {

std::string version() { return GCA_VERSION; }

std::string analyzeFile(const std::filesystem::path& input, const AnalysisOptions& options) {
    // TODO(stage 1+): lexer -> modal state machine -> motion -> analyzers -> report.
    return "gcode-analyzer " + version() + "\n"
         + "input:   " + input.string() + "\n"
         + "dialect: " + options.dialect + "\n"
         + "NOT IMPLEMENTED YET (see docs/SPEC.md)\n";
}

} // namespace gca
