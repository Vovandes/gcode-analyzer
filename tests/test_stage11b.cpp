#include "gca/analyzer.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/cli.hpp"
#include "gca/json_report.hpp"
#include "gca/machine_profile.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <sstream>

using namespace gca;
using namespace gca::test;
using Catch::Approx;
namespace {
AnalysisOptions gnc(bool machine = false) {
    AnalysisOptions o; o.dialect = "gnc62";
    if (machine) o.machineFile = std::filesystem::path(GCA_MACHINE_DIR) / "kede-uni800ut.ini";
    return o;
}
Report nc(std::string_view text, bool machine = false) { return analyzeText(text,"synthetic.nc",gnc(machine)); }
std::size_t warnings(const Report& r, std::string_view code) {
    return static_cast<std::size_t>(std::count_if(r.warnings.begin(),r.warnings.end(),[&](const auto& w) { return w.code == code; }));
}
}

TEST_CASE("GNC62 comments and labels", "[stage11b][lexer]") {
    const auto p = parseProgram("@( synthetic comment )\r\nN10 G0 X1 @ tail (unbalanced\nGOTO 10\nIF[#5999 EQ 0] THEN\nENDIF\n", {true});
    CHECK(p.diagnostics.empty()); REQUIRE(p.blocks.size() == 5);
    CHECK(p.blocks[0].comments[0].text == " synthetic comment ");
    CHECK(p.blocks[1].n == 10); CHECK(p.blocks[1].words.size() == 3);
    CHECK(p.blocks[1].comments[0].style == '@');
    CHECK(p.blocks[2].command == "GOTO"); CHECK(p.blocks[2].jumpTarget == 10);
    CHECK(p.blocks[3].control); CHECK(p.crlfLines == 1); CHECK(p.lfLines == 4);
    const auto r = nc("N10 G0 X0\nGOTO 10\nIF[#5999 EQ 0] THEN\nENDIF\nM30\n");
    CHECK(r.file.lexerDiagnostics == 0); CHECK_FALSE(hasWarning(r,"W013")); CHECK(hasWarning(r,"W027"));
}

TEST_CASE("GNC62 named tools and synthetic header lists", "[stage11b][tools]") {
    const auto r = nc("@( TOOLS LIST )\n@( T\"ЩУП\": synthetic probe )\n@( END TOOLS LIST )\n"
        "@( OPERATIONS LIST )\n@( synthetic measuring operation )\n@( END OPERATIONS LIST )\n"
        "T1 M6\nT\"ЩУП\" M6\nG239 T0.01\nT\"ЩУП\" M6\nM30\n");
    REQUIRE(r.toolSequence.size() == 3);
    CHECK(r.toolSequence[0].tool == 1); CHECK(r.toolSequence[1].tool == 2); CHECK(r.toolSequence[2].tool == 2);
    CHECK(r.preselectSequence.empty()); CHECK(r.toolNames.at(2) == "ЩУП");
    CHECK(r.headerTools.size() == 1); CHECK(r.headerOperations.size() == 1);
    CHECK_FALSE(hasWarning(r,"W017")); CHECK_FALSE(hasWarning(r,"W013"));
    CHECK(contains(renderTextReport(r),"имя T\"ЩУП\""));
    CHECK(contains(renderTextReport(r),"synthetic measuring operation"));
    CHECK(contains(renderJsonReport(r),"\"name\":\"ЩУП\""));
}

TEST_CASE("GNC62 auto detection explicit override and hint", "[stage11b][cli]") {
    // This entire header is synthetic, not copied from Q1-Q5.
    const std::string text = "@( MACHINE: Uni.5 800UT, GNC 62 )\nT\"SyntheticTool\" M6\nM30\n";
    AnalysisOptions o; o.machinesDirectory = GCA_MACHINE_DIR;
    const auto r = analyzeText(text,"synthetic.nc",o);
    CHECK(r.file.dialectId == "gnc62"); CHECK(r.file.machineProfile == "KEDE UNI 800 UT");
    CHECK(r.file.lexerDiagnostics == 0); CHECK_FALSE(hasWarning(r,"W028"));
    o.dialect = "fanuc-mill";
    CHECK(hasWarning(analyzeText(text,"synthetic.nc",o),"W028"));
    for (const auto* body : {"@ comment\nM30\n", "GDEF940 END\nGCALL\nM30\n", "GCALL\n"}) {
        o.dialect = "auto";
        const auto hint = analyzeText(body,"synthetic.nc",o);
        CHECK(hint.file.dialectId == "fanuc-mill"); CHECK(warnings(hint,"W028") == 1);
    }
    CHECK_FALSE(detectDialect("T\"MACHINE: Uni.5 800UT, GNC 62\" M6\n"));
    CHECK(warningSeverity("W028") == Severity::Info);
}

