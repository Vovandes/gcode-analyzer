#include "gca/json_report.hpp"
#include "gca/analyzers/warnings.hpp"
#include "gca/motion.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

namespace gca {
namespace {

void string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20U) out << "\\u00" << hex[c >> 4U] << hex[c & 15U];
            else out << static_cast<char>(c);
        }
    }
    out << '"';
}

void number(std::ostream& out, double value) {
    if (std::isfinite(value)) out << (value == 0.0 ? 0.0 : value);
    else out << "null";
}
void optional(std::ostream& out, std::optional<double> value) {
    if (value) number(out, *value);
    else out << "null";
}
void point(std::ostream& out, const Position& p) {
    out << '[';
    for (std::size_t a = 0; a < 3; ++a) {
        if (a != 0) out << ',';
        optional(out, p.known[a] ? std::optional<double>(p.v[a]) : std::nullopt);
    }
    out << ']';
}
void range(std::ostream& out, const Range& r) {
    if (!r.has) { out << "null"; return; }
    out << "{\"min\":"; number(out, r.min);
    out << ",\"max\":"; number(out, r.max); out << '}';
}
void box(std::ostream& out, const BBox& b) {
    out << '[';
    for (std::size_t a = 0; a < b.axis.size(); ++a) {
        if (a != 0) out << ',';
        range(out, b.axis[a]);
    }
    out << ']';
}
void time(std::ostream& out, const TimeBreakdown& t) {
    out << "{\"totalSec\":"; number(out, t.totalSec());
    out << ",\"rapidSec\":"; number(out, t.rapidSec);
    out << ",\"feedSec\":"; number(out, t.feedSec);
    out << ",\"cycleSec\":"; number(out, t.cycleSec);
    out << ",\"dwellSec\":"; number(out, t.dwellSec);
    out << ",\"rapidMm\":"; number(out, t.rapidMm);
    out << ",\"feedMm\":"; number(out, t.feedMm);
    out << ",\"cycleMm\":"; number(out, t.cycleMm);
    out << ",\"rapidMoves\":" << t.rapidMoves << ",\"feedMoves\":" << t.feedMoves
        << ",\"cycleHoles\":" << t.cycleHoles << '}';
}
template<class Container, class Writer>
void array(std::ostream& out, const Container& values, Writer writer) {
    out << '[';
    bool first = true;
    for (const auto& value : values) {
        if (!first) out << ',';
        first = false;
        writer(out, value);
    }
    out << ']';
}
const char* kindName(SegmentKind kind) {
    switch (kind) {
    case SegmentKind::Rapid: return "rapid";
    case SegmentKind::Linear: return "linear";
    case SegmentKind::Arc: return "arc";
    case SegmentKind::CycleHole: return "cycle";
    case SegmentKind::Dwell: return "dwell";
    case SegmentKind::Home: return "home";
    }
    return "linear";
}
const char* planeName(Plane plane) {
    switch (plane) {
    case Plane::XY: return "G17";
    case Plane::ZX: return "G18";
    case Plane::YZ: return "G19";
    }
    return "G17";
}

void segment(std::ostream& out, const Segment& s, double rapid) {
    // Optional additive fields preserve existing schema-1 consumers.
    out << "{\"line\":" << s.lineNo << ",\"kind\":"; string(out, kindName(s.kind));
    out << ",\"tool\":" << s.tool << ",\"plane\":"; string(out, planeName(s.plane));
    out << ",\"start\":"; point(out, s.from);
    out << ",\"end\":"; point(out, s.to);
    if (s.kind == SegmentKind::Arc) {
        const auto arc = computeSegmentArc(s);
        Position center;
        if (arc.ok) {
            const auto pa = planeAxes(s.arc.plane);
            center = s.from;
            center.v[static_cast<std::size_t>(pa.u)] = arc.centerU;
            center.v[static_cast<std::size_t>(pa.v)] = arc.centerV;
        }
        out << ",\"center\":"; point(out, center);
        out << ",\"cw\":" << (s.arc.cw ? "true" : "false") << ",\"radius\":";
        optional(out, arc.ok ? std::optional<double>(arc.radius) : std::nullopt);
    }
    out << ",\"feed\":";
    if (s.kind == SegmentKind::Rapid) number(out, rapid);
    else if (s.kind == SegmentKind::Home || s.kind == SegmentKind::Dwell) out << "null";
    else optional(out, feedMmPerMin(s));
    out << ",\"spindle\":"; optional(out, s.hasS ? std::optional<double>(s.s) : std::nullopt);
    out << ",\"b\":";
    optional(out, s.to.known[kAxisB] ? std::optional<double>(s.to.v[kAxisB]) : std::nullopt);
    if (s.kind == SegmentKind::Dwell) { out << ",\"dwellSec\":"; number(out, s.dwellSeconds); }
    if (s.kind == SegmentKind::CycleHole) {
        out << ",\"cycle\":{\"code\":"; string(out, s.cycle.code);
        out << ",\"zR\":"; number(out, s.cycle.zR);
        out << ",\"zBottom\":"; number(out, s.cycle.zBottom);
        out << ",\"zInit\":";
        optional(out, s.cycle.zInitKnown ? std::optional<double>(s.cycle.zInit) : std::nullopt);
        out << ",\"q\":"; optional(out, s.cycle.hasQ ? std::optional<double>(s.cycle.q) : std::nullopt);
        out << ",\"p\":"; optional(out, s.cycle.hasP ? std::optional<double>(s.cycle.p) : std::nullopt);
        out << ",\"returnToInitial\":" << (s.cycle.returnToInitial ? "true" : "false") << '}';
    }
    if (s.limitViolation) out << ",\"limitViolation\":true";
    if (s.machineCoordinates) out << ",\"machineCoordinates\":true";
    if (s.workFrom && s.workTo) {
        out << ",\"workStart\":"; point(out, *s.workFrom);
        out << ",\"workEnd\":"; point(out, *s.workTo);
    }
    out << '}';
}

