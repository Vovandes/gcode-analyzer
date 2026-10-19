#include "gca/analyzers/warnings.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gca;
using namespace gca::test;
using Catch::Approx;

namespace {

// Clean baseline: WCS, tool, length offset, spindle, feed.
const char* kBase =
    "O\n"
    "G21 G90 G17 G94\n"
    "G92 X0 Y0 Z0\n"
    "G54\n"
    "T1M6\n"
    "G0 G43 Z10. H1\n"
    "S1000 M3\n";

std::string prog(const std::string& body, const std::string& tail = "M5\nM30\n") {
    return std::string(kBase) + body + tail;
}

} // namespace

TEST_CASE("baseline program has no warnings", "[warnings][stage3]") {
    const Report r = analyze(prog("G0 X10.\nG1 X20. F100.\n"));
    CHECK(r.warnings.empty());
}

TEST_CASE("W001 motion before any WCS", "[warnings][stage3]") {
    const Report bad = analyze("O\nG92 X0 Y0 Z0\nT1M6\nS1000 M3\nG0 X10.\nG1 X20. F100.\nM30\n");
    CHECK(countWarnings(bad, warn::kNoWcs) == 1);     // reported once
    CHECK(bad.wcsNotes.size() >= 1);
    CHECK_FALSE(hasWarning(analyze(prog("G0 X10.\n")), warn::kNoWcs));
}

TEST_CASE("W002 zero offset: G54.1 P0, G54 P0, G54.1 without P", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G54.1 P0\nG0 X1.\n")), warn::kWcsZero));
    CHECK(hasWarning(analyze(prog("G54 P0\nG0 X1.\n")), warn::kWcsZero));
    CHECK(hasWarning(analyze(prog("G54.1\nG0 X1.\n")), warn::kWcsZero));
    CHECK_FALSE(hasWarning(analyze(prog("G54.1 P3\nG0 X1.\n")), warn::kWcsZero));
    CHECK_FALSE(hasWarning(analyze(prog("G55\nG0 X1.\n")), warn::kWcsZero));
}

TEST_CASE("W003 WCS change inside a tool operation", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G0 X1.\nG55\nG0 X2.\n")), warn::kWcsChange));
    // selecting a different WCS before the first motion of the operation is fine
    CHECK_FALSE(hasWarning(analyze("O\nG92 X0 Y0 Z0\nG54\nT1M6\nG55\nG0 X1.\nM30\n"), warn::kWcsChange));
    // same WCS repeated after the tool change is normal
    CHECK_FALSE(hasWarning(analyze(prog("G0 X1.\nT2M6\nG54\nG0 X2.\n")), warn::kWcsChange));
}

TEST_CASE("W004 cutting without spindle / without S", "[warnings][stage3]") {
    const std::string noSpindle = "O\nG92 X0 Y0 Z0\nG54\nT1M6\nG1 X10. F100.\nM30\n";
    CHECK(hasWarning(analyze(noSpindle), warn::kNoSpindle));
    const std::string noS = "O\nG92 X0 Y0 Z0\nG54\nT1M6\nM3\nG1 X10. F100.\nM30\n";
    CHECK(hasWarning(analyze(noS), warn::kNoSpindle));
    const std::string stopped = prog("M5\nG1 X10. F100.\n");
    CHECK(hasWarning(analyze(stopped), warn::kNoSpindle));
    CHECK_FALSE(hasWarning(analyze(prog("G0 X10.\n")), warn::kNoSpindle));   // rapid is fine
    CHECK_FALSE(hasWarning(analyze(prog("G1 X10. F100.\n")), warn::kNoSpindle));
}

TEST_CASE("W005 cutting move without F", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G1 X10.\n")), warn::kNoFeed));
    CHECK(hasWarning(analyze(prog("G2 X10. Y10. I5. J0.\n")), warn::kNoFeed));
    CHECK_FALSE(hasWarning(analyze(prog("G1 X10. F100.\n")), warn::kNoFeed));
    CHECK_FALSE(hasWarning(analyze(prog("G0 X10.\n")), warn::kNoFeed));
}

