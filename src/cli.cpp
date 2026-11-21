#include "gca/cli.hpp"

#include "gca/analyzer.hpp"
#include "gca/comparison.hpp"
#include "gca/dialect.hpp"
#include "gca/lexer.hpp"
#include "gca/options.hpp"
#include "gca/path_util.hpp"
#include "gca/json_report.hpp"
#include "gca/machine_profile.hpp"
#include <algorithm>
#include <cmath>

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>

namespace gca {

namespace {

constexpr const char kUtf8Bom[] = "\xEF\xBB\xBF";

/// Writes text to the file (UTF-8, optional BOM). Returns an exit code; messages go to `err`.
template<class Writer>
int writeOutputFile(const std::filesystem::path& path, bool bom, std::ostream& err, Writer write) {
    std::ofstream file(path, std::ios::binary);
    if (!file) { err << "Не удаётся открыть файл для записи: " << pathToUtf8(path) << '\n'; return kExitIoError; }
    if (bom) file.write(kUtf8Bom, 3);
    write(file);
    file.flush();
    if (!file) { err << "Не удаётся записать файл: " << pathToUtf8(path) << '\n'; return kExitIoError; }
    return kExitOk;
}

int writeFile(const std::filesystem::path& path, const std::string& text, bool bom, std::ostream& err) {
    return writeOutputFile(path, bom, err, [&](std::ostream& file) { file << text; });
}

/// stdout, or the --out file with a short message on stdout.
int emit(const std::string& text, const std::optional<std::filesystem::path>& outFile, bool bom, const char* what,
         std::ostream& out, std::ostream& err) {
    if (!outFile) { out << text; return kExitOk; }
    const int code = writeFile(*outFile, text, bom, err);
    if (code == kExitOk) out << what << " в " << pathToUtf8(*outFile) << '\n';
    return code;
}

std::string machinesText(const std::vector<MachineProfile>& machines) {
    std::string s;
    for (const auto& m : machines) s += m.id + '\t' + m.name + '\n';
    return s;
}

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) return true;
    const auto left = std::filesystem::absolute(a, ec).lexically_normal();
    if (ec) return false;
    const auto right = std::filesystem::absolute(b, ec).lexically_normal();
    return !ec && left == right;
}

} // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
           const std::filesystem::path& machinesDirectory) {
    AnalysisOptions options;
    options.machinesDirectory = machinesDirectory;
    OutputOptions output;
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> comparisonInput;
    std::optional<double> tolerance;
    std::optional<std::filesystem::path> jsonFile;
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
        if (a == "--compare") {
            if (input || args.size() - i < 3 || args[i + 1].empty() || args[i + 2].empty() ||
                args[i + 1].starts_with('-') || args[i + 2].starts_with('-')) {
                err << "Использование: --compare <старая.nc> <новая.nc> без отдельного входного файла\n";
                return kExitBadArguments;
            }
            input = pathFromUtf8(args[++i]);
            comparisonInput = pathFromUtf8(args[++i]);
            continue;
        }
        if (a == "--tolerance") {
            if (!needValue()) return kExitBadArguments;
            tolerance = parseNumber(args[++i]);
            if (!tolerance || !std::isfinite(*tolerance) || *tolerance < 0) {
                err << "--tolerance должно быть конечным неотрицательным числом\n";
                return kExitBadArguments;
            }
            continue;
        }
        if (a == "--json") {
            if (!needValue()) return kExitBadArguments;
            jsonFile = pathFromUtf8(args[++i]);
            continue;
        }
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
        if (a == "--machine-file") {
            if (!needValue()) return kExitBadArguments;
            options.machineFile = pathFromUtf8(args[++i]);
            continue;
        }
        if (a == "--wcs-offset") {
            if (!needValue()) return kExitBadArguments;
            try {
                auto [name, offset] = parseWcsOffset(args[++i]);
                options.wcsOffsets[name] = offset;
            } catch (const OptionError& e) { err << e.what() << '\n'; return kExitBadArguments; }
            continue;
        }
        if (a == "--rapid") {
            if (!needValue()) return kExitBadArguments;
            const auto v = parseNumber(args[++i]);
            if (!v || !(*v > 0.0)) { err << "--rapid должно быть положительным числом\n"; return kExitBadArguments; }
            options.rapidFeedMmPerMin = *v;
            continue;
        }
        if (a == "--safe-z" || a == "--arc-radius-tolerance") {
            if (!needValue()) return kExitBadArguments;
            const auto value = parseNumber(args[++i]);
            if (!value || (a == "--arc-radius-tolerance" && *value < 0)) {
                err << "Недопустимое значение для " << a << '\n';
                return kExitBadArguments;
            }
            if (a == "--safe-z") options.safeZ = *value;
            else options.arcRadiusTolerance = *value;
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

    if (tolerance && !comparisonInput) {
        err << "--tolerance используется вместе с --compare\n";
        return kExitBadArguments;
    }

    const IDialect* dialect = findDialect(options.dialect == "auto" ? "fanuc-mill" : options.dialect);
    if (dialect == nullptr) {
        err << "Неизвестный диалект: " << options.dialect << " (доступно:";
        for (const auto& id : dialectIds()) err << ' ' << id;
        err << ")\n";
        return kExitBadArguments;
    }
    std::vector<MachineProfile> available;
    try {
        available = loadMachines(*dialect, machinesDirectory);
        if (options.machineFile) {
            auto loaded = readMachineIni(*options.machineFile, available);
            bool replaced = false;
            for (auto& m : available) if (m.id == loaded.id) { m = loaded; replaced = true; break; }
            if (!replaced) available.push_back(std::move(loaded));
        }
    } catch (const IoError& e) { err << e.what() << '\n'; return kExitIoError; }
      catch (const OptionError& e) { err << e.what() << '\n'; return kExitBadArguments; }
    if (wantMachines) return emit(machinesText(available), output.outFile, bom, "Список станков записан", out, err);

    if (!input) { err << helpText(); return kExitBadArguments; }
    const bool known = std::any_of(available.begin(), available.end(), [&](const auto& m) { return m.id == options.machine; });
    if (!options.machineFile && options.machine != "auto" && !known) {
        err << "Неизвестный станок: " << options.machine << " (доступно: auto";
        for (const auto& m : available) err << ' ' << m.id;
        err << ")\n";
        return kExitBadArguments;
    }

    if (jsonFile && (sameFile(*jsonFile, *input) ||
                     (output.outFile && sameFile(*jsonFile, *output.outFile)))) {
        err << "Путь --json должен отличаться от входного файла и --out\n";
        return kExitBadArguments;
    }
    if (comparisonInput &&
        ((output.outFile && (sameFile(*output.outFile, *input) || sameFile(*output.outFile, *comparisonInput))) ||
         (jsonFile && sameFile(*jsonFile, *comparisonInput)))) {
        err << "Пути --out и --json должны отличаться от обеих сравниваемых УП\n";
        return kExitBadArguments;
    }

    Report report;
    try {
        report = analyzeFileReport(*input, options);
        if (comparisonInput) {
            const Report after = analyzeFileReport(*comparisonInput, options);
            const auto comparison = compareReports(report, after, tolerance.value_or(kDefaultComparisonTolerance));
            if (jsonFile) {
                const int code = writeOutputFile(*jsonFile, false, err, [&](std::ostream& file) {
                    writeJsonComparison(report, after, comparison, file);
                });
                if (code != kExitOk) return code;
            }
            const auto writeText = [&](std::ostream& stream) { writeTextComparison(report, after, comparison, stream); };
            if (!output.outFile) { writeText(out); return kExitOk; }
            const int code = writeOutputFile(*output.outFile, bom, err, writeText);
            if (code == kExitOk) out << "Отчёт записан в " << pathToUtf8(*output.outFile) << '\n';
            return code;
        }
    } catch (const IoError& e) {
        err << "Ошибка ввода-вывода: " << e.what() << '\n';
        return kExitIoError;
    } catch (const OptionError& e) {
        err << "Ошибка аргументов: " << e.what() << '\n';
        return kExitBadArguments;
    }
    if (jsonFile) {
        const int code = writeFile(*jsonFile, renderJsonReport(report), false, err);
        if (code != kExitOk) return code;
    }
    return emit(renderTextReport(report), output.outFile, bom, "Отчёт записан", out, err);
}

} // namespace gca
