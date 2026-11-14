#include "gca/machine_profile.hpp"
#include "gca/analyzers/machine_time.hpp"
#include "gca/json_report.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/cli.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

using namespace gca;
using namespace gca::test;
using Catch::Approx;

namespace {
std::filesystem::path profileFile(const std::string& ini, const std::string& id = "test") {
    const auto dir = std::filesystem::path(GCA_TEST_OUTPUT_DIR) / "stage11";
    std::filesystem::create_directories(dir);
    const auto file = dir / pathFromUtf8(id + ".ini");
    std::ofstream out(file, std::ios::binary); out << ini;
    return file;
}
Report withProfile(const std::string& body, const std::string& ini) {
    AnalysisOptions options; options.machineFile = profileFile(ini);
    return analyzeText("G92 X0 Y0 Z0 B0\nG54 S1000 M3\n" + body + "M30\n", "test.nc", options);
}
}

TEST_CASE("INI valid Cyrillic comments and empty values", "[stage11][ini]") {
    const auto p = parseMachineIni("\xEF\xBB\xBFname = Станок\r\n# Кириллица\r\n[axes]\nx_min=-10 ; минимум\nx_max=20\nG54=X1Y2Z3\n"
        "[limits]\nfeed_max=600\nspindle_max=\n[time]\nrapid_x=1200\naccel_x=10\ntool_change=0\n"
        "skip_first_change=true\n[mcodes]\nM15=coolant_extra | Доп. СОЖ\n", "demo");
    CHECK(p.name == "Станок"); CHECK(p.axes[0].min == -10); CHECK(p.axes[0].max == 20);
    CHECK(p.wcsOffsets.at("G54")[2] == 3); CHECK(p.feedMax == 600); CHECK_FALSE(p.spindleMax);
    CHECK(p.axes[0].rapid == 1200); CHECK(p.axes[0].acceleration == 10);
    CHECK(p.toolChangeSec == 0); CHECK(p.skipFirstChange); REQUIRE(p.mCodes.size() == 1);
    CHECK(p.mCodes[0].description == "Доп. СОЖ");
}

TEST_CASE("INI errors have physical line numbers", "[stage11][ini]") {
    for (const auto& invalid : {"oops", "[bad]", "[axes", "name=A\nname=B", "[axes]\nx_min=20\nx_max=10",
        "[time]\naccel_x=0", "[time]\nrapid_x=-1", "[limits]\nfeed_max=nan", "[limits]\nfeed_max=abc",
        "[time]\nunknown=1", "[time]\nskip_first_change=yes", "[time]\njunction_angle=181",
        "[mcodes]\nM15=invalid | text", "[mcodes]\nM1.5=other | text", "[axes]\nG54=Z1Z2"}) {
        INFO(invalid);
        try { (void)parseMachineIni(invalid, "bad"); FAIL("invalid INI accepted"); }
        catch (const OptionError& e) { CHECK(contains(e.what(), "строка ")); }
    }
    CHECK_THROWS_WITH(parseMachineIni("; comment\n[time]\naccel_x=0", "bad"), "строка 3: некорректное число: 0");
}

TEST_CASE("INI overrides built-in machine without losing M codes", "[stage11][ini]") {
    const auto* dialect = findDialect("fanuc-mill"); REQUIRE(dialect);
    const auto* base = findMachine(*dialect, "skymaster-vl1100a"); REQUIRE(base);
    auto p = parseMachineIni("[limits]\nfeed_max=100\n", base->id, base);
    CHECK(p.name == base->name); CHECK(p.mCodes.size() == base->mCodes.size()); CHECK(p.feedMax == 100);
    p = parseMachineIni("[limits]\nfeed_max=\n", base->id, &p); CHECK_FALSE(p.feedMax);
    const auto file = profileFile("[limits]\nfeed_max=123\n", base->id);
    const auto machines = loadMachines(*dialect, file.parent_path());
    const auto it = std::find_if(machines.begin(), machines.end(), [&](const auto& m) { return m.id == base->id; });
    REQUIRE(it != machines.end()); CHECK(it->feedMax == 123); CHECK(it->mCodes.size() == base->mCodes.size());
}

