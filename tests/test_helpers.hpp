// Shared helpers for tests.
#pragma once

#include "gca/analyzer.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace gca::test {

inline std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string samplePath(const char* name) {
    return (std::filesystem::path(GCA_SAMPLES_DIR) / name).string();
}

inline std::string sample1() { return readFile(samplePath("sample-01-skymaster-vl1100a.nc")); }
inline std::string sample2() { return readFile(samplePath("sample-02-bridgeport-1300-480.nc")); }

/// Analyze a small synthetic program (default options).
inline Report analyze(const std::string& text, double rapid = 10000.0) {
    AnalysisOptions o;
    o.rapidFeedMmPerMin = rapid;
    return analyzeText(text, "synthetic.nc", o);
}

inline std::size_t countOf(const std::vector<CodeStat>& v, const std::string& code) {
    for (const auto& c : v) {
        if (c.code == code) return c.count;
    }
    return 0;
}

} // namespace gca::test

namespace gca::test {

inline const ToolReport* findTool(const Report& r, int tool) {
    for (const auto& t : r.tools) {
        if (t.tool == tool) return &t;
    }
    return nullptr;
}

} // namespace gca::test

namespace gca::test {

inline std::size_t countWarnings(const Report& r, const char* code) {
    std::size_t n = 0;
    for (const auto& w : r.warnings) {
        if (w.code == code) ++n;
    }
    return n;
}

inline bool hasWarning(const Report& r, const char* code) { return countWarnings(r, code) > 0; }

} // namespace gca::test

namespace gca::test {

/// All cheat-sheet lines of one section (title prefix match) joined with '\n'.
inline std::string cheatSection(const Report& r, const std::string& titlePrefix) {
    for (const auto& s : r.cheatSheet) {
        if (s.title.rfind(titlePrefix, 0) == 0) {
            std::string out;
            for (const auto& l : s.lines) out += l + "\n";
            return out;
        }
    }
    return {};
}

inline bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace gca::test
