#include "gca/lexer.hpp"

#include <array>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <cmath>

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
        if (w.letter == letter && w.address.empty()) return &w;
    }
    return nullptr;
}

const Word* Block::findAddress(std::string_view address) const {
    for (const auto& w : words) {
        if ((!w.address.empty() && w.address == address) ||
            (w.address.empty() && address.size() == 1 && w.letter == address.front())) return &w;
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
    // Leading fractional zeros do not consume significant digits: scale can exceed 18.
    // Keep the historical exact decimal path for ordinary NC coordinates.
    double v = scale < kPow10.size() ? static_cast<double>(mant) / kPow10[scale] :
        static_cast<double>(mant) * std::pow(10.0, -static_cast<double>(scale));
    return neg ? -v : v;
}

Block lexLine(std::string_view line, std::size_t lineNo, std::vector<Diagnostic>* diagnostics, LexRules rules) {
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
        if (rules.extended && c == '@') {
            Comment cm;
            cm.style = '@'; cm.pos = i; cm.raw = std::string(line.substr(i));
            auto text = line.substr(i + 1);
            if (text.starts_with('(') && text.ends_with(')')) text = text.substr(1, text.size() - 2);
            cm.text = std::string(text); b.comments.push_back(std::move(cm));
            break;
        }
        if (rules.extended && c == '"') {
            const auto end = line.find('"', i + 1);
            if (end == std::string_view::npos) { report(diagnostics, lineNo, "unterminated string"); break; }
            // G211 "C" selects the turning spindle axis; it is not an axis word.
            b.callName = std::string(line.substr(i + 1, end - i - 1));
            i = end + 1; continue;
        }
        if (rules.extended && (c == '#' || c == '[')) {
            b.control = true; b.command = "EXPRESSION";
            const auto at = line.find('@', i);
            i = at == std::string_view::npos ? n : at; continue;
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
            if (rules.extended) {
                std::size_t end = i;
                while (end < n && (isAlpha(line[end]) || line[end] == '_')) ++end;
                std::string key(line.substr(i, end - i));
                for (auto& ch : key) ch = toUpper(ch);
                if (key == "GDEF") {
                    std::size_t j = end; while (j < n && isDigit(line[j])) ++j;
                    const auto code = parseNumber(line.substr(end, j - end));
                    if (!code || *code > 99999) report(diagnostics, lineNo, "invalid GDEF number");
                    else b.definition = static_cast<int>(*code);
                    i = j; continue;
                }
                if (key == "END" || key == "GCALL") {
                    if (key == "END") b.definitionEnd = true; else b.cycleCall = true;
                    i = end; continue;
                }
                // A..Z and AA..AZ are the documented GDEF parameter addresses.
                if (key.size() > 1 && !(key.size() == 2 && key.front() == 'A')) {
                    b.control = true; b.command = key;
                    const auto at = line.find('@', end);
                    auto arg = line.substr(end, at == std::string_view::npos ? n - end : at - end);
                    const auto first = arg.find_first_not_of(" \t");
                    if (first != std::string_view::npos) arg.remove_prefix(first);
                    if (key == "GOTO") {
                        const auto value = parseNumber(arg.substr(0, arg.find_first_of(" \t")));
                        if (value && *value >= 0 && *value <= std::numeric_limits<long>::max()) b.jumpTarget = static_cast<long>(*value);
                    } else if (key == "CALL") {
                        b.callName = std::string(arg.substr(0, arg.find_first_of(" \t")));
                        if (b.callName.size() >= 2 && b.callName.front() == '"' && b.callName.back() == '"')
                            b.callName = b.callName.substr(1, b.callName.size() - 2);
                    }
                    i = at == std::string_view::npos ? n : at; continue;
                }
            }
            Word w;
            w.letter = toUpper(c);
            w.lowercase = (c != w.letter);
            w.pos = i;
            w.precededBySpace = prevSpace;
            std::size_t valueStart = i + 1;
            if (rules.extended && w.letter == 'A' && valueStart < n && isAlpha(line[valueStart])) {
                w.address = "A"; w.address += toUpper(line[valueStart++]);
            }
            if (rules.extended && w.letter == 'T' && valueStart < n && line[valueStart] == '"') {
                const auto end = line.find('"', valueStart + 1);
                if (end == std::string_view::npos) { report(diagnostics, lineNo, "unterminated tool name"); break; }
                w.stringValue = std::string(line.substr(valueStart + 1, end - valueStart - 1));
                if (w.stringValue.empty()) report(diagnostics, lineNo, "empty tool name");
                w.rawText = std::string(line.substr(i, end + 1 - i));
                b.words.push_back(std::move(w)); i = end + 1; prevSpace = false; continue;
            }
            if (rules.extended && w.letter == 'S' && valueStart < n && line[valueStart] == '(') {
                const auto close = line.find(')', valueStart + 1);
                const auto channel = close == std::string_view::npos ? std::nullopt : parseNumber(line.substr(valueStart + 1, close - valueStart - 1));
                if (!channel || *channel < 1 || *channel > 16) { report(diagnostics, lineNo, "invalid spindle selector"); break; }
                w.channel = static_cast<int>(*channel); valueStart = close + 1;
                while (valueStart < n && isSpace(line[valueStart])) ++valueStart;
            }
            std::size_t j = valueStart;
            if (rules.extended && j < n && (line[j] == '#' || line[j] == '[')) {
                w.expression = true;
                int depth = 0;
                do {
                    if (line[j] == '[') ++depth;
                    if (line[j] == ']') --depth;
                    ++j;
                } while (j < n && (depth > 0 || isDigit(line[j]) || line[j] == '#' || line[j] == '.'));
                w.rawText = std::string(line.substr(i, j - i));
                b.words.push_back(std::move(w)); i = j; continue;
            }
            if (j < n && (line[j] == '+' || line[j] == '-')) ++j;
            while (j < n && (isDigit(line[j]) || line[j] == '.')) ++j;
            w.rawText = std::string(line.substr(i, j - i));
            const auto num = parseNumber(line.substr(valueStart, j - valueStart));
            if (num) {
                w.value = *num;
                w.hasValue = true;
            } else if (j > valueStart) {
                report(diagnostics, lineNo, "bad number in word '" + w.rawText + "'");
            } else if (w.letter != 'O' && !(rules.extended && w.channel > 0)) {
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

Program parseProgram(std::string_view text, LexRules rules) {
    Program p;
    p.rules = rules;
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
        p.blocks.push_back(lexLine(line, lineNo, &p.diagnostics, rules));
    }
    if (rules.extended) {
        // Allocate synthetic keys above every numeric T; preserve the original name in reports.
        int next = 0;
        for (const auto& b : p.blocks) for (const auto& w : b.words)
            if (w.letter == 'T' && w.hasValue && w.value >= 0 && w.value < 1000000) next = std::max(next, static_cast<int>(w.value));
        std::map<std::string, int> names;
        for (auto& b : p.blocks) {
            for (auto& w : b.words) if (!w.stringValue.empty()) {
                const auto [it, inserted] = names.emplace(w.stringValue, next + 1);
                if (inserted) { ++next; p.toolNames[next] = w.stringValue; }
                w.hasValue = true; w.value = it->second;
            }
            int channel = 0;
            for (const auto& w : b.words) if (w.letter == 'S' && w.channel > 0) channel = w.channel;
            for (auto& w : b.words) if (w.letter == 'M') w.channel = channel;
        }
    }
    return p;
}

} // namespace gca