TEST_CASE("CLI machine file listing selection and WCS syntax", "[stage11][ini][cli]") {
    const auto file = profileFile("name=Тестовый станок\n", "кириллица");
    std::ostringstream out, err;
    CHECK(runCli({"--list-machines", "--machine-file", pathToUtf8(file)}, out, err) == 0);
    CHECK(contains(out.str(), "кириллица\tТестовый станок"));
    const auto offset = parseWcsOffset("G59=X-1.5Y0Z20"); CHECK(offset.second[0] == -1.5);
    CHECK_THROWS(parseWcsOffset("G53=X0")); CHECK_THROWS(parseWcsOffset("G54="));
    CHECK_THROWS(parseWcsOffset("G54=X0X1")); CHECK_THROWS(parseWcsOffset("G54=X0 F1"));
}

TEST_CASE("Limits feed spindle effective G95 and boundary", "[stage11][limits]") {
    const std::string ini = "[limits]\nfeed_max=100\nspindle_max=1000\n";
    auto r = withProfile("G1 X1 F100\nS1001\nG95 G1 X2 F0.2\n", ini);
    CHECK(hasWarning(r, "W019")); CHECK(hasWarning(r, "W020"));
    REQUIRE(r.segments.size() == 2); CHECK_FALSE(r.segments[0].limitViolation); CHECK(r.segments[1].limitViolation);
    CHECK_FALSE(hasWarning(withProfile("G1 X1 F100\n", ini), "W019"));
    CHECK_FALSE(hasWarning(withProfile("G1 X1 F100\n", ini), "W020"));
    for (const auto code : {"W019", "W020", "W021"}) CHECK(warningSeverity(code) == Severity::Error);
    CHECK(warningSeverity("W022") == Severity::Info);
    CHECK(contains(renderJsonReport(r), "\"limitViolation\":true"));
}

TEST_CASE("Limits absolute axes offsets and arc extrema", "[stage11][limits]") {
    const std::string ini = "[axes]\nx_min=0\nx_max=100\ny_min=-5\ny_max=5\nz_min=-5\nz_max=20\nb_min=-90\nb_max=90\nG54=X10Y0Z0\n";
    CHECK_FALSE(hasWarning(withProfile("G1 X90 F100\n", ini), "W021"));
    CHECK(hasWarning(withProfile("G1 X91 F100\n", ini), "W021"));
    CHECK(hasWarning(withProfile("G0 B91\n", ini), "W021"));
    const auto arc = withProfile("G0 X10\nG3 I-10 J0 F100\n", ini);
    CHECK(hasWarning(arc, "W021")); CHECK_FALSE(hasWarning(arc, "W022"));
    CHECK(hasWarning(withProfile("G81 Z-6 R2 F100\nG80\n", ini), "W021"));
    const auto g53 = withProfile("G53 G0 X101\n", ini);
    CHECK(hasWarning(g53, "W021")); REQUIRE(g53.segments.size() == 1);
    CHECK(g53.segments[0].to.v[0] == 101);
}

TEST_CASE("Limits without offsets check span independently per WCS", "[stage11][limits]") {
    const std::string ini = "[axes]\nx_min=0\nx_max=100\n";
    const auto r = withProfile("G0 X200\nG1 X301 F100\n", ini);
    CHECK(hasWarning(r, "W021")); CHECK(hasWarning(r, "W022"));
    // A coordinate outside the nominal range alone does not prove a violation without an offset.
    AnalysisOptions o; o.machineFile = profileFile(ini);
    auto shortMove = analyzeText("G54 G0 X200\nG1 X201 F100\nM30\n", "test.nc", o);
    CHECK_FALSE(hasWarning(shortMove, "W021")); CHECK(hasWarning(shortMove, "W022"));
    const auto partial = withProfile("G1 X1 F100\n", "[axes]\nx_max=100\nG54=Y0Z0\n");
    CHECK(hasWarning(partial, "W022"));
}

TEST_CASE("Time triangle trapezoid and empty profile fallback", "[stage11][time]") {
    const auto trapezoid = withProfile("G1 X100 F600\n", "[time]\naccel_x=10\n");
    // v=10 mm/s, a=10: 1 s up + 9 s cruise + 1 s down = 11 s.
    CHECK(trapezoid.time.feedSec == Approx(10)); CHECK(trapezoid.machineTime.motion.feedSec == Approx(11));
    CHECK(trapezoid.machineTime.accelerations);
    // L=1, a=10: peak=sqrt(10), time=2*sqrt(1/10).
    const auto triangle = withProfile("G1 X1 F600\n", "[time]\naccel_x=10\n");
    CHECK(triangle.machineTime.motion.feedSec == Approx(2 * std::sqrt(0.1)));
    auto blank = withProfile("G0 X3 Y4\nG1 X13 F600\nG4 X1\n", "[time]\naccel_x=\n");
    CHECK(blank.machineTime.totalSec() == Approx(blank.time.totalSec())); CHECK_FALSE(blank.machineTime.accelerations);
    CHECK(contains(renderTextReport(blank), "оценка без ускорений"));
}

