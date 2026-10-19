#include "gca/analyzers/format.hpp"

#include "gca/motion.hpp"
#include "gca/text_util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace gca {

namespace {

using Lines = std::vector<std::string>;

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lowerChar(char c) { return std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(c)))); }

std::string count(std::size_t n) { return std::to_string(n); }

/// "n из m"
std::string ofTotal(std::size_t n, std::size_t m) { return count(n) + " из " + count(m); }

/// Most frequent entries of a counter map, descending.
std::vector<std::pair<std::string, std::size_t>> top(const std::map<std::string, std::size_t>& m, std::size_t limit) {
    std::vector<std::pair<std::string, std::size_t>> v(m.begin(), m.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (v.size() > limit) v.resize(limit);
    return v;
}

std::string quote(const std::string& s) { return "`" + s + "`"; }

bool isAxisLetter(char c) { return c == 'X' || c == 'Y' || c == 'Z' || c == 'B'; }

std::string joinList(const std::vector<std::string>& v, const std::string& sep) {
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i != 0) r += sep;
        r += v[i];
    }
    return r;
}

const char* zoneName(int z) {
    switch (z) {
    case 0: return "заголовок (до первой смены)";
    case 1: return "после смены, до первого рабочего хода";
    case 2: return "рабочие ходы";
    case 3: return "завершение операции (между операциями)";
    default: return "конец программы";
    }
}

} // namespace

void FormatAnalyzer::onBlock(const BlockContext& ctx) {
    if (!profile_) profile_ = ctx.profile;
    Rec r;
    r.block = ctx.block;
    r.activeTool = ctx.after.activeTool;
    r.spindleOffBefore = ctx.before.spindle == SpindleDir::Off;
    recs_.push_back(std::move(r));

    const Block& b = ctx.block;
    const ModalState& before = ctx.before;
    bool hasSegment = false;
    bool cutting = false;
    for (const auto& s : ctx.effect.segments) {
        if (s.kind == SegmentKind::Rapid || s.kind == SegmentKind::Linear || s.kind == SegmentKind::Arc ||
            s.kind == SegmentKind::CycleHole) {
            hasSegment = true;
        }
        if (s.kind == SegmentKind::Linear || s.kind == SegmentKind::Arc || s.kind == SegmentKind::CycleHole) cutting = true;
    }

    // repetition of modal G words
    for (const auto& w : b.words) {
        if (w.letter != 'G' || !w.hasValue) continue;
        const auto info = ctx.profile.g(w.value);
        if (!info || info->group <= 0) continue;
        if (info->cls == GClass::Rapid || info->cls == GClass::Linear || info->cls == GClass::ArcCw || info->cls == GClass::ArcCcw) {
            continue; // motion codes are counted separately
        }
        const std::string key = canonicalCode('G', w.value);
        auto& e = mod_.gRepeat[key];
        ++e.second;
        const auto it = before.groups.find(info->group);
        if (it != before.groups.end() && it->second == key) ++e.first;
    }

    // spindle M3/M4 and S repetition
    for (const auto& w : b.words) {
        if (!w.hasValue) continue;
        if (w.letter == 'M' && (w.value == 3.0 || w.value == 4.0)) {
            ++mod_.spindleTotal;
            if ((w.value == 3.0 && before.spindle == SpindleDir::Cw) || (w.value == 4.0 && before.spindle == SpindleDir::Ccw)) {
                ++mod_.spindleRepeated;
            }
        }
        if (w.letter == 'S') {
            ++mod_.sTotal;
            if (before.hasS && std::fabs(before.s - w.value) < 1e-9) ++mod_.sRepeated;
        }
    }

    if (!hasSegment) return;
    ++mod_.motionBlocks;

    // explicit motion code vs omitted
    const Word* motionWord = nullptr;
    for (const auto& w : b.words) {
        if (w.letter != 'G' || !w.hasValue) continue;
        const auto info = ctx.profile.g(w.value);
        if (info && (info->cls == GClass::Rapid || info->cls == GClass::Linear || info->cls == GClass::ArcCw || info->cls == GClass::ArcCcw)) {
            motionWord = &w;
        }
    }
    if (motionWord == nullptr) {
        ++mod_.motionCodeOmitted;
    } else {
        const auto info = ctx.profile.g(motionWord->value);
        if (before.hasMotion && info->cls == before.motion && before.cycleCode.empty()) ++mod_.motionCodeExplicitSame;
        else ++mod_.motionCodeExplicitChange;
    }

    // coordinates equal to the current position
    for (const auto& w : b.words) {
        if (!w.hasValue || !isAxisLetter(w.letter)) continue;
        const int ai = ctx.profile.dialect().axisIndex(w.letter);
        if (ai < 0) continue;
        const auto i = static_cast<std::size_t>(ai);
        if (before.dist == DistanceMode::Absolute && before.pos.known[i] && std::fabs(before.pos.v[i] - w.value) < 1e-9) {
            ++mod_.axisRepeated[w.letter];
        } else {
            ++mod_.axisChanged[w.letter];
        }
    }

    // F: only in cutting blocks
    if (cutting) {
        ++mod_.cutBlocks;
        const Word* f = b.find('F');
        if (f == nullptr) ++mod_.fOmitted;
        else if (before.hasF && std::fabs(before.f - f->value) < 1e-9) ++mod_.fSame;
        else ++mod_.fChange;
    }
}

void FormatAnalyzer::onSegment(const Segment& seg, const BlockContext&) {
    if (recs_.empty()) return;
    if (seg.kind == SegmentKind::Linear || seg.kind == SegmentKind::Arc || seg.kind == SegmentKind::CycleHole) {
        recs_.back().cut = true;
    }
    if (seg.kind != SegmentKind::Arc) return;
    ++arcs_.total;
    if (seg.arc.r && !seg.arc.i && !seg.arc.j && !seg.arc.k) ++arcs_.byRadius;
    else ++arcs_.byOffsets;
    for (const auto& v : {seg.arc.i, seg.arc.j, seg.arc.k}) {
        if (v && *v == 0.0) ++arcs_.explicitZeroOffset;
    }
    const ArcResult a = computeSegmentArc(seg);
    if (!a.ok) {
        ++arcs_.failed;
        return;
    }
    const bool helical = a.helixHeight > 1e-9;
    if (helical) ++arcs_.helical;
    if (a.fullCircle) {
        ++arcs_.fullCircle;
        if (helical) ++arcs_.helicalFullCircle;
    }
}

