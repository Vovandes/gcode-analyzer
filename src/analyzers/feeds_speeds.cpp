#include "gca/analyzers/feeds_speeds.hpp"

#include <cmath>

namespace gca {

FeedsSpeedsAnalyzer::Acc& FeedsSpeedsAnalyzer::acc(int tool) {
    if (perTool_.find(tool) == perTool_.end()) toolOrder_.push_back(tool);
    return perTool_[tool];
}

void FeedsSpeedsAnalyzer::onBlock(const BlockContext& ctx) {
    if (ctx.effect.dataBlock || ctx.block.control) return;
    for (const auto& w : ctx.block.words) {
        if (!w.hasValue || !w.address.empty()) continue;
        if (w.letter == 'F' && ctx.profile.dialect().feedDwellSeconds() && !ctx.effect.segments.empty() && ctx.effect.segments.front().kind == SegmentKind::Dwell) continue;
        if (w.letter == 'F') {
            if (ctx.after.feedMode == FeedMode::Unsupported) continue;
            Acc& a = acc(ctx.after.activeTool);
            (ctx.after.feedMode == FeedMode::PerRev ? a.feedPerRev : a.feedPerMin).add(w.value);
        } else if (w.letter == 'S') {
            const auto spindle = ctx.after.spindles.find(w.channel > 0 ? w.channel : ctx.after.spindleChannel);
            if (spindle != ctx.after.spindles.end() && spindle->second.css) continue;
            Acc& a = acc(ctx.after.activeTool);
            a.spindle.add(w.value);
            if (std::fabs(w.value - std::round(w.value)) > 1e-9) a.fractionalS = true;
        } else if (w.letter == 'M' && std::fabs(w.value - std::round(w.value)) < 1e-9) {
            const auto cls = ctx.profile.m(static_cast<int>(std::lround(w.value)));
            if (!cls) continue;
            const std::string key = canonicalCode('M', w.value);
            if (*cls == MClass::SpindleCw || *cls == MClass::SpindleCcw) {
                acc(ctx.after.activeTool).spindleDirs.insert(key);
            } else if (*cls == MClass::CoolantOn || *cls == MClass::CoolantOff || *cls == MClass::CoolantExtra) {
                acc(ctx.after.activeTool).coolant.insert(key);
            }
        }
    }
}

void FeedsSpeedsAnalyzer::finish(Report& report) {
    for (const int t : toolOrder_) {
        const Acc& a = perTool_[t];
        ToolReport& tr = report.tool(t);
        tr.feedPerMin = a.feedPerMin;
        tr.feedPerRev = a.feedPerRev;
        tr.spindle = a.spindle;
        tr.fractionalS = a.fractionalS;
        tr.spindleDirs = a.spindleDirs;
        tr.coolant = a.coolant;
    }
}

} // namespace gca
