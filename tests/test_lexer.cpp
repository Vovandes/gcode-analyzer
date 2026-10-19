#include "gca/lexer.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gca;
using Catch::Approx;

TEST_CASE("parseNumber is locale independent and exact for 3 decimals", "[lexer]") {
    CHECK(parseNumber("32.") == Approx(32.0));
    CHECK(parseNumber("-4.") == Approx(-4.0));
    CHECK(parseNumber("0.5") == Approx(0.5));
    CHECK(parseNumber(".25") == Approx(0.25));
    CHECK(parseNumber("-12.411") == -12.411);
    CHECK(parseNumber("5305.165") == 5305.165);
    CHECK(parseNumber("+7") == Approx(7.0));
    CHECK_FALSE(parseNumber("").has_value());
    CHECK_FALSE(parseNumber("-").has_value());
    CHECK_FALSE(parseNumber("1.2.3").has_value());
    CHECK_FALSE(parseNumber("12a").has_value());
}

TEST_CASE("glued words are split", "[lexer]") {
    SECTION("G05.1Q0") {
        const Block b = lexLine("G05.1Q0", 1);
        REQUIRE(b.words.size() == 2);
        CHECK(b.words[0].letter == 'G');
        CHECK(b.words[0].value == Approx(5.1));
        CHECK(b.words[0].rawText == "G05.1");
        CHECK(b.words[1].letter == 'Q');
        CHECK(b.words[1].rawText == "Q0");
        CHECK_FALSE(b.words[1].precededBySpace);
    }
    SECTION("T1M6 and M6T4") {
        const Block a = lexLine("T1M6", 1);
        REQUIRE(a.words.size() == 2);
        CHECK(a.words[0].rawText == "T1");
        CHECK(a.words[1].rawText == "M6");
        const Block b = lexLine("M6T4", 2);
        REQUIRE(b.words.size() == 2);
        CHECK(b.words[0].rawText == "M6");
        CHECK(b.words[1].rawText == "T4");
    }
    SECTION("G08P1") {
        const Block b = lexLine("G08P1", 1);
        REQUIRE(b.words.size() == 2);
        CHECK(b.words[0].rawText == "G08");
        CHECK(b.words[1].value == Approx(1.0));
    }
}

TEST_CASE("rawText keeps the original spelling", "[lexer]") {
    const Block b = lexLine("G43 Z32. H1 D1 S4200 M3", 5);
    REQUIRE(b.words.size() == 6);
    CHECK(b.words[1].rawText == "Z32.");
    CHECK(b.words[1].value == Approx(32.0));
    CHECK(b.words[2].rawText == "H1");
    const Block c = lexLine("G91 G28 Z0", 6);
    CHECK(c.words[2].rawText == "Z0");
    const Block d = lexLine("X-4. Y0.5 I0. J-1.338", 7);
    REQUIRE(d.words.size() == 4);
    CHECK(d.words[0].value == Approx(-4.0));
    CHECK(d.words[1].rawText == "Y0.5");
    CHECK(d.words[2].rawText == "I0.");
    CHECK(d.words[3].value == -1.338);
    CHECK(d.words[3].pos == 14);
}

TEST_CASE("negative and fractional values, S with fraction", "[lexer]") {
    const Block b = lexLine("S5305.165 M3", 1);
    REQUIRE(b.words.size() == 2);
    CHECK(b.words[0].value == 5305.165);
}

TEST_CASE("comments in parentheses and after semicolon", "[lexer]") {
    SECTION("whole-line comment with padding") {
        const Block b = lexLine("( T1: TOROIDALNAYA FREZA D63 L80 )", 1);
        CHECK(b.words.empty());
        REQUIRE(b.comments.size() == 1);
        CHECK(b.comments[0].text == " T1: TOROIDALNAYA FREZA D63 L80 ");
        CHECK(b.comments[0].style == '(');
    }
    SECTION("comment does not produce words") {
        const Block b = lexLine("G1 X10. (G2 Y5.) Y20.", 1);
        REQUIRE(b.words.size() == 3);
        CHECK(b.comments.size() == 1);
    }
    SECTION("semicolon comment") {
        const Block b = lexLine("G0 X1. ; move", 1);
        REQUIRE(b.words.size() == 2);
        REQUIRE(b.comments.size() == 1);
        CHECK(b.comments[0].style == ';');
        CHECK(b.comments[0].text == " move");
    }
    SECTION("unterminated comment is a diagnostic, not an exception") {
        std::vector<Diagnostic> d;
        const Block b = lexLine("G1 (oops", 3, &d);
        CHECK(b.comments.size() == 1);
        REQUIRE(d.size() == 1);
        CHECK(d[0].lineNo == 3);
    }
}