TEST_CASE("W006 G43 without H, W007 H differs from T", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G43 Z10.\n")), warn::kG43NoH));
    CHECK_FALSE(hasWarning(analyze(prog("G43 Z10. H1\n")), warn::kG43NoH));
    CHECK(hasWarning(analyze(prog("G43 Z10. H2\n")), warn::kHNotT));
    CHECK_FALSE(hasWarning(analyze(prog("G43 Z10. H1\n")), warn::kHNotT));
    CHECK(hasWarning(analyze("O\nG43 Z10. H1\nM30\n"), warn::kHNotT));          // before any tool change
}

TEST_CASE("W008 tool change with cutter compensation active, W009 compensation during G0", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G1 G41 D1 X5. F100.\nT2M6\n")), warn::kChangeComp));
    CHECK_FALSE(hasWarning(analyze(prog("G1 G41 D1 X5. F100.\nG40 X6.\nT2M6\n")), warn::kChangeComp));
    CHECK(hasWarning(analyze(prog("G1 G41 D1 X5. F100.\nG0 X9.\n")), warn::kCompRapid));
    CHECK_FALSE(hasWarning(analyze(prog("G0 X5.\nG1 G41 D1 X9. F100.\n")), warn::kCompRapid));
}

TEST_CASE("W010 G91 left active after G28", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G91 G28 Z0\nG1 X5. F100.\n")), warn::kG91AfterG28));
    CHECK_FALSE(hasWarning(analyze(prog("G91 G28 Z0\nG90\nG1 X5. F100.\n")), warn::kG91AfterG28));
    // as in the samples: G91 G28 ..., G90 afterwards
    CHECK_FALSE(hasWarning(analyze(prog("G91 G28 Z0\nG91 G28 Y0\nG90 G49 H0\n")), warn::kG91AfterG28));
    // program ends in G91
    CHECK(hasWarning(analyze(prog("G91 G28 Z0\n")), warn::kG91AfterG28));
    // an intentional incremental program is not flagged
    CHECK_FALSE(hasWarning(analyze(prog("G91\nG1 X5. F100.\nG1 Y5.\nG90\n")), warn::kG91AfterG28));
}

TEST_CASE("W011 cycle not cancelled before tool change / end", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G81 X5. Z-3. R2. F100.\nT2M6\n")), warn::kCycleActive));
    CHECK(hasWarning(analyze(prog("G81 X5. Z-3. R2. F100.\n")), warn::kCycleActive));         // at M30
    CHECK_FALSE(hasWarning(analyze(prog("G81 X5. Z-3. R2. F100.\nG80\nT2M6\n")), warn::kCycleActive));
}

TEST_CASE("W012 unknown codes, W013 unparsed text, W014 inch", "[warnings][stage3]") {
    const Report r = analyze(prog("G123\nG123\nM77\nG0 X1.\n"));
    CHECK(countWarnings(r, warn::kUnknownCode) == 2);       // one per distinct code
    CHECK(r.warnings.front().line > 0);
    CHECK_FALSE(hasWarning(analyze(prog("G0 X1.\n")), warn::kUnknownCode));
    CHECK(hasWarning(analyze(prog("G1 X1. F100. #5=3\n")), warn::kUnparsed));
    CHECK(hasWarning(analyze(prog("G1 E5. X1. F100.\n")), warn::kUnparsed));
    CHECK(hasWarning(analyze(prog("G20\nG0 X1.\n")), warn::kInch));
    CHECK_FALSE(hasWarning(analyze(prog("G21\nG0 X1.\n")), warn::kInch));
}

TEST_CASE("W015 coordinates without motion code, W016 incomplete cycle, W017 M6 without T", "[warnings][stage3]") {
    CHECK(hasWarning(analyze("O\nX5.\nM30\n"), warn::kNoMotionCode));
    CHECK_FALSE(hasWarning(analyze("O\nG0 X5.\nM30\n"), warn::kNoMotionCode));
    CHECK(hasWarning(analyze(prog("G81 X5.\nG80\n")), warn::kCycleIncomplete));
    CHECK_FALSE(hasWarning(analyze(prog("G81 X5. Z-3. R2. F100.\nG80\n")), warn::kCycleIncomplete));
    CHECK(hasWarning(analyze("O\nM6\nM30\n"), warn::kChangeNoTool));
    CHECK_FALSE(hasWarning(analyze("O\nT2\nM6\nM30\n"), warn::kChangeNoTool));
}