TEST_CASE("GNC62 GDEF GCALL parameters are not motion or modal feeds", "[stage11b][cycles]") {
    const auto r = nc("G54 G0 X0Y0Z200\nS1000 M3\nG94 F600\n"
        "GDEF923 D80 H-10 AH20 X0 Y0\nAN55 E5 S14001 F50000 END\nGCALL\n"
        "G0 X1Y2Z200\nG1 X11\nM30\n",true);
    CHECK(r.file.lexerDiagnostics == 0); CHECK_FALSE(hasWarning(r,"W012")); CHECK_FALSE(hasWarning(r,"W023"));
    CHECK_FALSE(hasWarning(r,"W020")); CHECK(r.toolSequence.empty());
    REQUIRE(r.segments.size() == 3);
    CHECK(r.segments[1].wcs == "G55"); CHECK_FALSE(r.segments[1].from.known[0]);
    CHECK(r.segments[2].feed == 600); CHECK(r.segments[2].s == 1000);
    CHECK(hasWarning(r,"W026"));
    CHECK(std::any_of(r.gCodes.begin(),r.gCodes.end(),[](const auto& c) { return c.code == "G923" && c.known; }));
    const auto incomplete = nc("GCALL\n"); CHECK(hasWarning(incomplete,"W023"));
    const auto unknown = nc("GDEF12345 X1 END\nGCALL\n"); CHECK(hasWarning(unknown,"W012"));
}

TEST_CASE("GNC62 measuring mandatory E and G94", "[stage11b][cycles]") {
    for (const int code : {923,924,933}) {
        const std::string params = " D80 H-10 AH20 X0 Y0 AC100 AD100 AN54";
        auto r = nc("G94\nGDEF" + std::to_string(code) + params + " END\nGCALL\n");
        CHECK(hasWarning(r,"W023"));
        r = nc("G94\nGDEF" + std::to_string(code) + params + " E5 END\nGCALL\n");
        CHECK_FALSE(hasWarning(r,"W023")); CHECK_FALSE(hasWarning(r,"W024"));
        r = nc("G95\nGDEF" + std::to_string(code) + params + " E5 END\nGCALL\n");
        CHECK(hasWarning(r,"W024"));
    }
    CHECK(hasWarning(nc("G95\nCALL prb_xyz\n"),"W024"));
    CHECK_FALSE(hasWarning(nc("G94\nCALL prb_xyz\n"),"W024"));
    CHECK_FALSE(hasWarning(nc("G95\nGDEF940 END\nGCALL\n"),"W024"));
    CHECK(warningSeverity("W023") == Severity::Error); CHECK(warningSeverity("W024") == Severity::Error);
    CHECK(hasWarning(nc("GDEF923 D80 H-10 AH20 X0Y0 AN54 E0 END\nGCALL\n"),"W023"));
}

TEST_CASE("GNC62 defective measuring subprograms have dedicated warnings", "[stage11b][cycles]") {
    for (const int code : {932,934,935,960}) {
        const auto r = nc("G94\nGDEF" + std::to_string(code) + " D80 H-10 AH20 X0 Y0 AC100 AD100 AN54 END\nGCALL\n");
        CHECK(warnings(r,"W025") == 1); CHECK_FALSE(hasWarning(r,"W012"));
        const auto w = std::find_if(r.warnings.begin(),r.warnings.end(),[](const auto& item) { return item.code == "W025"; });
        REQUIRE(w != r.warnings.end()); CHECK(contains(w->message,code == 960 ? "G55" : "GOTO"));
    }
    CHECK_FALSE(hasWarning(nc("GDEF922 D50 AN54 END\nGCALL\n"),"W025"));
    CHECK(warningSeverity("W025") == Severity::Warning);
}

