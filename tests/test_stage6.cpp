// Stage 6: UTF-8 paths, Russian messages, --bom, option table; stage 7 CLI modes (--machine, --list-*).
#include "gca/cli.hpp"
#include "gca/dialect.hpp"
#include "gca/options.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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

std::string known() { return (std::filesystem::path(GCA_TEST_DATA_DIR) / "known-time.nc").string(); }

std::filesystem::path tempFile(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("gca-test6-") + name);
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

std::vector<std::string> splitTabs(const std::string& s) {
    std::vector<std::string> f;
    std::size_t start = 0;
    for (;;) {
        const std::size_t p = s.find('\t', start);
        f.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return f;
}

} // namespace

TEST_CASE("pathFromUtf8 / pathToUtf8 round trip with Cyrillic", "[stage6][path]") {
    const std::string utf8 = "C:/УП/деталь №1.nc";
    const std::filesystem::path p = pathFromUtf8(utf8);
    CHECK(pathToUtf8(p) == utf8);
    CHECK(pathToUtf8(p.filename()) == "деталь №1.nc");
    CHECK(pathToUtf8(pathFromUtf8("")).empty());
}

TEST_CASE("CLI: input and output paths with Cyrillic", "[stage6][path][cli]") {
    const auto dir = tempFile("папка");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string input = pathToUtf8(dir / pathFromUtf8("деталь.nc"));
    const std::string output = pathToUtf8(dir / pathFromUtf8("отчёт.txt"));
    std::filesystem::copy_file(known(), pathFromUtf8(input));
    const auto r = cli({input, "--out", output});
    CHECK(r.code == 0);
    CHECK(r.out.find("отчёт.txt") != std::string::npos);
    REQUIRE(std::filesystem::exists(pathFromUtf8(output)));
    const std::string text = readFile(pathFromUtf8(output));
    CHECK(text.find("деталь.nc") != std::string::npos);   // the file name is shown in the report
    std::filesystem::remove_all(dir);
}

TEST_CASE("CLI: --bom writes EF BB BF, default has no BOM", "[stage6][cli]") {
    const auto file = tempFile("bom.txt");
    std::filesystem::remove(file);
    REQUIRE(cli({known(), "--out", pathToUtf8(file)}).code == 0);
    std::string plain = readFile(file);
    REQUIRE(plain.size() > 3);
    CHECK(plain.compare(0, 3, "\xEF\xBB\xBF") != 0);
    REQUIRE(cli({known(), "--out", pathToUtf8(file), "--bom"}).code == 0);
    const std::string withBom = readFile(file);
    REQUIRE(withBom.size() == plain.size() + 3);
    CHECK(withBom.compare(0, 3, "\xEF\xBB\xBF") == 0);
    CHECK(withBom.substr(3) == plain);
    std::filesystem::remove(file);
}

TEST_CASE("CLI: --help is Russian and built from the option table", "[stage6][cli][options]") {
    const auto h = cli({"--help"});
    CHECK(h.code == 0);
    CHECK(h.out.find("Использование") != std::string::npos);
    for (const auto& o : cliOptions()) CHECK(h.out.find(o.name) != std::string::npos);
    CHECK(h.out.find("--bom") != std::string::npos);
    CHECK(h.out.find("Коды возврата") != std::string::npos);
}

TEST_CASE("CLI: error messages are Russian", "[stage6][cli]") {
    const auto r = cli({known(), "--rapid", "abc"});
    CHECK(r.code == 2);
    CHECK(r.err.find("положительным") != std::string::npos);
    const auto m = cli({"nofile.nc"});
    CHECK(m.code == 1);
    CHECK(m.err.find("Ошибка ввода-вывода") != std::string::npos);
}

TEST_CASE("CLI: --list-options is one tab-separated line per option", "[stage7][cli][options]") {
    const auto r = cli({"--list-options"});
    CHECK(r.code == 0);
    const auto lines = splitLines(r.out);
    REQUIRE(lines.size() == cliOptions().size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto f = splitTabs(lines[i]);
        REQUIRE(f.size() == 5);
        CHECK(f[0] == cliOptions()[i].name);
        CHECK(f[4] == cliOptions()[i].description);
    }
    // the same table goes to a file when --out is given
    const auto file = tempFile("options.txt");
    REQUIRE(cli({"--list-options", "--out", pathToUtf8(file)}).code == 0);
    CHECK(readFile(file) == r.out);
    std::filesystem::remove(file);
}

TEST_CASE("CLI: --list-machines", "[stage7][cli][machine]") {
    const auto r = cli({"--list-machines"});
    CHECK(r.code == 0);
    const auto lines = splitLines(r.out);
    REQUIRE(lines.size() == findDialect("fanuc-mill")->machines().size());
    bool sky = false;
    for (const auto& l : lines) {
        const auto f = splitTabs(l);
        REQUIRE(f.size() == 2);
        if (f[0] == "skymaster-vl1100a") { sky = true; CHECK(f[1] == "Sky Master VL1100A"); }
    }
    CHECK(sky);
    CHECK(cli({"--list-machines", "--dialect", "siemens"}).code == 2);
}

TEST_CASE("CLI: --machine forces a profile, auto detects, unknown is an error", "[stage7][cli][machine]") {
    // known-time.nc has no "Generated for" header: no profile without --machine
    const auto none = cli({known()});
    CHECK(none.out.find("[профиль станка") == std::string::npos);
    const auto forced = cli({known(), "--machine", "bridgeport-1300-480"});
    CHECK(forced.code == 0);
    CHECK(forced.out.find("[профиль станка: Bridgeport 1300/480]") != std::string::npos);
    const auto autoOk = cli({known(), "--machine", "auto"});
    CHECK(autoOk.out == none.out);
    const auto bad = cli({known(), "--machine", "nope"});
    CHECK(bad.code == 2);
    CHECK(bad.err.find("skymaster-vl1100a") != std::string::npos);
    CHECK(cli({known(), "--machine"}).code == 2);
}

TEST_CASE("CLI: forced machine overrides detection on a sample", "[stage7][cli][machine][samples]") {
    const auto sky = cli({samplePath("sample-01-skymaster-vl1100a.nc")});
    CHECK(sky.out.find("[профиль станка: Sky Master VL1100A]") != std::string::npos);
    const auto bp = cli({samplePath("sample-01-skymaster-vl1100a.nc"), "--machine", "bridgeport-1300-480"});
    CHECK(bp.out.find("[профиль станка: Bridgeport 1300/480]") != std::string::npos);
}
