#include "gca/machine_profile.hpp"
#include "gca/analyzer.hpp"
#include "gca/path_util.hpp"
#include "gca/lexer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace gca {
namespace {
std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1));
}
std::optional<double> numeric(const std::string& value, bool positive = false) {
    if (value.empty()) return std::nullopt;
    auto n = parseNumber(value);
    if (!n || !std::isfinite(*n) || (positive && *n <= 0)) throw OptionError("некорректное число: " + value);
    return n;
}
bool wcsName(std::string_view s) { return s.size() == 3 && s[0] == 'G' && s[1] == '5' && s[2] >= '4' && s[2] <= '9'; }
}

std::pair<std::string, std::array<std::optional<double>, 3>> parseWcsOffset(std::string_view text) {
    const auto eq = text.find('=');
    const std::string name = trim(text.substr(0, eq));
    if (eq == std::string_view::npos || !wcsName(name)) throw OptionError("смещение ожидается в виде G54=X..Y..Z..");
    const std::string value = trim(text.substr(eq + 1));
    const auto program = parseProgram(value);
    std::array<std::optional<double>, 3> result{};
    if (!program.diagnostics.empty() || program.blocks.size() != 1 || !program.blocks[0].comments.empty())
        throw OptionError("некорректное смещение СК");
    for (const auto& word : program.blocks[0].words) {
        const auto at = std::string_view("XYZ").find(word.letter);
        if (at == std::string_view::npos || !word.hasValue || !std::isfinite(word.value) || result[at])
            throw OptionError("смещение допускает только X/Y/Z без повторов");
        result[at] = word.value;
    }
    if (std::none_of(result.begin(), result.end(), [](auto v) { return v.has_value(); }))
        throw OptionError("пустое смещение СК");
    return {name, result};
}

MachineProfile parseMachineIni(std::string_view text, std::string id, const MachineProfile* base) {
    MachineProfile profile = base ? *base : MachineProfile{};
    profile.id = std::move(id);
    if (profile.name.empty()) profile.name = profile.id;
    std::string section;
    std::set<std::string> keys;
    std::map<std::string, std::size_t> lines;
    std::size_t lineNo = 0;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    while (!text.empty()) {
        ++lineNo;
        const auto end = text.find('\n');
        std::string line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        line = trim(std::string_view(line).substr(0, line.find_first_of(";#")));
        if (line.empty()) continue;
        try {
            if (line.front() == '[') {
                if (line.back() != ']') throw OptionError("незакрытая секция");
                section = line.substr(1, line.size() - 2);
                if (section != "axes" && section != "limits" && section != "time" && section != "mcodes")
                    throw OptionError("неизвестная секция: " + section);
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) throw OptionError("ожидается ключ = значение");
            const std::string key = trim(std::string_view(line).substr(0, eq));
            const std::string value = trim(std::string_view(line).substr(eq + 1));
            if (!keys.insert(section + "." + key).second) throw OptionError("повтор ключа: " + key);
            lines[section + "." + key] = lineNo;
            bool found = false;
            if (section.empty()) {
                if (key == "name") { profile.name = value.empty() ? profile.id : value; found = true; }
                if (key == "match") { profile.match = value; found = true; }
            } else if (section == "axes") {
                for (std::size_t a = 0; a < kAxisCount; ++a) {
                    const std::string prefix(1, "xyzbac"[a]);
                    if (key == prefix + "_min") { profile.axes[a].min = numeric(value); found = true; }
                    if (key == prefix + "_max") { profile.axes[a].max = numeric(value); found = true; }
                    if (key == prefix + "_home") { profile.axes[a].home = numeric(value); found = true; }
                    if (key == prefix + "_home30") { profile.axes[a].home30 = numeric(value); found = true; }
                }
                if (wcsName(key)) {
                    if (value.empty()) profile.wcsOffsets.erase(key);
                    else profile.wcsOffsets[key] = parseWcsOffset(key + "=" + value).second;
                    found = true;
                }
            } else if (section == "limits") {
                if (key == "feed_max") { profile.feedMax = numeric(value, true); found = true; }
                if (key == "spindle_max") { profile.spindleMax = numeric(value, true); found = true; }
            } else if (section == "time") {
                for (std::size_t a = 0; a < kAxisCount; ++a) {
                    const std::string suffix(1, "xyzbac"[a]);
                    if (key == "rapid_" + suffix) { profile.axes[a].rapid = numeric(value, true); found = true; }
                    if (key == "accel_" + suffix) { profile.axes[a].acceleration = numeric(value, true); found = true; }
                    if (key == "rapid_accel_" + suffix) { profile.axes[a].rapidAcceleration = numeric(value, true); found = true; }
                }
                const std::pair<std::string_view, std::optional<double>*> times[] = {
                    {"tool_change", &profile.toolChangeSec}, {"b_seconds_per_90", &profile.bSecondsPer90},
                    {"spindle_start", &profile.spindleStartSec}, {"g28_time", &profile.g28Time}};
                for (const auto& [name, target] : times) if (key == name) {
                    *target = numeric(value);
                    if (*target && **target < 0) throw OptionError("время не может быть отрицательным");
                    found = true;
                }
                if (key == "junction_angle") {
                    profile.junctionAngle = numeric(value).value_or(30.0);
                    if (profile.junctionAngle < 0 || profile.junctionAngle > 180) throw OptionError("угол должен быть от 0 до 180");
                    found = true;
                }
                if (key == "skip_first_change") {
                    if (value != "true" && value != "false" && !value.empty()) throw OptionError("ожидается true/false");
                    profile.skipFirstChange = value == "true"; found = true;
                }
            } else if (section == "mcodes" && key.starts_with('M')) {
                const auto code = numeric(key.substr(1));
                if (!code || *code < 0 || *code > 99999 || std::floor(*code) != *code) throw OptionError("неверный M-код");
                const int m = static_cast<int>(*code);
                auto existing = std::find_if(profile.mCodes.begin(), profile.mCodes.end(), [m](const auto& c) { return c.code == m; });
                if (value.empty()) {
                    if (existing != profile.mCodes.end()) profile.mCodes.erase(existing);
                } else {
                    const auto bar = value.find('|');
                    if (bar == std::string::npos) throw OptionError("M-код: класс | описание");
                    const std::string cls = trim(std::string_view(value).substr(0, bar));
                    const std::map<std::string, MClass> classes = {
                        {"coolant_on", MClass::CoolantOn}, {"coolant_off", MClass::CoolantOff}, {"coolant_extra", MClass::CoolantExtra},
                        {"tool_change", MClass::ToolChange}, {"program_stop", MClass::ProgramStop}, {"optional_stop", MClass::OptionalStop},
                        {"program_end", MClass::ProgramEnd}, {"spindle_cw", MClass::SpindleCw}, {"spindle_ccw", MClass::SpindleCcw},
                        {"spindle_stop", MClass::SpindleStop}, {"spindle_orient", MClass::SpindleOrient},
                        {"sub_call", MClass::SubCall}, {"sub_return", MClass::SubReturn}, {"turning_cancel", MClass::TurningCancel}, {"other", MClass::Other}};
                    const auto c = classes.find(cls);
                    if (c == classes.end()) throw OptionError("неизвестный класс M-кода: " + cls);
                    MachineProfile::MCode mc{m, c->second, trim(std::string_view(value).substr(bar + 1))};
                    if (mc.description.empty()) throw OptionError("пустое описание M-кода");
                    if (existing == profile.mCodes.end()) profile.mCodes.push_back(mc); else *existing = mc;
                }
                found = true;
            }
            if (!found) throw OptionError("неизвестный ключ: " + key);
        } catch (const OptionError& e) { throw OptionError("строка " + std::to_string(lineNo) + ": " + e.what()); }
    }
    for (std::size_t a = 0; a < kAxisCount; ++a) {
        const auto& axis = profile.axes[a];
        if (axis.min && axis.max && *axis.min > *axis.max) {
            const std::string prefix = "axes." + std::string(1, "xyzbac"[a]);
            const auto n = std::max(lines[prefix + "_min"], lines[prefix + "_max"]);
            throw OptionError("строка " + std::to_string(n) + ": минимум оси больше максимума");
        }
    }
    return profile;
}

