#include "gca/modal_state.hpp"

#include <cmath>

namespace gca {

namespace {

bool hasDecimalPoint(const Word& w) { return w.rawText.find('.') != std::string::npos; }

ArcData readArc(const Block& block, const ModalState& state, const IDialect& dialect) {
    ArcData arc;
    arc.cw = state.motion == GClass::ArcCw;
    arc.plane = state.plane;
    arc.radiusPriority = dialect.arcRadiusPriority();
    for (const auto& word : block.words) {
        if (!word.hasValue || !word.address.empty()) continue;
        if (word.letter == 'I') arc.i = word.value;
        else if (word.letter == 'J') arc.j = word.value;
        else if (word.letter == 'K') arc.k = word.value;
        else if (word.letter == 'R') arc.r = word.value;
    }
    return arc;
}

} // namespace

Segment ModalMachine::makeSegment(SegmentKind kind, std::size_t lineNo, const Position& from) const {
    Segment s;
    s.kind = kind;
    s.plane = state_.plane;
    s.lineNo = lineNo;
    s.from = from;
    s.to = state_.pos;
    s.tool = state_.activeTool;
    s.wcs = state_.wcs;
    s.feedMode = state_.feedMode;
    s.hasF = state_.hasF;
    s.feed = state_.f;
    s.hasS = state_.hasS;
    s.s = state_.s;
    s.spindle = state_.spindle;
    s.comp = state_.comp;
    s.approximate = state_.transformed || state_.rtcp || state_.turning;
    s.offsetsUnknown = state_.offsetsUnknown;
    s.coordinateEpoch = state_.coordinateEpoch;
    s.spindleChannel = state_.spindleChannel;
    const auto spindle = state_.spindles.find(state_.spindleChannel);
    s.surfaceSpeed = spindle != state_.spindles.end() && spindle->second.css;
    if (s.surfaceSpeed) s.hasS = false;
    return s;
}

BlockEffect ModalMachine::apply(const Block& b) {
    BlockEffect eff;
    ModalState& st = state_;
    if (b.definition) {
        definition_ = BlockEffect::DefinitionCall{}; definition_->number = *b.definition;
        collectingDefinition_ = true;
    }
    if (collectingDefinition_) {
        for (const auto& w : b.words) if (w.letter != 'N') {
            const auto address = w.address.empty() ? std::string(1,w.letter) : w.address;
            definition_->parameters[address] = w;
        }
        if (b.definitionEnd) { definition_->complete = true; collectingDefinition_ = false; }
        eff.dataBlock = true;
    }
    if (b.cycleCall) {
        eff.dataBlock = true;
        eff.definitionCall = definition_.value_or(BlockEffect::DefinitionCall{});
        const auto info = profile_.dialect().definition(eff.definitionCall->number);
        // Subprogram internals are not executed. Do not connect two unrelated points across a call.
        st.pos.known.fill(false);
        if (info && info->measuring) {
            const auto wcs = eff.definitionCall->parameters.find(info->wcsAddress);
            if (wcs != eff.definitionCall->parameters.end() && wcs->second.hasValue &&
                wcs->second.value >= 53 && wcs->second.value <= 59) {
                st.wcs = canonicalCode('G', wcs->second.value); st.wcsSelected = true;
                st.machineWcs = wcs->second.value == 53;
            } else if (!info->wcsAddress.empty()) { st.wcs.clear(); st.wcsSelected = false; }
            if (info->writesWcs) { st.offsetsUnknown = true; ++st.coordinateEpoch; }
        }
    }
    if (eff.dataBlock) return eff;
    if (b.control) { st.pos.known.fill(false); return eff; }
    for (const auto& w : b.words) if (w.expression) { st.pos.known.fill(false); return eff; }
    if (b.words.empty()) return eff;

    bool sawDistanceWord = false;
    bool home = false;
    std::string homeCode;
    bool machineCoords = st.machineWcs;
    bool dwell = false;
    bool nonMotion = false;
    bool setPosition = false;
    bool cycleInBlock = false;
    bool spindleLimit = false;
    bool feedSet = false;

    // 1. G words (modal updates)
    for (const auto& w : b.words) {
        if (w.letter != 'G' || !w.address.empty() || !w.hasValue) continue;
        const auto info = profile_.g(w.value);
        if (!info) continue;
        const std::string key = canonicalCode('G', w.value);
        switch (info->cls) {
        case GClass::Rapid:
        case GClass::Linear:
        case GClass::ArcCw:
        case GClass::ArcCcw:
            st.hasMotion = true;
            st.unmodeledMotion = false;
            st.motion = info->cls;
            st.cycleCode.clear();
            break;
        case GClass::Cycle:
            st.unmodeledMotion = false;
            st.cycleCode = key;
            cycleInBlock = true;
            break;
        case GClass::CycleCancel:
            // Fanuc: G80 leaves group 01 in the G00 state.
            st.cycleCode.clear();
            st.cycle = CycleParams{};
            st.hasMotion = true;
            st.motion = GClass::Rapid;
            st.groups[1] = "G0";
            break;
        case GClass::ReturnInitial: st.cycleReturnInitial = true; break;
        case GClass::ReturnR: st.cycleReturnInitial = false; break;
        case GClass::PlaneXY: st.plane = Plane::XY; break;
        case GClass::PlaneZX: st.plane = Plane::ZX; break;
        case GClass::PlaneYZ: st.plane = Plane::YZ; break;
        case GClass::Inch: st.inch = true; break;
        case GClass::Metric: st.inch = false; break;
        case GClass::Absolute: st.dist = DistanceMode::Absolute; sawDistanceWord = true; break;
        case GClass::Incremental: st.dist = DistanceMode::Incremental; sawDistanceWord = true; break;
        case GClass::FeedPerMin: st.feedMode = FeedMode::PerMinute; break;
        case GClass::FeedPerRev: st.feedMode = FeedMode::PerRev; break;
        case GClass::UnsupportedFeed: st.feedMode = FeedMode::Unsupported; break;
        case GClass::Wcs: {
            const std::string previousWcs = st.wcs;
            const Word* p = b.find('P');
            const bool extended = std::fabs(w.value - 54.1) < 1e-6 || (std::fabs(w.value - 54.0) < 1e-9 && p != nullptr);
            st.wcsSelected = true;
            st.machineWcs = false; machineCoords = false;
            if (extended) {
                st.wcsP = (p != nullptr && p->hasValue) ? static_cast<int>(std::lround(p->value)) : -1;
                st.wcs = st.wcsP >= 0 ? "G54.1 P" + std::to_string(st.wcsP) : "G54.1";
            } else {
                st.wcsP = -1;
                st.wcs = key;
            }
            const auto* machine = profile_.machine();
            if (machine && machine->hasMotionData() && !previousWcs.empty() && previousWcs != st.wcs) {
                const auto old = machine->wcsOffsets.find(previousWcs), next = machine->wcsOffsets.find(st.wcs);
                for (std::size_t a = 0; a < 3; ++a) {
                    if (!st.offsetsUnknown && old != machine->wcsOffsets.end() && next != machine->wcsOffsets.end() && old->second[a] && next->second[a])
                        st.pos.v[a] += *old->second[a] - *next->second[a];
                    else st.pos.known[a] = false;
                }
            }
            break;
        }
        case GClass::Home: home = true; homeCode = key; break;
        case GClass::MachineCoords:
            machineCoords = true;
            if (profile_.dialect().lexicalRules().extended && !b.has('X') && !b.has('Y') && !b.has('Z') && !b.has('A') && !b.has('B') && !b.has('C')) {
                st.machineWcs = true; st.wcs = "G53"; st.wcsSelected = true; st.pos.known.fill(false);
            }
            break;
        case GClass::SetPosition: setPosition = true; break;
        case GClass::CompOff: st.comp = CompMode::Off; break;
        case GClass::CompLeft: st.comp = CompMode::Left; break;
        case GClass::CompRight: st.comp = CompMode::Right; break;
        case GClass::ToolLenPlus:
        case GClass::ToolLenMinus: st.lengthComp = true; break;
        case GClass::ToolLenCancel: st.lengthComp = false; break;
        case GClass::Dwell: dwell = true; break;
        case GClass::DataSetting:
        case GClass::MacroCall: nonMotion = true; break;
        case GClass::Transform:
            st.transformed = b.has('X') || b.has('Y') || b.has('Z') || b.has('A') || b.has('C') || w.value != 52;
            st.pos.known.fill(false); nonMotion = true; break;
        case GClass::TransformCancel: st.transformed = false; st.pos.known.fill(false); break;
        case GClass::RotaryAlign:
            st.pos.known.fill(false); nonMotion = true; eff.unmodeledMotion = true; break;
        case GClass::UnmodeledMotion:
            st.unmodeledMotion = true; st.pos.known.fill(false); nonMotion = true; eff.unmodeledMotion = true; break;
        case GClass::RtcpOn: st.rtcp = true; st.pos.known.fill(false); break;
        case GClass::RtcpOff: st.rtcp = false; st.pos.known.fill(false); break;
        case GClass::TurningOn: st.turning = true; st.spindleChannel = 2; nonMotion = true; break;
        case GClass::TurningOff: st.turning = false; st.spindleChannel = 0; nonMotion = true; break;
        case GClass::SpindleLimit: spindleLimit = true; nonMotion = true; break;
        case GClass::SurfaceSpeed:
        case GClass::FixedSpeed: {
            const auto* s = b.find('S'); const int channel = s && s->channel > 0 ? s->channel : st.spindleChannel;
            st.spindles[channel].css = info->cls == GClass::SurfaceSpeed;
            break;
        }
        case GClass::Other: break;
        }
        if (info->group > 0) st.groups[info->group] = key;
    }
    if (sawDistanceWord) st.g91FromG28 = false;

    if (nonMotion && profile_.dialect().lexicalRules().extended) {
        eff.dataBlock = true;
        return eff;
    }

    // 2. F, S, H, D
    for (const auto& w : b.words) {
        if (!w.hasValue || !w.address.empty()) continue;
        switch (w.letter) {
        case 'F':
            if (!(dwell && profile_.dialect().feedDwellSeconds())) {
                st.f = w.value;
                st.hasF = feedSet = true;
            }
            break;
        case 'S':
            if (spindleLimit) break;
            if (profile_.dialect().lexicalRules().extended) {
                const int channel = w.channel > 0 ? w.channel : st.spindleChannel;
                st.spindles[channel].s = w.value; st.spindles[channel].hasS = true;
            } else { st.s = w.value; st.hasS = true; }
            break;
        case 'H': st.h = static_cast<int>(std::lround(w.value)); break;
        case 'D': st.d = static_cast<int>(std::lround(w.value)); break;
        default: break;
        }
    }

    // 3. M words and tool selection
    std::size_t tIdx = b.words.size();
    std::size_t m6Idx = b.words.size();
    for (std::size_t i = 0; i < b.words.size(); ++i) {
        const auto& w = b.words[i];
        if (!w.hasValue) continue;
        if (w.letter == 'T' && tIdx == b.words.size()) tIdx = i;
        if (w.letter != 'M') continue;
        if (std::fabs(w.value - std::round(w.value)) > 1e-9) continue;
        const int mv = static_cast<int>(std::lround(w.value));
        const auto mc = profile_.m(mv);
        if (!mc) continue;
        const std::string mkey = canonicalCode('M', w.value);
        if (profile_.dialect().lexicalRules().extended &&
            (*mc == MClass::SpindleCw || *mc == MClass::SpindleCcw || *mc == MClass::SpindleStop)) {
            const int channel = w.channel > 0 ? w.channel : st.spindleChannel;
            st.spindles[channel].direction = *mc == MClass::SpindleCw ? SpindleDir::Cw :
                *mc == MClass::SpindleCcw ? SpindleDir::Ccw : SpindleDir::Off;
            continue;
        }
        switch (*mc) {
        case MClass::ToolChange:
            if (m6Idx == b.words.size()) {
                m6Idx = i;
                st.spindle = SpindleDir::Off;
                for (auto& [channel, spindle] : st.spindles) spindle.direction = SpindleDir::Off;
            }
            break;
        case MClass::SpindleCw: st.spindle = SpindleDir::Cw; break;
        case MClass::SpindleCcw: st.spindle = SpindleDir::Ccw; break;
        case MClass::SpindleStop: st.spindle = SpindleDir::Off; break;
        case MClass::CoolantOn:
        case MClass::CoolantExtra: st.coolant.insert(mkey); break;
        case MClass::CoolantOff: st.coolant.clear(); break;
        case MClass::ProgramEnd: st.ended = true; break;
        case MClass::TurningCancel: st.turning = false; st.spindleChannel = 0; break;
        default: break;
        }
    }
    if (profile_.dialect().lexicalRules().extended) {
        const auto& spindle = st.spindles[st.spindleChannel];
        st.hasS = spindle.hasS; st.s = spindle.s; st.spindle = spindle.direction;
    }
    if (m6Idx != b.words.size()) {
        ToolEvent ev;
        ev.kind = ToolEvent::Kind::Change;
        ev.lineNo = b.lineNo;
        ev.previousTool = st.activeTool;
        ev.hadTWord = tIdx != b.words.size();
        ev.tool = ev.hadTWord ? static_cast<int>(std::lround(b.words[tIdx].value)) : st.preselectedTool;
        ev.tBeforeM6 = ev.hadTWord && tIdx < m6Idx;
        ev.cycleActive = !st.cycleCode.empty();
        ev.comp = st.comp;
        st.activeTool = ev.tool;
        st.preselectedTool = 0;
        eff.toolEvents.push_back(ev);
    } else if (tIdx != b.words.size()) {
        ToolEvent ev;
        ev.kind = ToolEvent::Kind::Preselect;
        ev.lineNo = b.lineNo;
        ev.previousTool = st.activeTool;
        ev.hadTWord = true;
        ev.tool = static_cast<int>(std::lround(b.words[tIdx].value));
        st.preselectedTool = ev.tool;
        eff.toolEvents.push_back(ev);
    }

    if (feedSet) {
        st.feedTool = st.activeTool;
        st.feedSetLine = b.lineNo;
    }

    // 4. motion
    std::array<std::optional<double>, kAxisCount> target{};
    std::array<bool, kAxisCount> mentioned{};
    bool anyAxis = false;
    for (const auto& w : b.words) {
        const int ai = profile_.dialect().axisIndex(w.letter);
        if (ai < 0 || !w.address.empty() || !w.hasValue) continue;
        target[static_cast<std::size_t>(ai)] = w.value;
        mentioned[static_cast<std::size_t>(ai)] = true;
        anyAxis = true;
    }

    const Position before = st.pos;
    if (st.unmodeledMotion && anyAxis && !home && !machineCoords && !dwell) {
        st.pos.known.fill(false); eff.unmodeledMotion = true;
        return eff;
    }

    if (home) {
        Segment seg = makeSegment(SegmentKind::Home, b.lineNo, before);
        seg.homeCode = homeCode;
        seg.intermediate = before;
        const auto* machine = profile_.machine();
        const auto offset = machine ? machine->wcsOffsets.find(st.wcs) : std::map<std::string, std::array<std::optional<double>, 3>>::const_iterator{};
        for (std::size_t a = 0; a < kAxisCount; ++a) {
            if (mentioned[a] || !anyAxis) {
                if (target[a] && !profile_.dialect().uncertainHomeIntermediate()) {
                    if (st.dist == DistanceMode::Absolute) { seg.intermediate.v[a] = *target[a]; seg.intermediate.known[a] = true; }
                    else if (seg.intermediate.known[a]) seg.intermediate.v[a] += *target[a];
                }
                if (profile_.dialect().uncertainHomeIntermediate()) {
                    seg.intermediate.known[a] = false; seg.homePathUnknown = true;
                }
                st.pos.known[a] = false;
                if (machine) {
                    const auto reference = homeCode == "G30" ? machine->axes[a].home30 : machine->axes[a].home;
                    const auto shift = a >= 3 || st.machineWcs ? std::optional<double>(0) :
                        (!st.offsetsUnknown && offset != machine->wcsOffsets.end() ? offset->second[a] : std::nullopt);
                    if (reference && shift) { st.pos.v[a] = *reference - *shift; st.pos.known[a] = true; }
                }
                seg.axes[a] = true;
            }
        }
        seg.to = st.pos;
        if (st.dist == DistanceMode::Incremental) st.g91FromG28 = true;
        eff.segments.push_back(std::move(seg));
    } else if (machineCoords && !nonMotion && !dwell && (anyAxis ||
               (st.hasMotion && (st.motion == GClass::ArcCw || st.motion == GClass::ArcCcw) &&
                (b.has('I') || b.has('J') || b.has('K') || b.has('R'))))) {
        auto kind = st.hasMotion && st.motion == GClass::Linear ? SegmentKind::Linear : SegmentKind::Rapid;
        if (st.hasMotion && (st.motion == GClass::ArcCw || st.motion == GClass::ArcCcw)) kind = SegmentKind::Arc;
        Segment seg = makeSegment(kind, b.lineNo, before);
        if (kind == SegmentKind::Arc) seg.arc = readArc(b, st, profile_.dialect());
        seg.machineCoordinates = true;
        seg.approximate = false;
        seg.axes = mentioned;
        const auto* machine = profile_.machine();
        seg.to = before;
        for (std::size_t a = 0; a < kAxisCount; ++a) {
            std::optional<double> shift;
            if (a >= 3 || st.machineWcs) shift = 0;
            else if (machine && !st.offsetsUnknown) {
                const auto it = machine->wcsOffsets.find(st.wcs);
                if (it != machine->wcsOffsets.end()) shift = it->second[a];
            }
            if (shift && before.known[a]) seg.from.v[a] += *shift; else seg.from.known[a] = false;
            seg.to.v[a] = seg.from.v[a]; seg.to.known[a] = seg.from.known[a];
            if (mentioned[a]) {
                seg.to.v[a] = *target[a]; seg.to.known[a] = true;
                st.pos.known[a] = shift.has_value();
                if (shift) st.pos.v[a] = *target[a] - *shift;
            }
        }
        // Preserve the historical pipeline when no machine data has been supplied.
        if (machine) {
            seg.workFrom = before;
            seg.workTo = st.pos;
        }
        if ((machine && machine->hasMotionData()) || profile_.dialect().lexicalRules().extended)
            eff.segments.push_back(std::move(seg));
    } else if (dwell) {
        Segment seg = makeSegment(SegmentKind::Dwell, b.lineNo, before);
        if (const Word* f = b.find('F'); profile_.dialect().feedDwellSeconds() && f != nullptr && f->hasValue) {
            seg.dwellSeconds = f->value;
        } else if (const Word* x = b.find('X'); x != nullptr && x->hasValue) {
            seg.dwellSeconds = x->value;
        } else if (const Word* p = b.find('P'); p != nullptr && p->hasValue) {
            // Fanuc: P without decimal point is milliseconds, with a point - seconds.
            seg.dwellSeconds = hasDecimalPoint(*p) ? p->value : p->value / 1000.0;
        }
        eff.segments.push_back(std::move(seg));
    } else if (setPosition) {
        for (std::size_t a = 0; a < kAxisCount; ++a) {
            if (target[a]) {
                st.pos.v[a] = *target[a];
                st.pos.known[a] = true;
            }
        }
    } else if (nonMotion) {
        // G10 / G65: axis words are data
    } else if (!st.cycleCode.empty() && (cycleInBlock || target[kAxisX] || target[kAxisY])) {
        executeCycle(b, target, eff);
    } else if (!st.cycleCode.empty()) {
        // cycle active, block only updates parameters (Z/R/Q/P)
        for (const auto& w : b.words) {
            if (!w.hasValue) continue;
            switch (w.letter) {
            case 'Z': st.cycle.z = w.value; st.cycle.hasZ = true; break;
            case 'R': st.cycle.r = w.value; st.cycle.hasR = true; break;
            case 'Q': st.cycle.q = w.value; st.cycle.hasQ = true; break;
            case 'P': st.cycle.p = w.value; st.cycle.hasP = true; break;
            default: break;
            }
        }
        // X/Y/B-only handled above; B alone still moves
        if (target[kAxisB]) {
            if (st.dist == DistanceMode::Absolute) { st.pos.v[kAxisB] = *target[kAxisB]; st.pos.known[kAxisB] = true; }
            else if (st.pos.known[kAxisB]) st.pos.v[kAxisB] += *target[kAxisB];
        }
    } else if (anyAxis) {
        for (std::size_t a = 0; a < kAxisCount; ++a) {
            if (!target[a]) continue;
            if (st.dist == DistanceMode::Absolute) {
                st.pos.v[a] = *target[a];
                st.pos.known[a] = true;
            } else if (st.pos.known[a]) {
                st.pos.v[a] += *target[a];
            }
        }
        if (!st.hasMotion) {
            eff.moveWithoutMotionCode = true;
        } else {
            SegmentKind kind = SegmentKind::Linear;
            if (st.motion == GClass::Rapid) kind = SegmentKind::Rapid;
            else if (st.motion == GClass::ArcCw || st.motion == GClass::ArcCcw) kind = SegmentKind::Arc;
            Segment seg = makeSegment(kind, b.lineNo, before);
            seg.axes = mentioned;
            if (kind == SegmentKind::Arc) seg.arc = readArc(b, st, profile_.dialect());
            eff.segments.push_back(std::move(seg));
        }
    } else if (st.hasMotion && (st.motion == GClass::ArcCw || st.motion == GClass::ArcCcw) &&
               (b.has('I') || b.has('J') || b.has('K') || b.has('R'))) {
        // full circle without end point: "G2 I10." -> start == end
        Segment seg = makeSegment(SegmentKind::Arc, b.lineNo, before);
        seg.arc = readArc(b, st, profile_.dialect());
        eff.segments.push_back(std::move(seg));
    }

    // cycle entered in this block but not executed yet (e.g. "G81 Z.. R.. F.." without X/Y) is handled
    // inside executeCycle; parameters of a G80 block are discarded above.
    return eff;
}

void ModalMachine::executeCycle(const Block& b, const std::array<std::optional<double>, kAxisCount>& target,
                                BlockEffect& eff) {
    ModalState& st = state_;
    for (const auto& w : b.words) {
        if (!w.hasValue) continue;
        switch (w.letter) {
        case 'Z': st.cycle.z = w.value; st.cycle.hasZ = true; break;
        case 'R': st.cycle.r = w.value; st.cycle.hasR = true; break;
        case 'Q': st.cycle.q = w.value; st.cycle.hasQ = true; break;
        case 'P': st.cycle.p = w.value; st.cycle.hasP = true; break;
        default: break;
        }
    }
    const Position start = st.pos;

    // positioning in the plane at the initial level
    bool moved = false;
    for (const int a : {kAxisX, kAxisY, kAxisB}) {
        const auto ai = static_cast<std::size_t>(a);
        if (!target[ai]) continue;
        moved = true;
        if (st.dist == DistanceMode::Absolute) {
            st.pos.v[ai] = *target[ai];
            st.pos.known[ai] = true;
        } else if (st.pos.known[ai]) {
            st.pos.v[ai] += *target[ai];
        }
    }
    if (moved) {
        Segment seg = makeSegment(SegmentKind::Rapid, b.lineNo, start);
        seg.axes = {target[kAxisX].has_value(), target[kAxisY].has_value(), false, target[kAxisB].has_value()};
        eff.segments.push_back(std::move(seg));
    }

    if (!st.cycle.hasZ || !st.cycle.hasR) {
        eff.cycleIncomplete = true;
        return;
    }
    CycleData cd;
    cd.code = st.cycleCode;
    cd.zInitKnown = st.pos.known[kAxisZ];
    const double zInit = st.pos.known[kAxisZ] ? st.pos.v[kAxisZ] : 0.0;
    if (st.dist == DistanceMode::Absolute) {
        cd.zR = st.cycle.r;
        cd.zBottom = st.cycle.z;
    } else {
        cd.zR = zInit + st.cycle.r;
        cd.zBottom = cd.zR + st.cycle.z;
    }
    cd.zInit = cd.zInitKnown ? zInit : cd.zR;
    cd.q = st.cycle.q;
    cd.p = st.cycle.p;
    cd.hasQ = st.cycle.hasQ;
    cd.hasP = st.cycle.hasP;
    cd.returnToInitial = st.cycleReturnInitial;

    Segment seg = makeSegment(SegmentKind::CycleHole, b.lineNo, st.pos);
    seg.cycle = cd;
    seg.axes = {target[kAxisX].has_value(), target[kAxisY].has_value(), target[kAxisZ].has_value(), false};
    // after the cycle the tool stays at the initial level (G98) or at the R plane (G99)
    st.pos.v[kAxisZ] = cd.returnToInitial ? cd.zInit : cd.zR;
    st.pos.known[kAxisZ] = true;
    seg.to = st.pos;
    eff.segments.push_back(std::move(seg));
}

} // namespace gca
