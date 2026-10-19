#include "gca/motion.hpp"

#include <algorithm>
#include <cmath>

namespace gca {

namespace {

constexpr double kFullCircleTol = 1e-4;     // end point coincides with start point
constexpr double kPi2 = 2.0 * kPi;
const char* const kUnknownStart = "unknown start point";
constexpr double kPegClearanceMm = 1.0;     // G83 re-entry clearance / G73 retract (approximation)

double normalizePositive(double a) {
    double r = std::fmod(a, kPi2);
    if (r < 0.0) r += kPi2;
    return r;
}

} // namespace

double euclid(const double* a, const double* b, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = b[i] - a[i];
        s += d * d;
    }
    return std::sqrt(s);
}

ArcResult computeArc(double u0, double v0, double u1, double v1, double h0, double h1,
                     std::optional<double> iOff, std::optional<double> jOff,
                     std::optional<double> r, bool cw) {
    ArcResult res;
    res.cw = cw;
    const double chordU = u1 - u0;
    const double chordV = v1 - v0;
    const double chord = std::hypot(chordU, chordV);
    const bool coincident = chord < kFullCircleTol;

    if (iOff || jOff) {
        res.centerU = u0 + iOff.value_or(0.0);
        res.centerV = v0 + jOff.value_or(0.0);
    } else if (r) {
        if (coincident) {
            res.error = "R arc with coincident end points";
            return res;
        }
        const double rad = std::fabs(*r);
        const double h2 = rad * rad - (chord / 2.0) * (chord / 2.0);
        if (h2 < -1e-6) {
            res.error = "R is smaller than half of the chord";
            return res;
        }
        const double h = std::sqrt(std::max(h2, 0.0));
        // left normal of the chord direction
        const double nu = -chordV / chord;
        const double nv = chordU / chord;
        const double sign = (cw ? -1.0 : 1.0) * (*r >= 0.0 ? 1.0 : -1.0);
        res.centerU = (u0 + u1) / 2.0 + sign * h * nu;
        res.centerV = (v0 + v1) / 2.0 + sign * h * nv;
    } else {
        res.error = "arc without I/J/K or R";
        return res;
    }

    res.radius = std::hypot(u0 - res.centerU, v0 - res.centerV);
    const double radiusEnd = std::hypot(u1 - res.centerU, v1 - res.centerV);
    res.radiusMismatch = std::fabs(res.radius - radiusEnd);
    if (res.radius < 1e-9) {
        res.error = "zero radius";
        return res;
    }
    res.startAngle = std::atan2(v0 - res.centerV, u0 - res.centerU);
    const double endAngle = std::atan2(v1 - res.centerV, u1 - res.centerU);

    if (coincident) {
        res.fullCircle = true;
        res.sweep = kPi2;
    } else {
        res.sweep = cw ? normalizePositive(res.startAngle - endAngle) : normalizePositive(endAngle - res.startAngle);
        if (res.sweep < 1e-12) res.sweep = kPi2;
    }
    res.planarLength = res.radius * res.sweep;
    res.helixHeight = std::fabs(h1 - h0);
    res.length = std::hypot(res.planarLength, res.helixHeight);
    res.ok = true;
    return res;
}

Extents arcExtents(const ArcResult& arc, double u0, double v0, double u1, double v1) {
    Extents e{std::min(u0, u1), std::max(u0, u1), std::min(v0, v1), std::max(v0, v1)};
    if (!arc.ok) return e;
    // quadrant points at angles 0, 90, 180, 270 degrees lying inside the swept range
    for (int q = 0; q < 4; ++q) {
        const double a = q * kPi / 2.0;
        const double delta = arc.cw ? normalizePositive(arc.startAngle - a) : normalizePositive(a - arc.startAngle);
        if (delta <= arc.sweep + 1e-12) {
            const double u = arc.centerU + arc.radius * std::cos(a);
            const double v = arc.centerV + arc.radius * std::sin(a);
            e.uMin = std::min(e.uMin, u);
            e.uMax = std::max(e.uMax, u);
            e.vMin = std::min(e.vMin, v);
            e.vMax = std::max(e.vMax, v);
        }
    }
    return e;
}

PlaneAxes planeAxes(Plane plane) {
    switch (plane) {
    case Plane::XY: return {kAxisX, kAxisY, kAxisZ, 'I', 'J'};
    case Plane::ZX: return {kAxisZ, kAxisX, kAxisY, 'K', 'I'};
    case Plane::YZ: return {kAxisY, kAxisZ, kAxisX, 'J', 'K'};
    }
    return {kAxisX, kAxisY, kAxisZ, 'I', 'J'};
}

std::optional<double> feedMmPerMin(const Segment& seg) {
    if (!seg.hasF || !(seg.feed > 0.0)) return std::nullopt;
    if (seg.feedMode == FeedMode::PerMinute) return seg.feed;
    if (!seg.hasS || !(seg.s > 0.0) || seg.spindle == SpindleDir::Off) return std::nullopt;
    return seg.feed * seg.s;
}

