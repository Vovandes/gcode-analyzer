#include "gca/cli.hpp"
#include "gca/comparison.hpp"
#include "gca/path_util.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>

namespace {

using namespace gca;

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(GCA_TEST_DATA_DIR) / name;
}

Report analyzed(const char* name) { return analyzeFileReport(fixture(name), {}); }

const ToolComparison& forTool(const ComparisonReport& report, std::string_view tool = "T1") {
    const auto it = std::ranges::find(report.trajectory, tool, &ToolComparison::tool);
    REQUIRE(it != report.trajectory.end());
    return *it;
}

const ComparisonRow& summary(const ComparisonReport& report, std::string_view label) {
    const auto it = std::ranges::find(report.summary, label, &ComparisonRow::label);
    REQUIRE(it != report.summary.end());
    return *it;
}

std::string render(const Report& before, const Report& after, const ComparisonReport& comparison) {
    std::ostringstream text;
    writeTextComparison(before, after, comparison, text);
    return text.str();
}

void addPoint(Report& report, double x) {
    Segment segment;
    segment.kind = SegmentKind::Linear;
    segment.tool = 1;
    segment.lineNo = report.segments.size() + 1;
    segment.to.v[kAxisX] = x;
    segment.to.known[kAxisX] = true;
    report.segments.push_back(segment);
}

} // namespace

TEST_CASE("CompareSameFile reports identical format and trajectory", "[stage13]") {
    const auto before = analyzed("stage13-base.nc");
    const auto after = analyzed("stage13-base.nc");
    const auto result = compareReports(before, after);
    const auto& tool = forTool(result);
    CHECK(tool.presence == ToolPresence::Both);
    CHECK(tool.oldPoints == 4);
    CHECK(tool.newPoints == 4);
    CHECK(tool.differenceCount == 0);
    CHECK(result.format.empty());
    CHECK(result.newWarnings.empty());
    CHECK(result.resolvedWarnings.empty());
    CHECK(summary(result, "Кадры").difference == "0");
    // 30 mm at 600 mm/min takes 30 / 10 = 3 s.
    CHECK(summary(result, "Время без разгонов, с").before == "3");
    CHECK(summary(result, "Время по модели станка, с").after == "3");
    CHECK(test::contains(render(before, after, result), "T1: совпадает"));
}

TEST_CASE("CompareReformattedCopy shows format differences and matching points", "[stage13]") {
    const auto before = analyzed("stage13-base.nc");
    const auto after = analyzed("stage13-reformatted.nc");
    const auto result = compareReports(before, after);
    CHECK(forTool(result).differenceCount == 0);
    CHECK(result.newWarnings.empty());
    const auto text = render(before, after, result);
    for (const auto* label : {"Нумерация N", "Порядок слов", "Знаки после запятой",
                             "Шаблон смены инструмента", "Комментарии"}) {
        CAPTURE(label);
        CHECK(test::contains(text, label));
    }
    CHECK(test::contains(text, "первый номер 10"));
    CHECK(test::contains(text, "первый номер 100"));
    CHECK(test::contains(text, "целых с точкой"));
}

TEST_CASE("CompareShiftedPoint reports exactly one modified point and both source lines", "[stage13]") {
    const auto before = analyzed("stage13-base.nc");
    const auto after = analyzed("stage13-shifted.nc");
    const auto result = compareReports(before, after);
    const auto& tool = forTool(result);
    REQUIRE(tool.differenceCount == 1);
    REQUIRE(tool.differences.size() == 1);
    CHECK(tool.differences[0].kind == PointChange::Modified);
    CHECK(tool.differences[0].oldLine == 7);
    CHECK(tool.differences[0].newLine == 8);
    CHECK(test::contains(render(before, after, result), "изменена | 7 | 8"));
    const auto relaxed = compareReports(before, after, 0.1);
    CHECK(forTool(relaxed).differenceCount == 0);
}

TEST_CASE("CompareInsertedPoint restores alignment after the extra frame", "[stage13]") {
    const auto result = compareReports(analyzed("stage13-base.nc"), analyzed("stage13-inserted.nc"));
    const auto& tool = forTool(result);
    REQUIRE(tool.differenceCount == 1);
    REQUIRE(tool.differences.size() == 1);
    CHECK(tool.differences[0].kind == PointChange::Inserted);
    CHECK_FALSE(tool.differences[0].oldLine);
    CHECK(tool.differences[0].newLine == 8);
}

