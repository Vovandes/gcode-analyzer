#include "gca/analyzers/warnings.hpp"
#include "gca/cli.hpp"
#include "gca/json_report.hpp"
#include "gca/motion.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gca;
using namespace gca::test;
using Catch::Approx;

namespace {

std::filesystem::path data(const char* name) { return std::filesystem::path(GCA_TEST_DATA_DIR) / name; }

AnalysisOptions kede() {
    AnalysisOptions o;
    o.dialect = "gnc62";
    o.machine = "kede-uni800ut";
    o.machinesDirectory = GCA_MACHINE_DIR;
    return o;
}

const Warning* warning(const Report& r, const char* code) {
    const auto it = std::find_if(r.warnings.begin(), r.warnings.end(), [code](const auto& w) { return w.code == code; });
    return it != r.warnings.end() ? &*it : nullptr;
}

} // namespace

TEST_CASE("Stage12PostChecks checks spindle, feed, retract and compensation per operation", "[stage12]") {
    AnalysisOptions o;
    o.safeZ = 50;
    const auto good = analyzeFileReport(data("stage12-post-good.nc"), o);
    CHECK(good.warnings.empty());

    const auto bad = analyzeFileReport(data("stage12-post-bad.nc"), o);
    REQUIRE(warning(bad, warn::kInheritedFeed));
    CHECK(warning(bad, warn::kInheritedFeed)->line == 11);
    CHECK(contains(warning(bad, warn::kInheritedFeed)->message, "T1"));
    REQUIRE(warning(bad, warn::kNoFeed));
    CHECK(warning(bad, warn::kNoFeed)->line == 12);
    CHECK(contains(warning(bad, warn::kNoFeed)->message, "F0"));
    REQUIRE(warning(bad, warn::kNoSpindle));
    CHECK(warning(bad, warn::kNoSpindle)->line == 11);
    CHECK(countWarnings(bad, warn::kNoRetract) == 2);
    CHECK(hasWarning(bad, warn::kChangeComp));
    REQUIRE(warning(bad, warn::kEndCompensation));
    CHECK(contains(warning(bad, warn::kEndCompensation)->message, "G40"));
    CHECK(contains(warning(bad, warn::kEndCompensation)->message, "G49"));
    CHECK(hasWarning(bad, warn::kMissingEnd));

    for (const std::string command : {"M5", "S0", "T2 M6"}) {
        INFO(command);
        const auto invalid = analyze("G54 G0 X0 Y0 Z0\nS1000 M3 F600\n" + command + "\nG1 X10\nM30\n");
        CHECK(hasWarning(invalid, warn::kNoSpindle));
        const auto valid = analyze("G54 G0 X0 Y0 Z0\nS1000 M3 F600\n" + command + "\nS900 M4\nG1 X10\nM30\n");
        CHECK_FALSE(hasWarning(valid, warn::kNoSpindle));
    }
    const auto sameBlock = analyze("G54 G0 X0 Y0 Z0\nT1 M6 S1000 M3 F900 G1 X10\nM2\n");
    CHECK_FALSE(hasWarning(sameBlock, warn::kNoSpindle));
    CHECK_FALSE(hasWarning(sameBlock, warn::kNoFeed));
    CHECK_FALSE(hasWarning(sameBlock, warn::kInheritedFeed));
    const auto repeated = analyze("G54 G0 X0 Y0 Z0\nT1 M6 S1000 M3\nG1 X1 F600\nT1 M6 M3\nG1 X2\nM30\n");
    CHECK(hasWarning(repeated, warn::kInheritedFeed));
    const auto zeroAfterMissing = analyze("G54 G0 X0 Y0 Z0\nM3 S1000\nG1 X1\nF0\nG1 X2\nM30\n");
    CHECK(countWarnings(zeroAfterMissing, warn::kNoFeed) == 2);
}

