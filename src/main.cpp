// gcode-analyzer CLI (skeleton). Argument parsing is intentionally minimal;
// may be replaced by CLI11 (FetchContent) during implementation.
#include "gca/analyzer.hpp"

#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void printUsage() {
    std::cout << "Usage: gcode-analyzer <program.nc> [--out report.txt] [--dialect fanuc-mill]\n"
                 "       gcode-analyzer --help | --version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    gca::AnalysisOptions options;
    gca::OutputOptions output;
    std::optional<std::filesystem::path> input;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto a = args[i];
        if (a == "--help" || a == "-h") { printUsage(); return 0; }
        if (a == "--version")           { std::cout << gca::version() << '\n'; return 0; }
        if ((a == "--out" || a == "-o") && i + 1 < args.size()) { output.outFile = std::filesystem::path(args[++i]); continue; }
        if (a == "--dialect" && i + 1 < args.size())            { options.dialect = std::string(args[++i]); continue; }
        if (!a.empty() && a.front() == '-') { std::cerr << "Unknown option: " << a << '\n'; printUsage(); return 2; }
        input = std::filesystem::path(a);
    }

    if (!input) { printUsage(); return 2; }

    const std::string report = gca::analyzeFile(*input, options);

    if (output.outFile) {
        std::ofstream out(*output.outFile, std::ios::binary);
        if (!out) { std::cerr << "Cannot open output file: " << output.outFile->string() << '\n'; return 1; }
        out << report;
    } else {
        std::cout << report;
    }
    return 0;
}
