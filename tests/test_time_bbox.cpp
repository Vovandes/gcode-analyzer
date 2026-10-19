#include "gca/motion.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace gca;
using namespace gca::test;
using Catch::Approx;

namespace {

const char* kHeader =
    "O\n"
    "G21 G90 G17 G94\n"
    "G92 X0 Y0 Z0\n"
    "T1M6\n"
    "S1000 M3\n";

std::string prog(const std::string& body) { return std::string(kHeader) + body + "M5\nM30\n"; }

} // namespace

TEST_CASE("synthetic program with known time (rapid, line, arc, dwell)", "[time][stage2]") {
    const Report r = analyze(prog(
        "G0 X100.\n"                       // 100 mm rapid          -> 0.6 s
        "G1 X200. F600.\n"                 // 100 mm at 600         -> 10 s
        "G3 X210. Y10. I0. J10.\n"         // quarter circle R10    -> 15.708 mm -> 1.5708 s
        "G4 P500\n"                        // 0.5 s
        "G0 Z50.\n"));                     // 50 mm rapid           -> 0.3 s
    const double expected = 0.6 + 10.0 + 15.70796 / 600.0 * 60.0 + 0.5 + 0.3;
    CHECK(r.time.totalSec() == Approx(expected).epsilon(0.005));
    CHECK(r.time.rapidSec == Approx(0.9).epsilon(0.005));
    CHECK(r.time.feedSec == Approx(10.0 + 1.570796).epsilon(0.005));
    CHECK(r.time.dwellSec == Approx(0.5));
    CHECK(r.time.rapidMm == Approx(150.0));
    CHECK(r.time.feedMm == Approx(115.708).margin(1e-3));
    REQUIRE(findTool(r, 1) != nullptr);
    CHECK(findTool(r, 1)->time.totalSec() == Approx(expected).epsilon(0.005));
}

TEST_CASE("rapid feed option changes G0 time", "[time][stage2]") {
    const std::string p = prog("G0 X100.\n");
    CHECK(analyze(p, 10000.0).time.rapidSec == Approx(0.6));
    CHECK(analyze(p, 5000.0).time.rapidSec == Approx(1.2));
}

TEST_CASE("G95 feed per revolution uses S", "[time][stage2]") {
    const Report r = analyze(prog("G95\nG1 X100. F0.1\n"));      // 0.1 * 1000 = 100 mm/min -> 60 s
    CHECK(r.time.feedSec == Approx(60.0));
}

TEST_CASE("G91 incremental moves are measured", "[time][stage2]") {
    const Report r = analyze(prog("G91\nG1 X30. Y40. F500.\nG90\n"));  // 50 mm at 500 -> 6 s
    CHECK(r.time.feedMm == Approx(50.0));
    CHECK(r.time.feedSec == Approx(6.0));
}

TEST_CASE("full circle and helix in the program", "[time][stage2]") {
    const Report r = analyze(prog("G0 X10. Y0.\nG2 Z-1. I-5. J0. F300.\n"));  // end point = start, helix
    const double planar = 2.0 * kPi * 5.0;
    CHECK(r.time.feedMm == Approx(std::sqrt(planar * planar + 1.0)).margin(1e-6));
}

TEST_CASE("canned cycle time follows the cycle geometry", "[time][stage2][cycle]") {
    // initial level Z10, R2, bottom -5, G98: rapid 8 + 15, feed 7 at F100
    const Report r = analyze(prog("G0 Z10.\nG98 G81 X10. Y0. Z-5. R2. F100.\nG80\n"));
    REQUIRE(findTool(r, 1) != nullptr);
    const TimeBreakdown& t = findTool(r, 1)->time;
    CHECK(t.cycleHoles == 1);
    const double expected = (8.0 + 15.0) / 10000.0 * 60.0 + 7.0 / 100.0 * 60.0;
    CHECK(t.cycleSec == Approx(expected).epsilon(0.005));
    // positioning to X10 is counted as a normal rapid
    CHECK(t.rapidMm == Approx(10.0 + 10.0));
}

TEST_CASE("G99 returns only to the R plane; G83 pecks add time", "[time][stage2][cycle]") {
    const Report g99 = analyze(prog("G0 Z10.\nG99 G81 X10. Z-5. R2. F100.\nG80\n"));
    const Report g98 = analyze(prog("G0 Z10.\nG98 G81 X10. Z-5. R2. F100.\nG80\n"));
    CHECK(findTool(g99, 1)->time.cycleSec < findTool(g98, 1)->time.cycleSec);
    const Report plain = analyze(prog("G0 Z10.\nG98 G81 X10. Z-5. R2. F100.\nG80\n"));
    const Report peck = analyze(prog("G0 Z10.\nG98 G83 X10. Z-5. R2. Q2. F100.\nG80\n"));
    CHECK(findTool(peck, 1)->time.cycleSec > findTool(plain, 1)->time.cycleSec);
}

TEST_CASE("G95 in a cycle: F per revolution times S", "[time][stage2][cycle]") {
    const Report r = analyze(prog("G95\nG0 Z10.\nG98 G81 X10. Z-5. R2. F0.1\nG80\n"));
    const double expected = (8.0 + 15.0) / 10000.0 * 60.0 + 7.0 / 100.0 * 60.0;   // F = 0.1 * 1000 = 100
    CHECK(findTool(r, 1)->time.cycleSec == Approx(expected).epsilon(0.005));
}

