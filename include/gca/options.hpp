// The single table of command-line options: used by --help, --list-options (and so by the GUI).
#pragma once

#include <string>
#include <vector>

namespace gca {

struct OptionInfo {
    std::string name;         ///< "--out"
    std::string alias;        ///< "-o" or empty
    std::string value;        ///< value placeholder, e.g. "<файл>"; empty for flags
    std::string defaultValue; ///< default shown to the user; empty if none
    std::string description;  ///< Russian description
};

[[nodiscard]] const std::vector<OptionInfo>& cliOptions();

/// Text of --help (usage + table + exit codes), Russian, built from cliOptions().
[[nodiscard]] std::string helpText();

/// Machine-readable table: one option per line, fields separated by TAB:
/// name, alias, value, default, description.
[[nodiscard]] std::string optionsListText();

} // namespace gca
