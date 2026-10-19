#include "gca/analyzers/code_stats.hpp"

#include <algorithm>

namespace gca {

void CodeStatsAnalyzer::onBlock(const BlockContext& ctx) {
    for (const auto& w : ctx.block.words) {
        if ((w.letter != 'G' && w.letter != 'M') || !w.hasValue) continue;
        auto& map = (w.letter == 'G') ? g_ : m_;
        auto& e = map[canonicalCode(w.letter, w.value)];
        if (e.count == 0) {
            e.value = w.value;
            e.firstLine = ctx.block.lineNo;
        }
        ++e.count;
    }
}

void CodeStatsAnalyzer::finish(Report& report) {
    const auto convert = [this](const std::map<std::string, Entry>& src, std::vector<CodeStat>& dst) {
        for (const auto& [key, e] : src) {
            CodeStat s;
            s.code = key;
            s.count = e.count;
            s.firstLine = e.firstLine;
            if (const auto info = knowledge_.lookup(dialectId_, key)) {
                s.known = true;
                s.description = info->description;
                s.machineSpecific = info->machineSpecific;
                s.source = info->source;
            }
            dst.push_back(std::move(s));
        }
        std::sort(dst.begin(), dst.end(), [&src](const CodeStat& a, const CodeStat& b) {
            return src.at(a.code).value < src.at(b.code).value;
        });
    };
    convert(g_, report.gCodes);
    convert(m_, report.mCodes);
}

} // namespace gca