void FormatAnalyzer::onToolChange(const ToolEvent& ev, const BlockContext&) {
    if (recs_.empty()) return;
    Rec& r = recs_.back();
    if (ev.kind == ToolEvent::Kind::Change) {
        r.hasM6 = true;
        r.tool = ev.tool;
    } else {
        r.preselect = true;
    }
}

// ---------------------------------------------------------------------------------------------
// zones and abstraction
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::computeZones() {
    std::vector<std::size_t> m6;
    for (std::size_t i = 0; i < recs_.size(); ++i) {
        if (recs_[i].hasM6) m6.push_back(i);
    }
    for (auto& r : recs_) r.zone = Zone::Header;
    for (std::size_t k = 0; k < m6.size(); ++k) {
        const std::size_t start = m6[k];
        const std::size_t end = (k + 1 < m6.size()) ? m6[k + 1] : recs_.size();
        std::optional<std::size_t> firstCut;
        std::optional<std::size_t> lastCut;
        for (std::size_t i = start; i < end; ++i) {
            if (!recs_[i].cut) continue;
            if (!firstCut) firstCut = i;
            lastCut = i;
        }
        const bool last = (k + 1 == m6.size());
        if (!firstCut) {
            for (std::size_t i = start; i < end; ++i) recs_[i].zone = Zone::Setup;
            continue;
        }
        for (std::size_t i = start; i <= *firstCut; ++i) recs_[i].zone = Zone::Setup;
        for (std::size_t i = *firstCut + 1; i <= *lastCut; ++i) recs_[i].zone = Zone::Cut;
        for (std::size_t i = *lastCut + 1; i < end; ++i) recs_[i].zone = last ? Zone::End : Zone::Teardown;
    }
}

std::string FormatAnalyzer::abstractBlock(const Block& b, const std::string& commentLabel) const {
    if (b.isPercent) return "%";
    if (b.words.empty() && b.comments.empty()) return {};

    bool literalAxes = false;
    bool literalPQ = false;
    for (const auto& w : b.words) {
        if (w.letter != 'G' || !w.hasValue) continue;
        const std::string key = canonicalCode('G', w.value);
        if (key == "G28" || key == "G30" || key == "G53" || key == "G92") literalAxes = true;
        if (key == "G5.1" || key == "G5" || key == "G8") literalPQ = true;
    }

    struct Repl { std::size_t pos; std::size_t len; std::string text; };
    std::vector<Repl> repls;
    for (const auto& w : b.words) {
        if (!w.hasValue) continue;
        std::string ph;
        const std::string low = lowerChar(w.letter);
        switch (w.letter) {
        case 'T': ph = "T<t>"; break;
        case 'N': ph = "N<n>"; break;
        case 'H': case 'D': case 'S': case 'F':
            if (w.value != 0.0) ph = std::string(1, w.letter) + "<" + low + ">";
            break;
        case 'X': case 'Y': case 'Z': case 'B':
            if (!literalAxes) ph = std::string(1, w.letter) + "<" + low + ">";
            break;
        case 'I': case 'J': case 'K': case 'R':
            ph = std::string(1, w.letter) + "<" + low + ">";
            break;
        case 'P': case 'Q':
            if (!literalPQ) ph = std::string(1, w.letter) + "<" + low + ">";
            break;
        default: break;
        }
        if (!ph.empty()) repls.push_back({w.pos, w.rawText.size(), ph});
    }
    for (const auto& c : b.comments) {
        std::string t;
        if (c.style == '(') {
            const bool lead = !c.text.empty() && c.text.front() == ' ';
            const bool trail = !c.text.empty() && c.text.back() == ' ';
            t = std::string(lead ? "( " : "(") + commentLabel + (trail ? " )" : ")");
        } else {
            t = "; " + commentLabel;
        }
        repls.push_back({c.pos, c.raw.size(), t});
    }
    std::sort(repls.begin(), repls.end(), [](const Repl& a, const Repl& b2) { return a.pos > b2.pos; });
    std::string out = b.raw;
    for (const auto& r : repls) out.replace(r.pos, r.len, r.text);
    // trailing blanks are not a property of the block text
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    return out;
}

Lines FormatAnalyzer::abstractRange(std::size_t first, std::size_t last, std::size_t m6Index) const {
    Lines lines;
    for (std::size_t i = first; i <= last && i < recs_.size(); ++i) {
        const Block& b = recs_[i].block;
        if (b.isBlank()) continue;
        // comments before the M6 describe the next operation, after it - the tool
        const std::string label = i < m6Index ? "<комментарий следующей операции>" : "<описание инструмента>";
        lines.push_back(abstractBlock(b, label));
    }
    return lines;
}

std::string FormatAnalyzer::nearestAbstract(std::size_t from, int step) const {
    for (long i = static_cast<long>(from) + step; i >= 0 && i < static_cast<long>(recs_.size()); i += step) {
        const Block& b = recs_[static_cast<std::size_t>(i)].block;
        if (!b.isBlank()) return abstractBlock(b, "<комментарий>");
    }
    return "(нет)";
}

