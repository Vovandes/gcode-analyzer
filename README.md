# gcode-analyzer

Анализатор управляющих программ ЧПУ (Fanuc, фрезерование, 3 оси + индексная ось B) для разработки постпроцессоров (SprutCAM и др.).

По образцу рабочей программы строит текстовый отчёт: оценка времени обработки, список и порядок инструментов, рабочие СК, подачи и обороты, габариты траектории, статистика G/M-кодов, циклы сверления, предупреждения и **шпаргалка для постпроцессора** (структура программы, формат кадра, модальность, комментарии, поворот B).

> Статус: **каркас проекта**. Реализация ведётся по этапам из [`docs/SPEC.md`](docs/SPEC.md). Текущее состояние — в [`REPORT.md`](REPORT.md).

## Сборка

Требуется CMake ≥ 3.21 и компилятор с C++20 (GCC 11+, Clang 14+, MSVC 2022+).

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug   # Linux
cmake --preset msvc  && cmake --build --preset msvc-debug && ctest --preset msvc-debug   # Windows
```

## Использование

```bash
gcode-analyzer program.nc                    # отчёт в консоль
gcode-analyzer program.nc --out report.txt   # отчёт в файл
```

## Структура
- `docs/SPEC.md` — спецификация, факты о формате образцов, этапы
- `docs/PROMPT.md` — промпт для Claude Code
- `CLAUDE.md` — правила для Claude Code
- `samples/` — реальные УП-образцы (не изменять)
- `include/gca/`, `src/` — библиотека `gca_core` и CLI
- `tests/` — тесты Catch2