TEST_CASE("Stage12PostChecks distinguishes main programs, subprograms and trailing blocks", "[stage12]") {
    const auto early = analyzeFileReport(data("stage12-early-end.nc"), {});
    REQUIRE(warning(early, warn::kEarlyEnd));
    CHECK(warning(early, warn::kEarlyEnd)->line == 3);
    CHECK_FALSE(hasWarning(early, warn::kMissingEnd));
    CHECK_FALSE(hasWarning(analyze("G54\nM30\n(comment)\nN2\n%\n"), warn::kEarlyEnd));
    CHECK_FALSE(hasWarning(analyze("G54\nM2\n"), warn::kMissingEnd));
    CHECK_FALSE(hasWarning(analyzeFileReport(data("stage12-subprogram.nc"), {}), warn::kMissingEnd));
    CHECK_FALSE(hasWarning(analyzeFileReport(data("stage12-subprogram.nc"), kede()), warn::kMissingEnd));
    for (const std::string tail : {"M99", "M17", "RET", "N20 RET"}) {
        CHECK_FALSE(hasWarning(analyze("O1\nG54\n" + tail + "\n%\n"), warn::kMissingEnd));
    }
    CHECK(hasWarning(analyze("O1\nG54\nM99\nG0 X2\n"), warn::kMissingEnd));
    CHECK(hasWarning(analyzeText("GDEF800 END\nGCALL\n", "main.nc", kede()), warn::kMissingEnd));
    CHECK_FALSE(hasWarning(analyzeText("G54\nG4 F0\nM30\n", "dwell.nc", kede()), warn::kNoFeed));
}

TEST_CASE("Stage12PostChecks distinguishes work Z, reference return and KEDE machine Z", "[stage12]") {
    const std::string main = "G90 G54 G0 X0 Y0 Z100\nG0 Z25\nT1 M6\nM30\n";
    CHECK_FALSE(hasWarning(analyze(main), warn::kNoRetract));
    AnalysisOptions o;
    o.machineFile = data("stage12-retract.ini");
    CHECK(hasWarning(analyzeText(main, "retract.nc", o), warn::kNoRetract));
    o.safeZ = 25;
    CHECK_FALSE(hasWarning(analyzeText(main, "retract.nc", o), warn::kNoRetract));
    o.machineFile.reset();
    o.safeZ = 50;
    CHECK(hasWarning(analyzeText(main, "retract.nc", o), warn::kNoRetract));
    CHECK(hasWarning(analyzeText("G90 G54 G0 Z50\nG0 X5 Z20\nT1 M6\nM30\n", "lower.nc", o), warn::kNoRetract));
    for (const std::string command : {"G28 Z0", "G91 G28 Z0\nG90", "G53 Z0", "G0 Z50"}) {
        CHECK_FALSE(hasWarning(analyzeText("G54 G0 Z0\n" + command + "\nT1 M6\nM30\n", "retract.nc", o), warn::kNoRetract));
    }
    o = kede();
    CHECK(hasWarning(analyzeText(main, "fallback.nc", o), warn::kNoRetract));
    CHECK_FALSE(hasWarning(analyzeText("G54 G0 Z0\nG28 Z0\nT1 M6\nM30\n", "home.nc", o), warn::kNoRetract));
    for (const std::string height : {"665.105", "667"}) {
        CHECK_FALSE(hasWarning(analyzeText("G0 G53 Z" + height + "\nT1 M6\nM30\n", "machine.nc", o), warn::kNoRetract));
    }
    for (const std::string height : {"600", "668"}) {
        CHECK(hasWarning(analyzeText("G0 G53 Z" + height + "\nT1 M6\nM30\n", "machine.nc", o), warn::kNoRetract));
    }
    const auto unload = analyzeText("G0 G28 Z0\nT0 M6\nM30\n", "unload.nc", o);
    CHECK_FALSE(hasWarning(unload, warn::kChangeNoTool));
    CHECK(hasWarning(analyze("G0 G28 Z0\nT0 M6\nM30\n"), warn::kChangeNoTool));
    const auto transformed = analyzeText("G54 G52 X1\nG0 Z50\nG52\nT1 M6\nM30\n", "local.nc", o);
    CHECK_FALSE(hasWarning(transformed, warn::kNoRetract));
    CHECK(std::any_of(transformed.warnings.begin(), transformed.warnings.end(), [](const Warning& w) {
        return w.code == warn::kApproximate && contains(w.message, "отвод по Z перед M6");
    }));
    const auto hiddenCycle = analyzeText("G54 G0 Z50\nGDEF800 END\nGCALL\nT1 M6\nM30\n", "cycle.nc", o);
    CHECK_FALSE(hasWarning(hiddenCycle, warn::kNoRetract));
}