TEST_CASE("moves without feed are counted, not timed", "[time][stage2]") {
    const Report r = analyze(prog("G1 X50.\n"));
    CHECK(r.time.feedSec == Approx(0.0));
    CHECK(r.segmentsWithoutFeed == 1);
}

TEST_CASE("unknown start point after G28 is not fatal", "[time][stage2]") {
    const Report r = analyze("O\nG91 G28 Z0\nG90 G0 X10. Y10.\nG1 Z5. F100.\nG1 X20.\nM30\n");
    CHECK(r.partialSegments >= 1);
    CHECK(r.time.feedMm == Approx(10.0));          // Z5 from unknown is skipped, X10 -> X20 is counted
}

TEST_CASE("synthetic bounding box is exact", "[bbox][stage2]") {
    const Report r = analyze(prog(
        "G0 X-10. Y5. Z20.\n"
        "G1 Z-2. F100.\n"
        "G1 X40. Y25.\n"
        "G0 Z30.\n"));
    CHECK(r.box.axis[kAxisX].min == Approx(-10.0));
    CHECK(r.box.axis[kAxisX].max == Approx(40.0));
    CHECK(r.box.axis[kAxisY].min == Approx(5.0));      // only commanded targets enter the box
    CHECK(r.box.axis[kAxisZ].max == Approx(30.0));
    CHECK(r.box.axis[kAxisZ].min == Approx(-2.0));
    CHECK(r.workBox.axis[kAxisZ].min == Approx(-2.0));
    CHECK(r.workBox.axis[kAxisZ].max == Approx(20.0));
    CHECK(r.workBox.axis[kAxisX].max == Approx(40.0));
}

TEST_CASE("bounding box of a full circle uses quadrant points", "[bbox][stage2]") {
    const Report r = analyze(prog("G0 X10. Y0. Z0.\nG3 I-10. J0. F200.\n"));       // circle R10 around the origin
    CHECK(r.workBox.axis[kAxisX].min == Approx(-10.0));
    CHECK(r.workBox.axis[kAxisX].max == Approx(10.0));
    CHECK(r.workBox.axis[kAxisY].min == Approx(-10.0));
    CHECK(r.workBox.axis[kAxisY].max == Approx(10.0));
}

TEST_CASE("G28 positions never enter the bounding box", "[bbox][stage2]") {
    const Report r = analyze("O\nG90 G54 G0 X10. Y10. Z10.\nG91 G28 Z0\nG91 G28 Y0\nG90\nG0 X20.\nM30\n");
    CHECK(r.box.axis[kAxisZ].min == Approx(10.0));
    CHECK(r.box.axis[kAxisZ].max == Approx(10.0));
    CHECK(r.box.axis[kAxisX].max == Approx(20.0));
}

TEST_CASE("B axis range is collected, B does not add to length", "[bbox][stage2][axisB]") {
    const Report r = analyze(prog(
        "G0 B0.\nG0 B90.\nG1 X10. B45. F100.\nG0 B-90.\n"));
    REQUIRE(r.box.axis[kAxisB].has);
    CHECK(r.box.axis[kAxisB].min == Approx(-90.0));
    CHECK(r.box.axis[kAxisB].max == Approx(90.0));
    CHECK(r.time.feedMm == Approx(10.0));
    CHECK(r.time.rapidMm == Approx(0.0));
}

TEST_CASE("per-tool bounding boxes", "[bbox][stage2]") {
    const Report r = analyze(
        "O\nG92 X0 Y0 Z0\nT1M6\nS1000 M3\nG0 X10. Y10.\nG1 X20. F100.\n"
        "T2M6\nG0 X-5. Y-5.\nG1 X-8. F100.\nM30\n");
    REQUIRE(findTool(r, 1) != nullptr);
    REQUIRE(findTool(r, 2) != nullptr);
    CHECK(findTool(r, 1)->box.axis[kAxisX].max == Approx(20.0));
    CHECK(findTool(r, 1)->box.axis[kAxisX].min == Approx(10.0));
    CHECK(findTool(r, 2)->box.axis[kAxisX].min == Approx(-8.0));
    CHECK(findTool(r, 2)->workBox.axis[kAxisX].max == Approx(-5.0));
}

TEST_CASE("samples: time is positive, boxes exist, no exceptions", "[samples][stage2]") {
    for (const auto& text : {sample1(), sample2()}) {
        const Report r = analyze(text);
        CHECK(r.time.totalSec() > 0.0);
        CHECK(r.time.feedSec > 0.0);
        CHECK(r.time.cycleHoles > 0);
        CHECK(r.box.any());
        CHECK(r.workBox.any());
        CHECK_FALSE(r.box.axis[kAxisB].has);
        CHECK(r.timeNotes.empty());
        double sum = 0.0;
        for (const auto& t : r.tools) sum += t.time.totalSec();
        CHECK(sum == Approx(r.time.totalSec()));
    }
    CHECK(analyze(sample1()).time.cycleHoles == 54);
    CHECK(analyze(sample2()).time.cycleHoles == 28);
}