TEST_CASE("GNC62 measuring side effects invalidate position and WCS offsets", "[stage11b][cycles]") {
    const auto r = nc("G54 G0 X0Y0Z200\nGDEF924 AC100 AD100 H-10 AH20 X0Y0 AN55 E5 END\nGCALL\nG0 X1Y2Z200\n",true);
    CHECK(hasWarning(r,"W026")); CHECK_FALSE(hasWarning(r,"W021"));
    REQUIRE(r.segments.size() == 2); CHECK(r.segments.back().wcs == "G55"); CHECK_FALSE(r.segments.back().from.known[2]);
    CHECK(std::any_of(r.warnings.begin(),r.warnings.end(),[](const auto& w) { return w.code == "W026" && contains(w.message,"G28 Z0"); }));
    const auto writes = nc("GDEF930 A3 E-20 AN54 AG55 X0Y0Z0 END\nGCALL\nG0 X1Y2Z200\n",true);
    REQUIRE(writes.segments.size() == 1); CHECK(writes.segments[0].offsetsUnknown);
    CHECK(warningSeverity("W026") == Severity::Info);
}

TEST_CASE("GNC62 documented measuring families are known", "[stage11b][cycles]") {
    const auto& d = gnc62Dialect();
    for (const int code : {830,831,832,910,911,912,913,914,915,916,917,918,919,920,921,922,923,924,925,926,927,
                          930,931,932,933,934,935,940,941,942,950,951,952,953,954,957,960,961,962,963,965,966,
                          971,972,973,974,975,976,977,978,979}) {
        INFO(code); REQUIRE(d.definition(code)); CHECK(d.g(code)); CHECK(d.describeG(code));
    }
    CHECK_FALSE(d.g(12345)); CHECK_FALSE(d.definition(12345));
    CHECK(d.g(44)); CHECK_FALSE(d.g(81)); // Fanuc codes are not inherited without a GNC62 source.
}

TEST_CASE("GNC62 KEDE M code table overrides Fanuc meanings", "[stage11b][dialect]") {
    for (const int code : {3,4,5,6,7,8,9,10,15,16,19,21,22,23,24,25,26,27,28,29,31,32,33,34,35,36,37,38,
                          41,42,43,44,45,46,47,48,63,64,65,66,67,68,69,101,151,152,153,154}) {
        INFO(code); CHECK(gnc62Dialect().m(code)); CHECK(gnc62Dialect().describeM(code));
    }
    CHECK(gnc62Dialect().m(15) == MClass::Other); CHECK_FALSE(gnc62Dialect().m(777));
    CHECK_FALSE(hasWarning(nc("M15 M16 M151 M152 M153 M154 M31 M33 M63 M64\n"),"W012"));
    CHECK(hasWarning(nc("G12345 M777\n"),"W012"));
}

TEST_CASE("GNC62 turning spindle channels and dwell seconds", "[stage11b][turning]") {
    const auto r = nc("G54 G0 X20Y0Z0\nS6000 M3\nG211 \"C\"\nM151\nG93 S(2)800\nG97 S(2)300\n"
        "S(2)300 M3S(2)\nD2\nG18 G95 F0.1\nG1 X10 Z-1\nG4 F2\nG1 Z-2\nG210S(2)\nM16\nG17 G94 G1 X0 F600\nM30\n");
    CHECK(r.file.lexerDiagnostics == 0); CHECK_FALSE(hasWarning(r,"W012")); CHECK_FALSE(hasWarning(r,"W004"));
    REQUIRE(r.segments.size() == 5); CHECK(r.segments[1].plane == Plane::ZX);
    CHECK(r.segments[1].s == 300); CHECK(r.segments[1].spindleChannel == 2);
    CHECK(r.segments[2].dwellSeconds == 2); CHECK(r.segments[3].feed == 0.1);
    CHECK(r.segments.back().s == 6000); CHECK(r.segments.back().spindleChannel == 0);
    CHECK(r.time.dwellSec == 2); CHECK(hasWarning(r,"W027"));
}

