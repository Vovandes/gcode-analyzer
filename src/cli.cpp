#include "gca/cli.hpp"

#include "gca/analyzer.hpp"
#include "gca/dialect.hpp"
#include "gca/lexer.hpp"
#include "gca/options.hpp"
#include "gca/path_util.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>

namespace gca {

namespace {

constexpr const char kUtf8Bom[] = "\xEF\xBB\xBF";

/// Writes text to the file (UTF-8, optional BOM). Returns an exit code; messages go to `err`.
int writeFile(const std::filesystem::path& path, const std::string& text, bool bom, std::ostream& err) {
    std::ofstream file(path, std::ios::binary);
    if (!file) { err << "Не удаётся открыть файл для записи: " << pathToUtf8(path) << '\n'; return kExitIoError; }
    if (bom) file.write(kUtf8Bom, 3);
    file << text;
    file.flush();
    if (!file) { err << "Не удаётся записать файл: " << pathToUtf8(path) << '\n'; return kExitIoError; }
    return kExitOk;
}

/// stdout, or the --out file with a short message on stdout.
int emit(const std::string& text, const std::optional<std::filesystem::path>& outFile, bool bom, const char* what,
         std::ostream& out, std::ostream& err) {
    if (!outFile) { out << text; return kExitOk; }
    const int code = writeFile(*outFile, text, bom, err);
    if (code == kExitOk) out << what << " в " << pathToUtf8(*outFile) << '\n';
    return code;
}

std::string machinesText(const IDialect& dialect) {
    std::string s;
    for (const auto& m : dialect.machines()) s += m.id + '\t' + m.name + '\n';
    return s;
}

} // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    AnalysisOptions options;
    OutputOptions output;
    std::optional<std::filesystem::path> input;
    bool bom = false;
    bool wantHelp = false, wantVersion = false, wantOptions = false, wantMachines = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool hasNext = i + 1 < args.size();
        const auto needValue = [&]() {
            if (!hasNext) err << "Опции " << a << " нужно значение\n";
            return hasNext;
        };
        if (a == "--help" || a == "-h") { wantHelp = true; continue; }
        if (a == "--version") { wantVersion = true; continue; }
        if (a == "--list-options") { wantOptions = true; continue; }
        if (a == "--list-machines") { wantMachines = true; continue; }
        if (a == "--bom") { bom = true; continue; }
        if (a == "--out" || a == "-o") {
            if (!needValue()) return kExitBadArguments;
            output.outFile = pathFromUtf8(args[++i]);
            continue;
        }
        if (a == "--dialect") {
            if (!needValue()) return kExitBadArguments;
            options.dialect = args[++i];
            continue;
        }
        if (a == "--machine") {
            if (!needValue()) return kExitBadArguments;
            options.machine = args[++i];
            continue;
        }
        if (a == "--rapid") {
            if (!needValue()) return kExitBadArguments;
            const auto v = parseNumber(args[++i]);
            if (!v || !(*v > 0.0)) { err << "--rapid должно быть положительным числом\n"; return kExitBadArguments; }
            options.rapidFeedMmPerMin = *v;
            continue;
        }
        if (!a.empty() && a.front() == '-') {
            err << "Неизвестная опция: " << a << '\n' << helpText();
            return kExitBadArguments;
        }
        if (input) { err << "Поддерживается только один входной файл\n"; return kExitBadArguments; }
        input = pathFromUtf8(a);
    }

    if (wantHelp) return emit(helpText(), output.outFile, bom, "Справка записана", out, err);
    if (wantVersion) return emit("gcode-analyzer " + version() + '\n', output.outFile, bom, "Версия записана", out, err);
    if (wantOptions) return emit(optionsListText(), output.outFile, bom, "Список опций записан", out, err);

    const IDialect* dialect = findDialect(options.dialect);
    if (dialect == nullptr) {
        err << "Неизвестный диалект: " << options.dialect << " (доступно:";
        for (const auto& id : dialectIds()) err << ' ' << id;
        err << ")\n";
        return kExitBadArguments;
    }
    if (wantMachines) return emit(machinesText(*dialect), output.outFile, bom, "Список станков записан", out, err);

    if (!input) { err << helpText(); return kExitBadArguments; }
    if (options.machine != "auto" && findMachine(*dialect, options.machine) == nullptr) {
        err << "Неизвестный станок: " << options.machine << " (доступно: auto";
        for (const auto& m : dialect->machines()) err << ' ' << m.id;
        err << ")\n";
        return kExitBadArguments;
    }

    std::string report;
    try {
        report = analyzeFile(*input, options);
    } catch (const IoError& e) {
        err << "Ошибка ввода-вывода: " << e.what() << '\n';
        return kExitIoError;
    } catch (const OptionError& e) {
        err << "Ошибка аргументов: " << e.what() << '\n';
        return kExitBadArguments;
    }
    return emit(report, output.outFile, bom, "Отчёт записан", out, err);
}

} // namespace gca