void tool(std::ostream& out, const ToolReport& t) {
    const auto text = [](std::ostream& o, const std::string& v) { string(o, v); };
    const auto integer = [](std::ostream& o, auto v) { o << v; };
    out << "{\"tool\":" << t.tool << ",\"descriptions\":"; array(out, t.descriptions, text);
    out << ",\"changes\":" << t.changes << ",\"changeLines\":"; array(out, t.changeLines, integer);
    out << ",\"hValues\":"; array(out, t.hValues, integer);
    out << ",\"dValues\":"; array(out, t.dValues, integer);
    out << ",\"time\":"; time(out, t.time);
    out << ",\"box\":"; box(out, t.box);
    out << ",\"workBox\":"; box(out, t.workBox);
    out << ",\"feedPerMin\":"; range(out, t.feedPerMin);
    out << ",\"feedPerRev\":"; range(out, t.feedPerRev);
    out << ",\"spindle\":"; range(out, t.spindle);
    out << ",\"fractionalS\":" << (t.fractionalS ? "true" : "false");
    out << ",\"spindleDirs\":"; array(out, t.spindleDirs, text);
    out << ",\"coolant\":"; array(out, t.coolant, text);
    out << ",\"wcs\":"; array(out, t.wcs, text);
    out << ",\"cycleHoles\":{";
    bool first = true;
    for (const auto& [code, count] : t.cycleHoles) {
        if (!first) out << ',';
        first = false;
        string(out, code); out << ':' << count;
    }
    out << "}}";
}

} // namespace

void writeJsonReport(const Report& report, std::ostream& output) {
    // Isolate formatting from the caller's locale, precision and stream flags.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4);
    const auto& f = report.file;
    out << "{\n  \"schema\":1,\n  \"file\":{\"name\":"; string(out, f.fileName);
    out << ",\"lines\":" << f.lines << ",\"blocks\":" << f.blocks << ",\"dialect\":"; string(out, f.dialectId);
    out << ",\"machine\":"; string(out, f.machineName);
    out << ",\"machineProfile\":"; string(out, f.machineProfile);
    out << ",\"rapidFeed\":"; number(out, f.rapidFeed); out << "},\n  \"segments\":";
    array(out, report.segments, [&f](std::ostream& o, const Segment& s) { segment(o, s, f.rapidFeed); });
    out << ",\n  \"warnings\":";
    array(out, report.warnings, [](std::ostream& o, const Warning& w) {
        o << "{\"line\":" << w.line << ",\"code\":"; string(o, w.code);
        o << ",\"severity\":"; string(o, severityName(warningSeverity(w.code)));
        o << ",\"message\":"; string(o, w.message);
        o << ",\"text\":"; string(o, w.text); o << '}';
    });
    out << ",\n  \"tools\":"; array(out, report.tools, tool);
    out << ",\n  \"time\":"; time(out, report.time);
    out << ",\n  \"limits\":";
    array(out, report.limits, [](std::ostream& o, const Report::LimitCheck& c) {
        o << "{\"parameter\":"; string(o, c.parameter);
        o << ",\"valueMin\":"; optional(o, c.valueMin);
        o << ",\"valueMax\":"; optional(o, c.valueMax);
        o << ",\"limitMin\":"; optional(o, c.limitMin);
        o << ",\"limitMax\":"; optional(o, c.limitMax);
        o << ",\"status\":"; string(o, c.status); o << ",\"line\":" << c.line << '}';
    });
    const auto& mt = report.machineTime;
    out << ",\n  \"machineTime\":{\"totalSec\":"; number(out, mt.totalSec());
    out << ",\"accelerations\":" << (mt.accelerations ? "true" : "false");
    out << ",\"profileApplied\":" << (report.hasMachineProfile ? "true" : "false");
    out << ",\"motion\":"; time(out, mt.motion);
    out << ",\"toolChangeSec\":"; number(out, mt.toolChangeSec);
    out << ",\"homeSec\":"; number(out, mt.homeSec);
    out << ",\"bSec\":"; number(out, mt.bSec);
    out << ",\"spindleSec\":"; number(out, mt.spindleSec);
    out << ",\"programStops\":" << report.programStops << ",\"optionalStops\":" << report.optionalStops;
    out << ",\"notes\":"; array(out, mt.notes, [](std::ostream& o, const std::string& s) { string(o, s); }); out << '}';
    out << "\n}\n";
    output << out.str();
}

std::string renderJsonReport(const Report& report) {
    std::ostringstream out;
    writeJsonReport(report, out);
    return out.str();
}

} // namespace gca
