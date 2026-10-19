// gcode-analyzer: public API (skeleton).
// Full module layout is described in docs/SPEC.md; this header is a stub
// that will be split into lexer / modal state / motion / analyzers / report.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gca {

/// Library version (from CMake project version).
[[nodiscard]] std::string version();

/// Where the text report goes.
struct OutputOptions {
    std::optional<std::filesystem::path> outFile; ///< empty -> stdout
};

/// Analysis settings (will grow: dialect profile, rapid feed, etc.).
struct AnalysisOptions {
    std::string dialect = "fanuc-mill"; ///< dialect/profile id
    double rapidFeedMmPerMin = 10000.0; ///< G0 rate for time estimate
};

/// Analyze a program and return the text report.
/// STUB: currently returns a placeholder; to be implemented per docs/SPEC.md.
[[nodiscard]] std::string analyzeFile(const std::filesystem::path& input,
                                      const AnalysisOptions& options);

} // namespace gca
