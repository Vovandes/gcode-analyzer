#include "gca/analyzer.hpp"

#include "gca/analyzers/code_stats.hpp"
#include "gca/analyzers/bbox.hpp"
#include "gca/analyzers/cycles.hpp"
#include "gca/analyzers/feeds_speeds.hpp"
#include "gca/analyzers/time.hpp"
#include "gca/analyzers/tools.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/analyzers/wcs.hpp"
#include "gca/knowledge.hpp"
#include "gca/modal_state.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace gca {

std::string version() { return GCA_VERSION; }

ToolReport& Report::tool(int number) {
    for (auto& t : tools) {
        if (t.tool == number) return t;
    }
    tools.emplace_back();
    tools.back().tool = number;
    return tools.back();
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Text after "Generated for" in a header comment ("=== Generated for Sky Master VL1100A ===").
std::string findMachineName(const Program& program) {
    for (const auto& b : program.blocks) {
        for (const auto& c : b.comments) {
            const std::string l = lower(c.text);
            const std::size_t p = l.find("generated for");
            if (p == std::string::npos) continue;
            std::string name = c.text.substr(p + std::string("generated for").size());
            const auto bad = [](char ch) { return ch == '=' || ch == ' ' || ch == '\t'; };
            while (!name.empty() && bad(name.front())) name.erase(name.begin());
            while (!name.empty() && bad(name.back())) name.pop_back();
            return name;
        }
        for (const auto& w : b.words) {
            if (w.letter == 'M' && w.hasValue && w.value == 6.0) return {};
        }
    }
    return {};
}

} // namespace

Report analyzeProgram(const Program& program, const std::string& fileName, const AnalysisOptions& options) {
    const IDialect* dialect = findDialect(options.dialect);
    if (dialect == nullptr) throw OptionError("unknown dialect: " + options.dialect);
    if (!(options.rapidFeedMmPerMin > 0.0)) throw OptionError("rapid feed must be positive");

    Report report;
    report.file.fileName = fileName;
    report.file.dialectId = std::string(dialect->id());
    report.file.dialectName = std::string(dialect->name());
    report.file.lines = program.blocks.size();
    report.file.crlfLines = program.crlfLines;
    report.file.lfLines = program.lfLines;
    report.file.lexerDiagnostics = program.diagnostics.size();
    report.file.rapidFeed = options.rapidFeedMmPerMin;
    for (const auto& b : program.blocks) {
        if (!b.isBlank()) ++report.file.blocks;
    }
    for (const auto& d : program.diagnostics) {
        report.diagnostics.push_back("строка " + std::to_string(d.lineNo) + ": " + d.message);
    }

    report.file.machineName = findMachineName(program);
    const MachineProfile* machine = detectMachine(*dialect, report.file.machineName);
    if (machine != nullptr) report.file.machineProfile = machine->name;
    const Profile profile(*dialect, machine);

    KnowledgeChain knowledge;
    knowledge.add(std::make_unique<BuiltinKnowledgeProvider>(profile));
    knowledge.add(std::make_unique<OnlineKnowledgeProviderStub>()); // extension point, not implemented

    AnalyzerList analyzers;
    analyzers.push_back(std::make_unique<ToolsAnalyzer>());
    analyzers.push_back(std::make_unique<TimeAnalyzer>(options.rapidFeedMmPerMin));
    analyzers.push_back(std::make_unique<BBoxAnalyzer>());
    analyzers.push_back(std::make_unique<WcsAnalyzer>());
    analyzers.push_back(std::make_unique<FeedsSpeedsAnalyzer>());
    analyzers.push_back(std::make_unique<CyclesAnalyzer>());
    analyzers.push_back(std::make_unique<WarningsAnalyzer>(program));
    analyzers.push_back(std::make_unique<CodeStatsAnalyzer>(knowledge, report.file.dialectId));

    ModalMachine machineState(profile);
    for (std::size_t i = 0; i < program.blocks.size(); ++i) {
        const Block& block = program.blocks[i];
        const ModalState before = machineState.state();
        const BlockEffect effect = machineState.apply(block);
        const BlockContext ctx{block, i, before, machineState.state(), effect, profile};
        for (auto& a : analyzers) {
            a->onBlock(ctx);
            for (const auto& s : effect.segments) a->onSegment(s, ctx);
            for (const auto& e : effect.toolEvents) a->onToolChange(e, ctx);
        }
    }
    for (auto& a : analyzers) a->finish(report);
    return report;
}

Report analyzeText(std::string_view text, const std::string& fileName, const AnalysisOptions& options) {
    const Program program = parseProgram(text);
    return analyzeProgram(program, fileName, options);
}

std::string analyzeFile(const std::filesystem::path& input, const AnalysisOptions& options) {
    std::ifstream in(input, std::ios::binary);
    if (!in) throw IoError("cannot open input file: " + input.string());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw IoError("cannot read input file: " + input.string());
    const Report report = analyzeText(text, input.filename().string(), options);
    return renderTextReport(report);
}

} // namespace gca
