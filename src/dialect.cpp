#include "gca/dialect.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace gca {

std::string canonicalCode(char letter, double value) {
    const long long scaled = std::llround(value * 1000.0);
    const long long a = scaled < 0 ? -scaled : scaled;
    std::string s(1, letter);
    if (scaled < 0) s += '-';
    s += std::to_string(a / 1000);
    long long frac = a % 1000;
    if (frac != 0) {
        std::string f = std::to_string(frac);
        f.insert(0, 3 - f.size(), '0');
        while (!f.empty() && f.back() == '0') f.pop_back();
        s += '.';
        s += f;
    }
    return s;
}

std::string displayCode(std::string_view canonical) {
    if (canonical.size() < 2) return std::string(canonical);
    std::string s(1, canonical.front());
    std::string_view rest = canonical.substr(1);
    const std::size_t dot = rest.find('.');
    const std::string_view ip = rest.substr(0, dot);
    if (ip.size() < 2) s += '0';
    s += ip;
    if (dot != std::string_view::npos) s += rest.substr(dot);
    return s;
}

std::optional<MClass> Profile::m(int v) const {
    if (machine_ != nullptr) {
        for (const auto& mc : machine_->mCodes) {
            if (mc.code == v) return mc.cls;
        }
    }
    return dialect_->m(v);
}

std::optional<CodeInfo> Profile::describe(char letter, double value) const {
    if (letter == 'M' && machine_ != nullptr) {
        const int mv = static_cast<int>(std::lround(value));
        for (const auto& mc : machine_->mCodes) {
            if (mc.code == mv && std::fabs(value - mv) < 1e-9) {
                CodeInfo ci;
                ci.code = canonicalCode('M', value);
                ci.description = mc.description;
                ci.machineSpecific = true;
                ci.source = machine_->name;
                return ci;
            }
        }
    }
    if (letter == 'G') return dialect_->describeG(value);
    if (letter == 'M') {
        if (std::fabs(value - std::round(value)) > 1e-9) return std::nullopt;
        return dialect_->describeM(static_cast<int>(std::lround(value)));
    }
    return std::nullopt;
}

