#include "gca/knowledge.hpp"

#include "gca/lexer.hpp"

namespace gca {

std::optional<CodeInfo> BuiltinKnowledgeProvider::lookup(std::string_view dialectId,
                                                         std::string_view code) const {
    if (dialectId != profile_.dialect().id() || code.size() < 2) return std::nullopt;
    const char letter = code.front();
    const auto value = parseNumber(code.substr(1));
    if (!value || (letter != 'G' && letter != 'M')) return std::nullopt;
    return profile_.describe(letter, *value);
}

std::optional<CodeInfo> KnowledgeChain::lookup(std::string_view dialectId, std::string_view code) const {
    for (const auto& p : providers_) {
        if (auto r = p->lookup(dialectId, code)) return r;
    }
    return std::nullopt;
}

} // namespace gca
