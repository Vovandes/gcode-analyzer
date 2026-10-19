// Lexer: text of a program -> blocks of words and comments.
// Errors never throw: they are collected in Program::diagnostics.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gca {

/// One address word, e.g. G05.1, X-4., T1. Letter is stored upper-case.
struct Word {
    char letter = '\0';
    double value = 0.0;
    bool hasValue = false;
    std::string rawText;            ///< exactly as written, including the letter
    std::size_t pos = 0;            ///< offset of rawText in Block::raw
    bool precededBySpace = true;    ///< whitespace (or line start) before the word
    bool lowercase = false;         ///< letter was written in lower case

    /// Number part of rawText (without the letter).
    [[nodiscard]] std::string_view rawNumber() const {
        return std::string_view(rawText).substr(1);
    }
};

struct Comment {
    std::string text;   ///< inner text without delimiters
    std::string raw;    ///< with delimiters
    char style = '(';   ///< '(' or ';'
    std::size_t pos = 0;
};

/// One source line. N is kept in `words` too (as the first word it appears at)
/// and is mirrored in `n` for convenience.
struct Block {
    std::size_t lineNo = 0;         ///< 1-based
    std::string raw;                ///< line without end-of-line characters
    std::optional<long> n;
    std::vector<Word> words;
    std::vector<Comment> comments;
    bool blockDelete = false;       ///< leading '/'
    bool isPercent = false;         ///< line is just '%'
    bool trailingWhitespace = false;

    [[nodiscard]] bool isBlank() const {
        return words.empty() && comments.empty() && !isPercent;
    }
    /// First word with the given letter, or nullptr.
    [[nodiscard]] const Word* find(char letter) const;
    [[nodiscard]] bool has(char letter) const { return find(letter) != nullptr; }
};

struct Diagnostic {
    std::size_t lineNo = 0;
    std::string message;
};

struct Program {
    std::vector<Block> blocks;          ///< one per source line, blank lines included
    std::vector<Diagnostic> diagnostics;
    std::size_t crlfLines = 0;
    std::size_t lfLines = 0;
    std::size_t noEolLines = 0;         ///< last line without end-of-line
};

/// Locale-independent number parser: [+-]digits[.digits] | [+-].digits | [+-]digits.
[[nodiscard]] std::optional<double> parseNumber(std::string_view text);

/// Tokenize one line.
[[nodiscard]] Block lexLine(std::string_view line, std::size_t lineNo,
                            std::vector<Diagnostic>* diagnostics = nullptr);

/// Split text into lines (CRLF or LF) and tokenize every line.
[[nodiscard]] Program parseProgram(std::string_view text);

} // namespace gca
