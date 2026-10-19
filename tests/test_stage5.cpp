#include "gca/cli.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace gca;
using namespace gca::test;

namespace {

struct CliResult {
    int code = -1;
    std::string out;
    std::string err;
};

CliResult cli(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    CliResult r;
    r.code = runCli(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

std::string dataPath(const char* name) { return (std::filesystem::path(GCA_TEST_DATA_DIR) / name).string(); }

std::string normalizeEol(std::string s) {
    std::string r;
    r.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        r += s[i];
    }
    return r;
}

/// Compares the text with tests/golden/<name>. If the file tests/golden/UPDATE exists the golden
/// file is rewritten instead (conscious update of the reference).
void checkGolden(const std::string& name, const std::string& actual) {
    const std::filesystem::path dir{GCA_GOLDEN_DIR};
    const std::filesystem::path file = dir / name;
    if (std::filesystem::exists(dir / "UPDATE")) {
        std::ofstream out(file, std::ios::binary);
        out << actual;
        SUCCEED("golden updated: " + name);
        return;
    }
    REQUIRE(std::filesystem::exists(file));
    const std::string expected = normalizeEol(readFile(file));
    CHECK(normalizeEol(actual) == expected);
}

std::filesystem::path tempFile(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("gca-test-") + name);
}

} // namespace

TEST_CASE("CLI: --help and --version", "[cli][stage5]") {
    const auto h = cli({"--help"});
    CHECK(h.code == 0);
    CHECK(h.out.find("--out") != std::string::npos);
    CHECK(h.out.find("--dialect") != std::string::npos);
    CHECK(h.out.find("--rapid") != std::string::npos);
    const auto v = cli({"--version"});
    CHECK(v.code == 0);
    CHECK(v.out.find(version()) != std::string::npos);
}

TEST_CASE("CLI: exit code 2 for bad arguments", "[cli][stage5]") {
    CHECK(cli({}).code == 2);
    CHECK(cli({"--bogus", "x.nc"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--rapid"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--rapid", "abc"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--rapid", "-5"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--rapid", "0"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--dialect"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--dialect", "siemens"}).code == 2);
    CHECK(cli({dataPath("known-time.nc"), "--out"}).code == 2);
    CHECK(cli({"a.nc", "b.nc"}).code == 2);
}

TEST_CASE("CLI: exit code 1 for I/O errors", "[cli][stage5]") {
    const auto missing = cli({dataPath("no-such-file.nc")});
    CHECK(missing.code == 1);
    CHECK_FALSE(missing.err.empty());
    const auto badOut = cli({dataPath("known-time.nc"), "--out", dataPath("no-such-dir/report.txt")});
    CHECK(badOut.code == 1);
}

TEST_CASE("CLI: report to stdout", "[cli][stage5]") {
    const auto r = cli({dataPath("known-time.nc")});
    CHECK(r.code == 0);
    CHECK(r.out.find("== Время обработки") != std::string::npos);
    CHECK(r.out.find("Шпаргалка для постпроцессора") != std::string::npos);
}

TEST_CASE("CLI: --out writes a full report file and prints only a short message", "[cli][stage5]") {
    const auto file = tempFile("report1.txt");
    std::filesystem::remove(file);
    const auto r = cli({samplePath("sample-01-skymaster-vl1100a.nc"), "--out", file.string()});
    CHECK(r.code == 0);
    CHECK(r.out.find("Отчёт записан") != std::string::npos);
    CHECK(r.out.find("== Время") == std::string::npos);
    REQUIRE(std::filesystem::exists(file));
    const std::string text = readFile(file);
    for (const char* section : {"== Файл ==", "== Инструменты ==", "== Время обработки", "== Габариты траектории", "== Рабочие системы координат",
                                "== Подачи и обороты", "== Циклы сверления", "== G-коды ==", "== M-коды ==", "== Машинно-специфичные",
                                "== Предупреждения ==", "== Шпаргалка для постпроцессора =="}) {
        CHECK(text.find(section) != std::string::npos);
    }
    CHECK(text.find('\r') == std::string::npos);        // '\n' line endings
    std::filesystem::remove(file);
}

