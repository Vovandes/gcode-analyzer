// gcode-analyzer CLI: thin wrapper over gca_core (see gca::runCli).
// Windows-only part: UTF-8 console and UTF-8 command-line arguments (so Cyrillic paths and the Russian
// report work in cmd and PowerShell without chcp). Nothing platform-specific lives in gca_core.
#include "gca/cli.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace {

/// Switches the console to UTF-8 and restores the previous code pages on exit (RAII).
class ConsoleUtf8 {
public:
    ConsoleUtf8() : oldOut_(GetConsoleOutputCP()), oldIn_(GetConsoleCP()) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
    ~ConsoleUtf8() {
        if (oldOut_ != 0) SetConsoleOutputCP(oldOut_);
        if (oldIn_ != 0) SetConsoleCP(oldIn_);
    }
    ConsoleUtf8(const ConsoleUtf8&) = delete;
    ConsoleUtf8& operator=(const ConsoleUtf8&) = delete;

private:
    UINT oldOut_;
    UINT oldIn_;
};

std::string wideToUtf8(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    s.pop_back(); // terminating zero
    return s;
}

/// Command-line arguments (without the program name) in UTF-8, independent of the ANSI code page.
std::vector<std::string> utf8Arguments() {
    std::vector<std::string> args;
    int argc = 0;
    wchar_t** argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvW == nullptr) return args;
    for (int i = 1; i < argc; ++i) args.push_back(wideToUtf8(argvW[i]));
    LocalFree(argvW);
    return args;
}

} // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    const ConsoleUtf8 console;
    const std::vector<std::string> args = utf8Arguments();
#else
    const std::vector<std::string> args(argv + 1, argv + argc);
#endif
    // Full buffering: multi-byte characters are not split at write boundaries on the console.
    static char buffer[1 << 16];
    std::setvbuf(stdout, buffer, _IOFBF, sizeof buffer);
    const int code = gca::runCli(args, std::cout, std::cerr);
    std::cout.flush();
    std::fflush(stdout);
    return code;
}
