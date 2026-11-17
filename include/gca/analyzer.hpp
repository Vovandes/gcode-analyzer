// gcode-analyzer: public API.
#pragma once

#include "gca/lexer.hpp"
#include "gca/report.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gca {

/// Library version (from CMake project version).
[[nodiscard]] std::string version();

/// Where the text report goes.
struct OutputOptions {
    std::optional<std::filesystem::path> outFile; ///< empty -> stdout
};

/// Analysis settings.
struct AnalysisOptions {
    std::string dialect = "auto"; ///< header detection, falling back to fanuc-mill
    double rapidFeedMmPerMin = 10000.0; ///< G0 rate for time estimate
    std::string machine = "auto";       ///< machine profile id, "auto" = detect by "Generated for ..."
    std::filesystem::path machinesDirectory;
    std::optional<std::filesystem::path> machineFile;
    std::map<std::string, std::array<std::optional<double>, 3>> wcsOffsets;
};

/// Input file cannot be read.
struct IoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
/// Bad options (unknown dialect or machine, non-positive rapid feed).
struct OptionError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/// Run the whole pipeline on an already parsed program.
[[nodiscard]] Report analyzeProgram(const Program& program, const std::string& fileName,
                                    const AnalysisOptions& options);
/// Convenience: text of a program -> Report.
[[nodiscard]] Report analyzeText(std::string_view text, const std::string& fileName,
                                 const AnalysisOptions& options);
/// Read a file, analyze it, return the text report. Throws IoError / OptionError.
[[nodiscard]] std::string analyzeFile(const std::filesystem::path& input, const AnalysisOptions& options);
/// Read and analyze once; the same model feeds text and JSON writers.
[[nodiscard]] Report analyzeFileReport(const std::filesystem::path& input, const AnalysisOptions& options);

} // namespace gca
