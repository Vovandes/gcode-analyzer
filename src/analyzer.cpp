#include "gca/analyzer.hpp"
#include "gca/path_util.hpp"

#include "gca/analyzers/code_stats.hpp"
#include "gca/analyzers/bbox.hpp"
#include "gca/analyzers/cycles.hpp"
#include "gca/analyzers/feeds_speeds.hpp"
#include "gca/analyzers/format.hpp"
#include "gca/analyzers/time.hpp"
#include "gca/analyzers/tools.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/analyzers/wcs.hpp"
#include "gca/knowledge.hpp"
#include "gca/modal_state.hpp"
#include "gca/machine_profile.hpp"
#include "gca/analyzers/limits.hpp"
#include "gca/analyzers/machine_time.hpp"

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
std::string findMachineName(const Program& program, std::string_view headerKey) {
    for (const auto& b : program.blocks) {
        for (const auto& c : b.comments) {
            const std::string l = lower(c.text);
            const std::size_t p = l.find(headerKey);
            if (p == std::string::npos) continue;
            std::string name = c.text.substr(p + headerKey.size());
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
    std::string source;
    if (options.dialect == "auto") for (const auto& b : program.blocks) source += b.raw + '\n';
    const IDialect* dialect = options.dialect == "auto" ? detectDialect(source) : findDialect(options.dialect);
    if (!dialect && options.dialect == "auto") dialect = findDialect("fanuc-mill");
    if (dialect == nullptr) throw OptionError("unknown dialect: " + options.dialect);
    if (program.rules.extended != dialect->lexicalRules().extended) {
        if (source.empty()) for (const auto& b : program.blocks) source += b.raw + '\n';
        auto reparsed = parseProgram(source, dialect->lexicalRules());
        reparsed.crlfLines = program.crlfLines; reparsed.lfLines = program.lfLines; reparsed.noEolLines = program.noEolLines;
        auto selected = options; selected.dialect = std::string(dialect->id());
        return analyzeProgram(reparsed, fileName, selected);
    }
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

    report.file.machineName = findMachineName(program, dialect->machineHeaderKey());
    report.toolNames = program.toolNames;
    if (dialect->lexicalRules().extended) {
        std::vector<std::string>* list = nullptr;
        for (const auto& b : program.blocks) for (const auto& c : b.comments) {
            const auto first = c.text.find_first_not_of(" \t");
            auto text = first == std::string::npos ? std::string{} : c.text.substr(first);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
            const auto key = lower(text);
            if (key == "tools list") list = &report.headerTools;
            else if (key == "operations list") list = &report.headerOperations;
            else if (key == "end tools list" || key == "end operations list") list = nullptr;
            else if (list && !text.empty()) list->push_back(text);
        }
    }
    const MachineProfile* machine = nullptr;
    auto machines = loadMachines(*dialect, options.machinesDirectory);
    std::optional<MachineProfile> explicitMachine;
    if (options.machineFile) {
        explicitMachine = readMachineIni(*options.machineFile, machines);
        machine = &*explicitMachine;
    } else if (options.machine.empty() || options.machine == "auto") {
        std::string header = lower(report.file.machineName);
        header.erase(std::remove_if(header.begin(), header.end(), [](unsigned char c) { return std::isspace(c) != 0; }), header.end());
        for (const auto& m : machines) {
            std::string match = lower(m.match);
            match.erase(std::remove_if(match.begin(), match.end(), [](unsigned char c) { return std::isspace(c) != 0; }), match.end());
            if (!match.empty() && header.find(match) != std::string::npos) { machine = &m; break; }
        }
    } else {
        for (const auto& m : machines) if (m.id == options.machine) { machine = &m; break; }
        if (machine == nullptr) throw OptionError("unknown machine: " + options.machine);
    }
    std::optional<MachineProfile> configured;
    if (machine) {
        configured = *machine;
        for (const auto& [name, offset] : options.wcsOffsets) configured->wcsOffsets[name] = offset;
        machine = &*configured;
    }
    if (machine != nullptr) report.file.machineProfile = machine->name;
    report.hasMachineProfile = machine != nullptr;
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
    analyzers.push_back(std::make_unique<FormatAnalyzer>());
    analyzers.push_back(std::make_unique<CodeStatsAnalyzer>(knowledge, report.file.dialectId));
    if (machine) analyzers.push_back(std::make_unique<LimitsAnalyzer>(program, *machine));
    analyzers.push_back(std::make_unique<MachineTimeAnalyzer>(machine, options.rapidFeedMmPerMin));

    ModalMachine machineState(profile);
    for (std::size_t i = 0; i < program.blocks.size(); ++i) {
        const Block& block = program.blocks[i];
        const ModalState before = machineState.state();
        const BlockEffect effect = machineState.apply(block);
        report.segments.insert(report.segments.end(), effect.segments.begin(), effect.segments.end());
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
    const IDialect* dialect = options.dialect == "auto" ? detectDialect(text) : findDialect(options.dialect);
    const Program program = parseProgram(text, dialect ? dialect->lexicalRules() : LexRules{});
    return analyzeProgram(program, fileName, options);
}

Report analyzeFileReport(const std::filesystem::path& input, const AnalysisOptions& options) {
    std::ifstream in(input, std::ios::binary);
    if (!in) throw IoError("cannot open input file: " + pathToUtf8(input));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw IoError("cannot read input file: " + pathToUtf8(input));
    return analyzeText(text, pathToUtf8(input.filename()), options);
}

std::string analyzeFile(const std::filesystem::path& input, const AnalysisOptions& options) {
    return renderTextReport(analyzeFileReport(input, options));
}

} // namespace gca
