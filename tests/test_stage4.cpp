#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace gca;
using namespace gca::test;

TEST_CASE("cheat-sheet has all six sections", "[cheatsheet][stage4]") {
    const Report r = analyze(sample1());
    REQUIRE(r.cheatSheet.size() == 6);
    CHECK(r.cheatSheet[0].title.rfind("1.", 0) == 0);
    CHECK(r.cheatSheet[5].title.rfind("6.", 0) == 0);
    for (const auto& s : r.cheatSheet) CHECK_FALSE(s.lines.empty());
}

TEST_CASE("sample 1 structure: header, O without number, % only at the end, T1M6 and preselect", "[cheatsheet][stage4][samples]") {
    const Report r = analyze(sample1());
    const std::string s = cheatSection(r, "1.");
    CHECK(contains(s, "Строка O без номера"));
    CHECK(contains(s, "Символ % встречается 1 раз (строка 4971)"));
    CHECK(contains(s, "последняя строка файла"));
    CHECK(contains(s, "( TOOL LIST: )"));
    CHECK(contains(s, "записей такого вида подряд: 11"));
    CHECK(contains(s, "T перед M6 (`T<t>M6`): 8"));
    CHECK_FALSE(contains(s, "M6 перед T"));
    CHECK(contains(s, "Предвыбор следующего инструмента: отдельный кадр `T<t>` после смены, 7 раз"));
}

TEST_CASE("sample 2 structure: M6T4 order", "[cheatsheet][stage4][samples]") {
    const Report r = analyze(sample2());
    const std::string s = cheatSection(r, "1.");
    CHECK(contains(s, "M6 перед T (`M6T<t>`): 7"));
    CHECK_FALSE(contains(s, "T перед M6"));
    CHECK(contains(s, "Предвыбор следующего инструмента: отдельный кадр `T<t>` после смены, 6 раз"));
}

TEST_CASE("tool change template is printed with substitutions", "[cheatsheet][stage4][samples]") {
    const std::string s1 = cheatSection(analyze(sample1()), "1.");
    CHECK(contains(s1, "-> [7/7] T<t>M6"));
    CHECK(contains(s1, "G43 Z<z> H<h> D<d> S<s> M3"));
    CHECK(contains(s1, "G90 G54 G94 G64 G17 X<x> Y<y>"));
    CHECK(contains(s1, "N<n>"));
    CHECK(contains(s1, "G91 G28 Z0."));
    CHECK(contains(s1, "(<описание инструмента>)"));
    CHECK(contains(s1, "(<комментарий следующей операции>)"));
    const std::string s2 = cheatSection(analyze(sample2()), "1.");
    CHECK(contains(s2, "-> [6/6] M6T<t>"));
    CHECK(contains(s2, "G05.1Q0"));
    CHECK(contains(s2, "G08P1"));
    CHECK(contains(s2, "G91 G28 X0"));
    CHECK(contains(s2, "G43 Z<z> H<h> D<d> S<s> M3"));
}

TEST_CASE("program end template", "[cheatsheet][stage4][samples]") {
    const std::string s = cheatSection(analyze(sample1()), "1.");
    CHECK(contains(s, "Конец программы"));
    CHECK(contains(s, "G90 G49 H0"));
    CHECK(contains(s, "M30"));
}

TEST_CASE("N numbering: only at the start of operations, separate block, 8 times", "[cheatsheet][stage4][samples]") {
    const std::string s1 = cheatSection(analyze(sample1()), "2.");
    CHECK(contains(s1, "Нумерация N: есть в 8 из"));
    CHECK(contains(s1, "Всегда отдельным кадром"));
    CHECK(contains(s1, "N нумерует операции, а не кадры подряд"));
    CHECK(contains(s1, "Шпиндель остановлен (M5) перед N: 8 из 8"));
    CHECK(contains(s1, "равен номеру инструмента следующей операции в 8 из 8"));
    const std::string s2 = cheatSection(analyze(sample2()), "2.");
    CHECK(contains(s2, "Нумерация N: есть в 8 из"));
    CHECK(contains(s2, "в 7 из 8"));
    CHECK(contains(s2, "N130"));          // the exception from SPEC
}