namespace {

/// Length over the linear axes known at both ends. Sets `partial` if a moved axis had an unknown start.
double linearLength(const Segment& seg, bool& partial) {
    double sum = 0.0;
    for (int a = kAxisX; a <= kAxisZ; ++a) {
        const auto i = static_cast<std::size_t>(a);
        if (!seg.to.known[i]) continue;
        if (!seg.from.known[i]) {
            if (seg.axes[i]) partial = true;
            continue;
        }
        const double d = seg.to.v[i] - seg.from.v[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

void addFeed(SegmentMetrics& m, const Segment& seg, double mm) {
    if (mm <= 0.0) return;
    m.feedMm += mm;
    if (const auto f = feedMmPerMin(seg)) m.feedSec += mm / *f * 60.0;
    else m.noFeed = true;
}

void measureCycle(const Segment& seg, double rapid, SegmentMetrics& m) {
    const CycleData& c = seg.cycle;
    const double depth = std::fabs(c.zR - c.zBottom);
    double rapidMm = std::fabs(c.zInit - c.zR);    // approach to the R plane
    double feedMm = depth;
    double feedBack = 0.0;                          // part of the way out done at feed
    const std::string& code = c.code;

    if (code == "G83" || code == "G73") {
        if (c.hasQ && c.q > 0.0 && depth > c.q) {
            const int pecks = static_cast<int>(std::ceil(depth / c.q - 1e-9));
            for (int k = 1; k < pecks; ++k) {
                if (code == "G83") {
                    rapidMm += 2.0 * k * c.q - kPegClearanceMm;   // out to R and back near the previous depth
                    feedMm += kPegClearanceMm;
                } else {
                    rapidMm += 2.0 * kPegClearanceMm;              // chip-breaking retract
                }
            }
        }
    }
    if (code == "G85" || code == "G89" || code == "G84" || code == "G74") feedBack = depth;

    double outRapid = (c.returnToInitial ? std::fabs(c.zInit - c.zBottom) : depth) - feedBack;
    if (outRapid < 0.0) outRapid = 0.0;
    rapidMm += outRapid;
    feedMm += feedBack;

    m.rapidMm += rapidMm;
    m.rapidSec += rapidMm / rapid * 60.0;
    addFeed(m, seg, feedMm);
    if (c.hasP && c.p > 0.0 && (code == "G82" || code == "G89" || code == "G88")) {
        m.dwellSec += c.p / 1000.0;
    }
}

} // namespace

ArcResult computeSegmentArc(const Segment& seg) {
    const PlaneAxes pa = planeAxes(seg.arc.plane);
    const auto u = static_cast<std::size_t>(pa.u);
    const auto v = static_cast<std::size_t>(pa.v);
    const auto h = static_cast<std::size_t>(pa.h);
    if (!seg.from.known[u] || !seg.from.known[v] || !seg.to.known[u] || !seg.to.known[v]) {
        ArcResult r;
        r.error = kUnknownStart;
        return r;
    }
    const auto offset = [&seg](char letter) -> std::optional<double> {
        if (letter == 'I') return seg.arc.i;
        if (letter == 'J') return seg.arc.j;
        return seg.arc.k;
    };
    const bool known3 = seg.from.known[h] && seg.to.known[h];
    return computeArc(seg.from.v[u], seg.from.v[v], seg.to.v[u], seg.to.v[v],
                      known3 ? seg.from.v[h] : 0.0, known3 ? seg.to.v[h] : 0.0,
                      offset(pa.uOffset), offset(pa.vOffset), seg.arc.r, seg.arc.cw);
}

SegmentMetrics measureSegment(const Segment& seg, double rapidMmPerMin) {
    SegmentMetrics m;
    switch (seg.kind) {
    case SegmentKind::Home:
        break;
    case SegmentKind::Dwell:
        m.dwellSec = seg.dwellSeconds;
        break;
    case SegmentKind::Rapid: {
        m.rapidMm = linearLength(seg, m.partial);
        m.rapidSec = m.rapidMm / rapidMmPerMin * 60.0;
        break;
    }
    case SegmentKind::Linear: {
        addFeed(m, seg, linearLength(seg, m.partial));
        break;
    }
    case SegmentKind::Arc: {
        const ArcResult a = computeSegmentArc(seg);
        if (!a.ok) {
            if (a.error == kUnknownStart) {
                m.partial = true;
            } else {
                m.arcError = true;
                m.arcMessage = a.error;
            }
            break;
        }
        addFeed(m, seg, a.length);
        break;
    }
    case SegmentKind::CycleHole:
        measureCycle(seg, rapidMmPerMin, m);
        break;
    }
    return m;
}

} // namespace gca