TEST_CASE("Time junction below above threshold and stop barriers", "[stage11][time]") {
    const std::string ini = "[time]\naccel_x=10\naccel_y=10\n";
    // Two collinear 100-mm moves: a single 200-mm trapezoid = 21 s; a 90-degree corner = 22 s.
    CHECK(withProfile("G1 X100 F600\nX200\n", ini).machineTime.motion.feedSec == Approx(21));
    CHECK(withProfile("G1 X100 F600\nY100\n", ini).machineTime.motion.feedSec == Approx(22));
    CHECK(withProfile("G1 X100 F600\nM0\nX200\n", ini).machineTime.motion.feedSec == Approx(22));
    CHECK(withProfile("G1 X100 F600\nT2M6\nX200\n", ini).machineTime.motion.feedSec == Approx(22));
    // 20-degree bend, second length is exactly 100 mm: 21 s with a 30-degree threshold.
    CHECK(withProfile("G1 X100 F600\nX193.9692620786 Y34.2020143326\n", ini).machineTime.motion.feedSec == Approx(21).margin(1e-8));
    CHECK(withProfile("G1 X100 F600\nX200\n", ini + "junction_angle=0\n").machineTime.motion.feedSec == Approx(21));
    // Two short collinear moves form a triangle of total length 2 mm.
    CHECK(withProfile("G1 X1 F600\nX2\n", ini).machineTime.motion.feedSec == Approx(2 * std::sqrt(0.2)));
}

TEST_CASE("Time rapid independent axes and slowest feed acceleration", "[stage11][time]") {
    const auto r = withProfile("G0 X100 Y100\n", "[time]\nrapid_x=600\nrapid_y=1200\naccel_x=10\naccel_y=20\n");
    // X: 100/10+10/10=11 s, Y: 100/20+20/20=6 s. Simultaneous move: max=11.
    CHECK(r.machineTime.motion.rapidSec == Approx(11));
    const auto line = withProfile("G1 X60 Y80 F600\n", "[time]\naccel_x=20\naccel_y=10\n");
    CHECK(line.machineTime.motion.feedSec == Approx(11));
}

TEST_CASE("Time tool changes B dwell spindle and operator stops", "[stage11][time]") {
    const std::string body = "T1M6\nT2M6\nG0 B90\nG4 X1\nM0\nM1\n";
    const std::string ini = "[time]\ntool_change=7\nb_seconds_per_90=2\nspindle_start=3\n";
    auto r = withProfile(body, ini);
    CHECK(r.machineTime.toolChangeSec == Approx(14)); CHECK(r.machineTime.bSec == Approx(2));
    CHECK(r.machineTime.spindleSec == Approx(3)); CHECK(r.machineTime.totalSec() == Approx(20));
    CHECK(r.programStops == 1); CHECK(r.optionalStops == 1);
    r = withProfile(body, ini + "skip_first_change=true\n"); CHECK(r.machineTime.totalSec() == Approx(13));
}

TEST_CASE("Time G28 G30 via intermediate and fixed fallback", "[stage11][time]") {
    const std::string ini = "[axes]\nG54=X0Y0Z0\nx_home=100\nx_home30=200\n[time]\nrapid_x=600\naccel_x=100\n";
    // Start X10, intermediate X15, home X100: .6 + 8.6 = 9.2 seconds.
    const auto r = withProfile("G0 X10\nG91 G28 X5\nG90\n", ini);
    CHECK(r.machineTime.homeSec == Approx(9.2)); CHECK(r.segments.back().to.known[0]);
    CHECK(r.segments.back().to.v[0] == 100);
    CHECK(withProfile("G0 X10\nG91 G30 X5\nG90\n", ini).machineTime.homeSec == Approx(19.2));
    CHECK(withProfile("G91 G28 Z0\nG90\n", "[time]\ng28_time=4\n").machineTime.homeSec == Approx(4));
}

