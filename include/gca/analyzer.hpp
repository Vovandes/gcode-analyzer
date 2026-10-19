// gcode-analyzer: public API (skeleton).
// This header is a stub
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
/// STUB: currently returns a placeholder; to be implemented.
[[nodiscard]] std::string analyzeFile(const std::filesystem::path& input,
                                      const AnalysisOptions& options);

} // namespace gca
