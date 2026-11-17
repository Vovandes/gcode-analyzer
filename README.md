# gcode-analyzer

Анализатор управляющих программ ЧПУ (Fanuc, фрезерование, 3 оси + индексная ось B) для разработки постпроцессоров (SprutCAM и др.).

По образцу рабочей программы строит текстовый отчёт: оценка времени обработки, список и порядок инструментов, рабочие СК, подачи и обороты, габариты траектории, статистика G/M-кодов, циклы сверления, предупреждения и **шпаргалка для постпроцессора** (структура программы, формат кадра, модальность, комментарии, поворот B).

Текущая версия — в файле [`VERSION`](VERSION), история изменений — в [`CHANGELOG.md`](CHANGELOG.md).

## Скачать готовую программу

Страница [Releases](../../releases) → `gcode-analyzer-<версия>-win-x64.zip`. Ничего устанавливать не нужно (ни Visual C++, ни .NET), работает на Windows 10/11 x64. Распакуйте куда угодно — в том числе на флешку и в путь с русскими буквами и пробелами.

С версии 0.9.0 окно включает .NET 10. Для старой Windows 10 без поддержки CET используйте `gcode-analyzer-<версия>-win-x64-compat.zip`: у окна отключён CET, CLI одинаковый в обоих архивах. Совместимость с конкретной машиной KEDE Probe нужно проверить на ней.

В папке:
- `GcodeAnalyzerGui.exe` — окно;
- `gcode-analyzer.exe` — консольный анализатор;
- `samples/` — примеры программ;
- `machines/` — профили станков, включая незаполненный шаблон KEDE;
- `README.txt` — краткая инструкция.

Если Windows блокирует скачанный файл: «Свойства» zip-архива → «Разблокировать» до распаковки, или «Подробнее» → «Выполнить в любом случае».

## Окно

Запустите `GcodeAnalyzerGui.exe`, нажмите «Открыть…» или перетащите файл `.nc` в окно, затем «Анализировать».
- отчёт показывается в окне (список разделов слева) или сохраняется в `.txt`; флажок BOM — для Блокнота;
- поле аргументов (например, `--rapid 12000`) и кнопка «Справка по аргументам»;
- выбор профиля станка из списка (по умолчанию — автоопределение);
- «Открыть профиль…» выбирает INI-файл; вкладка «Пределы станка» показывает проверки, нарушения выделены красным в таблице, программе и обводке траектории;
- `gcode-analyzer.exe` должен лежать рядом с окном.

## Консоль

```bat
gcode-analyzer.exe program.nc                       :: отчёт в консоль
gcode-analyzer.exe program.nc --out report.txt      :: отчёт в файл (UTF-8)
gcode-analyzer.exe program.nc --out report.txt --bom :: с BOM для Блокнота
gcode-analyzer.exe program.nc --out report.txt --json analysis.json :: TXT и JSON за один анализ
gcode-analyzer.exe program.nc --rapid 15000         :: скорость G0 для оценки времени, мм/мин (по умолчанию 10000)
gcode-analyzer.exe program.nc --machine auto        :: профиль станка (auto — по комментарию «Generated for ...»)
gcode-analyzer.exe program.nc --machine-file machines\my-machine.ini --wcs-offset G54=X0Y0Z0
gcode-analyzer.exe --list-machines                  :: профили станков
gcode-analyzer.exe --help | --version | --list-options
```

Консоль Windows сама переключается на UTF-8: русский текст и пути с кириллицей и пробелами работают без `chcp`.
Коды возврата: 0 — успех, 1 — ошибка ввода-вывода, 2 — ошибка аргументов.

`--json` пишет UTF-8 без BOM независимо от `--bom`; описание полей — [docs/JSON.md](docs/JSON.md). Текстовый отчёт сохраняет прежний формат.

Машинно-специфичные коды берутся из профиля станка: Sky Master VL1100A (`M15` — дополнительная подача СОЖ), Bridgeport 1300/480 (`M53` — СОЖ через шпиндель).

Профили автоматически загружаются из `machines/` рядом с **CLI**, независимо от текущей папки. INI с тем же id дополняет встроенный профиль. Формат, источники чисел и формулы времени — [docs/MACHINE_PROFILES.md](docs/MACHINE_PROFILES.md). Без заданных ускорений используется оценка без ускорений; прежнее время остаётся в отчёте рядом с новой оценкой. `machines/kede-uni800ut.ini` содержит подтверждённые параметры KEDE UNI 800 UT из выгрузки стойки; пустая заготовка `kede-template.ini` сохранена. Синтетический пример `tests/data/machine-stage11.*` не описывает реальный станок.

С версии 0.11.1 добавлен [диалект GNC62](docs/GNC62.md): автоопределение по шапке `MACHINE: Uni.5 800UT, GNC 62` или `--dialect gnc62`, именованные инструменты, GDEF/GCALL, проверки измерительных циклов и пределы A/C. При отсутствии этой шапки признаки `@`/GDEF/GCALL дают подсказку выбора диалекта. Макросы, внутренние ходы измерения и полная кинематика RTCP не исполняются: неполнота оценки явно обозначена.

## Сборка из исходников

C++: CMake ≥ 3.21 и компилятор C++20 (GCC 11+, Clang 14+, MSVC 2022+). Окно: .NET 10 SDK (только Windows). `global.json` выбирает последний установленный стабильный SDK 10.0, начиная с 10.0.100.

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug              # Linux
cmake --preset msvc  && cmake --build --preset msvc-debug && ctest --preset msvc-debug    # Windows
dotnet build gui/GcodeAnalyzer.sln -c Debug && dotnet test gui/GcodeAnalyzer.sln -c Debug  # окно
powershell -ExecutionPolicy Bypass -File scripts/package.ps1                               # портативный zip в dist/
```

Упаковка по умолчанию создаёт оба zip. Для одного варианта: `scripts/package.ps1 -Variant standard` или `-Variant compat`. Проверка архива: `scripts/smoke-package.ps1 -Zip dist/<имя>.zip`; проверка CET: `scripts/check-cet.ps1 -Exe <путь> -Expect on|off`. Скрипты поддерживают PowerShell 5.1; старые каталоги сборок и посторонние архивы не удаляются.

## Выпуск версии

1. Поднять номер в `VERSION` и дописать `CHANGELOG.md`.
2. `git tag vX.Y.Z && git push --tags` — GitHub Actions соберёт zip, проверит его и создаст релиз.

Автосборка (`.github/workflows/ci.yml`): Linux GCC и Clang, Windows MSVC, окно .NET с тестами, упаковка zip со smoke-тестом в папке с русским именем. Артефакт zip сохраняется только для `main` и тегов, 7 дней.

## Структура
- `include/gca/`, `src/` — библиотека `gca_core` и CLI
- `gui/` — окно WinForms (`GcodeAnalyzer.Gui`) и его тесты xUnit
- `scripts/` — упаковка zip и проверка зависимостей
- `samples/` — реальные УП-образцы (не изменять)
- `tests/` — тесты Catch2; `tests/data/` синтетические программы; `tests/golden/` эталоны отчётов (обновление: создать пустой `tests/golden/UPDATE`, запустить тесты, удалить файл, проверить `git diff`)