// ---------------------------------------------------------------------------------------------
// section 1: structure
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildStructure(Report& report) {
    CheatSection sec;
    sec.title = "1. Структура программы";
    Lines& L = sec.lines;

    std::vector<std::size_t> m6;
    for (std::size_t i = 0; i < recs_.size(); ++i) {
        if (recs_[i].hasM6) m6.push_back(i);
    }

    // --- start
    std::size_t headerEnd = m6.empty() ? recs_.size() : m6.front();
    std::size_t percentLines = 0;
    std::size_t firstPercent = 0;
    std::size_t lastPercent = 0;
    for (const auto& r : recs_) {
        if (r.block.isPercent) {
            if (percentLines == 0) firstPercent = r.block.lineNo;
            lastPercent = r.block.lineNo;
            ++percentLines;
        }
    }
    L.push_back("Начало программы (до первой смены инструмента), строки показаны как в файле:");
    {
        std::size_t shown = 0;
        std::size_t toolListRun = 0;
        std::string runExample;
        const auto flushRun = [&]() {
            if (toolListRun > 0) {
                L.push_back("    " + runExample + "    [записей такого вида подряд: " + count(toolListRun) +
                            "; список с повторами, по одной записи на операцию]");
                toolListRun = 0;
            }
        };
        for (std::size_t i = 0; i < headerEnd; ++i) {
            const Block& b = recs_[i].block;
            if (b.isBlank()) continue;
            bool isToolEntry = false;
            if (b.words.empty() && b.comments.size() == 1) {
                const std::string t = trim(b.comments[0].text);
                if (t.size() > 2 && t[0] == 'T' && std::isdigit(static_cast<unsigned char>(t[1]))) {
                    std::size_t k = 1;
                    while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
                    isToolEntry = k < t.size() && t[k] == ':';
                }
            }
            if (isToolEntry) {
                if (toolListRun == 0) runExample = b.raw;
                ++toolListRun;
                continue;
            }
            flushRun();
            if (shown++ < 40) L.push_back("    " + trim(b.raw));
        }
        flushRun();
    }
    {
        std::string facts;
        const Block* o = nullptr;
        for (const auto& r : recs_) {
            if (r.block.find('O') != nullptr) { o = &r.block; break; }
        }
        if (o != nullptr) {
            const Word* ow = o->find('O');
            facts += std::string("Строка O ") + (ow->hasValue ? "с номером (" + ow->rawText + ")" : "без номера") +
                     " в строке " + count(o->lineNo) + ". ";
        } else {
            facts += "Строки O нет. ";
        }
        if (percentLines == 0) {
            facts += "Символ % не используется.";
        } else {
            facts += "Символ % встречается " + count(percentLines) + " раз (" + (lastPercent != firstPercent ? "строки " + count(firstPercent) + " .. " + count(lastPercent) : "строка " + count(firstPercent)) + ")" +
                     (firstPercent == 1 ? ", в начале файла" : ", не в начале файла") +
                     (!recs_.empty() && lastPercent == recs_.back().block.lineNo ? ", последняя строка файла." : ".");
        }
        L.push_back(facts);
    }

    // --- tool change
    L.push_back("");
    if (m6.empty()) {
        L.push_back("Смены инструмента (M6) в программе нет.");
    } else {
        // first change
        {
            std::size_t end = m6.size() > 1 ? m6[1] : recs_.size();
            std::size_t last = m6[0];
            for (std::size_t i = m6[0]; i < end; ++i) {
                last = i;
                if (recs_[i].cut) break;
            }
            L.push_back("Первая смена инструмента и подход (строки " + count(recs_[m6[0]].block.lineNo) + ".." +
                        count(recs_[last].block.lineNo) + "), значения заменены подстановками:");
            for (const auto& line : abstractRange(m6[0], last, m6[0])) L.push_back("    " + line);
        }
        // following changes: end of the previous operation + start of this one
        std::vector<Lines> transitions;
        std::vector<std::size_t> m6Pos;
        for (std::size_t k = 1; k < m6.size(); ++k) {
            const std::size_t prevStart = m6[k - 1];
            std::size_t lastCut = prevStart;
            for (std::size_t i = prevStart; i < m6[k]; ++i) {
                if (recs_[i].cut) lastCut = i;
            }
            const std::size_t end = (k + 1 < m6.size()) ? m6[k + 1] : recs_.size();
            std::size_t firstCut = end > 0 ? end - 1 : 0;
            for (std::size_t i = m6[k]; i < end; ++i) {
                if (recs_[i].cut) { firstCut = i; break; }
            }
            const std::size_t first = lastCut + 1;
            if (first > firstCut) continue;
            transitions.push_back(abstractRange(first, firstCut, m6[k]));
            std::size_t pos = 0;
            std::size_t n = 0;
            for (std::size_t i = first; i <= firstCut; ++i) {
                if (recs_[i].block.isBlank()) continue;
                if (i == m6[k]) pos = n;
                ++n;
            }
            m6Pos.push_back(pos);
        }
        if (transitions.empty()) {
            L.push_back("Остальных смен нет (инструмент один).");
        } else {
            const std::size_t n = transitions.size();
            std::map<std::string, std::size_t> freq;
            std::vector<std::string> firstSeen;
            std::set<std::string> distinctVariants;
            for (const auto& t : transitions) {
                std::set<std::string> uniq(t.begin(), t.end());
                for (const auto& line : uniq) {
                    if (freq[line]++ == 0) firstSeen.push_back(line);
                }
                distinctVariants.insert(joinList(t, "\n"));
            }
            // backbone = the real transition whose lines are the most typical
            std::size_t best = 0;
            std::size_t bestScore = 0;
            for (std::size_t i = 0; i < transitions.size(); ++i) {
                std::size_t score = 0;
                for (const auto& line : transitions[i]) score += freq[line];
                if (score > bestScore) { bestScore = score; best = i; }
            }
            Lines backbone;
            for (const auto& line : transitions[best]) {
                if (freq[line] * 2 >= n) backbone.push_back(line);
            }
            std::size_t m6InBackbone = backbone.size();
            for (std::size_t i = 0; i < backbone.size(); ++i) {
                if (backbone[i].find("M6") != std::string::npos) { m6InBackbone = i; break; }
            }
            L.push_back("");
            L.push_back("Шаблон смены инструмента между операциями: от последнего рабочего хода предыдущей операции");
            L.push_back("до первого рабочего хода следующей. Смен: " + count(n) + ", различных вариантов записи: " +
                        count(distinctVariants.size()) + ". В скобках: в скольких сменах строка встречается.");
            L.push_back("Подстановки: <t> номер T, <h> <d> <s> <f> <n> значения, <x> <y> <z> координаты. Строка смены помечена стрелкой.");
            for (std::size_t i = 0; i < backbone.size(); ++i) {
                const bool isM6 = (i == m6InBackbone);
                L.push_back(std::string("  ") + (isM6 ? "->" : "  ") + " [" + count(freq[backbone[i]]) + "/" + count(n) + "] " + backbone[i]);
            }
            std::vector<std::string> extras;
            for (const auto& line : firstSeen) {
                if (std::find(backbone.begin(), backbone.end(), line) == backbone.end()) extras.push_back(line);
            }
            std::stable_sort(extras.begin(), extras.end(), [&freq](const std::string& a2, const std::string& b2) { return freq[a2] > freq[b2]; });
            if (!extras.empty()) {
                L.push_back("Строки, не вошедшие в основной шаблон (есть в части смен, порядок может отличаться):");
                std::size_t shown = 0;
                for (const auto& line : extras) {
                    if (shown++ >= 12) break;
                    L.push_back("       [" + count(freq[line]) + "/" + count(n) + "] " + line);
                }
                if (extras.size() > 12) L.push_back("       ... и ещё строк: " + count(extras.size() - 12));
            }
        }
    }

    // --- tool change spelling and preselect
    {
        std::size_t tFirst = 0, mFirst = 0, mOnly = 0, pre = 0;
        for (const auto& r : recs_) {
            if (r.preselect) ++pre;
            if (!r.hasM6) continue;
            const Word* t = r.block.find('T');
            if (t == nullptr) { ++mOnly; continue; }
            const Word* m = nullptr;
            for (const auto& w : r.block.words) {
                if (w.letter == 'M' && w.hasValue && w.value == 6.0) { m = &w; break; }
            }
            if (m != nullptr && t->pos < m->pos) ++tFirst;
            else ++mFirst;
        }
        L.push_back("");
        Lines parts;
        if (tFirst > 0) parts.push_back("T перед M6 (`T<t>M6`): " + count(tFirst));
        if (mFirst > 0) parts.push_back("M6 перед T (`M6T<t>`): " + count(mFirst));
        if (mOnly > 0) parts.push_back("M6 без T: " + count(mOnly));
        if (!m6.empty()) {
            L.push_back("Запись смены инструмента: " + joinList(parts, "; ") + ".");
            L.push_back("Предвыбор следующего инструмента: отдельный кадр `T<t>` после смены, " + count(pre) +
                        " раз (смен " + count(m6.size()) + (pre + 1 >= m6.size() ? ", перед каждой сменой, кроме первой)." : ")."));
        }
    }

    // --- end
    L.push_back("");
    {
        std::size_t endFirst = recs_.size();
        for (std::size_t i = 0; i < recs_.size(); ++i) {
            if (recs_[i].zone == Zone::End) { endFirst = i; break; }
        }
        if (endFirst == recs_.size()) {
            L.push_back("Конец программы: участок завершения после последней операции не найден.");
        } else {
            L.push_back("Конец программы (после последнего рабочего хода, строка " + count(recs_[endFirst].block.lineNo) + " и далее):");
            std::size_t shown = 0;
            for (std::size_t i = endFirst; i < recs_.size() && shown < 20; ++i) {
                if (recs_[i].block.isBlank()) continue;
                L.push_back("    " + abstractBlock(recs_[i].block, "<комментарий>"));
                ++shown;
            }
        }
    }

    report.cheatSheet.push_back(std::move(sec));
}

