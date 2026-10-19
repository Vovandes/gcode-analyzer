// Command-line front end as a library function, so that it can be tested without spawning a process.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace gca {

constexpr int kExitOk = 0;
constexpr int kExitIoError = 1;
constexpr int kExitBadArguments = 2;

/// args = arguments without the program name. Report/help go to `out`, messages about errors to `err`.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace gca