namespace {

struct GEntry {
    const char* code;
    const char* description;
    GClass cls;
    int group;
    bool machineSpecific;
};

// Modal groups (own numbering): 1 motion, 2 plane, 3 distance, 5 feed mode, 6 units, 7 cutter comp,
// 8 tool length, 9 canned cycles, 10 cycle return level, 12 macro modal, 14 WCS, 15 cutting mode,
// 16 rotation, 17 scaling, 18 polar, 0 non-modal.
const GEntry kGTable[] = {
    {"G0", "быстрое позиционирование", GClass::Rapid, 1, false},
    {"G1", "линейная интерполяция", GClass::Linear, 1, false},
    {"G2", "круговая интерполяция по часовой", GClass::ArcCw, 1, false},
    {"G3", "круговая интерполяция против часовой", GClass::ArcCcw, 1, false},
    {"G4", "выдержка", GClass::Dwell, 0, false},
    {"G5", "режим высокоскоростной обработки (G05 P..)", GClass::Other, 0, true},
    {"G5.1", "AI-контурное управление / предпросмотр (G05.1 Q1/Q0)", GClass::Other, 0, true},
    {"G8", "предпросмотр кадров (G08 P1/P0)", GClass::Other, 0, true},
    {"G9", "точный останов в кадре", GClass::Other, 0, false},
    {"G10", "ввод данных программой", GClass::DataSetting, 0, false},
    {"G11", "отмена ввода данных", GClass::Other, 0, false},
    {"G15", "отмена полярных координат", GClass::Other, 18, false},
    {"G16", "полярные координаты", GClass::Other, 18, false},
    {"G17", "плоскость XY", GClass::PlaneXY, 2, false},
    {"G18", "плоскость ZX", GClass::PlaneZX, 2, false},
    {"G19", "плоскость YZ", GClass::PlaneYZ, 2, false},
    {"G20", "дюймовый ввод", GClass::Inch, 6, false},
    {"G21", "метрический ввод", GClass::Metric, 6, false},
    {"G22", "контроль зон хода включён", GClass::Other, 4, false},
    {"G23", "контроль зон хода выключен", GClass::Other, 4, false},
    {"G27", "проверка возврата в референтную точку", GClass::Other, 0, false},
    {"G28", "возврат в референтную точку", GClass::Home, 0, false},
    {"G29", "возврат из референтной точки", GClass::Other, 0, false},
    {"G30", "возврат во 2-ю референтную точку", GClass::Home, 0, false},
    {"G31", "пропуск (измерительный датчик)", GClass::Other, 0, false},
    {"G40", "отмена коррекции радиуса", GClass::CompOff, 7, false},
    {"G41", "коррекция радиуса слева", GClass::CompLeft, 7, false},
    {"G42", "коррекция радиуса справа", GClass::CompRight, 7, false},
    {"G43", "коррекция длины инструмента (+)", GClass::ToolLenPlus, 8, false},
    {"G44", "коррекция длины инструмента (-)", GClass::ToolLenMinus, 8, false},
    {"G49", "отмена коррекции длины", GClass::ToolLenCancel, 8, false},
    {"G50", "отмена масштабирования", GClass::Other, 17, false},
    {"G51", "масштабирование", GClass::Other, 17, false},
    {"G52", "локальная система координат", GClass::Other, 0, false},
    {"G53", "перемещение в машинных координатах", GClass::MachineCoords, 0, false},
    {"G54", "рабочая система координат 1", GClass::Wcs, 14, false},
    {"G55", "рабочая система координат 2", GClass::Wcs, 14, false},
    {"G56", "рабочая система координат 3", GClass::Wcs, 14, false},
    {"G57", "рабочая система координат 4", GClass::Wcs, 14, false},
    {"G58", "рабочая система координат 5", GClass::Wcs, 14, false},
    {"G59", "рабочая система координат 6", GClass::Wcs, 14, false},
    {"G54.1", "дополнительная рабочая СК (G54.1 Pn)", GClass::Wcs, 14, false},
    {"G60", "позиционирование с одного направления", GClass::Other, 0, false},
    {"G61", "режим точного останова", GClass::Other, 15, false},
    {"G62", "автоматическое ускорение на углах", GClass::Other, 15, false},
    {"G63", "режим нарезания резьбы метчиком", GClass::Other, 15, false},
    {"G64", "режим резания (сглаживание)", GClass::Other, 15, true},
    {"G65", "вызов макропрограммы", GClass::MacroCall, 0, false},
    {"G66", "модальный вызов макропрограммы", GClass::Other, 12, false},
    {"G67", "отмена модального вызова макро", GClass::Other, 12, false},
    {"G68", "поворот системы координат", GClass::Other, 16, false},
    {"G69", "отмена поворота системы координат", GClass::Other, 16, false},
    {"G73", "цикл глубокого сверления (с дроблением стружки)", GClass::Cycle, 9, false},
    {"G74", "цикл левого нарезания резьбы метчиком", GClass::Cycle, 9, false},
    {"G76", "цикл тонкого растачивания", GClass::Cycle, 9, false},
    {"G80", "отмена постоянного цикла", GClass::CycleCancel, 9, false},
    {"G81", "цикл сверления", GClass::Cycle, 9, false},
    {"G82", "цикл сверления с выдержкой", GClass::Cycle, 9, false},
    {"G83", "цикл глубокого сверления (с выводом)", GClass::Cycle, 9, false},
    {"G84", "цикл нарезания резьбы метчиком", GClass::Cycle, 9, false},
    {"G85", "цикл растачивания (подача туда и обратно)", GClass::Cycle, 9, false},
    {"G86", "цикл растачивания с остановом шпинделя", GClass::Cycle, 9, false},
    {"G87", "цикл обратного растачивания", GClass::Cycle, 9, false},
    {"G88", "цикл растачивания с выдержкой и ручным выводом", GClass::Cycle, 9, false},
    {"G89", "цикл растачивания с выдержкой", GClass::Cycle, 9, false},
    {"G90", "абсолютные координаты", GClass::Absolute, 3, false},
    {"G91", "приращения", GClass::Incremental, 3, false},
    {"G92", "установка системы координат / предустановка позиции", GClass::SetPosition, 0, false},
    {"G93", "подача обратно пропорциональна времени", GClass::Other, 5, false},
    {"G94", "подача в мм/мин", GClass::FeedPerMin, 5, false},
    {"G95", "подача в мм/об", GClass::FeedPerRev, 5, false},
    {"G96", "постоянная скорость резания", GClass::Other, 13, false},
    {"G97", "постоянные обороты", GClass::Other, 13, false},
    {"G98", "возврат в цикле на исходный уровень", GClass::ReturnInitial, 10, false},
    {"G99", "возврат в цикле на плоскость R", GClass::ReturnR, 10, false},
};

struct MEntry {
    int code;
    const char* description;
    MClass cls;
};

const MEntry kMTable[] = {
    {0, "программируемый останов", MClass::ProgramStop},
    {1, "останов по выбору", MClass::OptionalStop},
    {2, "конец программы", MClass::ProgramEnd},
    {3, "шпиндель по часовой", MClass::SpindleCw},
    {4, "шпиндель против часовой", MClass::SpindleCcw},
    {5, "останов шпинделя", MClass::SpindleStop},
    {6, "смена инструмента", MClass::ToolChange},
    {7, "СОЖ туман", MClass::CoolantOn},
    {8, "СОЖ полив", MClass::CoolantOn},
    {9, "СОЖ выключена", MClass::CoolantOff},
    {19, "ориентация шпинделя", MClass::SpindleOrient},
    {30, "конец программы с перемоткой", MClass::ProgramEnd},
    {98, "вызов подпрограммы", MClass::SubCall},
    {99, "возврат из подпрограммы", MClass::SubReturn},
};

class FanucMillDialect final : public IDialect {
public:
    FanucMillDialect() {
        for (const auto& e : kGTable) gIndex_.emplace(e.code, &e);
        for (const auto& e : kMTable) mIndex_.emplace(e.code, &e);
        machines_ = {
            {"skymaster-vl1100a", "Sky Master VL1100A", "skymaster",
             {{15, MClass::CoolantExtra, "дополнительная подача СОЖ (Sky Master)"}}},
            {"bridgeport-1300-480", "Bridgeport 1300/480", "bridgeport",
             {{53, MClass::CoolantExtra, "подача СОЖ через шпиндель (Bridgeport)"}}},
        };
    }