TEST_CASE("CompareDeletedPoint restores alignment after the missing frame", "[stage13]") {
    const auto result = compareReports(analyzed("stage13-inserted.nc"), analyzed("stage13-base.nc"));
    const auto& tool = forTool(result);
    REQUIRE(tool.differenceCount == 1);
    REQUIRE(tool.differences.size() == 1);
    CHECK(tool.differences[0].kind == PointChange::Deleted);
    CHECK(tool.differences[0].oldLine == 8);
    CHECK_FALSE(tool.differences[0].newLine);
}

TEST_CASE("CompareMissingTool reports added or removed tool without alignment", "[stage13]") {
    const auto one = analyzed("stage13-base.nc");
    const auto two = analyzed("stage13-two-tools.nc");
    const auto removed = compareReports(two, one);
    CHECK(forTool(removed).differenceCount == 0);
    const auto& oldTool = forTool(removed, "T2");
    CHECK(oldTool.oldPoints == 2);
    CHECK(oldTool.newPoints == 0);
    CHECK(oldTool.presence == ToolPresence::Removed);
    CHECK(oldTool.differenceCount == 0);
    CHECK(oldTool.differences.empty());
    CHECK(summary(removed, "Инструменты пропали").before == "T2");
    CHECK(test::contains(render(two, one, removed), "T2: инструмент пропал"));

    const auto added = compareReports(one, two);
    const auto& newTool = forTool(added, "T2");
    CHECK(newTool.presence == ToolPresence::Added);
    CHECK(newTool.differenceCount == 0);
    CHECK(newTool.differences.empty());
    CHECK(summary(added, "Инструменты добавлены").after == "T2");
    CHECK(test::contains(render(one, two, added), "T2: инструмент добавлен"));
}

TEST_CASE("CompareNamedTool matches GNC62 names across local tool IDs", "[stage13]") {
    AnalysisOptions options;
    options.dialect = "gnc62";
    const std::string program = "T\"DRILL\" M6\nG90 G54 G94\nS1000 M3\nG0 X0 Y0 Z0\nG1 X10 F600\nM30\n";
    const auto before = analyzeText(program, "old.nc", options);
    const auto after = analyzeText("T99\n" + program, "new.nc", options);
    REQUIRE(before.toolNames.size() == 1);
    REQUIRE(after.toolNames.size() == 1);
    CHECK(before.toolNames.begin()->first != after.toolNames.begin()->first);
    const auto result = compareReports(before, after);
    REQUIRE(result.trajectory.size() == 1);
    const auto& named = forTool(result, "T\"DRILL\"");
    CHECK(named.presence == ToolPresence::Both);
    CHECK(named.differenceCount == 0);
}

TEST_CASE("CompareWideGap reconstructs the alignment and limits displayed differences", "[stage13]") {
    Report before, after;
    for (int i = 0; i < 160; ++i) {
        const double x = i * 10.0;
        addPoint(before, x);
        addPoint(after, i == 0 || i == 159 ? x + 0.1 : x);
        if (i == 79)
            for (int j = 1; j <= 40; ++j) addPoint(after, x + j * 0.1);
    }
    const auto result = compareReports(before, after);
    const auto& tool = forTool(result);
    CHECK(tool.oldPoints == 160);
    CHECK(tool.newPoints == 200);
    CHECK(tool.differenceCount == 42);
    REQUIRE(tool.differences.size() == 20);
    CHECK(tool.differences.front().kind == PointChange::Modified);
    CHECK(tool.differences.front().oldLine == 1);
    CHECK(tool.differences.front().newLine == 1);
    for (std::size_t i = 1; i < tool.differences.size(); ++i) {
        CHECK(tool.differences[i].kind == PointChange::Inserted);
        CHECK_FALSE(tool.differences[i].oldLine);
        CHECK(tool.differences[i].newLine == 80 + i);
    }
}

TEST_CASE("CompareBand keeps matches around nonadjacent edits", "[stage13]") {
    Report before, after;
    for (int i = 0; i < 100; ++i) {
        addPoint(before, i * 10.0);
        if (i == 50) continue;
        if (i == 15) addPoint(after, 145.0);
        addPoint(after, i == 30 ? 300.1 : i * 10.0);
    }
    const auto result = compareReports(before, after);
    const auto& tool = forTool(result);
    REQUIRE(tool.differenceCount == 3);
    REQUIRE(tool.differences.size() == 3);
    CHECK(tool.differences[0].kind == PointChange::Inserted);
    CHECK(tool.differences[0].newLine == 16);
    CHECK(tool.differences[1].kind == PointChange::Modified);
    CHECK(tool.differences[1].oldLine == 31);
    CHECK(tool.differences[1].newLine == 32);
    CHECK(tool.differences[2].kind == PointChange::Deleted);
    CHECK(tool.differences[2].oldLine == 51);
}