// ---------------------------------------------------------------------------------------------
// section 2: block format
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildBlockFormat(Report& report) {
    CheatSection sec;
    sec.title = "2. Формат кадра";
    Lines& L = sec.lines;

    // N numbering
    {
        std::size_t total = 0;
        std::size_t onlyN = 0;
        std::size_t spindleStopped = 0;
        std::size_t equalOpTool = 0;
        std::size_t withOpTool = 0;
        std::map<std::string, std::size_t> before;
        std::map<std::string, std::size_t> after;
        Lines exceptions;
        std::vector<long> values;
        std::size_t contentBlocks = 0;
        for (std::size_t i = 0; i < recs_.size(); ++i) {
            const Block& b = recs_[i].block;
            if (!b.words.empty()) ++contentBlocks;
            if (!b.n || b.words.empty()) continue;
            ++total;
            values.push_back(*b.n);
            if (b.words.size() == 1) ++onlyN;
            if (recs_[i].spindleOffBefore) ++spindleStopped;
            before[nearestAbstract(i, -1)]++;
            after[nearestAbstract(i, +1)]++;
            // tool that works in the next operation = active tool at the next cutting block
            for (std::size_t j = i + 1; j < recs_.size(); ++j) {
                if (recs_[j].cut) {
                    ++withOpTool;
                    if (recs_[j].activeTool == *b.n) ++equalOpTool;
                    else exceptions.push_back("N" + std::to_string(*b.n) + " (строка " + count(b.lineNo) + ", инструмент следующей операции T" + std::to_string(recs_[j].activeTool) + ")");
                    break;
                }
            }
        }
        if (total == 0) {
            L.push_back("Нумерация N: не используется.");
        } else {
            const bool sparse = total * 10 < contentBlocks;
            L.push_back("Нумерация N: есть в " + ofTotal(total, contentBlocks) + " кадров. " +
                        (onlyN == total ? "Всегда отдельным кадром (только слово N)." :
                         "Отдельным кадром (только N): " + ofTotal(onlyN, total) + "."));
            L.push_back(sparse ? "  N нумерует операции, а не кадры подряд."
                               : "  N нумерует кадры подряд.");
            L.push_back("  Шпиндель остановлен (M5) перед N: " + ofTotal(spindleStopped, total) +
                        (spindleStopped == total ? ", то есть N стоит в начале новой операции, сразу после окончания предыдущей." : "."));
            if (!before.empty()) {
                const auto b1 = top(before, 1).front();
                const auto a1 = top(after, 1).front();
                L.push_back("  Обычно перед N: " + quote(b1.first) + " (" + count(b1.second) + "), после N: " + quote(a1.first) + " (" + count(a1.second) + ").");
            }
            if (withOpTool > 0) {
                L.push_back("  Связь с инструментом: N равен номеру инструмента следующей операции в " + ofTotal(equalOpTool, withOpTool) + " случаях" +
                            (exceptions.empty() ? "." : "; исключения: " + joinList(exceptions, ", ") + ". Правило надо подтвердить у технолога."));
            }
            if (!sparse && values.size() > 2) {
                std::map<std::string, std::size_t> steps;
                for (std::size_t i = 1; i < values.size(); ++i) steps[std::to_string(values[i] - values[i - 1])]++;
                const auto s = top(steps, 1).front();
                L.push_back("  Шаг нумерации: чаще всего " + s.first + " (" + count(s.second) + " раз), первый номер " + std::to_string(values.front()) + ".");
            }
        }
    }

    // word order
    {
        std::map<char, std::pair<double, std::size_t>> rank;     // letter -> (sum of normalized positions, n)
        std::map<std::string, std::size_t> seqs;
        for (const auto& r : recs_) {
            const Block& b = r.block;
            if (b.words.size() < 2) continue;
            std::string seq;
            for (std::size_t i = 0; i < b.words.size(); ++i) {
                const char c = b.words[i].letter;
                if (seq.empty() || seq.back() != c) seq += c;
                auto& e = rank[c];
                e.first += static_cast<double>(i) / static_cast<double>(b.words.size() - 1);
                ++e.second;
            }
            std::string spaced;
            for (const char c : seq) { if (!spaced.empty()) spaced += ' '; spaced += c; }
            ++seqs[spaced];
        }
        L.push_back("");
        if (rank.empty()) {
            L.push_back("Порядок слов: кадров из нескольких слов нет.");
        } else {
            std::vector<std::pair<double, char>> order;
            for (const auto& [c, e] : rank) order.emplace_back(e.first / static_cast<double>(e.second), c);
            std::sort(order.begin(), order.end());
            std::string o;
            for (const auto& p : order) { if (!o.empty()) o += ' '; o += p.second; }
            L.push_back("Типичный порядок адресов в кадре (по средней позиции): " + o);
            L.push_back("Самые частые последовательности слов в кадрах из двух и более слов:");
            for (const auto& [s, n] : top(seqs, 8)) L.push_back("    " + s + "  (" + count(n) + ")");
        }
    }

    // spaces between words
    {
        std::size_t pairs = 0;
        std::size_t glued = 0;
        std::map<std::string, std::size_t> gluedKinds;
        std::map<std::string, std::string> example;
        std::size_t trailing = 0;
        std::string trailingExample;
        std::size_t upper = 0;
        std::size_t lower = 0;
        std::size_t maxLen = 0;
        for (const auto& r : recs_) {
            const Block& b = r.block;
            maxLen = std::max(maxLen, b.raw.size());
            if (b.trailingWhitespace) {
                if (trailing == 0) trailingExample = b.raw;
                ++trailing;
            }
            for (std::size_t i = 0; i < b.words.size(); ++i) {
                if (b.words[i].lowercase) ++lower; else ++upper;
                if (i == 0) continue;
                ++pairs;
                if (!b.words[i].precededBySpace) {
                    ++glued;
                    const std::string kind = std::string(1, b.words[i - 1].letter) + "+" + std::string(1, b.words[i].letter);
                    ++gluedKinds[kind];
                    if (example.find(kind) == example.end()) example[kind] = b.words[i - 1].rawText + b.words[i].rawText;
                }
            }
        }
        L.push_back("");
        if (glued == 0) {
            L.push_back("Слова в кадре всегда разделены пробелом (пар слов: " + count(pairs) + ").");
        } else {
            L.push_back("Слова без пробела: " + ofTotal(glued, pairs) + " пар соседних слов. По видам:");
            for (const auto& [kind, n] : top(gluedKinds, 8)) {
                L.push_back("    " + kind + ": " + count(n) + " (например " + quote(example[kind]) + ")");
            }
            L.push_back("Остальные слова разделены одним пробелом.");
        }
        L.push_back(std::string("Регистр: ") + (lower == 0 ? "только верхний (" + count(upper) + " слов)." : "есть нижний регистр: " + ofTotal(lower, upper + lower) + " слов."));
        if (trailing > 0) L.push_back("Хвостовые пробелы в конце кадра: " + count(trailing) + " кадров (например " + quote(trailingExample) + ").");
        else L.push_back("Хвостовых пробелов нет.");
        L.push_back("Концы строк: CRLF " + count(report.file.crlfLines) + ", LF " + count(report.file.lfLines) +
                    "; самая длинная строка " + count(maxLen) + " символов.");
    }

    // number format per address
    {
        struct Dec {
            std::size_t words = 0, intNoPoint = 0, intPoint = 0, fraction = 0, trailingZero = 0, leadingDot = 0, plus = 0;
            std::size_t zeroNoPoint = 0, zeroPoint = 0;
            std::string exZeroNoPoint, exZeroPoint;
            std::size_t minDec = 99, maxDec = 0;
            std::string exNoPoint, exPoint;
        };
        std::map<char, Dec> dec;
        const std::string letters = "XYZBIJKRQPFSHD";
        for (const auto& r : recs_) {
            for (const auto& w : r.block.words) {
                if (!w.hasValue || letters.find(w.letter) == std::string::npos) continue;
                Dec& d = dec[w.letter];
                ++d.words;
                std::string_view n = w.rawNumber();
                if (!n.empty() && (n.front() == '+' || n.front() == '-')) {
                    if (n.front() == '+') ++d.plus;
                    n.remove_prefix(1);
                }
                const std::size_t dot = n.find('.');
                if (isAxisLetter(w.letter) && w.value == 0.0 && (dot == std::string_view::npos || dot + 1 == n.size())) {
                    if (dot == std::string_view::npos) { ++d.zeroNoPoint; if (d.exZeroNoPoint.empty()) d.exZeroNoPoint = w.rawText; }
                    else { ++d.zeroPoint; if (d.exZeroPoint.empty()) d.exZeroPoint = w.rawText; }
                }
                if (dot == std::string_view::npos) {
                    ++d.intNoPoint;
                    if (d.exNoPoint.empty()) d.exNoPoint = w.rawText;
                    d.minDec = std::min<std::size_t>(d.minDec, 0);
                    continue;
                }
                if (dot == 0) ++d.leadingDot;
                const std::size_t decimals = n.size() - dot - 1;
                d.minDec = std::min(d.minDec, decimals);
                d.maxDec = std::max(d.maxDec, decimals);
                if (decimals == 0) {
                    ++d.intPoint;
                    if (d.exPoint.empty()) d.exPoint = w.rawText;
                } else {
                    ++d.fraction;
                    if (n.back() == '0') ++d.trailingZero;
                }
            }
        }
        L.push_back("");
        L.push_back("Числа по адресам (знаков после запятой; целые значения с точкой `Z32.` или без `Z0`):");
        std::size_t anyTrailingZero = 0;
        std::size_t anyFraction = 0;
        std::size_t anyLeadingDot = 0;
        std::size_t anyPlus = 0;
        std::size_t zeroNoPoint = 0, zeroPoint = 0;
        std::string exZeroNoPoint, exZeroPoint;
        for (const auto& [letter, d] : dec) {
            zeroNoPoint += d.zeroNoPoint;
            zeroPoint += d.zeroPoint;
            if (exZeroNoPoint.empty()) exZeroNoPoint = d.exZeroNoPoint;
            if (exZeroPoint.empty()) exZeroPoint = d.exZeroPoint;
            std::string s = std::string("    ") + letter + ": слов " + count(d.words) + ", знаков после запятой до " + count(d.maxDec);
            s += "; целых с точкой " + count(d.intPoint) + (d.exPoint.empty() ? "" : " (" + quote(d.exPoint) + ")");
            s += ", без точки " + count(d.intNoPoint) + (d.exNoPoint.empty() ? "" : " (" + quote(d.exNoPoint) + ")");
            L.push_back(s);
            anyTrailingZero += d.trailingZero;
            anyFraction += d.fraction;
            anyLeadingDot += d.leadingDot;
            anyPlus += d.plus;
        }
        if (zeroNoPoint + zeroPoint > 0) {
            L.push_back("Нулевое значение оси: без точки " + count(zeroNoPoint) + (exZeroNoPoint.empty() ? "" : " (" + quote(exZeroNoPoint) + ")") +
                        ", с точкой " + count(zeroPoint) + (exZeroPoint.empty() ? "" : " (" + quote(exZeroPoint) + ")") +
                        (zeroNoPoint > 0 && zeroPoint > 0 ? ": запись нуля непоследовательна." : "."));
        }
        L.push_back(std::string("Нули справа в дробной части: ") + (anyTrailingZero == 0 ? "не пишутся (`1.5`, не `1.500`)" : "пишутся в " + ofTotal(anyTrailingZero, anyFraction) + " дробных числах") + ".");
        L.push_back(std::string("Ноль перед точкой у чисел меньше 1: ") + (anyLeadingDot == 0 ? "пишется (`0.5`)" : "не пишется в " + count(anyLeadingDot) + " случаях (`.5`)") + ".");
        L.push_back(std::string("Знак `+` у положительных чисел: ") + (anyPlus == 0 ? "не пишется." : "пишется в " + count(anyPlus) + " случаях."));
    }

    // leading zeros in G/M/T/H/D codes
    {
        std::map<char, std::set<std::string>> padded;
        std::map<char, std::set<std::string>> plain;
        for (const auto& r : recs_) {
            for (const auto& w : r.block.words) {
                if (!w.hasValue || (w.letter != 'G' && w.letter != 'M' && w.letter != 'T' && w.letter != 'H' && w.letter != 'D')) continue;
                if (w.value >= 10.0 || w.value < 0.0) continue;
                const std::string_view n = w.rawNumber();
                if (n.size() >= 2 && n.front() == '0' && n[1] != '.') padded[w.letter].insert(w.rawText);
                else plain[w.letter].insert(w.rawText);
            }
        }
        L.push_back("");
        L.push_back("Ведущие нули в кодах меньше 10:");
        for (const char c : std::string("GMTHD")) {
            if (padded[c].empty() && plain[c].empty()) continue;
            std::string s = std::string("    ") + c + ": без нуля - " + (plain[c].empty() ? "нет" : joinList(Lines(plain[c].begin(), plain[c].end()), " "));
            s += "; с нулём - " + (padded[c].empty() ? std::string("нет") : joinList(Lines(padded[c].begin(), padded[c].end()), " "));
            L.push_back(s);
        }
    }

    // arcs
    {
        L.push_back("");
        if (arcs_.total == 0) {
            L.push_back("Дуги G2/G3 в программе не встречаются.");
        } else {
            L.push_back("Дуги G2/G3: " + count(arcs_.total) + " кадров. Центр задан смещениями I/J/K: " + count(arcs_.byOffsets) +
                        ", радиусом R: " + count(arcs_.byRadius) + (arcs_.byRadius == 0 ? " (R не используется)." : "."));
            L.push_back("  Винтовых дуг (движение по третьей оси): " + count(arcs_.helical) + "; полных окружностей (конец = начало): " +
                        count(arcs_.fullCircle) + ", из них винтовых (резьбофрезерование): " + count(arcs_.helicalFullCircle) + ".");
            L.push_back("  Нулевые смещения пишутся явно (`I0.`): " + count(arcs_.explicitZeroOffset) + " раз.");
            if (arcs_.failed > 0) L.push_back("  Дуг, которые не удалось рассчитать: " + count(arcs_.failed) + ".");
        }
    }

    report.cheatSheet.push_back(std::move(sec));
}

