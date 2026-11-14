#include "gca/json_report.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/cli.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <locale>
#include <sstream>

using namespace gca;
using namespace gca::test;

namespace {
Report data(const char* name) {
    return analyzeText(readFile(std::filesystem::path(GCA_TEST_DATA_DIR) / name), name, AnalysisOptions{});
}
struct CliResult { int code; std::string out, err; };
CliResult cli(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    const int code = runCli(args, out, err);
    return {code, out.str(), err.str()};
}
std::filesystem::path output(const char* name) {
    const auto dir = std::filesystem::path(GCA_TEST_OUTPUT_DIR) / pathFromUtf8("папка с пробелом");
    std::filesystem::create_directories(dir);
    return dir / pathFromUtf8(name);
}
}

TEST_CASE("JSON golden known-time", "[stage9][json][golden]") {
    const auto r = data("known-time.nc");
    CHECK(renderJsonReport(r) == readFile(std::filesystem::path(GCA_GOLDEN_DIR) / "known-time.json"));
    REQUIRE(r.segments.size() == 5);
    // 100 mm rapid: .6 s; 100 mm feed: 10 s; R10 quarter circle: pi/2 s;
    // dwell: .5 s; 50 mm rapid: .3 s. Total = 11.4 + pi/2 seconds.
    CHECK(r.time.totalSec() == Catch::Approx(11.4 + 3.14159265358979323846 / 2.0));
}

TEST_CASE("JSON golden axis-b-index", "[stage9][json][golden]") {
    const auto r = data("axis-b-index.nc");
    CHECK(renderJsonReport(r) == readFile(std::filesystem::path(GCA_GOLDEN_DIR) / "axis-b-index.json"));
    CHECK(contains(renderJsonReport(r), "\"b\":90.0000"));
    CHECK(contains(renderJsonReport(r), "\"b\":180.0000"));
}

TEST_CASE("JSON escaping", "[stage9][json]") {
    Report r;
    r.file.fileName = "деталь \"1\"\\тест.nc";
    r.warnings.push_back({7, "\"\\\n\r\t\b\f\x01", "W013", "кириллица"});
    const auto json = renderJsonReport(r);
    CHECK(contains(json, "деталь \\\"1\\\"\\\\тест.nc"));
    CHECK(contains(json, "\"text\":\"\\\"\\\\\\n\\r\\t\\b\\f\\u0001\""));
    CHECK(contains(json, "кириллица"));
    CHECK(json.find('\x01') == std::string::npos);
    CHECK(json.compare(0, 3, "\xEF\xBB\xBF") != 0);
}

TEST_CASE("JSON arc geometry in all planes", "[stage9][json][geometry]") {
    // In each oriented plane: start=(10,0), end=(0,10), center=(0,0), R=10.
    for (const auto& program : {
        "G92 X10 Y0 Z0\nG17 G3 X0 Y10 I-10 J0 F600\n",
        "G92 X0 Y0 Z10\nG18 G3 Z0 X10 K-10 I0 F600\n",
        "G92 X0 Y10 Z0\nG19 G3 Y0 Z10 J-10 K0 F600\n"}) {
        const auto r = analyze(program);
        REQUIRE(r.segments.size() == 1);
        const auto json = renderJsonReport(r);
        CHECK(contains(json, "\"center\":[0.0000,0.0000,0.0000]"));
        CHECK(contains(json, "\"cw\":false,\"radius\":10.0000"));
        CHECK(r.time.feedMm == Catch::Approx(5.0 * 3.14159265358979323846));
    }
    const auto cw = renderJsonReport(analyze("G92 X10 Y0 Z0\nG2 X0 Y-10 R10 F600\n"));
    CHECK(contains(cw, "\"cw\":true,\"radius\":10.0000"));
    const auto circle = renderJsonReport(analyze("G92 X10 Y0 Z0\nG2 I-10 J0 F600\n"));
    CHECK(contains(circle, "\"start\":[10.0000,0.0000,0.0000],\"end\":[10.0000,0.0000,0.0000]"));
    const auto helix = renderJsonReport(analyze("G92 X10 Y0 Z0\nG3 X0 Y10 Z5 I-10 J0 F600\n"));
    CHECK(contains(helix, "\"end\":[0.0000,10.0000,5.0000],\"center\":[0.0000,0.0000,0.0000]"));
}

