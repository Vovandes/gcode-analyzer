#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace gca;
using namespace gca::test;

namespace {

std::vector<int> toolOrder(const Report& r) {
    std::vector<int> v;
    for (const auto& u : r.toolSequence) v.push_back(u.tool);
    return v;
}

} // namespace

TEST_CASE("sample 1: code counters match SPEC section 5", "[samples][stage1]") {
    const Report r = analyze(sample1());
    CHECK(countOf(r.gCodes, "G0") == 181);
    CHECK(countOf(r.gCodes, "G1") == 1179);
    CHECK(countOf(r.gCodes, "G2") == 192);
    CHECK(countOf(r.gCodes, "G3") == 852);
    CHECK(countOf(r.gCodes, "G81") == 42);
    CHECK(countOf(r.gCodes, "G83") == 12);
    CHECK(countOf(r.mCodes, "M6") == 8);
    CHECK(countOf(r.mCodes, "M30") == 1);
    CHECK(r.file.lexerDiagnostics == 0);
}

TEST_CASE("sample 2: code counters match SPEC section 5", "[samples][stage1]") {
    const Report r = analyze(sample2());
    CHECK(countOf(r.gCodes, "G0") == 61);
    CHECK(countOf(r.gCodes, "G1") == 918);
    CHECK(countOf(r.gCodes, "G2") == 570);
    CHECK(countOf(r.gCodes, "G3") == 304);
    CHECK(countOf(r.gCodes, "G42") == 570);
    CHECK(countOf(r.gCodes, "G81") == 14);
    CHECK(countOf(r.gCodes, "G83") == 14);
    CHECK(countOf(r.mCodes, "M6") == 7);
    CHECK(countOf(r.mCodes, "M53") == 1);
}

TEST_CASE("sample tool sequences match SPEC section 5", "[samples][stage1][tools]") {
    const Report r1 = analyze(sample1());
    CHECK(toolOrder(r1) == std::vector<int>{1, 3, 6, 11, 4, 7, 6, 7});
    const Report r2 = analyze(sample2());
    CHECK(toolOrder(r2) == std::vector<int>{4, 6, 12, 8, 6, 14, 15});
}

TEST_CASE("sample tool-change spelling: T1M6 vs M6T4, preselect of the next tool", "[samples][stage1][tools]") {
    const Report r1 = analyze(sample1());
    for (const auto& u : r1.toolSequence) {
        CHECK(u.tBeforeM6);
        CHECK(u.hadTWord);
    }
    CHECK(r1.preselectSequence == std::vector<int>{3, 6, 11, 4, 7, 6, 7});
    // the preselect before every change but the first equals the changed tool
    for (std::size_t i = 1; i < r1.toolSequence.size(); ++i) {
        CHECK(r1.toolSequence[i].preselectedBefore == r1.toolSequence[i].tool);
    }

    const Report r2 = analyze(sample2());
    for (const auto& u : r2.toolSequence) {
        CHECK_FALSE(u.tBeforeM6);
        CHECK(u.hadTWord);
    }
    CHECK(r2.preselectSequence == std::vector<int>{6, 12, 8, 6, 14, 15});
}

TEST_CASE("tool descriptions come from TOOL LIST and from the comment after the change", "[samples][stage1][tools]") {
    const Report r = analyze(sample1());
    REQUIRE_FALSE(r.tools.empty());
    CHECK(r.tools[0].tool == 1);
    REQUIRE(r.tools[0].descriptions.size() >= 1);
    CHECK(r.tools[0].descriptions[0] == "TOROIDALNAYA FREZA D63 L80");
    CHECK(r.tools[0].hValues == std::set<int>{1});
    CHECK(r.tools[0].dValues == std::set<int>{1});
}

TEST_CASE("machine profile is detected from the header comment", "[samples][stage1]") {
    CHECK(analyze(sample1()).file.machineProfile == "Sky Master VL1100A");
    CHECK(analyze(sample2()).file.machineProfile == "Bridgeport 1300/480");
}

TEST_CASE("unknown codes are marked, known are described", "[stage1][codes]") {
    const Report r = analyze("O\nG1 X1. F100.\nG123\nM77\nM30\n");
    bool g123 = false;
    for (const auto& c : r.gCodes) {
        if (c.code == "G123") { g123 = true; CHECK_FALSE(c.known); }
        if (c.code == "G1") CHECK(c.known);
    }
    CHECK(g123);
    for (const auto& c : r.mCodes) {
        if (c.code == "M77") CHECK_FALSE(c.known);
        if (c.code == "M30") CHECK(c.known);
    }
}

TEST_CASE("M15 and M53 are machine specific only for their machine", "[stage1][codes]") {
    const Report r1 = analyze(sample1());
    for (const auto& c : r1.mCodes) {
        if (c.code == "M15") {
            CHECK(c.known);
            CHECK(c.machineSpecific);
            CHECK(c.count == 29);
        }
    }
    const Report r2 = analyze(sample2());
    bool saw53 = false;
    for (const auto& c : r2.mCodes) {
        if (c.code == "M53") { saw53 = true; CHECK(c.known); CHECK(c.machineSpecific); }
    }
    CHECK(saw53);
}

TEST_CASE("G and M statistics are sorted by number", "[stage1][codes]") {
    const Report r = analyze(sample1());
    for (std::size_t i = 1; i < r.gCodes.size(); ++i) {
        CHECK(r.gCodes[i - 1].code != r.gCodes[i].code);
    }
    CHECK(r.gCodes.front().code == "G0");
    CHECK(r.mCodes.front().code == "M0");
    CHECK(r.mCodes.back().code == "M30");
}
