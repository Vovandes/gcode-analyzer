#include "gca/cli.hpp"

#include "gca/analyzer.hpp"
#include "gca/dialect.hpp"
#include "gca/lexer.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>

namespace gca {

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

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    AnalysisOptions options;
    OutputOptions output;
    std::optional<std::filesystem::path> input;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool hasNext = i + 1 < args.size();
        if (a == "--help" || a == "-h") { printUsage(out); return kExitOk; }
        if (a == "--version") { out << "gcode-analyzer " << version() << '\n'; return kExitOk; }
        if (a == "--out" || a == "-o") {
            if (!hasNext) { err << "Option " << a << " needs a value\n"; return kExitBadArguments; }
            output.outFile = std::filesystem::path(args[++i]);
            continue;
        }
        if (a == "--dialect") {
            if (!hasNext) { err << "Option --dialect needs a value\n"; return kExitBadArguments; }
            options.dialect = args[++i];
            continue;
        }
        if (a == "--rapid") {
            if (!hasNext) { err << "Option --rapid needs a value\n"; return kExitBadArguments; }
            const auto v = parseNumber(args[++i]);
            if (!v || !(*v > 0.0)) { err << "--rapid must be a positive number\n"; return kExitBadArguments; }
            options.rapidFeedMmPerMin = *v;
            continue;
        }
        if (!a.empty() && a.front() == '-') {
            err << "Unknown option: " << a << '\n';
            printUsage(err);
            return kExitBadArguments;
        }
        if (input) { err << "Only one input file is supported\n"; return kExitBadArguments; }
        input = std::filesystem::path(a);
    }

    if (!input) { printUsage(err); return kExitBadArguments; }
    if (findDialect(options.dialect) == nullptr) {
        err << "Unknown dialect: " << options.dialect << " (available:";
        for (const auto& id : dialectIds()) err << ' ' << id;
        err << ")\n";
        return kExitBadArguments;
    }

    std::string report;
    try {
        report = analyzeFile(*input, options);
    } catch (const IoError& e) {
        err << e.what() << '\n';
        return kExitIoError;
    } catch (const OptionError& e) {
        err << e.what() << '\n';
        return kExitBadArguments;
    }

    if (output.outFile) {
        std::ofstream file(*output.outFile, std::ios::binary);
        if (!file) { err << "Cannot open output file: " << output.outFile->string() << '\n'; return kExitIoError; }
        file << report;
        file.flush();
        if (!file) { err << "Cannot write output file: " << output.outFile->string() << '\n'; return kExitIoError; }
        out << "Report written to " << output.outFile->string() << '\n';
    } else {
        out << report;
    }
    return kExitOk;
}

} // namespace gca
