#include "gca/dialect.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace gca {
namespace {
struct Entry { double code; const char* text; GClass cls; int group = 0; };
// GNC62 programming manual 2021, chapters 2, 4, 6, 8; turning manual V09.
const Entry entries[] = {
    {1.6, "подвод по вектору инструмента", GClass::UnmodeledMotion},
    {2.1, "дуга по часовой с углом раскрытия", GClass::UnmodeledMotion},
    {3.1, "дуга против часовой с углом раскрытия", GClass::UnmodeledMotion},
    {2.2, "дуга по часовой с дополнительным приращением", GClass::UnmodeledMotion},
    {3.2, "дуга против часовой с дополнительным приращением", GClass::UnmodeledMotion},
    {2.4, "трёхмерная дуга по трём точкам", GClass::UnmodeledMotion},
    {2.7, "дуга по часовой вокруг поворотной оси", GClass::UnmodeledMotion},
    {3.7, "дуга против часовой вокруг поворотной оси", GClass::UnmodeledMotion},
    {2.8, "спиральная интерполяция по часовой", GClass::UnmodeledMotion},
    {3.8, "спиральная интерполяция против часовой", GClass::UnmodeledMotion},
    {2.81, "спиральная интерполяция с нижним пределом", GClass::UnmodeledMotion},
    {3.81, "спиральная интерполяция с нижним пределом", GClass::UnmodeledMotion},
    {2.9, "дуга с тангенциальным переходом", GClass::UnmodeledMotion},
    {6, "приведение координаты C к диапазону -360...360", GClass::DataSetting},
    {6.1, "двойная круговая интерполяция с подгонкой", GClass::UnmodeledMotion},
    {6.2, "сплайновая интерполяция NURBS", GClass::UnmodeledMotion},
    {6.3, "начало C-сплайновой интерполяции", GClass::UnmodeledMotion},
    {6.4, "завершение C-сплайновой интерполяции", GClass::Other},
    {6.11, "начало подгонки двойной дуги", GClass::UnmodeledMotion},
    {6.12, "завершение подгонки двойной дуги", GClass::Other},
    {6.31, "сплайновая интерполяция с ограничением", GClass::UnmodeledMotion},
    {6.32, "сплайновая интерполяция с ограничением", GClass::UnmodeledMotion},
    {6.33, "C-сплайн по пяти точкам", GClass::UnmodeledMotion},
    {6.41, "завершение сплайновой интерполяции", GClass::Other},
    {6.42, "завершение сплайновой интерполяции", GClass::Other},
    {6.43, "завершение C-сплайна по пяти точкам", GClass::Other},
    {4, "выдержка, F в секундах", GClass::Dwell},
    {28, "возврат в опорную точку выбранных осей", GClass::Home},
    {52, "задание/отмена локальной СК (адреса не являются перемещением)", GClass::Transform},
    {53, "машинная СК; без адресов — модальный выбор", GClass::MachineCoords},
    {93, "ограничение максимальных оборотов шпинделя", GClass::SpindleLimit},
    {96, "постоянная скорость резания", GClass::SurfaceSpeed, 13},
    {97, "постоянные обороты шпинделя", GClass::FixedSpeed, 13},
    {200, "позиционирование поворотных осей", GClass::RotaryAlign},
    {200.1, "автоматическое выравнивание по G52, без RTCP", GClass::RotaryAlign},
    {200.2, "автоматическое выравнивание по G52, без RTCP", GClass::RotaryAlign},
    {201, "преобразование направления инструмента", GClass::Transform},
    {203, "включение RTCP", GClass::RtcpOn},
    {204, "отмена RTCP", GClass::RtcpOff},
    {209, "ось C как шпиндель без включения диаметра", GClass::TurningOn},
    {210, "перевод шпинделя в режим оси", GClass::TurningOff},
    {211, "токарный режим, ось как шпиндель", GClass::TurningOn},
    {211.1, "токарный режим, вариант ориентации", GClass::TurningOn},
    {211.2, "токарный режим, вариант ориентации", GClass::TurningOn},
    {211.3, "токарный режим, вариант ориентации", GClass::TurningOn},
    {211.4, "токарный режим, вариант ориентации", GClass::TurningOn},
    {213, "выключение добавления маховика", GClass::Other},
    {226, "включение вращения локальной системы координат", GClass::Transform},
    {227, "отмена вращения локальной системы координат", GClass::TransformCancel},
    {239, "оптимизация траектории сплайнового соединения; T — допуск", GClass::DataSetting},
    {240, "отмена оптимизации сплайнового соединения", GClass::Other},
    {310, "инициализация измерительной задачи", GClass::Other},
    {600, "настройка синхронных осей подачи F; адреса — флаги участия", GClass::DataSetting},
    {10, "синхронизация координат с модулем компиляции", GClass::DataSetting},
    {11, "сброс длины инструмента по пяти осям", GClass::DataSetting},
    {12.1, "полярная интерполяция", GClass::Transform},
    {13.1, "отмена полярной интерполяции", GClass::TransformCancel},
    {15, "отмена полярного программирования", GClass::TransformCancel},
    {16, "полярное программирование", GClass::Transform},
    {16.1, "определение полюса", GClass::Transform},
    {29, "возврат из опорной точки", GClass::RotaryAlign},
    {31, "измерительное перемещение", GClass::RotaryAlign},
    {31.1, "измерение инверсивного сигнала", GClass::RotaryAlign},
    {31.2, "измерение с игнорированием изменения сигнала", GClass::RotaryAlign},
    {32, "нарезание резьбы с постоянным шагом", GClass::RotaryAlign},
    {33, "нарезание резьбы", GClass::RotaryAlign},
    {40.2, "отмена коррекции износа радиуса", GClass::CompOff},
    {41.2, "коррекция износа радиуса слева", GClass::CompLeft},
    {42.2, "коррекция износа радиуса справа", GClass::CompRight},
    {40.8, "отмена пятиосевой коррекции радиуса", GClass::CompOff},
    {41.8, "пятиосевая коррекция слева", GClass::Transform},
    {42.8, "пятиосевая коррекция справа", GClass::Transform},
    {50, "отмена масштабирования", GClass::TransformCancel},
    {51, "масштабирование", GClass::Transform},
    {50.1, "отмена зеркального отображения", GClass::TransformCancel},
    {51.1, "зеркальное отображение", GClass::Transform},
    {68, "вращение системы координат", GClass::Transform},
    {69, "отмена вращения системы координат", GClass::TransformCancel},
    {84.6, "настройка оборотов для жёсткого нарезания резьбы", GClass::DataSetting},
    {84.1, "геометрическая ось жёсткого нарезания резьбы", GClass::DataSetting},
    {92, "задание системы координат заготовки", GClass::DataSetting},
    {96.1, "опорная ось постоянной скорости резания", GClass::DataSetting},
    {126, "временная отмена кратчайшего пути C", GClass::Other},
    {127, "восстановление кратчайшего пути C", GClass::Other},
    {132, "выбор шпинделя для резьбы", GClass::DataSetting},
    {193, "подача обратно пропорциональна времени", GClass::UnsupportedFeed},
    {194, "пятиосевая совместная интерполяция", GClass::UnsupportedFeed},
    {198, "возврат цикла на исходную точку", GClass::ReturnInitial},
    {199, "возврат цикла на R", GClass::ReturnR},
    {201.1, "выравнивание по наклонной плоскости с RTCP", GClass::RotaryAlign},
    {201.2, "выравнивание по наклонной плоскости с RTCP", GClass::RotaryAlign},
    {202, "отмена обработки в наклонной плоскости", GClass::TransformCancel},
    {205, "длина инструмента по пяти осям", GClass::DataSetting},
    {206, "временная дополнительная длина инструмента", GClass::DataSetting},
    {212, "добавление маховика", GClass::Other},
    {212.1, "добавление маховика с ограничением", GClass::Other},
    {214, "интерполяция по большой дуге", GClass::Other},
    {215, "отмена интерполяции по большой дуге", GClass::Other},
    {216, "оптимизация скорости микросегментов", GClass::Other},
    {217, "отмена оптимизации скорости микросегментов", GClass::Other},
    {220, "программирование диаметра", GClass::Transform},
    {220.1, "выбор оси диаметра", GClass::Transform},
    {221, "программирование радиуса", GClass::TransformCancel},
    {222, "сохранение информации модуля обработки", GClass::DataSetting},
    {223, "восстановление информации модуля обработки", GClass::DataSetting},
    {237, "запуск сбора данных", GClass::Other},
    {237.1, "запуск сбора данных", GClass::Other},
    {238, "останов сбора данных", GClass::Other},
    {241, "сглаживание скорости поворотной оси", GClass::Other},
    {242, "отмена сглаживания скорости поворотной оси", GClass::Other},
    {243, "активация линейного объединения", GClass::Other},
    {244, "отмена линейного объединения", GClass::Other},
    {245, "оптимизация соединения круговых интерполяций", GClass::Other},
    {246, "отмена оптимизации соединения круговых интерполяций", GClass::Other},
    {247, "оптимизация соединения B-сплайнов", GClass::Other},
    {248, "отмена оптимизации соединения B-сплайнов", GClass::Other},
    {256, "полиномиальная интерполяция", GClass::UnmodeledMotion},
    {257, "отмена полиномиальной интерполяции", GClass::Other},
    {312, "ожидание декодирования", GClass::Other},
    {313, "фиксация координат в локальной СК", GClass::DataSetting},
    {314, "фиксация координат в СК заготовки", GClass::DataSetting},
    {319, "отмена измерительной задачи", GClass::Other},
    {320, "измерительный множитель скорости 100", GClass::Other},
    {321, "отмена измерительного множителя", GClass::Other},
    {323.1, "коническая интерполяция по часовой", GClass::UnmodeledMotion},
    {323.2, "коническая интерполяция по часовой", GClass::UnmodeledMotion},
    {324.1, "коническая интерполяция против часовой", GClass::UnmodeledMotion},
    {324.2, "коническая интерполяция против часовой", GClass::UnmodeledMotion},
    {325, "коническая интерполяция со средним вектором", GClass::UnmodeledMotion},
    {327, "отмена конической интерполяции", GClass::Other},
    {328, "синусоидальные колебания оси", GClass::UnmodeledMotion},
    {329, "отмена синусоидальных колебаний оси", GClass::Other},
    {362, "автоматическое отклонение инструмента", GClass::UnmodeledMotion},
    {363, "отмена автоматического отклонения инструмента", GClass::Other},
    {540, "расширенная система координат", GClass::Transform},
    {642, "подгонка микросегментных сплайнов", GClass::Other},
    {642.6, "отмена подгонки сплайнов", GClass::Other},
    {643, "сглаживание погрешности траектории", GClass::Other},
    {643.6, "отмена сглаживания погрешности траектории", GClass::Other},
    {798, "многократный вызов цикла сверления", GClass::UnmodeledMotion},
    {799, "очистка модальных параметров цикла", GClass::DataSetting}
};
struct MEntry { int code; const char* text; MClass cls = MClass::Other; };
// refs/kede/txt/KEDE_M_codes.txt; no Fanuc machine-specific meanings are inherited.
const MEntry mEntries[] = {
    {0,"программируемый останов",MClass::ProgramStop}, {1,"останов по выбору",MClass::OptionalStop},
    {2,"конец программы",MClass::ProgramEnd}, {30,"конец программы",MClass::ProgramEnd},
    {3,"прямое вращение шпинделя",MClass::SpindleCw}, {4,"обратное вращение шпинделя",MClass::SpindleCcw},
    {5,"останов шпинделя",MClass::SpindleStop}, {6,"смена инструмента",MClass::ToolChange},
    {7,"СОЖ через центр шпинделя",MClass::CoolantOn}, {8,"наружная СОЖ",MClass::CoolantOn},
    {9,"выключение СОЖ",MClass::CoolantOff}, {10,"воздушное охлаждение",MClass::CoolantExtra},
    {15,"позиционирование и зажим шпинделя при точении"}, {16,"отмена точения, тормоз шпинделя",MClass::TurningCancel},
    {19,"позиционирование шпинделя",MClass::SpindleOrient},
    {21,"открытие дверей (опция)"}, {22,"закрытие дверей (опция)"},
    {23,"разжим оснастки (опция)"}, {24,"зажим оснастки (опция)"}, {25,"проверка заготовок (опция)"},
    {26,"включение продувки (опция)"}, {27,"выключение продувки (опция)"},
    {28,"включение позиционирования по нулевой точке"}, {29,"выключение позиционирования по нулевой точке"},
    {31,"разблокировка оси A"}, {32,"блокировка оси A"}, {33,"разблокировка оси C"}, {34,"блокировка оси C"},
    {35,"открытие дверцы магазина"}, {36,"закрытие дверцы магазина"},
    {37,"открытие навеса рабочей зоны (опция)"}, {38,"открытие навеса рабочей зоны (как в таблице KEDE)"},
    {41,"выключение предварительной балансировки"}, {42,"включение предварительной балансировки"},
    {43,"выключение вытяжки масляного тумана"}, {44,"включение вытяжки масляного тумана"},
    {45,"включение датчика вибрации"}, {46,"выключение датчика вибрации"},
    {47,"запуск RFID"}, {48,"выключение RFID"},
    {63,"включение лазерного измерителя"}, {64,"выключение лазерного измерителя"},
    {65,"включение датчика резцов"}, {66,"выключение датчика резцов"},
    {67,"проверка измерителя инструмента"}, {68,"проверка беспроводного датчика"}, {69,"проверка датчика резцов"},
    {101,"удаление информации"}, {151,"токарный зажим/ориентация шпинделя"},
    {152,"токарный зажим/ориентация шпинделя"}, {153,"токарный зажим/ориентация шпинделя"},
    {154,"токарный зажим/ориентация шпинделя"},
    {253,"сигнал нарушения допуска G963 (SubPrgs/G963.nc)"}
};

class Gnc62Dialect final : public IDialect {
public:
    Gnc62Dialect() {
        // Only shared codes actually documented for GNC62; the classes come from IDialect.
        for (const int code : {0,1,2,3,17,18,19,20,21,40,41,42,43,44,49,54,55,56,57,58,59,
                              84,90,91,94,95}) {
            const auto* base = findDialect("fanuc-mill");
            g_[canonicalCode('G', code)] = {*base->g(code), *base->describeG(code)};
            g_[canonicalCode('G', code)].second.source = "gnc62";
        }
        for (const auto& e : entries) {
            const auto key = canonicalCode('G', e.code);
            g_[key] = {GInfo{e.cls,e.group}, CodeInfo{key,e.text,true,"GNC62 programming manual 2021"}};
        }
        const auto add = [&](int number, const char* text, const char* required, bool probe = true,
                             const char* wcs = "", bool home = false, bool writes = false) {
            DefinitionInfo d; d.number = number; d.description = text; d.measuring = true;
            d.requiresG94 = probe; d.wcsAddress = wcs; d.homeZ = home; d.writesWcs = writes;
            std::istringstream in(required); std::string address;
            while (in >> address) d.required.push_back(address);
            definitions_[number] = std::move(d);
        };
        // Required addresses are restricted to demonstrated safety checks, not every optional format field.
        // Source: measuring_guide/02_CYCLES_REFERENCE.md, sections 8, 10, 14.2-14.4.
        add(910,"калибровка длины щупа","H Z"); add(911,"калибровка радиуса щупа","D");
        for (int n = 912; n <= 919; ++n) add(n,"старое семейство измерительных циклов","",true,"",false,true);
        add(920,"измерение точки","A E",true,"AN");
        add(921,"расстояние между точками","A E",true,"AN");
        add(922,"измерение отверстия","D",true,"AN");
        add(923,"измерение цапфы","D H AH X Y E",true,"AN");
        add(924,"измерение прямоугольного кармана","AC AD H AH X Y E",true,"AN",true);
        add(925,"измерение прямоугольной цапфы","B C H AH X Y",true,"AN",true);
        add(926,"старое семейство измерительных циклов","",true,"",false,true);
        add(927,"диаметр и компенсация износа","D H AH X Y",true,"AN",true);
        add(930,"СК по точке","A E",true,"AN",false,true);
        add(931,"СК по двум точкам","A E",true,"AN",true,true);
        add(932,"СК по отверстию","D",true,"AN",true,true);
        add(933,"СК по круглой цапфе","D H AH X Y E",true,"AN",false,true);
        add(934,"СК по карману","AC AD H AH X Y",true,"AN",true,true);
        add(935,"СК по прямоугольной цапфе","AC AD H AH X Y",true,"AN",true,true);
        add(940,"лазер: длина инструмента","",false,"",true);
        add(941,"лазер: радиус инструмента","",false,"",true);
        add(942,"лазер: контроль поломки","A",false,"",true);
        add(950,"механический ноль A","D X Y Z P");
        add(951,"калибровка WRCS","A D H X Y Z");
        add(952,"торцевое биение","R Z C D H",true,"",true);
        add(953,"радиальное биение","D H AH X Y",true,"",true);
        add(954,"перепад высот","E",true,"AN");
        add(957,"alfa: угол C","A W",true,"W",false,true);
        add(960,"выравнивание по трём точкам","",true,"",true,true);
        add(961,"выравнивание по C","E",true,"AN",true,true);
        add(962,"угол по отверстиям","D F",true,"AN",true,true);
        add(963,"проверка допуска","",false);
        add(965,"выравнивание по A: два касания Z","E",true,"AN",false,true);
        add(966,"выравнивание по A: два касания Y","E",true,"AN",false,true);
        add(830,"старый цикл: центр отверстия","",true,"",false,true);
        add(831,"старый цикл: центр цапфы","",true,"",false,true);
        add(832,"старый цикл: ноль Z","",true,"",false,true);
        add(971,"alfa: отверстие","W D",true,"W",false,true);
        add(972,"alfa: бобышка","W D H",true,"W",false,true);
        add(973,"alfa: карман","W X Y",true,"W",false,true);
        add(974,"alfa: блок","W X Y H",true,"W",false,true);
        add(975,"alfa: паз","W",true,"W",false,true);
        add(976,"alfa: стенка","W H",true,"W",false,true);
        add(977,"alfa: угол C","A",true,"W",false,true);
        add(978,"alfa: угол детали","A W D X Y H",true,"W",false,true);
        add(979,"alfa: поверхность","W D E H",true,"W",false,true);
        for (const int n : {923,924,933}) definitions_[n].positive.push_back("E");
        for (const int n : {932,934}) definitions_[n].defect = "GOTO 290: в предоставленной подпрограмме нет N290";
        definitions_[935].defect = "GOTO 230: в предоставленной подпрограмме нет N230";
        definitions_[960].defect = "в предоставленной подпрограмме зашиты G56 и перезапись G55";
        for (const int n : {800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,840,841,847,850,851,862,880,881,900,901,996,999}) {
            DefinitionInfo d; d.number = n; d.description = "подпрограмма обработки/переключения режимов GNC62";
            definitions_[n] = d;
        }
    }
    std::string_view id() const override { return "gnc62"; }
    std::string_view name() const override { return "KEDE GNC62, фрезерование и точение"; }
    std::optional<GInfo> g(double v) const override {
        const auto it = g_.find(canonicalCode('G',v));
        if (it != g_.end()) return it->second.first;
        if (v == static_cast<int>(v) && definition(static_cast<int>(v))) return GInfo{GClass::MacroCall,0};
        return std::nullopt;
    }
    std::optional<CodeInfo> describeG(double v) const override {
        const auto key = canonicalCode('G',v); const auto it = g_.find(key);
        if (it != g_.end()) return it->second.second;
        if (v == static_cast<int>(v)) if (const auto d = definition(static_cast<int>(v)))
            return CodeInfo{key,d->description,true,"KEDE SubPrgs / measuring guide"};
        return std::nullopt;
    }
    std::optional<MClass> m(int v) const override {
        for (const auto& e : mEntries) if (e.code == v) return e.cls;
        return std::nullopt;
    }
    std::optional<CodeInfo> describeM(int v) const override {
        for (const auto& e : mEntries) if (e.code == v) return CodeInfo{canonicalCode('M',v),e.text,true,"KEDE M-code table"};
        return std::nullopt;
    }
    int axisIndex(char letter) const override {
        const auto at = std::string_view(kAxisLetters).find(letter);
        return at == std::string_view::npos ? -1 : static_cast<int>(at);
    }
    bool isLinearAxis(int index) const override { return index >= 0 && index < 3; }
    bool isKnownAddress(char letter) const override { return letter >= 'A' && letter <= 'Z'; }
    LexRules lexicalRules() const override { return {true}; }
    bool uncertainHomeIntermediate() const override { return true; }
    bool feedDwellSeconds() const override { return true; }
    bool requiresMinuteFeed(std::string_view name) const override { return name == "prb_xyz" || name == "prb_xyz.nc"; }
    std::string_view machineHeaderKey() const override { return "machine:"; }
    std::optional<DefinitionInfo> definition(int n) const override {
        const auto it = definitions_.find(n); return it == definitions_.end() ? std::nullopt : std::optional(it->second);
    }
    const std::vector<MachineProfile>& machines() const override { return machines_; }
private:
    std::map<std::string,std::pair<GInfo,CodeInfo>> g_;
    std::map<int,DefinitionInfo> definitions_;
    std::vector<MachineProfile> machines_;
};
}
const IDialect& gnc62Dialect() { static const Gnc62Dialect d; return d; }
const IDialect* detectDialect(std::string_view text) {
    // Require a controller identification in a comment; a tool name cannot select a dialect.
    while (!text.empty()) {
        const auto nl = text.find('\n'); auto line = text.substr(0,nl);
        const auto at = line.find('@');
        if (at != std::string_view::npos) {
            std::string key;
            for (const auto c : line.substr(at + 1)) {
                if (c == ' ' || c == '\t' || c == '\r') continue;
                key += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            }
            if (key.find("MACHINE:UNI.5800UT,GNC62") != std::string::npos) return &gnc62Dialect();
        }
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return nullptr;
}
} // namespace gca