TEST_CASE("GNC62 CSS is not RPM and G93 is not inverse feed", "[stage11b][turning]") {
    const auto r = nc("G211 \"C\"\nG97 S(2)300\nM3S(2)\nG95 F0.1\nG93 S(2)800\nG54 G0 X10Y0Z0\n"
        "G1 Z-1\nG96 S(2)100\nG1 Z-2\nG97 S(2)400\nG1 Z-3\n",true);
    REQUIRE(r.segments.size() == 4);
    CHECK(r.segments[1].s == 300); CHECK(r.segments[1].feedMode == FeedMode::PerRev);
    CHECK(r.segments[2].surfaceSpeed); CHECK_FALSE(r.segments[2].hasS);
    CHECK(r.segments[3].s == 400); CHECK_FALSE(r.segments[3].surfaceSpeed);
    CHECK_FALSE(hasWarning(r,"W004")); CHECK_FALSE(hasWarning(r,"W020"));
    const auto cancelled = nc("G211 \"C\"\nM16\nG54 G0 X0Y0Z0\n");
    REQUIRE(cancelled.segments.size() == 1); CHECK(cancelled.segments[0].spindleChannel == 0);
    const auto inverse = nc("G54 G0 X0Y0Z0\nS1000M3\nG193 F10 G1 X10\nG94 G1 X20 F600\n");
    REQUIRE(inverse.segments.size() == 3); CHECK(hasWarning(inverse,"W027"));
    CHECK_FALSE(hasWarning(inverse,"W012")); CHECK(inverse.segments[1].feedMode == FeedMode::Unsupported);
    CHECK(inverse.time.feedSec == Approx(1));
}

TEST_CASE("GNC62 transforms never turn parameters into axis moves", "[stage11b][geometry]") {
    const auto r = nc("G54 G0 X0Y0Z200 A0C0\nG52 X10000 Y20000 Z30000 A25 B0 C45\nG226\nG200.2 F5000\nG203\n"
        "G0 X1Y2Z200 A25C45\nG204\nG227\nG52\nG0 X0Y0Z200\n",true);
    REQUIRE(r.segments.size() == 3); CHECK(r.segments[1].approximate);
    CHECK_FALSE(hasWarning(r,"W012")); CHECK_FALSE(hasWarning(r,"W021")); CHECK(hasWarning(r,"W027"));
    CHECK_FALSE(r.segments.back().approximate);
    const auto spiral = nc("G54 G0 X0Y0Z0\nG2.8 X10Y0Z-1 R5\nX20Y0Z-2\nG1 X30 F600 S1000M3\n");
    CHECK_FALSE(hasWarning(spiral,"W012")); CHECK(hasWarning(spiral,"W027"));
    REQUIRE(spiral.segments.size() == 2); CHECK_FALSE(spiral.segments.back().from.known[0]);
}

TEST_CASE("Measuring WCS writes separate span checks before and after", "[stage11b][limits]") {
    const auto dir = std::filesystem::path(GCA_TEST_OUTPUT_DIR)/"stage11b";
    std::filesystem::create_directories(dir); const auto file = dir/"span.ini";
    { std::ofstream out(file); out << "[axes]\nx_min=0\nx_max=100\n"; }
    auto o = gnc(); o.machineFile = file;
    const auto r = analyzeText("G54 G0 X0Y0Z0\nG0 X1\nGDEF930 A1 E10 AN54 AG54 END\nGCALL\nG0 X200Y0Z0\nG0 X201\n","span.nc",o);
    CHECK_FALSE(hasWarning(r,"W021")); CHECK(hasWarning(r,"W022"));
    CHECK(std::count_if(r.limits.begin(),r.limits.end(),[](const auto& l) { return l.parameter == "X размах G54"; }) == 2);
}

