#include "gca/lexer.hpp"

#include <array>
#include <cstdint>

namespace gca {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
char toUpper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

void report(std::vector<Diagnostic>* d, std::size_t line, std::string msg) {
    if (d != nullptr) d->push_back({line, std::move(msg)});
}

} // namespace

const Word* Block::find(char letter) const {
    for (const auto& w : words) {
        if (w.letter == letter) return &w;
    }
    return nullptr;
}

std::optional<double> parseNumber(std::string_view s) {
    static constexpr std::array<double, 19> kPow10 = {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18};
    std::size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    std::uint64_t mant = 0;
    int significant = 0;
    std::size_t scale = 0;
    bool seenDot = false;
    bool anyDigit = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            if (seenDot) return std::nullopt;
            seenDot = true;
            continue;
        }
        if (!isDigit(c)) return std::nullopt;
        anyDigit = true;
        if (significant < 18) {
            mant = mant * 10 + static_cast<std::uint64_t>(c - '0');
            if (mant != 0) ++significant;
            if (seenDot) ++scale;
        } else if (!seenDot) {
            return std::nullopt; // integer part too long
        }
        // extra fractional digits beyond precision are dropped
    }
    if (!anyDigit) return std::nullopt;
    double v = static_cast<double>(mant) / kPow10[scale];
    return neg ? -v : v;
}

Block lexLine(std::string_view line, std::size_t lineNo, std::vector<Diagnostic>* diagnostics) {
    Block b;
    b.lineNo = lineNo;
    b.raw = std::string(line);
    if (!line.empty() && (line.back() == ' ' || line.back() == '\t')) b.trailingWhitespace = true;

    const std::size_t n = line.size();
    std::size_t i = 0;
    bool prevSpace = true;
    bool badRun = false;

    // block delete
    {
        std::size_t k = 0;
        while (k < n && isSpace(line[k])) ++k;
        if (k < n && line[k] == '/') {
            b.blockDelete = true;
            i = k + 1;
            if (i < n && isDigit(line[i])) ++i; // optional skip level
        }
    }

    while (i < n) {
        const char c = line[i];
        if (isSpace(c)) {
            prevSpace = true;
            badRun = false;
            ++i;
            continue;
        }
        if (c == '(') {
            int depth = 0;
            std::size_t j = i;
            bool closed = false;
            for (; j < n; ++j) {
                if (line[j] == '(') ++depth;
                else if (line[j] == ')') {
                    --depth;
                    if (depth == 0) { closed = true; break; }
                }
            }
            Comment cm;
            cm.style = '(';
            cm.pos = i;
            if (closed) {
                cm.raw = std::string(line.substr(i, j - i + 1));
                cm.text = std::string(line.substr(i + 1, j - i - 1));
                i = j + 1;
            } else {
                report(diagnostics, lineNo, "unterminated comment");
                cm.raw = std::string(line.substr(i));
                cm.text = std::string(line.substr(i + 1));
                i = n;
            }
            b.comments.push_back(std::move(cm));
            prevSpace = true;
            badRun = false;
            continue;
        }
        if (c == ';') {
            Comment cm;
            cm.style = ';';
            cm.pos = i;
            cm.raw = std::string(line.substr(i));
            cm.text = std::string(line.substr(i + 1));
            b.comments.push_back(std::move(cm));
            i = n;
            break;
        }
        if (c == '%') {
            bool alone = true;
            for (std::size_t k = 0; k < n; ++k) {
                if (k != i && !isSpace(line[k])) { alone = false; break; }
            }
            if (alone) b.isPercent = true;
            else report(diagnostics, lineNo, "'%' inside a block");
            ++i;
            continue;
        }
        if (isAlpha(c)) {
            Word w;
            w.letter = toUpper(c);
            w.lowercase = (c != w.letter);
            w.pos = i;
            w.precededBySpace = prevSpace;
            std::size_t j = i + 1;
            if (j < n && (line[j] == '+' || line[j] == '-')) ++j;
            while (j < n && (isDigit(line[j]) || line[j] == '.')) ++j;
            w.rawText = std::string(line.substr(i, j - i));
            const auto num = parseNumber(line.substr(i + 1, j - i - 1));
            if (num) {
                w.value = *num;
                w.hasValue = true;
            } else if (j > i + 1) {
                report(diagnostics, lineNo, "bad number in word '" + w.rawText + "'");
            } else if (w.letter != 'O') {
                report(diagnostics, lineNo, "address '" + w.rawText + "' without value");
            }
            if (w.letter == 'N' && w.hasValue && !b.n) b.n = static_cast<long>(w.value);
            b.words.push_back(std::move(w));
            i = j;
            prevSpace = false;
            badRun = false;
            continue;
        }
        if (!badRun) {
            report(diagnostics, lineNo, std::string("unexpected character '") + c + "'");
            badRun = true;
        }
        ++i;
        prevSpace = false;
    }
    return b;
}

Program parseProgram(std::string_view text) {
    Program p;
    std::size_t start = 0;
    std::size_t lineNo = 0;
    while (start < text.size()) {
        const std::size_t nl = text.find('\n', start);
        std::string_view line;
        if (nl == std::string_view::npos) {
            line = text.substr(start);
            start = text.size();
            ++p.noEolLines;
        } else {
            line = text.substr(start, nl - start);
            start = nl + 1;
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
                ++p.crlfLines;
            } else {
                ++p.lfLines;
            }
        }
        ++lineNo;
        p.blocks.push_back(lexLine(line, lineNo, &p.diagnostics));
    }
    return p;
}

} // namespace gca
