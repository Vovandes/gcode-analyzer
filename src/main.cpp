// gcode-analyzer CLI: thin wrapper over gca_core (see gca::runCli).
#include "gca/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return gca::runCli(args, std::cout, std::cerr);
}