TEST_CASE("CompareSummary tracks warning multiset, WCS, and F and S ranges", "[stage13]") {
    const auto before = test::analyze("T1 M6\nG54 G90 G94\nG0 X0 Y0 Z0\nG1 X3 F300\nM30\n");
    const auto after = test::analyze("T1 M6\nG55 G90 G94\nS2000 M3\nG0 X0 Y0 Z0\nG1 X3 F600\nM30\n");
    const auto result = compareReports(before, after);
    CHECK(summary(result, "Рабочие СК").difference == "-G54, +G55");
    CHECK(summary(result, "T1 · Рабочие СК").difference == "-G54, +G55");
    CHECK(summary(result, "T1 · F G94, мм/мин").after == "600");
    CHECK(summary(result, "T1 · S, об/мин").after == "2000");
    CHECK(summary(result, "Ошибки").trend == ComparisonTrend::Improved);
    REQUIRE(result.resolvedWarnings.size() == 1);
    CHECK(result.resolvedWarnings.front().code == "W004");
    CHECK(result.resolvedWarnings.front().line == 4);
    CHECK(result.newWarnings.empty());

    auto renumbered = before;
    REQUIRE_FALSE(renumbered.warnings.empty());
    renumbered.warnings.front().line += 50;
    renumbered.warnings.front().text = "N999 G1 X3.0";
    const auto same = compareReports(before, renumbered);
    CHECK(same.newWarnings.empty());
    CHECK(same.resolvedWarnings.empty());
    renumbered.warnings.push_back(renumbered.warnings.front());
    const auto repeated = compareReports(before, renumbered);
    CHECK(repeated.newWarnings.size() == 1);
}

TEST_CASE("CompareCliOptions exposes mode, validates paths, and writes TXT and JSON", "[stage13][cli]") {
    const auto oldPath = pathToUtf8(fixture("stage13-base.nc"));
    const auto newPath = pathToUtf8(fixture("stage13-shifted.nc"));
    const auto dir = std::filesystem::path(GCA_TEST_OUTPUT_DIR) / "stage13";
    std::filesystem::create_directories(dir);
    const auto txt = dir / pathFromUtf8("сравнение двух УП.txt");
    const auto json = dir / "comparison.json";
    std::ostringstream out, err;
    REQUIRE(runCli({"--list-options"}, out, err) == kExitOk);
    CHECK(test::contains(out.str(), "--compare\t"));
    CHECK(test::contains(out.str(), "--tolerance\t\t<мм>\t0.01"));
    CHECK(runCli({"--compare", oldPath, "--out"}, out, err) == kExitBadArguments);
    CHECK(runCli({"--compare", oldPath, newPath, "--tolerance", "-0.01"}, out, err) == kExitBadArguments);
    CHECK(runCli({oldPath, "--tolerance", "0.01"}, out, err) == kExitBadArguments);
    CHECK(runCli({"--compare", oldPath, newPath, "--out", oldPath}, out, err) == kExitBadArguments);
    CHECK(runCli({"--compare", oldPath, newPath, "--json", newPath}, out, err) == kExitBadArguments);
    CHECK(runCli({"--compare", oldPath, newPath, "--out", pathToUtf8(txt), "--json", pathToUtf8(txt)}, out, err) == kExitBadArguments);
    CHECK(runCli({"--compare", oldPath, pathToUtf8(dir / "missing.nc")}, out, err) == kExitIoError);
    REQUIRE(runCli({"--compare", oldPath, newPath, "--out", pathToUtf8(txt),
                    "--json", pathToUtf8(json), "--bom"}, out, err) == kExitOk);
    const auto written = test::readFile(txt);
    CHECK(written.starts_with("\xEF\xBB\xBF"));
    CHECK(test::contains(written, "изменена | 7 | 8"));
    const auto jsonText = test::readFile(json);
    CHECK(jsonText.starts_with("{"));
    CHECK(test::contains(jsonText, "\"kind\":\"comparison\""));
    CHECK(test::contains(jsonText, R"({"kind":"modified","oldLine":7,"newLine":8})"));
    out.str("");
    CHECK(runCli({"--compare", oldPath, newPath, "--tolerance", "0.101"}, out, err) == kExitOk);
    CHECK(test::contains(out.str(), "T1: совпадает"));
}