TEST_CASE("CLI: --dialect fanuc-mill is accepted, --rapid changes the time", "[cli][stage5]") {
    const auto a = cli({dataPath("known-time.nc"), "--dialect", "fanuc-mill"});
    CHECK(a.code == 0);
    const Report r10 = analyzeText(readFile(dataPath("known-time.nc")), "known-time.nc", [] { AnalysisOptions o; return o; }());
    AnalysisOptions slow;
    slow.rapidFeedMmPerMin = 1000.0;
    const Report r1 = analyzeText(readFile(dataPath("known-time.nc")), "known-time.nc", slow);
    CHECK(r1.time.rapidSec > r10.time.rapidSec * 9.0);
    const auto b = cli({dataPath("known-time.nc"), "--rapid", "1000"});
    CHECK(b.code == 0);
    CHECK(b.out != a.out);
    CHECK(b.out.find("Скорость G0: 1000 мм/мин") != std::string::npos);
}

TEST_CASE("test data: known time", "[stage5][data]") {
    const Report r = analyzeText(readFile(dataPath("known-time.nc")), "known-time.nc", AnalysisOptions{});
    const double expected = 0.6 + 10.0 + 15.70796 / 600.0 * 60.0 + 0.5 + 0.3;
    CHECK(r.time.totalSec() == Catch::Approx(expected).epsilon(0.005));
    CHECK(r.warnings.empty());
}

TEST_CASE("test data: warnings program triggers the expected warnings", "[stage5][data]") {
    const Report r = analyzeText(readFile(dataPath("many-warnings.nc")), "many-warnings.nc", AnalysisOptions{});
    for (const char* code : {"W001", "W004", "W005", "W006", "W009", "W010", "W011", "W012", "W014", "W017"}) {
        INFO(code);
        CHECK(hasWarning(r, code));
    }
}

TEST_CASE("test data: axis B indexing program", "[stage5][data][axisB]") {
    const Report r = analyzeText(readFile(dataPath("axis-b-index.nc")), "axis-b-index.nc", AnalysisOptions{});
    REQUIRE(r.box.axis[kAxisB].has);
    CHECK(r.box.axis[kAxisB].min == Catch::Approx(0.0));
    CHECK(r.box.axis[kAxisB].max == Catch::Approx(180.0));
    const std::string b = cheatSection(r, "5.");
    CHECK(b.find("Ось B в программе не используется") == std::string::npos);
    CHECK(b.find("диапазон 0 .. 180") != std::string::npos);
    CHECK(b.find("M11") != std::string::npos);
    CHECK(b.find("M10") != std::string::npos);
    CHECK(r.file.lexerDiagnostics == 0);
}

TEST_CASE("golden: sample 1 report", "[golden][stage5][samples]") {
    AnalysisOptions o;
    const Report r = analyzeText(sample1(), "sample-01-skymaster-vl1100a.nc", o);
    checkGolden("sample-01-skymaster-vl1100a.txt", renderTextReport(r));
}

TEST_CASE("golden: sample 2 report", "[golden][stage5][samples]") {
    AnalysisOptions o;
    const Report r = analyzeText(sample2(), "sample-02-bridgeport-1300-480.nc", o);
    checkGolden("sample-02-bridgeport-1300-480.txt", renderTextReport(r));
}

TEST_CASE("golden: axis B report", "[golden][stage5][data][axisB]") {
    const Report r = analyzeText(readFile(dataPath("axis-b-index.nc")), "axis-b-index.nc", AnalysisOptions{});
    checkGolden("axis-b-index.txt", renderTextReport(r));
}

TEST_CASE("report does not depend on the line ending style of the input", "[stage5][eol]") {
    std::string lf = readFile(dataPath("axis-b-index.nc"));
    std::string crlf;
    for (const char c : lf) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const Report a = analyzeText(lf, "x.nc", AnalysisOptions{});
    const Report b = analyzeText(crlf, "x.nc", AnalysisOptions{});
    CHECK(a.time.totalSec() == Catch::Approx(b.time.totalSec()));
    CHECK(a.toolSequence.size() == b.toolSequence.size());
    CHECK(a.warnings.size() == b.warnings.size());
    CHECK(a.file.lfLines > 0);
    CHECK(b.file.crlfLines > 0);
}

TEST_CASE("input without trailing newline and with trailing percent", "[stage5][eol]") {
    const Report r = analyzeText("O\nG0 X1.\nM30\n%", "x.nc", AnalysisOptions{});
    CHECK(r.file.lines == 4);
}