TEST_CASE("number format: dot after integers, 3 decimals, no trailing zeros, no R in arcs", "[cheatsheet][stage4][samples]") {
    for (const auto& text : {sample1(), sample2()}) {
        const std::string s = cheatSection(analyze(text), "2.");
        CHECK(contains(s, "знаков после запятой до 3"));
        CHECK(contains(s, "(`Z32.`)") != contains(s, "(`Z-1.`)"));        // an example of Z written with a point
        CHECK(contains(s, "Нули справа в дробной части: не пишутся"));
        CHECK(contains(s, "Ноль перед точкой у чисел меньше 1: пишется"));
        CHECK(contains(s, "R не используется"));
        CHECK(contains(s, "Центр задан смещениями I/J/K"));
        CHECK(contains(s, "полных окружностей"));
        CHECK(contains(s, "Нулевое значение оси"));
        CHECK(contains(s, "запись нуля непоследовательна"));
        CHECK(contains(s, "CRLF"));
        CHECK(contains(s, "только верхний"));
    }
}

TEST_CASE("glued words are listed", "[cheatsheet][stage4][samples]") {
    const std::string s1 = cheatSection(analyze(sample1()), "2.");
    CHECK(contains(s1, "T+M: 8"));
    CHECK(contains(s1, "`G05.1Q0`"));
    const std::string s2 = cheatSection(analyze(sample2()), "2.");
    CHECK(contains(s2, "M+T: 7"));
    CHECK(contains(s2, "`M6T4`"));
}

TEST_CASE("leading zeros: G05.1 and G08 with zero, G1 without", "[cheatsheet][stage4][samples]") {
    const std::string s = cheatSection(analyze(sample1()), "2.");
    CHECK(contains(s, "G: без нуля - G0 G1 G2 G3; с нулём - G05.1 G08"));
}

TEST_CASE("modality: G1 and F omitted, only changed coordinates written", "[cheatsheet][stage4][samples]") {
    const std::string s = cheatSection(analyze(sample1()), "3.");
    CHECK(contains(s, "Код движения (G0/G1/G2/G3): опущен"));
    CHECK(contains(s, "Вывод: неизменившиеся координаты опускаются"));
    CHECK(contains(s, "опущена 4060"));
    CHECK(contains(s, "G17"));
}

TEST_CASE("comments: ( ) style, ASCII, upper case, kinds", "[cheatsheet][stage4][samples]") {
    const std::string s = cheatSection(analyze(sample2()), "4.");
    CHECK(contains(s, "в скобках `( )`:"));
    CHECK(contains(s, "после `;`: 0"));
    CHECK(contains(s, "Только ASCII"));
    CHECK(contains(s, "Виды комментариев"));
}

TEST_CASE("axis B is absent in the samples", "[cheatsheet][stage4][samples][axisB]") {
    for (const auto& text : {sample1(), sample2()}) {
        CHECK(contains(cheatSection(analyze(text), "5."), "Ось B в программе не используется"));
    }
}

TEST_CASE("machine specific codes: place in the structure is derived", "[cheatsheet][stage4][samples]") {
    const std::string s1 = cheatSection(analyze(sample1()), "6.");
    CHECK(contains(s1, "G05.1: 2 раз"));
    CHECK(contains(s1, "G08: 2 раз"));
    CHECK(contains(s1, "M15: 29 раз"));
    CHECK(contains(s1, "дополнительная подача СОЖ"));
    CHECK(contains(s1, "после смены, до первого рабочего хода"));
    CHECK_FALSE(contains(s1, "M53"));
    const std::string s2 = cheatSection(analyze(sample2()), "6.");
    CHECK(contains(s2, "G05.1: 16 раз"));
    CHECK(contains(s2, "G08: 16 раз"));
    CHECK(contains(s2, "M53: 1 раз"));
    CHECK(contains(s2, "через шпиндель"));
}

// ---- synthetic programs in a completely different style: nothing may be hardcoded ----