    std::string_view id() const override { return "fanuc-mill"; }
    std::string_view name() const override { return "Fanuc, фрезерная группа (3 оси + индексная B)"; }

    std::optional<GInfo> g(double value) const override {
        const auto it = gIndex_.find(canonicalCode('G', value));
        if (it == gIndex_.end()) return std::nullopt;
        return GInfo{it->second->cls, it->second->group};
    }
    std::optional<CodeInfo> describeG(double value) const override {
        const std::string key = canonicalCode('G', value);
        const auto it = gIndex_.find(key);
        if (it == gIndex_.end()) return std::nullopt;
        return CodeInfo{key, it->second->description, it->second->machineSpecific, std::string(id())};
    }
    std::optional<MClass> m(int value) const override {
        const auto it = mIndex_.find(value);
        if (it == mIndex_.end()) return std::nullopt;
        return it->second->cls;
    }
    std::optional<CodeInfo> describeM(int value) const override {
        const auto it = mIndex_.find(value);
        if (it == mIndex_.end()) return std::nullopt;
        return CodeInfo{canonicalCode('M', value), it->second->description, false, std::string(id())};
    }

    int axisIndex(char letter) const override {
        for (int i = 0; i < kAxisCount; ++i) {
            if (kAxisLetters[i] == letter) return i;
        }
        return -1;
    }
    bool isLinearAxis(int index) const override { return index >= 0 && index < kAxisB; }
    bool isKnownAddress(char letter) const override {
        static const std::string known = "ABDFGHIJKLMNOPQRSTXYZ";
        return known.find(letter) != std::string::npos;
    }
    const std::vector<MachineProfile>& machines() const override { return machines_; }

private:
    std::unordered_map<std::string, const GEntry*> gIndex_;
    std::unordered_map<int, const MEntry*> mIndex_;
    std::vector<MachineProfile> machines_;
};

const FanucMillDialect& fanucMill() {
    static const FanucMillDialect d;
    return d;
}

std::string squash(std::string_view s) {
    std::string r;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '-' || c == '_') continue;
        r += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return r;
}

} // namespace

const IDialect* findDialect(std::string_view id) {
    if (id == fanucMill().id()) return &fanucMill();
    return nullptr;
}

std::vector<std::string> dialectIds() { return {std::string(fanucMill().id())}; }

const MachineProfile* findMachine(const IDialect& dialect, std::string_view id) {
    for (const auto& m : dialect.machines()) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const MachineProfile* detectMachine(const IDialect& dialect, std::string_view headerText) {
    const std::string s = squash(headerText);
    for (const auto& m : dialect.machines()) {
        if (s.find(m.match) != std::string::npos) return &m;
    }
    return nullptr;
}

} // namespace gca