TEST_CASE("JSON unknown axes and invalid arc", "[stage9][json]") {
    const auto json = renderJsonReport(analyze("G0 X10\nG91 G28 Z0\nG90 G3 X20 Y10 R1 F100\n"));
    CHECK(contains(json, "\"start\":[null,null,null],\"end\":[10.0000,null,null]"));
    CHECK(contains(json, "\"kind\":\"home\""));
    CHECK(contains(json, "\"center\":[null,null,null],\"cw\":false,\"radius\":null"));
    CHECK(contains(json, "\"b\":null"));
}

TEST_CASE("Warning severities", "[stage9][json][warnings]") {
    const std::array<std::string_view, 18> expected{
        "warning", "warning", "warning", "error", "error", "warning", "warning", "error", "error",
        "error", "error", "warning", "info", "warning", "info", "error", "error", "error"};
    Report r;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto code = "W" + std::string(i < 9 ? "00" : "0") + std::to_string(i + 1);
        CHECK(severityName(warningSeverity(code)) == expected[i]);
        r.warnings.push_back({i + 1, "", code, ""});
        CHECK(contains(renderJsonReport(r), "\"code\":\"" + code + "\",\"severity\":\"" + std::string(expected[i]) + "\""));
    }
}

TEST_CASE("JSON numbers ignore locale and reject nonfinite values", "[stage9][json]") {
    struct Comma : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
    };
    auto r = data("known-time.nc");
    r.time.dwellSec = std::numeric_limits<double>::infinity();
    std::ostringstream out;
    out.imbue(std::locale(std::locale::classic(), new Comma));
    out << std::scientific;
    writeJsonReport(r, out);
    CHECK(contains(out.str(), "\"rapidSec\":0.9000"));
    CHECK(contains(out.str(), "\"dwellSec\":null"));
    CHECK(out.str().find("inf") == std::string::npos);
}

TEST_CASE("JSON cycle and effective G95 feed", "[stage9][json]") {
    const auto r = analyze("G92 X0 Y0 Z10\nG54 S1000 M3\nG95 G81 Z-5 R2 F0.1\nG80\n");
    const auto json = renderJsonReport(r);
    CHECK(contains(json, "\"kind\":\"cycle\""));
    CHECK(contains(json, "\"feed\":100.0000"));
    CHECK(contains(json, "\"zR\":2.0000,\"zBottom\":-5.0000,\"zInit\":10.0000"));
    CHECK(contains(json, "\"returnToInitial\":true"));
    CHECK(contains(renderJsonReport(analyze("G18 G0 X1\n")), "\"plane\":\"G18\""));
}

TEST_CASE("CLI JSON output and errors", "[stage9][json][cli]") {
    const auto input = output("деталь 1.nc");
    { std::ofstream file(input, std::ios::binary); file << readFile(std::filesystem::path(GCA_TEST_DATA_DIR) / "known-time.nc"); }
    const auto txt = output("отчёт.txt"), json = output("данные.json");
    const std::string in = pathToUtf8(input), j = pathToUtf8(json), t = pathToUtf8(txt);
    REQUIRE(cli({in, "--out", t, "--json", j, "--bom"}).code == 0);
    CHECK(readFile(json) == renderJsonReport(analyzeFileReport(input, AnalysisOptions{})));
    CHECK(readFile(txt).substr(3) == analyzeFile(input, AnalysisOptions{}));
    CHECK(readFile(txt).substr(0, 3) == "\xEF\xBB\xBF");
    CHECK(cli({in, "--json", j}).out == cli({in}).out);
    CHECK(cli({in, "--json"}).code == kExitBadArguments);
    CHECK(cli({in, "--json", pathToUtf8(output("missing") / "data.json")}).code == kExitIoError);
    const auto original = readFile(input);
    CHECK(cli({in, "--json", in}).code == kExitBadArguments);
    CHECK(readFile(input) == original);
    CHECK(cli({in, "--out", t, "--json", t}).code == kExitBadArguments);
}