TEST_CASE("Stage12ArcChecks checks three planes, mixed addresses and measured helix geometry", "[stage12]") {
    const auto good = analyzeFileReport(data("stage12-arcs-good.nc"), {});
    REQUIRE(good.warnings.size() == 1);
    CHECK(good.warnings.front().code == warn::kHelix);
    const auto helix = computeSegmentArc(good.segments.at(1));
    REQUIRE(helix.ok);
    // Quarter circle of R10 and 4 mm travel on Z.
    CHECK(helix.length == Approx(std::hypot(5 * kPi, 4.0)));
    const auto full = computeSegmentArc(good.segments.at(2));
    REQUIRE(full.ok);
    CHECK(full.fullCircle);
    CHECK(full.length == Approx(20 * kPi));

    const auto bad = analyzeFileReport(data("stage12-arcs-bad.nc"), {});
    REQUIRE(warning(bad, warn::kMixedArc));
    CHECK(contains(warning(bad, warn::kMixedArc)->message, "использует R"));
    CHECK(computeSegmentArc(bad.segments.at(1)).radius == Approx(11));
    CHECK(countWarnings(bad, warn::kArcPlane) == 3);
    CHECK(countWarnings(bad, warn::kArc) == 3);
    CHECK_FALSE(hasWarning(bad, warn::kHelix));
    AnalysisOptions o;
    o.arcRadiusTolerance = 1;
    CHECK_FALSE(hasWarning(analyzeFileReport(data("stage12-arcs-bad.nc"), o), warn::kMixedArc));
    CHECK(warningSeverity(warn::kArcPlane) == Severity::Error);
    CHECK(warningSeverity(warn::kHelix) == Severity::Info);
}

TEST_CASE("Stage12ToolTotals shares time model and sums tool geometry", "[stage12]") {
    const auto known = analyzeFileReport(data("known-time.nc"), {});
    REQUIRE(known.tools.size() == 1);
    const auto& tool = known.tools.front();
    // 150 mm of rapid at 10000 mm/min, 100 mm + a quarter-circle R10 at F600, and 0.5 s dwell.
    CHECK(tool.detail.motionTime.rapidSec == Approx(0.9));
    CHECK(tool.detail.motionTime.feedSec == Approx(10 + kPi / 2));
    CHECK(tool.detail.motionTime.dwellSec == Approx(0.5));
    CHECK(tool.detail.workingMm == Approx(100 + 5 * kPi));
    CHECK(tool.detail.idleMm == Approx(150));
    CHECK(tool.detail.feedMmPerMin.min == Approx(600));
    CHECK(tool.detail.rpm.min == Approx(1000));
    CHECK(tool.box.axis[kAxisX].max == Approx(210));
    CHECK_FALSE(tool.diameterMm);
    CHECK(known.toolTotals.motionTime.totalSec() == Approx(tool.detail.motionTime.totalSec()));

    const auto pair = analyzeFileReport(data("stage12-post-good.nc"), {});
    const auto* first = findTool(pair, 1);
    const auto* second = findTool(pair, 2);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->diameterMm == 63);
    CHECK(second->diameterMm == 6.5);
    CHECK(first->hValues.contains(1));
    CHECK(second->dValues.contains(2));
    CHECK(pair.toolTotals.workingMm == Approx(first->detail.workingMm + second->detail.workingMm));
    CHECK(pair.toolTotals.idleMm == Approx(first->detail.idleMm + second->detail.idleMm));
    CHECK(pair.toolTotals.motionTime.totalSec() == Approx(first->detail.motionTime.totalSec() + second->detail.motionTime.totalSec()));
    CHECK(pair.toolTotals.rpm.max == Approx(2000));
    CHECK(pair.toolTotalBox.axis[kAxisX].max == Approx(40));
    CHECK(contains(renderTextReport(pair), "ИТОГО | — | — | — |"));
    CHECK(contains(renderJsonReport(pair), "\"toolTotals\":{\"detail\":"));
}

