// gcode-analyzer CLI: thin wrapper over gca_core.
#include "gca/analyzer.hpp"
#include "gca/dialect.hpp"
#include "gca/lexer.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::ostream& os) {
    os << "Usage: gcode-analyzer <program.nc> [--out report.txt] [--dialect fanuc-mill] [--rapid 10000]\n"
          "       gcode-analyzer --help | --version\n"
          "\n"
          "  --out, -o <file>   write the report to a UTF-8 text file instead of stdout\n"
          "  --dialect <id>     controller profile (default fanuc-mill)\n"
          "  --rapid <mm/min>   rapid traverse rate for the time estimate (default 10000)\n"
          "\n"
          "Exit codes: 0 ok, 1 I/O error, 2 bad arguments\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    gca::AnalysisOptions options;
    gca::OutputOptions output;
    std::optional<std::filesystem::path> input;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const bool hasNext = i + 1 < args.size();
        if (a == "--help" || a == "-h") { printUsage(std::cout); return 0; }
        if (a == "--version") { std::cout << "gcode-analyzer " << gca::version() << '\n'; return 0; }
        if (a == "--out" || a == "-o") {
            if (!hasNext) { std::cerr << "Option " << a << " needs a value\n"; return 2; }
            output.outFile = std::filesystem::path(std::string(args[++i]));
            continue;
        }
        if (a == "--dialect") {
            if (!hasNext) { std::cerr << "Option --dialect needs a value\n"; return 2; }
            options.dialect = std::string(args[++i]);
            continue;
        }
        if (a == "--rapid") {
            if (!hasNext) { std::cerr << "Option --rapid needs a value\n"; return 2; }
            const auto v = gca::parseNumber(args[++i]);
            if (!v || !(*v > 0.0)) { std::cerr << "--rapid must be a positive number\n"; return 2; }
            options.rapidFeedMmPerMin = *v;
            continue;
        }
        if (!a.empty() && a.front() == '-') {
            std::cerr << "Unknown option: " << a << '\n';
            printUsage(std::cerr);
            return 2;
        }
        if (input) { std::cerr << "Only one input file is supported\n"; return 2; }
        input = std::filesystem::path(std::string(a));
    }

    if (!input) { printUsage(std::cerr); return 2; }
    if (gca::findDialect(options.dialect) == nullptr) {
        std::cerr << "Unknown dialect: " << options.dialect << " (available:";
        for (const auto& id : gca::dialectIds()) std::cerr << ' ' << id;
        std::cerr << ")\n";
        return 2;
    }

    std::string report;
    try {
        report = gca::analyzeFile(*input, options);
    } catch (const gca::IoError& e) {
        std::cerr << e.what() << '\n';
        return 1;
    } catch (const gca::OptionError& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }

    if (output.outFile) {
        std::ofstream out(*output.outFile, std::ios::binary);
        if (!out) { std::cerr << "Cannot open output file: " << output.outFile->string() << '\n'; return 1; }
        out << report;
        out.flush();
        if (!out) { std::cerr << "Cannot write output file: " << output.outFile->string() << '\n'; return 1; }
        std::cout << "Report written to " << output.outFile->string() << '\n';
    } else {
        std::cout << report;
    }
    return 0;
}
