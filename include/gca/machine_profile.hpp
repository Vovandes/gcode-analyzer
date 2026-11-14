#pragma once

#include "gca/dialect.hpp"
#include <filesystem>

namespace gca {
// Empty values explicitly clear inherited parameters. Errors include the source line.
[[nodiscard]] MachineProfile parseMachineIni(std::string_view text, std::string id,
                                             const MachineProfile* base = nullptr);
[[nodiscard]] MachineProfile readMachineIni(const std::filesystem::path& file,
                                            const std::vector<MachineProfile>& bases);
[[nodiscard]] std::vector<MachineProfile> loadMachines(const IDialect& dialect,
                                                       const std::filesystem::path& directory);
[[nodiscard]] std::pair<std::string, std::array<std::optional<double>, 3>> parseWcsOffset(std::string_view text);
} // namespace gca