// ---------------------------------------------------------------------------------------------
// section 3: modality
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildModality(Report& report) {
    CheatSection sec;
    sec.title = "3. Модальность";
    Lines& L = sec.lines;
    const Modality& m = mod_;

    L.push_back("Кадров с перемещением: " + count(m.motionBlocks) + ".");
    L.push_back("Код движения (G0/G1/G2/G3): опущен при неизменном режиме в " + ofTotal(m.motionCodeOmitted, m.motionBlocks) +
                " кадрах; указан при смене режима " + count(m.motionCodeExplicitChange) + " раз; повторён без смены режима " +
                count(m.motionCodeExplicitSame) + " раз.");
    if (m.cutBlocks > 0) {
        L.push_back("Подача F в рабочих кадрах (" + count(m.cutBlocks) + "): опущена " + count(m.fOmitted) + ", указана при изменении " +
                    count(m.fChange) + ", повторена с тем же значением " + count(m.fSame) + ".");
    }
    {
        std::string rep = "Координаты, не изменившиеся относительно текущей позиции (абсолютный режим):";
        std::size_t totalRep = 0, totalCh = 0;
        for (const char c : std::string("XYZB")) {
            const auto r = m.axisRepeated.find(c);
            const auto ch = m.axisChanged.find(c);
            const std::size_t nr = r == m.axisRepeated.end() ? 0 : r->second;
            const std::size_t nc = ch == m.axisChanged.end() ? 0 : ch->second;
            if (nr + nc == 0) continue;
            rep += std::string(" ") + c + " повторено " + count(nr) + " из " + count(nr + nc) + ";";
            totalRep += nr;
            totalCh += nc;
        }
        L.push_back(rep);
        if (totalRep + totalCh > 0) {
            L.push_back(totalRep * 20 <= totalRep + totalCh
                            ? "Вывод: неизменившиеся координаты опускаются, пишутся только изменившиеся."
                            : "Вывод: неизменившиеся координаты частично повторяются, проверить правила вывода.");
        }
    }
    if (m.sTotal > 0) L.push_back("Обороты S повторены с тем же значением: " + ofTotal(m.sRepeated, m.sTotal) + ".");
    if (m.spindleTotal > 0) L.push_back("M3/M4 повторены при уже включённом шпинделе: " + ofTotal(m.spindleRepeated, m.spindleTotal) + ".");

    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> reps;
    for (const auto& [code, e] : m.gRepeat) {
        if (e.first > 0) reps.emplace_back(code, e);
    }
    std::sort(reps.begin(), reps.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    if (reps.empty()) {
        L.push_back("Модальные G-коды (плоскость, G90/G91, G94/G95, СК и др.) повторно не пишутся.");
    } else {
        L.push_back("Модальные G-коды, которые пишутся повторно при том же значении (повторов из всех вхождений):");
        std::string s = "   ";
        for (const auto& [code, e] : reps) s += " " + displayCode(code) + " " + ofTotal(e.first, e.second) + ";";
        L.push_back(s);
    }
    {
        std::vector<std::string> never;
        for (const auto& [code, e] : m.gRepeat) {
            if (e.first == 0 && e.second > 1) never.push_back(displayCode(code));
        }
        if (!never.empty()) L.push_back("Модальные коды, которые не повторяются (каждое вхождение - смена значения): " + joinList(never, " ") + ".");
    }

    report.cheatSheet.push_back(std::move(sec));
}

// ---------------------------------------------------------------------------------------------
// section 4: comments
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildComments(Report& report) {
    CheatSection sec;
    sec.title = "4. Комментарии";
    Lines& L = sec.lines;

    std::size_t total = 0, paren = 0, semi = 0, ownLine = 0, inBlock = 0;
    std::size_t padded = 0, tight = 0;
    std::size_t upperLetters = 0, lowerLetters = 0, nonAscii = 0, maxLen = 0;
    std::set<char> special;
    std::map<std::string, std::size_t> kinds;
    std::map<std::string, std::string> kindExample;
    for (const auto& r : recs_) {
        for (const auto& c : r.block.comments) {
            ++total;
            if (c.style == '(') ++paren; else ++semi;
            if (r.block.words.empty() && r.block.comments.size() == 1) ++ownLine; else ++inBlock;
            if (c.style == '(') {
                const bool lead = !c.text.empty() && c.text.front() == ' ';
                const bool trail = !c.text.empty() && c.text.back() == ' ';
                if (lead && trail) ++padded; else if (!lead && !trail) ++tight;
            }
            maxLen = std::max(maxLen, c.text.size());
            for (const char ch : c.text) {
                const auto u = static_cast<unsigned char>(ch);
                if (u >= 0x80) ++nonAscii;
                else if (std::isupper(u)) ++upperLetters;
                else if (std::islower(u)) ++lowerLetters;
                else if (!std::isalnum(u) && ch != ' ') special.insert(ch);
            }
            // kind = first word, cut at ':' / digits
            const std::string t = trim(c.text);
            std::string first;
            for (const char ch : t) {
                if (ch == ' ' || ch == ':') break;
                first += ch;
            }
            {
                std::string norm;
                for (const char ch : first) {
                    if (std::isdigit(static_cast<unsigned char>(ch))) {
                        if (norm.size() < 3 || norm.compare(norm.size() - 3, 3, "<n>") != 0) norm += "<n>";
                    } else {
                        norm += ch;
                    }
                }
                first = norm;
            }
            ++kinds[first];
            if (kindExample.find(first) == kindExample.end()) kindExample[first] = c.raw;
        }
    }
    if (total == 0) {
        L.push_back("Комментариев нет.");
        report.cheatSheet.push_back(std::move(sec));
        return;
    }
    L.push_back("Всего комментариев: " + count(total) + " (в скобках `( )`: " + count(paren) + ", после `;`: " + count(semi) + ").");
    L.push_back("Отдельной строкой: " + count(ownLine) + ", в одном кадре со словами: " + count(inBlock) + ".");
    if (paren > 0) {
        L.push_back("Пробелы внутри скобок: `( текст )` - " + count(padded) + ", `(текст)` - " + count(tight) +
                    (padded + tight < paren ? ", смешанно - " + count(paren - padded - tight) : "") + ".");
    }
    L.push_back(std::string("Регистр букв: ") + (lowerLetters == 0 ? "только верхний" : "есть нижний (" + count(lowerLetters) + " из " + count(lowerLetters + upperLetters) + ")") + ".");
    L.push_back(nonAscii == 0 ? "Только ASCII (кириллица транслитерируется), символов вне ASCII нет."
                              : "Символов вне ASCII в комментариях: " + count(nonAscii) + ".");
    {
        std::string sp;
        for (const char c : special) { sp += c; sp += ' '; }
        L.push_back("Знаки в комментариях, кроме букв и цифр: " + (sp.empty() ? std::string("нет") : sp) + ". Самый длинный комментарий: " + count(maxLen) + " символов.");
    }
    L.push_back("Виды комментариев по первому слову:");
    std::map<std::string, std::size_t> kindCounts = kinds;
    for (const auto& [k, n] : top(kindCounts, 8)) {
        L.push_back("    " + k + ": " + count(n) + ", например " + quote(kindExample[k]));
    }

    report.cheatSheet.push_back(std::move(sec));
}

// ---------------------------------------------------------------------------------------------
// section 5: rotary axis
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildRotary(Report& report) {
    CheatSection sec;
    sec.title = "5. Поворот оси B";
    Lines& L = sec.lines;

    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < recs_.size(); ++i) {
        if (recs_[i].block.has('B')) idx.push_back(i);
    }
    if (idx.empty()) {
        L.push_back("Ось B в программе не используется (слов B нет).");
        report.cheatSheet.push_back(std::move(sec));
        return;
    }

    std::map<std::string, std::size_t> patterns;
    std::map<std::string, std::string> example;
    std::set<double> values;
    std::map<std::string, std::size_t> mBefore, mSame, mAfter;
    const auto collectM = [&](std::size_t i, std::map<std::string, std::size_t>& dst) {
        if (i >= recs_.size()) return;
        for (const auto& w : recs_[i].block.words) {
            if (w.letter == 'M' && w.hasValue) dst[canonicalCode('M', w.value)]++;
        }
    };
    for (const std::size_t i : idx) {
        const Block& b = recs_[i].block;
        std::string pat;
        for (const auto& w : b.words) {
            if (w.letter == 'N') continue;
            if (w.letter == 'G' || w.letter == 'M') pat += displayCode(canonicalCode(w.letter, w.value)) + " ";
            else pat += std::string(1, w.letter) + " ";
        }
        if (!pat.empty()) pat.pop_back();
        ++patterns[pat];
        if (example.find(pat) == example.end()) example[pat] = trim(b.raw);
        for (const auto& w : b.words) {
            if (w.letter == 'B' && w.hasValue) values.insert(w.value);
        }
        if (i > 0) collectM(i - 1, mBefore);
        collectM(i, mSame);
        collectM(i + 1, mAfter);
    }
    L.push_back("Кадров с B: " + count(idx.size()) + ", различных значений угла: " + count(values.size()) + ", диапазон " +
                trimmedNumber(*values.begin()) + " .. " + trimmedNumber(*values.rbegin()) + ".");
    L.push_back("Как программируется (состав кадра с B):");
    for (const auto& [p, n] : top(patterns, 6)) L.push_back("    " + p + "  (" + count(n) + "), например " + quote(example[p]));
    const auto describeM = [&](const std::map<std::string, std::size_t>& m) {
        Lines out;
        for (const auto& [code, n] : top(m, 5)) {
            std::string d;
            if (const auto info = profile_->describe('M', parseNumber(std::string_view(code).substr(1)).value_or(0.0))) d = " - " + info->description;
            else d = " - ?";
            out.push_back("    " + displayCode(code) + " x" + count(n) + d);
        }
        return out;
    };
    for (const auto& [title, m] : {std::pair<const char*, const std::map<std::string, std::size_t>*>{"в кадре перед поворотом", &mBefore},
                                   {"в том же кадре", &mSame}, {"в кадре после поворота", &mAfter}}) {
        if (m->empty()) continue;
        L.push_back(std::string("M-коды ") + title + ":");
        for (auto& line : describeM(*m)) L.push_back(std::move(line));
    }

    report.cheatSheet.push_back(std::move(sec));
}