TEST_CASE("KEDE profile uses real controller parameters over XML", "[stage11b][ini]") {
    CHECK(parseNumber("0.00034604383273520423") == Approx(0.00034604383273520423));
    CHECK(parseNumber("0.0000000000000000000000001") == Approx(1e-25).epsilon(1e-12));
    const auto p = readMachineIni(std::filesystem::path(GCA_MACHINE_DIR)/"kede-uni800ut.ini",{});
    CHECK(p.axes[0].min == -402); CHECK(p.axes[0].max == 402);
    CHECK(p.axes[1].min == -422); CHECK(p.axes[1].max == 382);
    CHECK(p.axes[2].min == 113); CHECK(p.axes[2].max == 667);
    CHECK(p.axes[4].min == -132); CHECK(p.axes[4].max == 132);
    CHECK_FALSE(p.axes[3].min); CHECK_FALSE(p.axes[3].max); CHECK_FALSE(p.axes[5].min); CHECK_FALSE(p.axes[5].max);
    CHECK_FALSE(p.feedMax); CHECK(p.spindleMax == 14000); CHECK_FALSE(p.toolChangeSec); CHECK(p.wcsOffsets.empty());
    CHECK(p.axes[0].rapid == 48000); CHECK(p.axes[4].rapid == 7200); CHECK(p.axes[5].rapid == 25200);
    CHECK(p.axes[0].acceleration == 7000); CHECK(p.axes[0].rapidAcceleration == 4000);
    CHECK(p.axes[4].acceleration == 1800); CHECK(p.axes[5].rapidAcceleration == 3240);
    CHECK(p.axes[0].home == Approx(-399.9432551094965)); CHECK(p.axes[1].home == Approx(379.9973446964845));
    CHECK(p.axes[2].home == 665.105); CHECK(p.axes[4].home == Approx(0.00034604383273520423));
}

TEST_CASE("KEDE limits XYZ A boundaries and unlimited C B feed", "[stage11b][limits]") {
    for (const auto* body : {"G53 G0 X403\n", "G53 G0 Y383\n", "G53 G0 Z668\n", "G53 G0 A133\n"})
        CHECK(hasWarning(nc(body,true),"W021"));
    const auto r = nc("G53 G0 X-402Y-422Z113 A-132 C100000 B100000\nG53 G0 X402Y382Z667 A132\nF999999 S14000\n",true);
    CHECK_FALSE(hasWarning(r,"W021")); CHECK_FALSE(hasWarning(r,"W019")); CHECK_FALSE(hasWarning(r,"W020"));
    CHECK(hasWarning(nc("S14001\n",true),"W020"));
    const auto rot = nc("G53 G0 A133 C999999\n",true);
    CHECK(contains(renderJsonReport(rot),"\"a\":133.0000"));
    CHECK(contains(renderJsonReport(rot),"\"c\":999999.0000"));
}

TEST_CASE("Separate rapid acceleration preserves old profiles", "[stage11b][time]") {
    const auto dir = std::filesystem::path(GCA_TEST_OUTPUT_DIR)/"stage11b";
    std::filesystem::create_directories(dir); const auto file = dir/"time.ini";
    { std::ofstream out(file); out << "[time]\nrapid_x=600\naccel_x=10\nrapid_accel_x=20\n"; }
    AnalysisOptions o; o.machineFile = file;
    const auto r = analyzeText("G92 X0Y0Z0\nG54 S1000M3\nG0 X100\nG1 X200 F600\nM30\n","time.nc",o);
    // G0: L/v+v/a = 100/10+10/20 = 10.5 s; G1: 100/10+10/10 = 11 s.
    CHECK(r.machineTime.motion.rapidSec == Approx(10.5)); CHECK(r.machineTime.motion.feedSec == Approx(11));
    CHECK_THROWS(parseMachineIni("[time]\nrapid_accel_a=0\n","bad"));
    CHECK_THROWS(parseMachineIni("[axes]\nc_min=10\nc_max=-10\n","bad"));
}

TEST_CASE("KEDE rotary rapid has independent known time", "[stage11b][time]") {
    const auto r = nc("G53 G0 A0 C0\nG53 G0 A120 C420\n",true);
    // A: 120/120+120/1080; C: 420/420+420/3240. Simultaneous G0=max(A,C).
    CHECK(r.machineTime.motion.rapidSec == Approx(1 + 420.0/3240));
}

