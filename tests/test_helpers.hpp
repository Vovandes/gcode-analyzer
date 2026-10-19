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