// ---------------------------------------------------------------------------------------------
// section 6: machine specific codes
// ---------------------------------------------------------------------------------------------

void FormatAnalyzer::buildMachineSpecific(Report& report) {
    CheatSection sec;
    sec.title = "6. Машинно-специфичные коды";
    Lines& L = sec.lines;

    struct Info {
        std::string description;
        bool known = false;
        std::size_t total = 0;
        std::array<std::size_t, 5> zones{};
        std::map<std::string, std::size_t> blocks, prev, next;
    };
    std::map<std::string, Info> infos;
    std::vector<std::string> order;

    for (std::size_t i = 0; i < recs_.size(); ++i) {
        const Block& b = recs_[i].block;
        for (const auto& w : b.words) {
            if (!w.hasValue || (w.letter != 'G' && w.letter != 'M') || !profile_) continue;
            const std::string key = canonicalCode(w.letter, w.value);
            const auto desc = profile_->describe(w.letter, w.value);
            bool known = desc.has_value();
            if (w.letter == 'G') known = profile_->g(w.value).has_value();
            if (known && !(desc && desc->machineSpecific)) continue;
            auto it = infos.find(key);
            if (it == infos.end()) {
                it = infos.emplace(key, Info{}).first;
                order.push_back(key);
                it->second.known = known;
                if (desc) it->second.description = desc->description + " [" + desc->source + "]";
            }
            Info& inf = it->second;
            ++inf.total;
            ++inf.zones[static_cast<std::size_t>(recs_[i].zone)];
            ++inf.blocks[abstractBlock(b, "<комментарий>")];
            ++inf.prev[nearestAbstract(i, -1)];
            ++inf.next[nearestAbstract(i, +1)];
        }
    }
    if (infos.empty()) {
        L.push_back("Машинно-специфичных и неизвестных кодов нет.");
        report.cheatSheet.push_back(std::move(sec));
        return;
    }
    std::sort(order.begin(), order.end(), [](const std::string& a, const std::string& b) {
        if (a[0] != b[0]) return a[0] < b[0];
        return parseNumber(std::string_view(a).substr(1)).value_or(0.0) < parseNumber(std::string_view(b).substr(1)).value_or(0.0);
    });
    for (const auto& key : order) {
        const Info& inf = infos[key];
        L.push_back(displayCode(key) + ": " + count(inf.total) + " раз. " + (inf.known ? inf.description : "?  неизвестный код, смысл надо уточнить"));
        std::string z = "    где стоит:";
        for (std::size_t k = 0; k < inf.zones.size(); ++k) {
            if (inf.zones[k] > 0) z += std::string(" ") + zoneName(static_cast<int>(k)) + " - " + count(inf.zones[k]) + ";";
        }
        L.push_back(z);
        const auto b1 = top(inf.blocks, 1).front();
        const auto p1 = top(inf.prev, 1).front();
        const auto n1 = top(inf.next, 1).front();
        L.push_back("    типичный кадр: " + quote(b1.first) + " (" + count(b1.second) + "); перед ним: " + quote(p1.first) +
                    " (" + count(p1.second) + "); после: " + quote(n1.first) + " (" + count(n1.second) + ")");
    }

    report.cheatSheet.push_back(std::move(sec));
}

void FormatAnalyzer::finish(Report& report) {
    if (!profile_) return;
    computeZones();
    buildStructure(report);
    buildBlockFormat(report);
    buildModality(report);
    buildComments(report);
    buildRotary(report);
    buildMachineSpecific(report);
}

} // namespace gca