TEST_CASE("Stage12ToolTotals splits cycle strokes and includes profile accelerations", "[stage12]") {
    const std::string nc = "G54 G90 G0 X0 Y0 Z10\nG95 F0.2 S1000 M3\nG81 Z-5 R0 X0 Y0\nG80\nM30\n";
    const auto report = analyze(nc);
    const auto& total = report.toolTotals;
    // 10 mm approach + 15 mm return at G0, 5 mm drilling at 0.2*1000 = 200 mm/min.
    CHECK(total.workingMm == Approx(5));
    CHECK(total.idleMm == Approx(25));
    CHECK(total.feedMmPerMin.min == Approx(200));
    CHECK(total.motionTime.cycleSec == Approx(1.65));
    auto o = kede();
    const auto withProfile = analyzeText(nc, "cycle.nc", o);
    CHECK(withProfile.machineTime.accelerations);
    CHECK(withProfile.toolTotals.motionTime.totalSec() == Approx(withProfile.machineTime.motion.totalSec()));
    CHECK(withProfile.toolTotals.motionTime.cycleSec != Approx(total.motionTime.cycleSec));
}

TEST_CASE("Stage12Samples keeps Fanuc and KEDE fragment errors clear", "[stage12]") {
    for (const auto& text : {sample1(), sample2()}) {
        const auto report = analyze(text);
        for (const auto& w : report.warnings) CHECK(warningSeverity(w.code) != Severity::Error);
    }
    for (int n = 1; n <= 5; ++n) {
        const auto file = std::filesystem::path(GCA_TEST_DATA_DIR) / ("gnc62-q" + std::to_string(n) + "-body.nc");
        auto text = readFile(file);
        INFO(n);
        CHECK_FALSE(contains(text, "TOOLS LIST"));
        CHECK_FALSE(contains(text, "MACHINE TIME"));
        CHECK_FALSE(contains(text, "PART:"));
        CHECK(hasWarning(analyzeText(text, "fragment.nc", kede()), warn::kMissingEnd));
        // The fixture is a truncated body, so supply a synthetic terminator for the error check.
        const auto report = analyzeText(text + "\nM30\n", "fragment.nc", kede());
        for (const auto& w : report.warnings) {
            INFO(w.code << " " << w.line);
            CHECK(warningSeverity(w.code) != Severity::Error);
        }
    }
}

TEST_CASE("Stage12 CLI exposes both parameters and validates their input", "[stage12]") {
    std::ostringstream out, err;
    REQUIRE(runCli({"--list-options"}, out, err) == kExitOk);
    CHECK(contains(out.str(), "--safe-z\t"));
    CHECK(contains(out.str(), "--arc-radius-tolerance\t"));
    const auto arcs = pathToUtf8(data("stage12-arcs-bad.nc"));
    const auto post = pathToUtf8(data("stage12-post-bad.nc"));
    out.str("");
    REQUIRE(runCli({arcs, "--arc-radius-tolerance", "1"}, out, err) == kExitOk);
    CHECK_FALSE(contains(out.str(), "[W034]"));
    CHECK(runCli({arcs, "--arc-radius-tolerance", "-0.01"}, out, err) == kExitBadArguments);
    out.str("");
    REQUIRE(runCli({post, "--safe-z", "50"}, out, err) == kExitOk);
    CHECK(contains(out.str(), "[W030]"));
    CHECK(runCli({post, "--safe-z", "abc"}, out, err) == kExitBadArguments);
}
