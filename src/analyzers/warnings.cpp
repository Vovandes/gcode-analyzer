#include "gca/analyzers/warnings.hpp"

#include "gca/motion.hpp"
#include "gca/text_util.hpp"

#include <algorithm>
#include <cmath>

namespace gca {

namespace {

constexpr double kArcRadiusTolerance = 0.02;    // mm; Fanuc alarms around 0.01..0.05 depending on parameter

std::string trimmed(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

} // namespace

std::string WarningsAnalyzer::lineText(std::size_t line) const {
    if (line == 0 || line > program_.blocks.size()) return {};
    return trimmed(program_.blocks[line - 1].raw);
}

void WarningsAnalyzer::emit(std::size_t line, const char* code, std::string message) {
    Warning w;
    w.line = line;
    w.text = lineText(line);
    w.code = code;
    w.message = std::move(message);
    warnings_.push_back(std::move(w));
}

void WarningsAnalyzer::emitOncePerOp(std::size_t line, const char* code, std::string message) {
    const std::string key = std::string(code) + "#" + std::to_string(op_);
    if (!once_.insert(key).second) return;
    emit(line, code, std::move(message));
}

void WarningsAnalyzer::onBlock(const BlockContext& ctx) {
    const Block& b = ctx.block;
    const IDialect& dialect = ctx.profile.dialect();
    std::string upperRaw = b.raw;
    for (auto& c : upperRaw) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

    if (!dialect.lexicalRules().extended &&
        (b.raw.find('@') != std::string::npos || upperRaw.find("GDEF") != std::string::npos || upperRaw.find("GCALL") != std::string::npos) &&
        once_.insert(warn::kDialectHint).second)
        emit(b.lineNo, warn::kDialectHint, "похоже на GNC62, используйте --dialect gnc62");
    if (dialect.lexicalRules().extended) {
        if ((ctx.effect.unmodeledMotion || ctx.after.feedMode == FeedMode::Unsupported) && once_.insert("unmodeled").second)
            emit(b.lineNo, warn::kApproximate, "Документированный режим требует дополнительной модели; его внутренние перемещения или подача по времени не рассчитаны");
        if (b.control || std::any_of(b.words.begin(), b.words.end(), [](const Word& w) { return w.expression; })) {
            if (once_.insert("control").second)
                emit(b.lineNo, warn::kApproximate, "Макросы, GOTO и подпрограммы не исполняются: анализ по порядку строк, время и траектория неполные");
        }
        if (dialect.requiresMinuteFeed(b.callName) && ctx.before.feedMode != FeedMode::PerMinute)
            emit(b.lineNo, warn::kMeasureFeedMode, "prb_xyz требует G94 (подача в минуту)");
        if ((ctx.after.transformed || ctx.after.rtcp || ctx.after.turning) && once_.insert("geometry").second)
            emit(b.lineNo, warn::kApproximate, "Локальная СК, RTCP и токарная геометрия показаны номинально; преобразование и диаметр X не рассчитаны, абсолютные пределы XYZ не проверяются");
        if (ctx.effect.definitionCall) {
            const auto& call = *ctx.effect.definitionCall;
            const auto d = dialect.definition(call.number);
            if (!call.complete) emit(b.lineNo, warn::kMeasureAddress, "GCALL без завершённого GDEF ... END");
            if (!d) emit(b.lineNo, warn::kUnknownCode, "неизвестный цикл GDEF" + std::to_string(call.number));
            else {
                for (const auto& address : d->required) {
                    const auto p = call.parameters.find(address);
                    if (p == call.parameters.end() || (!p->second.hasValue && !p->second.expression))
                        emit(b.lineNo, warn::kMeasureAddress, "GDEF" + std::to_string(call.number) + ": не задан обязательный адрес " + address);
                }
                for (const auto& address : d->positive) {
                    const auto p = call.parameters.find(address);
                    if (p != call.parameters.end() && p->second.hasValue && p->second.value <= 0)
                        emit(b.lineNo, warn::kMeasureAddress, "GDEF" + std::to_string(call.number) + ": адрес " + address + " должен быть положительным");
                }
                if (d->requiresG94 && ctx.before.feedMode != FeedMode::PerMinute)
                    emit(b.lineNo, warn::kMeasureFeedMode, "GDEF" + std::to_string(call.number) + ": подпрограмма касания prb_xyz требует G94");
                if (!d->defect.empty()) emit(b.lineNo, warn::kMeasureDefect, "GDEF" + std::to_string(call.number) + ": " + d->defect + "; проверить установленную версию цикла");
                if (d->measuring) {
                    std::string effects = "GDEF" + std::to_string(call.number) + ": внутренние ходы и время измерения не рассчитаны; конечная позиция неизвестна";
                    if (!d->wcsAddress.empty()) effects += "; СК из " + d->wcsAddress + " не восстанавливается (после: " + (ctx.after.wcs.empty() ? "неизвестна" : ctx.after.wcs) + ")";
                    if (d->homeZ) effects += "; цикл выполняет G28 Z0";
                    if (d->writesWcs) effects += "; записывает смещения СК, прежние смещения больше не используются";
                    emit(b.lineNo, warn::kMeasureEffects, effects);
                } else emit(b.lineNo, warn::kApproximate, "GDEF" + std::to_string(call.number) + ": внутренние движения подпрограммы не рассчитаны");
            }
        }
    }

    for (const auto& w : b.words) {
        if (!w.hasValue || !w.address.empty()) continue;
        if (ctx.effect.dataBlock && w.letter != 'G' && w.letter != 'M') continue;
        if (w.letter == 'G') {
            const auto info = ctx.profile.g(w.value);
            const std::string key = canonicalCode('G', w.value);
            if (!info) {
                const auto it = unknown_.find(key);
                if (it == unknown_.end()) {
                    unknown_[key] = {warnings_.size(), 1};
                    emit(b.lineNo, warn::kUnknownCode, "неизвестный G-код " + displayCode(key));
                } else {
                    ++it->second.second;
                }
                continue;
            }
            if (info->cls == GClass::Inch) emit(b.lineNo, warn::kInch, "G20 (дюймы): не поддерживается, размеры считаются в мм");
            if (info->cls == GClass::Wcs) {
                if (ctx.after.wcsP == 0) {
                    emit(b.lineNo, warn::kWcsZero, "нулевое смещение " + ctx.after.wcs + " вместо G54");
                } else if (ctx.after.wcs == "G54.1") {
                    emit(b.lineNo, warn::kWcsZero, "G54.1 без номера P");
                }
                if (opHasMotion_ && ctx.before.wcsSelected && ctx.before.wcs != ctx.after.wcs) {
                    emit(b.lineNo, warn::kWcsChange,
                         "смена СК " + ctx.before.wcs + " -> " + ctx.after.wcs + " внутри работы инструмента");
                }
            }
            if (info->cls == GClass::ToolLenPlus || info->cls == GClass::ToolLenMinus) {
                const Word* h = b.find('H');
                if (h == nullptr || !h->hasValue) {
                    emit(b.lineNo, warn::kG43NoH, "G43 без H");
                } else {
                    const int hv = static_cast<int>(std::lround(h->value));
                    if (ctx.after.activeTool == 0) {
                        emit(b.lineNo, warn::kHNotT, "G43 H" + std::to_string(hv) + " до первой смены инструмента");
                    } else if (hv != ctx.after.activeTool) {
                        emit(b.lineNo, warn::kHNotT,
                             "H" + std::to_string(hv) + " не совпадает с номером инструмента T" +
                                 std::to_string(ctx.after.activeTool));
                    }
                }
            }
        } else if (w.letter == 'M') {
            const bool integral = std::fabs(w.value - std::round(w.value)) < 1e-9;
            const bool known = integral && ctx.profile.m(static_cast<int>(std::lround(w.value))).has_value();
            if (!known) {
                const std::string key = canonicalCode('M', w.value);
                const auto it = unknown_.find(key);
                if (it == unknown_.end()) {
                    unknown_[key] = {warnings_.size(), 1};
                    emit(b.lineNo, warn::kUnknownCode, "неизвестный M-код " + displayCode(key));
                } else {
                    ++it->second.second;
                }
            }
        } else if (!dialect.isKnownAddress(w.letter)) {
            emit(b.lineNo, warn::kUnparsed, std::string("неизвестный адрес '") + w.letter + "' (слово " + w.rawText + ")");
        }
    }

    if (ctx.effect.moveWithoutMotionCode) {
        emit(b.lineNo, warn::kNoMotionCode, "координаты без G0/G1/G2/G3: режим перемещения не задан");
    }
    if (ctx.effect.cycleIncomplete) {
        emit(b.lineNo, warn::kCycleIncomplete, "цикл " + ctx.after.cycleCode + " без Z или R");
    }

    if (ctx.after.ended && !ctx.before.ended && !endChecked_) {
        endChecked_ = true;
        if (ctx.after.g91FromG28 && ctx.after.dist == DistanceMode::Incremental) {
            emit(b.lineNo, warn::kG91AfterG28, "программа закончена с активным G91 (после G28)");
        }
        if (!ctx.after.cycleCode.empty()) {
            emit(b.lineNo, warn::kCycleActive, "программа закончена, цикл " + ctx.after.cycleCode + " не отменён G80");
        }
    }
}

void WarningsAnalyzer::onSegment(const Segment& seg, const BlockContext& ctx) {
    if (seg.homePathUnknown && once_.insert("homePath").second)
        emit(seg.lineNo, warn::kApproximate, "G28 GNC62: трактовка промежуточной точки требует проверки на станке; путь и время возврата не рассчитаны");
    if (seg.kind == SegmentKind::Home || seg.kind == SegmentKind::Dwell) return;
    const std::size_t line = seg.lineNo;

    if (seg.wcs.empty() && !seg.machineCoordinates && once_.insert(warn::kNoWcs).second) {
        emit(line, warn::kNoWcs, "перемещение до назначения рабочей СК (G54..G59 / G54.1)");
    }
    opHasMotion_ = true;

    if (ctx.after.g91FromG28) {
        if (!g91Reported_) {
            g91Reported_ = true;
            emit(line, warn::kG91AfterG28, "перемещение при G91, оставшемся активным после G28");
        }
    } else {
        g91Reported_ = false;
    }

    if (seg.kind == SegmentKind::Rapid && seg.comp != CompMode::Off) {
        emitOncePerOp(line, warn::kCompRapid, "коррекция радиуса G41/G42 активна при G0");
    }

    if (seg.kind == SegmentKind::Linear || seg.kind == SegmentKind::Arc || seg.kind == SegmentKind::CycleHole) {
        if (seg.spindle == SpindleDir::Off) {
            emitOncePerOp(line, warn::kNoSpindle, "рабочий ход при невключённом шпинделе (нет M3/M4)");
        } else if (!seg.surfaceSpeed && (!seg.hasS || !(seg.s > 0.0))) {
            emitOncePerOp(line, warn::kNoSpindle, "рабочий ход без заданных оборотов S");
        }
        if (!seg.hasF || !(seg.feed > 0.0)) {
            emitOncePerOp(line, warn::kNoFeed, "рабочий ход без подачи F");
        }
    }

    if (seg.kind == SegmentKind::Arc) {
        const ArcResult a = computeSegmentArc(seg);
        if (!a.ok) {
            if (a.error != "unknown start point") emit(line, warn::kArc, "дуга не рассчитана: " + a.error);
        } else if (a.radiusMismatch > kArcRadiusTolerance) {
            emit(line, warn::kArc, "радиусы начала и конца дуги различаются на " + trimmedNumber(a.radiusMismatch) + " мм");
        }
    }
}

void WarningsAnalyzer::onToolChange(const ToolEvent& ev, const BlockContext&) {
    if (ev.kind != ToolEvent::Kind::Change) return;
    if (ev.comp != CompMode::Off) emit(ev.lineNo, warn::kChangeComp, "смена инструмента без отмены коррекции радиуса (G40)");
    if (ev.cycleActive) emit(ev.lineNo, warn::kCycleActive, "смена инструмента: цикл не отменён G80");
    if (ev.tool == 0) emit(ev.lineNo, warn::kChangeNoTool, "M6 без номера инструмента и без предвыбора T");
    ++op_;
    opHasMotion_ = false;
}

void WarningsAnalyzer::finish(Report& report) {
    for (const auto& [code, info] : unknown_) {
        if (info.second > 1) {
            warnings_[info.first].message += " (всего вхождений: " + std::to_string(info.second) + ")";
        }
    }
    for (const auto& d : program_.diagnostics) {
        Warning w;
        w.line = d.lineNo;
        w.text = lineText(d.lineNo);
        w.code = warn::kUnparsed;
        w.message = "неразобранный текст: " + d.message;
        warnings_.push_back(std::move(w));
    }
    std::stable_sort(warnings_.begin(), warnings_.end(),
                     [](const Warning& a, const Warning& b) { return a.line < b.line; });
    report.warnings = std::move(warnings_);
}

} // namespace gca
