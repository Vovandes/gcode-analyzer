#include "gca/analyzers/machine_time.hpp"
#include "gca/motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gca {
double acceleratedSeconds(double length, double speed, double acceleration, double entry, double exit) {
    if (length <= 0 || speed <= 0) return 0;
    if (acceleration <= 0) return length / speed;
    entry = std::clamp(entry, 0.0, speed); exit = std::clamp(exit, 0.0, speed);
    const double peak = std::min(speed, std::sqrt(acceleration * length + (entry * entry + exit * exit) / 2));
    const double up = std::max(0.0, (peak * peak - entry * entry) / (2 * acceleration));
    const double down = std::max(0.0, (peak * peak - exit * exit) / (2 * acceleration));
    return std::max(0.0, peak - entry) / acceleration + std::max(0.0, peak - exit) / acceleration +
        std::max(0.0, length - up - down) / peak;
}
namespace {
void note(Report::MachineTime& t, const std::string& text) {
    if (std::find(t.notes.begin(), t.notes.end(), text) == t.notes.end()) t.notes.push_back(text);
}
double axisTime(double length, std::size_t axis, const MachineProfile& p, double fallback, Report::MachineTime& result, bool feed = false) {
    const auto& a = p.axes[axis];
    const double rate = feed ? fallback : a.rapid.value_or(fallback);
    if (length <= 1e-12) return 0;
    const auto acceleration = feed ? a.acceleration : a.rapidAcceleration ? a.rapidAcceleration : a.acceleration;
    if (acceleration) result.accelerations = true;
    else note(result, "оценка без ускорений: для части перемещений ускорение не задано");
    return acceleratedSeconds(length, rate / 60, acceleration.value_or(0));
}
double rapidTime(const Position& from, const Position& to, const MachineProfile& p, double rapid, Report::MachineTime& t) {
    bool custom = false;
    double maximum = 0, sum = 0;
    for (std::size_t a = 0; a < kAxisCount; ++a) {
        if (a == kAxisB) continue; // Legacy B time is a separate profile constant.
        if (!from.known[a] || !to.known[a]) {
            if (from.known[a] != to.known[a]) note(t, "Неполная оценка: неизвестна начальная или конечная координата");
            continue;
        }
        const double d = std::fabs(to.v[a] - from.v[a]);
        if (d < 1e-12) continue;
        custom = custom || p.axes[a].rapid.has_value() || p.axes[a].acceleration.has_value() || p.axes[a].rapidAcceleration.has_value();
        if (a >= 3 && !p.axes[a].rapid) { note(t, "Скорость поворотной оси не задана: её время не учтено"); continue; }
        if (a >= 3) custom = true;
        sum += d * d;
        maximum = std::max(maximum, axisTime(d, a, p, rapid, t));
    }
    // With an empty profile use the historical Euclidean rate exactly.
    return custom ? maximum : std::sqrt(sum) / rapid * 60;
}
struct FeedMove {
    double length = 0, speed = 0, acceleration = 0;
    std::array<double, 3> entryDirection{}, exitDirection{};
    bool valid = false;
};
FeedMove describe(const Segment& s, const MachineProfile& p, double rapid, Report::MachineTime& result) {
    FeedMove f;
    if (s.kind != SegmentKind::Linear && s.kind != SegmentKind::Arc) return f;
    const auto metrics = measureSegment(s, rapid);
    const auto rate = feedMmPerMin(s);
    if (!rate || metrics.feedMm <= 0 || metrics.partial) return f;
    f.length = metrics.feedMm; f.speed = *rate / 60;
    f.acceleration = std::numeric_limits<double>::max();
    bool missing = false;
    for (std::size_t a = 0; a < 3; ++a) {
        double d = 0;
        if (s.from.known[a] && s.to.known[a]) d = s.to.v[a] - s.from.v[a];
        f.entryDirection[a] = f.exitDirection[a] = d / f.length;
        bool participating = std::fabs(d) > 1e-12;
        if (s.kind == SegmentKind::Arc) {
            const auto axes = planeAxes(s.plane);
            participating = participating || a == static_cast<std::size_t>(axes.u) || a == static_cast<std::size_t>(axes.v);
        }
        if (!participating) continue;
        if (p.axes[a].acceleration) f.acceleration = std::min(f.acceleration, *p.axes[a].acceleration);
        else missing = true;
    }
    if (missing || f.acceleration == std::numeric_limits<double>::max()) {
        f.acceleration = 0;
        note(result, "оценка без ускорений: для части перемещений ускорение не задано");
    } else result.accelerations = true;
    if (s.kind == SegmentKind::Arc) {
        const auto arc = computeSegmentArc(s);
        if (!arc.ok) return {};
        const auto axes = planeAxes(s.plane);
        const auto u = static_cast<std::size_t>(axes.u), v = static_cast<std::size_t>(axes.v);
        const double sign = arc.cw ? -1.0 : 1.0;
        const double end = arc.startAngle + sign * arc.sweep;
        const double ratio = arc.planarLength / f.length;
        f.entryDirection[u] = -sign * std::sin(arc.startAngle) * ratio;
        f.entryDirection[v] = sign * std::cos(arc.startAngle) * ratio;
        f.exitDirection[u] = -sign * std::sin(end) * ratio;
        f.exitDirection[v] = sign * std::cos(end) * ratio;
    }
    f.valid = true;
    return f;
}
double cycleTime(const Segment& s, const MachineProfile& p, double rapid, Report::MachineTime& result) {
    const auto& c = s.cycle;
    const auto original = measureSegment(s, rapid);
    const auto rate = feedMmPerMin(s);
    if (!p.axes[2].rapid && !p.axes[2].acceleration && !p.axes[2].rapidAcceleration) return original.rapidSec + original.feedSec;
    const auto feed = [&](double length) { return rate ? axisTime(length, 2, p, *rate, result, true) : 0.0; };
    const auto fast = [&](double length) { return axisTime(length, 2, p, rapid, result); };
    double t = c.zInitKnown ? fast(std::fabs(c.zInit - c.zR)) : 0;
    const double depth = std::fabs(c.zBottom - c.zR);
    if ((c.code == "G83" || c.code == "G73") && c.hasQ && c.q > 0 && depth > c.q) {
        double previous = 0, reentry = 0;
        for (double next = std::min(c.q, depth); previous < depth - 1e-9; next = std::min(next + c.q, depth)) {
            t += feed(next - previous + reentry);
            if (next < depth - 1e-9) {
                reentry = std::min(1.0, next);
                t += c.code == "G83" ? fast(next) + fast(next - reentry) : fast(reentry) + fast(reentry);
                if (c.code == "G73") reentry = 0;
            }
            previous = next;
        }
    } else t += feed(depth);
    const bool feedOut = c.code == "G85" || c.code == "G89" || c.code == "G84" || c.code == "G74";
    if (feedOut) { t += feed(depth); if (c.returnToInitial) t += fast(std::fabs(c.zInit - c.zR)); }
    else t += fast(c.returnToInitial ? std::fabs(c.zInit - c.zBottom) : depth);
    return t;
}
}
void MachineTimeAnalyzer::onBlock(const BlockContext& ctx) {
    bool barrier = ctx.before.wcs != ctx.after.wcs || ctx.effect.dataBlock || ctx.block.control;
    for (const auto& w : ctx.block.words) if (w.letter == 'M' && w.hasValue) {
        const auto cls = ctx.profile.m(static_cast<int>(w.value));
        if (cls && (*cls == MClass::ProgramStop || *cls == MClass::OptionalStop || *cls == MClass::ToolChange ||
            *cls == MClass::SpindleStop || *cls == MClass::ProgramEnd)) barrier = true;
    }
    if (barrier) ++group_;
    if (machine_ && machine_->spindleStartSec && ctx.after.spindle != SpindleDir::Off &&
        (ctx.before.spindle != ctx.after.spindle || ctx.before.s != ctx.after.s)) {
        result_.spindleSec += *machine_->spindleStartSec;
        ++group_;
    }
}
void MachineTimeAnalyzer::onSegment(const Segment& s, const BlockContext&) {
    const bool rotary = s.axes[3] || s.axes[4] || s.axes[5];
    if (rotary) ++group_;
    groups_.push_back(group_);
    if (rotary) ++group_;
    if (s.kind != SegmentKind::Linear && s.kind != SegmentKind::Arc) ++group_;
}
void MachineTimeAnalyzer::onToolChange(const ToolEvent& event, const BlockContext&) {
    if (!machine_ || event.kind != ToolEvent::Kind::Change) return;
    if (!(changes_++ == 0 && machine_->skipFirstChange)) result_.toolChangeSec += machine_->toolChangeSec.value_or(0);
    if (!machine_->toolChangeSec) note(result_, "Время смены инструмента не задано и не учтено");
}
void MachineTimeAnalyzer::finish(Report& report) {
    result_.motion = report.time;
    if (!machine_) {
        note(result_, "точная модель не применена: нет профиля; оценка без ускорений");
        report.machineTime = result_; return;
    }
    const auto& p = *machine_;
    std::vector<FeedMove> feed;
    const auto& moves = report.segments;
    for (const auto& move : moves) feed.push_back(describe(move, p, rapid_, result_));
    std::vector<double> junction(moves.size() + 1, 0);
    for (std::size_t i = 1; i < moves.size(); ++i) {
        const auto& prev = feed[i - 1]; const auto& next = feed[i];
        if (!prev.valid || !next.valid || !prev.acceleration || !next.acceleration || groups_[i - 1] != groups_[i]) continue;
        double dot = 0;
        bool connected = moves[i - 1].wcs == moves[i].wcs;
        for (std::size_t a = 0; a < 3; ++a) {
            dot += prev.exitDirection[a] * next.entryDirection[a];
            if (moves[i - 1].to.known[a] && moves[i].from.known[a] &&
                std::fabs(moves[i - 1].to.v[a] - moves[i].from.v[a]) > 1e-7) connected = false;
        }
        if (connected && std::acos(std::clamp(dot, -1.0, 1.0)) * 180 / kPi <= p.junctionAngle + 1e-9)
            junction[i] = std::min({prev.speed, next.speed, std::sqrt(2 * prev.acceleration * prev.length), std::sqrt(2 * next.acceleration * next.length)});
    }
    result_.motion.rapidSec = result_.motion.feedSec = result_.motion.cycleSec = result_.motion.dwellSec = 0;
    std::map<int, TimeBreakdown*> perTool;
    for (auto& tool : report.tools) {
        auto& time = tool.detail.motionTime;
        time.rapidSec = time.feedSec = time.cycleSec = time.dwellSec = 0;
        perTool.emplace(tool.tool, &time);
    }
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto& s = moves[i];
        const auto original = measureSegment(s, rapid_);
        auto& time = *perTool.at(s.tool);
        double rapidSec = 0, feedSec = 0, cycleSec = 0, dwellSec = 0;
        bool fixedHome = false;
        switch (s.kind) {
        case SegmentKind::Rapid: rapidSec = rapidTime(s.from, s.to, p, rapid_, result_); break;
        case SegmentKind::Linear:
        case SegmentKind::Arc:
            feedSec = feed[i].valid ? acceleratedSeconds(feed[i].length, feed[i].speed, feed[i].acceleration, junction[i], junction[i + 1]) : original.feedSec;
            break;
        case SegmentKind::CycleHole:
            cycleSec = cycleTime(s, p, rapid_, result_);
            dwellSec = original.dwellSec;
            break;
        case SegmentKind::Dwell: dwellSec = original.dwellSec; break;
        case SegmentKind::Home: {
            bool known = true;
            for (std::size_t a = 0; a < kAxisCount; ++a) if (s.axes[a]) known = known && s.from.known[a] && s.intermediate.known[a] && s.to.known[a];
            if (known) result_.homeSec += rapidTime(s.from, s.intermediate, p, rapid_, result_) + rapidTime(s.intermediate, s.to, p, rapid_, result_);
            else if (p.g28Time) { result_.homeSec += *p.g28Time; fixedHome = true; }
            else note(result_, "G28/G30 не учтён: неизвестен путь и не задано g28_time");
            break;
        }
        }
        time.rapidSec += rapidSec;
        time.feedSec += feedSec;
        time.cycleSec += cycleSec;
        time.dwellSec += dwellSec;
        result_.motion.rapidSec += rapidSec;
        result_.motion.feedSec += feedSec;
        result_.motion.cycleSec += cycleSec;
        result_.motion.dwellSec += dwellSec;
        if (s.approximate) note(result_, "Номинальная геометрия: локальная СК, RTCP или точение; время является приближением");
        if (s.surfaceSpeed) note(result_, "S в режиме постоянной скорости резания не является оборотами; G95 без известного RPM по времени не рассчитан");
        if (s.axes[4] || s.axes[5]) {
            if (s.kind != SegmentKind::Rapid && s.kind != SegmentKind::Home)
                note(result_, "Рабочее движение A/C: время поворотных осей и синхронизация с XYZ не рассчитаны");
        }
        double bDistance = s.from.known[3] && s.to.known[3] ? std::fabs(s.to.v[3] - s.from.v[3]) : 0;
        if (s.kind == SegmentKind::Home && s.from.known[3] && s.intermediate.known[3] && s.to.known[3])
            bDistance = std::fabs(s.intermediate.v[3] - s.from.v[3]) + std::fabs(s.to.v[3] - s.intermediate.v[3]);
        if (!fixedHome && bDistance > 1e-9) {
            if (p.bSecondsPer90) result_.bSec += bDistance / 90 * *p.bSecondsPer90;
            else note(result_, "Время поворота B не задано и не учтено");
        }
        if (!fixedHome && s.axes[3] && (!s.from.known[3] || !s.to.known[3]))
            note(result_, "Неполная оценка B: неизвестна начальная или конечная координата");
        if (original.partial) note(result_, "Неполная оценка: неизвестна начальная или конечная координата");
    }
    if (!result_.accelerations) {
        const auto detailed = std::find(result_.notes.begin(), result_.notes.end(), "оценка без ускорений: для части перемещений ускорение не задано");
        if (detailed == result_.notes.end()) note(result_, "оценка без ускорений");
    }
    report.machineTime = result_;
}
} // namespace gca