TEST_CASE("W018 arc radius mismatch", "[warnings][stage3]") {
    CHECK(hasWarning(analyze(prog("G0 X0. Y0.\nG2 X10. Y0. I-3. J0. F100.\n")), warn::kArc));
    CHECK_FALSE(hasWarning(analyze(prog("G0 X10. Y0.\nG3 X0. Y10. I-10. J0. F100.\n")), warn::kArc));
}

TEST_CASE("warnings carry line number and block text and are sorted", "[warnings][stage3]") {
    const Report r = analyze(prog("G1 X10.\nG123\n"));
    REQUIRE(r.warnings.size() >= 2);
    for (std::size_t i = 1; i < r.warnings.size(); ++i) CHECK(r.warnings[i - 1].line <= r.warnings[i].line);
    CHECK(r.warnings.front().text.empty() == false);
}

TEST_CASE("samples produce no warnings", "[samples][stage3]") {
    CHECK(analyze(sample1()).warnings.empty());
    CHECK(analyze(sample2()).warnings.empty());
}

TEST_CASE("WCS usage: G54 found in the samples", "[samples][stage3][wcs]") {
    for (const auto& text : {sample1(), sample2()}) {
        const Report r = analyze(text);
        REQUIRE(r.wcsUsage.size() == 1);
        CHECK(r.wcsUsage[0].wcs == "G54");
        CHECK(r.wcsUsage[0].selections >= 7);
        CHECK(r.wcsUsage[0].moves > 1000);
    }
}

TEST_CASE("WCS notes: different WCS per tool is reported", "[stage3][wcs]") {
    const Report r = analyze(
        "O\nG92 X0 Y0 Z0\nT1M6\nS1000 M3\nG54\nG1 X5. F100.\nT2M6\nG55\nG1 X6. F100.\nM30\n");
    REQUIRE(r.wcsNotes.size() >= 1);
    CHECK(r.wcsNotes[0].find("Разные СК") != std::string::npos);
    CHECK(findTool(r, 1)->wcs.count("G54") == 1);
    CHECK(findTool(r, 2)->wcs.count("G55") == 1);
    const Report ext = analyze("O\nG92 X0 Y0 Z0\nT1M6\nS1000 M3\nG54.1 P3\nG1 X5. F100.\nM30\n");
    CHECK(ext.wcsUsage.at(0).wcs == "G54.1 P3");
}

TEST_CASE("feeds and speeds per tool", "[stage3][feeds]") {
    const Report r = analyze(
        "O\nG92 X0 Y0 Z0\nG54\n"
        "T1M6\nS4200.5 M3\nM8\nG1 X5. F1500.\nF900.\nG0 X0.\n"
        "G95\nG1 X5. F0.05\nM9\n"
        "T2M6\nG94 S8000 M4\nM7\nG1 X7. F300.\nM9\nM30\n");
    const ToolReport* t1 = findTool(r, 1);
    const ToolReport* t2 = findTool(r, 2);
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    CHECK(t1->feedPerMin.min == Approx(900.0));
    CHECK(t1->feedPerMin.max == Approx(1500.0));
    CHECK(t1->feedPerRev.min == Approx(0.05));
    CHECK(t1->feedPerRev.max == Approx(0.05));
    CHECK(t1->spindle.min == Approx(4200.5));
    CHECK(t1->fractionalS);
    CHECK(t1->spindleDirs == std::set<std::string>{"M3"});
    CHECK(t1->coolant == std::set<std::string>{"M8", "M9"});
    CHECK(t2->spindle.max == Approx(8000.0));
    CHECK_FALSE(t2->fractionalS);
    CHECK(t2->spindleDirs == std::set<std::string>{"M4"});
    CHECK(t2->coolant == std::set<std::string>{"M7", "M9"});
    CHECK_FALSE(t2->feedPerRev.has);
}