TEST_CASE("N, O, percent, block delete, blank lines", "[lexer]") {
    SECTION("N word") {
        const Block b = lexLine("N130", 1);
        REQUIRE(b.n.has_value());
        CHECK(*b.n == 130);
        CHECK(b.words.size() == 1);
    }
    SECTION("O without number") {
        std::vector<Diagnostic> d;
        const Block b = lexLine("O", 1, &d);
        REQUIRE(b.words.size() == 1);
        CHECK(b.words[0].letter == 'O');
        CHECK_FALSE(b.words[0].hasValue);
        CHECK(d.empty());
    }
    SECTION("O with number") {
        const Block b = lexLine("O1234", 1);
        CHECK(b.words[0].value == Approx(1234.0));
    }
    SECTION("percent") {
        CHECK(lexLine("%", 1).isPercent);
        CHECK(lexLine(" % ", 1).isPercent);
    }
    SECTION("block delete") {
        const Block b = lexLine("/G1 X5.", 1);
        CHECK(b.blockDelete);
        CHECK(b.words.size() == 2);
    }
    SECTION("blank and trailing whitespace") {
        CHECK(lexLine("", 1).isBlank());
        CHECK(lexLine("   ", 1).isBlank());
        CHECK(lexLine("M5 ", 1).trailingWhitespace);
        CHECK_FALSE(lexLine("M5", 1).trailingWhitespace);
    }
    SECTION("lower case letters are accepted") {
        const Block b = lexLine("g1 x10.", 1);
        REQUIRE(b.words.size() == 2);
        CHECK(b.words[0].letter == 'G');
        CHECK(b.words[0].lowercase);
        CHECK(b.words[0].rawText == "g1");
    }
}

TEST_CASE("garbage is reported through diagnostics", "[lexer]") {
    std::vector<Diagnostic> d;
    const Block b = lexLine("G1 #5=3 X1.", 9, &d);
    CHECK(b.words.size() >= 2);
    CHECK_FALSE(d.empty());
    CHECK(d[0].lineNo == 9);
    std::vector<Diagnostic> d2;
    (void)lexLine("X", 1, &d2);
    CHECK(d2.size() == 1);
}

TEST_CASE("CRLF and LF are both supported", "[lexer]") {
    const Program crlf = parseProgram("O\r\nG1 X1.\r\n%\r\n");
    CHECK(crlf.blocks.size() == 3);
    CHECK(crlf.crlfLines == 3);
    CHECK(crlf.lfLines == 0);
    CHECK(crlf.blocks[1].raw == "G1 X1.");
    CHECK(crlf.blocks[2].isPercent);

    const Program lf = parseProgram("O\nG1 X1.\n%\n");
    CHECK(lf.blocks.size() == 3);
    CHECK(lf.lfLines == 3);
    CHECK(lf.crlfLines == 0);

    const Program mixed = parseProgram("G0 X1.\r\nG1 X2.\nM30");
    CHECK(mixed.blocks.size() == 3);
    CHECK(mixed.crlfLines == 1);
    CHECK(mixed.lfLines == 1);
    CHECK(mixed.noEolLines == 1);
    CHECK(mixed.blocks[1].lineNo == 2);
}

TEST_CASE("both samples are tokenized without diagnostics", "[lexer][samples]") {
    for (const auto& text : {test::sample1(), test::sample2()}) {
        const Program p = parseProgram(text);
        CHECK(p.diagnostics.empty());
        CHECK(p.lfLines == 0);
        CHECK(p.crlfLines == p.blocks.size());
    }
    CHECK(parseProgram(test::sample1()).blocks.size() == 4971);
    CHECK(parseProgram(test::sample2()).blocks.size() == 3626);
}
