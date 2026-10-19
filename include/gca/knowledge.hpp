// Extension point: where the meaning of a G/M code comes from.
// Chain: built-in table of the profile -> (future) local cache -> (future) online search.
#pragma once

#include "gca/dialect.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace gca {

class ICodeKnowledgeProvider {
public:
    virtual ~ICodeKnowledgeProvider() = default;
    /// `code` is a canonical key ("G54.1", "M6").
    [[nodiscard]] virtual std::optional<CodeInfo> lookup(std::string_view dialectId,
                                                         std::string_view code) const = 0;
};

/// Built-in tables of the active profile (dialect + machine overlay).
class BuiltinKnowledgeProvider final : public ICodeKnowledgeProvider {
public:
    explicit BuiltinKnowledgeProvider(const Profile& profile) : profile_(profile) {}
    [[nodiscard]] std::optional<CodeInfo> lookup(std::string_view dialectId,
                                                 std::string_view code) const override;
private:
    Profile profile_;
};

/// Placeholder for the future online search. Intentionally not implemented: always empty.
class OnlineKnowledgeProviderStub final : public ICodeKnowledgeProvider {
public:
    [[nodiscard]] std::optional<CodeInfo> lookup(std::string_view, std::string_view) const override {
        return std::nullopt;
    }
};

/// Asks providers in order, returns the first answer.
class KnowledgeChain final : public ICodeKnowledgeProvider {
public:
    void add(std::unique_ptr<ICodeKnowledgeProvider> provider) { providers_.push_back(std::move(provider)); }
    [[nodiscard]] std::optional<CodeInfo> lookup(std::string_view dialectId,
                                                 std::string_view code) const override;
private:
    std::vector<std::unique_ptr<ICodeKnowledgeProvider>> providers_;
};

} // namespace gca
