#pragma once

#include "gca/analyzers/analyzer.hpp"
#include "gca/knowledge.hpp"

#include <map>

namespace gca {

/// Counts G/M words and describes them through the knowledge chain (unknown -> '?').
class CodeStatsAnalyzer final : public IAnalyzer {
public:
    CodeStatsAnalyzer(const KnowledgeChain& knowledge, std::string dialectId)
        : knowledge_(knowledge), dialectId_(std::move(dialectId)) {}
    void onBlock(const BlockContext& ctx) override;
    void finish(Report& report) override;

private:
    struct Entry { double value = 0.0; std::size_t count = 0; std::size_t firstLine = 0; };
    const KnowledgeChain& knowledge_;
    std::string dialectId_;
    std::map<std::string, Entry> g_;
    std::map<std::string, Entry> m_;
};

} // namespace gca