MachineProfile readMachineIni(const std::filesystem::path& file, const std::vector<MachineProfile>& bases) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw IoError("не удаётся открыть профиль: " + pathToUtf8(file));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw IoError("не удаётся прочитать профиль: " + pathToUtf8(file));
    const auto id = pathToUtf8(file.stem());
    const auto base = std::find_if(bases.begin(), bases.end(), [&](const auto& m) { return m.id == id; });
    try { return parseMachineIni(text, id, base == bases.end() ? nullptr : &*base); }
    catch (const OptionError& e) { throw OptionError(pathToUtf8(file) + ": " + e.what()); }
}

std::vector<MachineProfile> loadMachines(const IDialect& dialect, const std::filesystem::path& directory) {
    auto machines = dialect.machines();
    if (directory.empty()) return machines;
    std::error_code ec;
    const bool exists = std::filesystem::exists(directory, ec);
    if (ec) throw IoError("не удаётся проверить каталог профилей: " + pathToUtf8(directory));
    if (!exists) return machines;
    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator iter(directory, ec), end;
    if (ec) throw IoError("не удаётся открыть каталог профилей: " + pathToUtf8(directory));
    for (; iter != end; iter.increment(ec)) {
        if (ec) throw IoError("не удаётся прочитать каталог профилей: " + pathToUtf8(directory));
        if (iter->path().extension() == ".ini") files.push_back(iter->path());
    }
    if (ec) throw IoError("не удаётся прочитать каталог профилей: " + pathToUtf8(directory));
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        auto profile = readMachineIni(file, machines);
        auto old = std::find_if(machines.begin(), machines.end(), [&](const auto& m) { return m.id == profile.id; });
        if (old == machines.end()) machines.push_back(std::move(profile)); else *old = std::move(profile);
    }
    return machines;
}
} // namespace gca