TEST_CASE("synthetic LF program with numbered blocks, G01, ;comments, spaces", "[cheatsheet][stage4][style]") {
    const std::string text =
        "%\n"
        "O1001\n"
        "N10 G90 G54 G00 X+10.5 Y.5\n"
        "N20 T01 M06 ; tool one\n"
        "N30 G43 H01 Z50.\n"
        "N40 S1200 M03\n"
        "N50 G01 X20.250 Y10. F300.5\n"
        "N60 M05\n"
        "N70 M30\n"
        "%\n";
    const Report r = analyze(text);
    const std::string s1 = cheatSection(r, "1.");
    const std::string s2 = cheatSection(r, "2.");
    CHECK(contains(s1, "Строка O с номером (O1001)"));
    CHECK(contains(s1, "Символ % встречается 2 раз"));
    CHECK(contains(s2, "N нумерует кадры подряд"));
    CHECK(contains(s2, "Шаг нумерации: чаще всего 10"));
    CHECK(contains(s2, "LF 10"));
    CHECK(contains(s2, "CRLF 0"));
    CHECK(contains(s2, "Нули справа в дробной части: пишутся"));
    CHECK(contains(s2, "не пишется в 1 случаях (`.5`)"));
    CHECK(contains(s2, "Знак `+` у положительных чисел: пишется"));
    CHECK(contains(s2, "с нулём - G00 G01"));
    CHECK(contains(s2, "M: без нуля - нет; с нулём - M03 M05 M06"));
    CHECK(contains(s2, "Слова в кадре всегда разделены пробелом"));
    CHECK(contains(cheatSection(r, "4."), "после `;`: 1"));
}

TEST_CASE("synthetic lower case program", "[cheatsheet][stage4][style]") {
    const Report r = analyze("g90 g54 g0 x1. y2.\nt1 m6\ng1 x5. f100\nm30\n");
    CHECK(contains(cheatSection(r, "2."), "есть нижний регистр"));
}

TEST_CASE("synthetic program with axis B: how B is programmed and which M codes surround it", "[cheatsheet][stage4][axisB]") {
    const std::string text =
        "O\n"
        "G90 G54 G0 X0 Y0 Z50.\n"
        "T1M6\n"
        "S1000 M3\n"
        "M11\n"
        "G0 B90.\n"
        "M10\n"
        "G1 X10. F100.\n"
        "G0 Z50.\n"
        "M11\n"
        "G0 B180.\n"
        "M10\n"
        "G1 X20.\n"
        "G0 X0 Y0 B0.\n"
        "M5\nM30\n";
    const Report r = analyze(text);
    const std::string b = cheatSection(r, "5.");
    CHECK_FALSE(contains(b, "не используется"));
    CHECK(contains(b, "Кадров с B: 3"));
    CHECK(contains(b, "различных значений угла: 3"));
    CHECK(contains(b, "диапазон 0 .. 180"));
    CHECK(contains(b, "G00 B"));                       // separate block with G0
    CHECK(contains(b, "G00 X Y B"));                   // together with the XY move
    CHECK(contains(b, "M-коды в кадре перед поворотом"));
    CHECK(contains(b, "M11 x2"));
    CHECK(contains(b, "M-коды в кадре после поворота"));
    CHECK(contains(b, "M10 x2"));
    // M10/M11 are not in the Fanuc table: they show up as unknown codes in sections 5 and 6
    CHECK(contains(b, "M11 x2 - ?"));
    CHECK(contains(cheatSection(r, "6."), "M10: 2 раз"));
    CHECK(contains(cheatSection(r, "6."), "?"));
    // B range reaches the report
    CHECK(r.box.axis[kAxisB].max == 180.0);
}

TEST_CASE("cheat-sheet on a program without tool changes does not crash", "[cheatsheet][stage4]") {
    const Report r = analyze("O\nG0 X1.\nG1 X2. F100.\nM30\n");
    REQUIRE(r.cheatSheet.size() == 6);
    CHECK(contains(cheatSection(r, "1."), "Смены инструмента (M6) в программе нет"));
    const Report e = analyze("");
    CHECK(e.cheatSheet.size() <= 6);
}

TEST_CASE("cheat-sheet on an empty program and a program of comments only", "[cheatsheet][stage4]") {
    CHECK_NOTHROW(analyze("\n\n"));
    CHECK_NOTHROW(analyze("(only a comment)\n"));
    CHECK_NOTHROW(analyze("%\n%\n"));
}
