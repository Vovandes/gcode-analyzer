// UTF-8 <-> std::filesystem::path helpers. On Windows path::string() uses the ANSI code page and can throw or
// garble Cyrillic, so all paths that come from the command line or go to messages pass through these functions.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gca {

/// UTF-8 text (command line, GUI) -> path.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    std::u8string s;
    s.reserve(utf8.size());
    for (const char c : utf8) s.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(s);
}

/// path -> UTF-8 text (messages, report).
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    std::string r;
    r.reserve(s.size());
    for (const char8_t c : s) r.push_back(static_cast<char>(c));
    return r;
}

} // namespace gca