TEST_CASE("canned cycles: holes per cycle and tool, parameters, G98/G99", "[stage3][cycle]") {
    const Report r = analyze(
        "O\nG92 X0 Y0 Z20\nG54\n"
        "T1M6\nS1000 M3\nG95\n"
        "G98 G81 X5. Z-3. R2. F0.05\nX10.\nX15.\nG80\n"
        "G99 G83 X20. Z-10. R2. Q3. F0.1\nG80\n"
        "G82 X30. Z-2. R2. P500 F0.1\nG80\n"
        "T2M6\nG98 G81 X5. Z-5. R2. F0.2\nG80\nM30\n");
    REQUIRE(r.cycles.size() == 4);
    const auto find = [&r](const char* code, int tool) -> const CycleSummary* {
        for (const auto& c : r.cycles) if (c.code == code && c.tool == tool) return &c;
        return nullptr;
    };
    const CycleSummary* a = find("G81", 1);
    REQUIRE(a != nullptr);
    CHECK(a->holes == 3);
    CHECK(a->returnInitial == 3);
    CHECK(a->z.min == Approx(-3.0));
    CHECK(a->r.max == Approx(2.0));
    CHECK(a->feedPerRev.max == Approx(0.05));
    const CycleSummary* b = find("G83", 1);
    REQUIRE(b != nullptr);
    CHECK(b->holes == 1);
    CHECK(b->returnR == 1);
    CHECK(b->q.max == Approx(3.0));
    const CycleSummary* c = find("G82", 1);
    REQUIRE(c != nullptr);
    CHECK(c->p.max == Approx(500.0));
    CHECK(find("G81", 2)->holes == 1);
    CHECK(findTool(r, 1)->cycleHoles.at("G81") == 3);
    CHECK_FALSE(hasWarning(r, warn::kCycleActive));
}

TEST_CASE("samples: drilling cycles are found with hole counts", "[samples][stage3][cycle]") {
    const Report r1 = analyze(sample1());
    std::size_t g81 = 0, g83 = 0;
    for (const auto& c : r1.cycles) {
        if (c.code == "G81") g81 += c.holes;
        if (c.code == "G83") g83 += c.holes;
        CHECK(c.returnInitial == c.holes);
        CHECK(c.feedPerRev.has);               // G95 inside cycles
    }
    CHECK(g81 == 42);
    CHECK(g83 == 12);
    const Report r2 = analyze(sample2());
    g81 = 0; g83 = 0;
    for (const auto& c : r2.cycles) {
        if (c.code == "G81") g81 += c.holes;
        if (c.code == "G83") g83 += c.holes;
    }
    CHECK(g81 == 14);
    CHECK(g83 == 14);
}

TEST_CASE("samples: M15 / M53 are listed as machine specific with the profile description", "[samples][stage3][codes]") {
    const auto find = [](const Report& r, const char* code) -> const CodeStat* {
        for (const auto& c : r.mCodes) if (c.code == code) return &c;
        return nullptr;
    };
    const Report r1 = analyze(sample1());
    const CodeStat* m15 = find(r1, "M15");
    REQUIRE(m15 != nullptr);
    CHECK(m15->machineSpecific);
    CHECK(m15->description.find("СОЖ") != std::string::npos);
    CHECK(m15->source == "Sky Master VL1100A");
    const Report r2 = analyze(sample2());
    const CodeStat* m53 = find(r2, "M53");
    REQUIRE(m53 != nullptr);
    CHECK(m53->machineSpecific);
    CHECK(m53->description.find("через шпиндель") != std::string::npos);
    CHECK(m53->source == "Bridgeport 1300/480");
    CHECK(find(r2, "M15") == nullptr);
}

TEST_CASE("samples: S can be fractional (S5305.165)", "[samples][stage3][feeds]") {
    const Report r = analyze(sample1());
    bool fractional = false;
    for (const auto& t : r.tools) fractional = fractional || t.fractionalS;
    CHECK(fractional);
}
