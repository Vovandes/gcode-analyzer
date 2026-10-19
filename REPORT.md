# REPORT — состояние работ

_Обновляется Claude Code в конце каждой сессии/этапа (см. `CLAUDE.md`)._

## Дата и этап
2026-10-19, этап 1 (лексер и модальное состояние) завершён.

## Что сделано
- `lexer` (`include/gca/lexer.hpp`, `src/lexer.cpp`): строка -> `Block` со словами (`Word` хранит `rawText`, смещение, признак пробела перед словом, регистр), комментарии `(...)` и `;`, `%`, `O` без номера, `N`, `/`, CRLF/LF/смешанные, слитные слова (`G05.1Q0`, `T1M6`, `M6T4`). Числа парсятся собственной локале-независимой функцией (`parseNumber`), без `std::from_chars` для double. Ошибки идут в `Program::diagnostics`, исключений нет.
- `dialect` (`include/gca/dialect.hpp`, `src/dialect.cpp`): `IDialect`, `FanucMillDialect` (таблицы G/M с модальными группами, классами и русскими описаниями), реестр `findDialect`, `Profile` = диалект + профиль станка. Профили станков (`MachineProfile`) выбираются автоматически по комментарию `Generated for ...`: Sky Master (M15 = доп. СОЖ), Bridgeport (M53 = СОЖ через шпиндель).
- `knowledge` (`include/gca/knowledge.hpp`): `ICodeKnowledgeProvider`, встроенный провайдер, заглушка онлайн-поиска (всегда пусто), цепочка `KnowledgeChain`.
- `modal_state` (`include/gca/modal_state.hpp`, `src/modal_state.cpp`): `ModalMachine::apply(Block)` -> `BlockEffect` (сегменты движения и события инструмента). Состояние: G0..G3/циклы, плоскость, G90/G91, G94/G95, СК (включая `G54.1 Pn` и `G54 Pn`), G41/G42/G40, G43/G49, T/M6/предвыбор, S, F, шпиндель, СОЖ, позиция с флагами «неизвестна».
- Анализаторы (`include/gca/analyzers/`): интерфейс `IAnalyzer`, `CodeStatsAnalyzer` (статистика G/M, `?` для неизвестных), `ToolsAnalyzer` (порядок использования, предвыбор, описания, H/D).
- `report` + `TextReportWriter` (`writeTextReport`), конвейер в `src/analyzer.cpp`, тонкий CLI (`src/main.cpp`).

## Как собрать и запустить
Как в `CLAUDE.md`: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`.

## Результаты тестов
Linux, GCC 13.3, Debug: `100% tests passed, 0 tests failed out of 32`; Catch2: `All tests passed (322 assertions in 32 test cases)`. Сборка clang без предупреждений. MSVC локально не проверялся (CI).

## Проверка на образцах
- Образец 1: G0 181, G1 1179, G2 192, G3 852, G81 42, G83 12; инструменты T1 T3 T6 T11 T4 T7 T6 T7. Всё совпадает с SPEC, закреплено тестами.
- Образец 2: G0 61, G1 918, G2 570, G3 304, G42 570, G81 14, G83 14; инструменты T4 T6 T12 T8 T6 T14 T15.
- Лексер: 0 диагностик на обоих образцах.

## Известные проблемы и ограничения
- После `G80` группа 01 считается равной `G0` (как у Fanuc), поэтому `X.. Y..` после `G80` это ускоренное перемещение.
- `G4 P`: без десятичной точки миллисекунды, с точкой секунды (Fanuc). `G4 X` всегда секунды.
- Позиция после `G28` по указанным осям неизвестна, пока ось не задана абсолютно.

## Следующие шаги
Этап 2: геометрия дуг, время, габариты.
