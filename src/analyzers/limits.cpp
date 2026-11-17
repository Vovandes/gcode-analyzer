#include "gca/analyzers/limits.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/motion.hpp"
#include "gca/text_util.hpp"
#include <algorithm>
#include <cmath>

namespace gca {
void LimitsAnalyzer::warning(std::size_t line, const char* code, const std::string& message) {
    if (!emitted_.insert({line, code}).second) return;
    const std::string raw = line > 0 && line <= program_.blocks.size() ? program_.blocks[line - 1].raw : "";
    warnings_.push_back({line, raw, code, message});
}
void LimitsAnalyzer::scalar(std::string parameter, double value, std::optional<double> maximum, const char* code, std::size_t line) {
    if (!maximum || !checked_.insert({line, parameter}).second) return;
    const bool bad = value > *maximum + 1e-9;
    checks_.push_back({parameter, value, value, {}, maximum, bad ? "error" : "ok", line});
    if (bad) warning(line, code, parameter + " " + trimmedNumber(value) + " превышает предел " + trimmedNumber(*maximum));
}
void LimitsAnalyzer::onBlock(const BlockContext& ctx) {
    if (ctx.effect.dataBlock || ctx.block.control) return;
    const auto& s = ctx.after;
    const auto spindle = s.spindles.find(s.spindleChannel);
    const bool rpm = spindle == s.spindles.end() || !spindle->second.css;
    if (ctx.block.has('S') && s.hasS && rpm && s.spindleChannel == 0) scalar("S", std::fabs(s.s), machine_.spindleMax, warn::kSpindleLimit, ctx.block.lineNo);
    if ((ctx.block.has('F') || (ctx.block.has('S') && s.feedMode == FeedMode::PerRev)) && s.hasF) {
        if (s.feedMode == FeedMode::PerMinute) scalar("F", s.f, machine_.feedMax, warn::kFeedLimit, ctx.block.lineNo);
        else if (s.feedMode == FeedMode::PerRev && s.hasS && rpm) scalar("F", s.f * s.s, machine_.feedMax, warn::kFeedLimit, ctx.block.lineNo);
    }
}
void LimitsAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    const std::size_t index = index_++;
    const bool cutting = seg.kind == SegmentKind::Linear || seg.kind == SegmentKind::Arc || seg.kind == SegmentKind::CycleHole;
    if (cutting) if (auto f = feedMmPerMin(seg)) {
        scalar("F", *f, machine_.feedMax, warn::kFeedLimit, seg.lineNo);
        if (machine_.feedMax && *f > *machine_.feedMax) bad_.insert(index);
    }
    if (seg.hasS && seg.spindleChannel == 0) {
        scalar("S", std::fabs(seg.s), machine_.spindleMax, warn::kSpindleLimit, seg.lineNo);
        if (machine_.spindleMax && std::fabs(seg.s) > *machine_.spindleMax) bad_.insert(index);
    }
    if (seg.kind == SegmentKind::Dwell) return;
    std::array<Range, kAxisCount> bounds{};
    for (std::size_t a = 0; a < kAxisCount; ++a) {
        if (seg.from.known[a]) bounds[a].add(seg.from.v[a]);
        if (seg.to.known[a]) bounds[a].add(seg.to.v[a]);
        if (seg.kind == SegmentKind::Home && seg.intermediate.known[a]) bounds[a].add(seg.intermediate.v[a]);
    }
    if (seg.kind == SegmentKind::Arc) {
        const auto arc = computeSegmentArc(seg);
        if (arc.ok) {
            const auto p = planeAxes(seg.plane);
            const auto u = static_cast<std::size_t>(p.u), v = static_cast<std::size_t>(p.v);
            const auto e = arcExtents(arc, seg.from.v[u], seg.from.v[v], seg.to.v[u], seg.to.v[v]);
            bounds[u].add(e.uMin); bounds[u].add(e.uMax); bounds[v].add(e.vMin); bounds[v].add(e.vMax);
        }
    }
    if (seg.kind == SegmentKind::CycleHole) {
        bounds[2].add(seg.cycle.zR); bounds[2].add(seg.cycle.zBottom);
        if (seg.cycle.zInitKnown) bounds[2].add(seg.cycle.zInit);
    }
    const auto wcs = machine_.wcsOffsets.find(seg.wcs);
    for (std::size_t a = 0; a < kAxisCount; ++a) {
        if (seg.approximate && a < 3) continue;
        const auto& limit = machine_.axes[a];
        const auto& range = bounds[a];
        if ((!limit.min && !limit.max) || !range.has) continue;
        const std::string parameter(1, kAxisLetters[a]);
        std::optional<double> offset;
        if (a >= 3 || seg.machineCoordinates) offset = 0;
        else if (!seg.offsetsUnknown && wcs != machine_.wcsOffsets.end()) offset = wcs->second[a];
        if (offset) {
            const double low = range.min + *offset, high = range.max + *offset;
            const bool bad = (limit.min && low < *limit.min - 1e-9) || (limit.max && high > *limit.max + 1e-9);
            checks_.push_back({parameter, low, high, limit.min, limit.max, bad ? "error" : "ok", seg.lineNo});
            if (bad) { bad_.insert(index); warning(seg.lineNo, warn::kAxisLimit, "Выход за пределы оси " + parameter + " (машинные координаты)"); }
        } else {
            auto& span = spans_[{seg.wcs, a, seg.coordinateEpoch}];
            span.range.add(range.min); span.range.add(range.max); span.segments.push_back(index);
            if (!span.line) span.line = seg.lineNo;
        }
    }
}
void LimitsAnalyzer::finish(Report& report) {
    for (const auto& [key, span] : spans_) {
        const auto& wcs = std::get<0>(key);
        const auto a = std::get<1>(key);
        const auto& axis = machine_.axes[a];
        const std::optional<double> width = axis.min && axis.max ? std::optional<double>(*axis.max - *axis.min) : std::nullopt;
        const double value = span.range.max - span.range.min;
        const bool bad = width && value > *width + 1e-9;
        const std::string parameter = std::string(1, kAxisLetters[a]) + " размах " + (wcs.empty() ? "без СК" : wcs);
        checks_.push_back({parameter, value, value, {}, width, bad ? "error" : "info", span.line});
        warning(span.line, warn::kNoWcsOffset, "Абсолютные пределы не проверены: нет смещений СК, " + parameter);
        if (bad) {
            warning(span.line, warn::kAxisLimit, "Размах превышает ход оси: " + parameter);
            bad_.insert(span.segments.begin(), span.segments.end());
        }
    }
    report.limits = checks_;
    report.warnings.insert(report.warnings.end(), warnings_.begin(), warnings_.end());
    for (auto i : bad_) if (i < report.segments.size()) report.segments[i].limitViolation = true;
}
} // namespace gca