TEST_CASE("GNC62 reference zero is not a cutting move to Z zero", "[stage11b][home]") {
    const auto r = nc("G53 G0 Z200\nG28 Z0\n",true);
    REQUIRE(r.segments.size() == 2); CHECK(r.segments.back().kind == SegmentKind::Home);
    CHECK(r.segments.back().homePathUnknown); CHECK_FALSE(r.segments.back().intermediate.known[2]);
    CHECK_FALSE(hasWarning(r,"W021"));
    CHECK(std::any_of(r.machineTime.notes.begin(),r.machineTime.notes.end(),[](const auto& s) { return contains(s,"G28/G30 не учтён"); }));
}

TEST_CASE("GNC62 modal G53 keeps arc geometry in machine coordinates", "[stage11b][geometry]") {
    const auto r = nc("G53\nG0 X10Y0Z200\nS1000M3\nG3 X0Y10 I-10J0 F600\nG54\nG0 X0Y0Z200\n",true);
    REQUIRE(r.segments.size() == 3); CHECK(r.segments[1].kind == SegmentKind::Arc);
    CHECK(r.segments[1].machineCoordinates); CHECK(r.segments[1].wcs == "G53");
    // Quarter circle of radius 10: length=pi*10/2, speed=10 mm/s, t=pi/2 s.
    CHECK(r.time.feedSec == Approx(3.141592653589793/2));
    CHECK_FALSE(r.segments.back().machineCoordinates); CHECK_FALSE(r.segments.back().from.known[0]);
    const auto circle = nc("G53\nG0 X10Y0Z200\nS1000M3\nG3 I-10J0 F600\n",true);
    REQUIRE(circle.segments.size() == 2); CHECK(circle.segments.back().machineCoordinates);
    // Full R10 circle at 10 mm/s: 2*pi*10/10 = 2*pi seconds.
    CHECK(circle.time.feedSec == Approx(2*3.141592653589793));
}

TEST_CASE("Stage11 report cosmetics do not change calculations", "[stage11b][report]") {
    const auto r = nc("G54 G0 X0Y0Z0\nG1 X10 F600 S1000M3\n");
    const auto text = renderTextReport(r);
    CHECK_FALSE(contains(text,"Параметр | Значение в УП"));
    const auto first = text.find("оценка без ускорений"); REQUIRE(first != std::string::npos);
    CHECK(text.find("оценка без ускорений",first+1) == std::string::npos);
}

TEST_CASE("GNC62 Q body fragments have no header or parse errors", "[stage11b][fragments]") {
    for (int n = 1; n <= 5; ++n) {
        const auto file = std::filesystem::path(GCA_TEST_DATA_DIR)/("gnc62-q"+std::to_string(n)+"-body.nc");
        const auto text = readFile(file);
        CHECK_FALSE(contains(text,"TOOLS LIST")); CHECK_FALSE(contains(text,"OPERATIONS LIST"));
        CHECK_FALSE(contains(text,"MACHINE TIME")); CHECK_FALSE(contains(text,"PART:"));
        const auto r = analyzeText(text,pathToUtf8(file.filename()),gnc(true));
        INFO(n); CHECK(r.file.lexerDiagnostics == 0); CHECK_FALSE(hasWarning(r,"W012")); CHECK_FALSE(hasWarning(r,"W013"));
        CHECK_FALSE(r.segments.empty()); CHECK_FALSE(r.toolSequence.empty());
    }
}

TEST_CASE("GNC62 CLI synthetic report and JSON", "[stage11b][cli]") {
    const auto input = std::filesystem::path(GCA_TEST_DATA_DIR)/"gnc62-synthetic.nc";
    const auto output = std::filesystem::path(GCA_TEST_OUTPUT_DIR)/"gnc62-synthetic.json";
    std::ostringstream out,err;
    CHECK(runCli({pathToUtf8(input),"--json",pathToUtf8(output)},out,err,GCA_MACHINE_DIR) == 0);
    CHECK(err.str().empty()); CHECK(contains(out.str(),"KEDE UNI 800 UT"));
    const auto json = readFile(output); CHECK(contains(json,"\"dialect\":\"gnc62\""));
    CHECK(contains(json,"\"code\":\"W025\"")); CHECK(contains(json,"\"code\":\"W021\""));
    CHECK(contains(json,"\"severity\":\"info\""));
}