TEST_CASE("Time canned cycle uses individual strokes and dwell", "[stage11][time]") {
    // Z10 -> R2 rapid 8 mm, feed 7 mm at 10 mm/s, rapid return 15 mm at 10 mm/s; a=10.
    // Rapid triangle 2sqrt(.8), feed triangle 2sqrt(.7), return trapezoid 2.5; dwell .5.
    const auto r = withProfile("G0 Z10\nG82 Z-5 R2 P500 F600\nG80\n", "[time]\nrapid_z=600\naccel_z=10\n");
    CHECK(r.machineTime.motion.cycleSec == Approx(2 * std::sqrt(0.8) + 2 * std::sqrt(0.7) + 2.5));
    CHECK(r.machineTime.motion.dwellSec == Approx(0.5));
    const auto peck = withProfile("G0 Z10\nG83 Z-5 R2 Q2 F600\nG80\n", "[time]\nrapid_z=600\naccel_z=10\n");
    CHECK(peck.machineTime.motion.cycleSec > r.machineTime.motion.cycleSec);
}

TEST_CASE("WCS change preserves machine position and CLI offsets override profile", "[stage11][limits]") {
    const std::string ini = "[axes]\nx_min=0\nx_max=100\nG54=X10Y0Z0\nG55=X20Y0Z0\n";
    const auto r = withProfile("G0 X0\nG55\nG1 X81 F100\n", ini);
    REQUIRE(r.segments.size() == 2); CHECK(r.segments[1].from.v[0] == Approx(-10));
    CHECK(hasWarning(r, "W021"));
    AnalysisOptions o; o.machineFile = profileFile(ini);
    o.wcsOffsets["G54"] = parseWcsOffset("G54=X0Y0Z0").second;
    CHECK_FALSE(hasWarning(analyzeText("G92 X0Y0Z0\nG54 G1 X100 F100\nM30\n", "test.nc", o), "W021"));
}

TEST_CASE("Stage11 synthetic golden text and JSON", "[stage11][golden]") {
    AnalysisOptions o; o.machineFile = std::filesystem::path(GCA_TEST_DATA_DIR) / "machine-stage11.ini";
    const auto r = analyzeFileReport(std::filesystem::path(GCA_TEST_DATA_DIR) / "machine-stage11.nc", o);
    CHECK(renderTextReport(r) == readFile(std::filesystem::path(GCA_GOLDEN_DIR) / "machine-stage11.txt"));
    CHECK(renderJsonReport(r) == readFile(std::filesystem::path(GCA_GOLDEN_DIR) / "machine-stage11.json"));
    // Y10 at a=10 reaches sqrt(10*10)=10 mm/s: 2*sqrt(10/10)=2 s.
    CHECK(r.machineTime.totalSec() == Approx(11 + 2 + 2 + 1 + 7 + 3));
    CHECK(hasWarning(r, "W019")); CHECK(hasWarning(r, "W020")); CHECK(hasWarning(r, "W021"));
}

TEST_CASE("Time arcs use length tangents and G95 feed", "[stage11][time]") {
    const std::string ini = "[time]\naccel_x=10\naccel_y=10\n";
    // Two R10 full circles at 10 mm/s with a tangential join: 4*pi + 1 seconds.
    const auto r = withProfile("G0 X10\nG3 I-10 J0 F600\nG3 I-10 J0\n", ini);
    CHECK(r.machineTime.motion.feedSec == Approx(4 * 3.14159265358979323846 + 1));
    // G95 F=.6 at S1000 -> 600 mm/min, same 11-second trapezoid.
    CHECK(withProfile("G95 G1 X100 F0.6\n", ini).machineTime.motion.feedSec == Approx(11));
}

TEST_CASE("G53 preserves all target axes and known working coordinates", "[stage11][limits][time]") {
    const auto r = withProfile("G53 G0 X80 Y40 Z20\nG1 X90 F600\n", "[axes]\nG54=X10Y20Z30\n[time]\naccel_x=10\n");
    REQUIRE(r.segments.size() == 2);
    const auto& s = r.segments[0]; CHECK(s.machineCoordinates);
    CHECK(s.to.v[0] == 80); CHECK(s.to.v[1] == 40); CHECK(s.to.v[2] == 20);
    REQUIRE(s.workTo); CHECK(s.workTo->v[0] == 70); CHECK(s.workTo->v[1] == 20); CHECK(s.workTo->v[2] == -10);
    CHECK(r.segments[1].from.v[0] == 70);
}
