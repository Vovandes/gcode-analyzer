# Этап 8 — портативный архив (zip) для Windows 10 x64

Прочитай `CLAUDE.md`, `docs/SPEC.md`, `REPORT.md`. Работай в ветке `stage8-package` (от `main`), в конце — push ветки, PR не сливай.

## Цель
Один zip, который Владимир копирует на изолированный рабочий компьютер (Windows 10 x64, интернета и прав админа может не быть), распаковывает в любую папку (в т.ч. с русскими буквами и пробелами в пути, флешка) и запускает. **Ничего не устанавливать**: ни Visual C++ Redistributable, ни .NET. Размер архива 70–150 МБ — допустимо.

## Состав архива
`gcode-analyzer-<версия>-win-x64.zip` → одна папка `gcode-analyzer-<версия>/`:
- `GcodeAnalyzerGui.exe` — окно (WinForms), self-contained .NET 8, **single-file** (`PublishSingleFile`, `IncludeNativeLibrariesForSelfExtract`, `EnableCompressionInSingleFile`; без trimming — WinForms его не поддерживает), `RuntimeIdentifier=win-x64`.
- `gcode-analyzer.exe` — CLI, собран MSVC Release со **статическим рантаймом** (`CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded$<$<CONFIG:Debug>:Debug>`, политика CMP0091). Не должен зависеть от `VCRUNTIME140*.dll`, `MSVCP140*.dll`, `api-ms-win-crt-*` — проверь `dumpbin /dependents` (в CI — обязательно).
- `samples/` — копии двух примеров.
- `README.txt` (UTF-8 с BOM, по-русски, коротко): как запустить окно, как запустить CLI из консоли (примеры с `--out`, `--rapid`, `--machine`), что делать, если Windows пишет «Защитник Windows предупреждает» / файл заблокирован после скачивания (Свойства → «Разблокировать», или «Подробнее → Выполнить в любом случае»).
Ничего лишнего (pdb и т.п.).

## Версия
- Единый номер версии (например, `0.8.0`) в одном месте; CLI печатает его по `--version`, окно — в заголовке.
- В CI при теге `v*` версия берётся из тега.

## Сборка
1. `scripts/package.ps1` (совместим с PowerShell 5.1): собирает CLI (Release, статический рантайм), `dotnet publish` окна, складывает папку, создаёт zip в `dist/`. Запуск локально одной командой: `powershell -ExecutionPolicy Bypass -File scripts/package.ps1`. `dist/` — в `.gitignore`.
2. Окно уже ищет `gcode-analyzer.exe` рядом с собой — убедись, что в single-file сборке путь берётся от `AppContext.BaseDirectory` / `Environment.ProcessPath`, а не от временной папки распаковки.
3. Тесты xUnit и Catch2 остаются зелёными; добавь тест, что поиск exe рядом с собой работает при пути с кириллицей и пробелами (если ещё нет).

## CI (`.github/workflows/ci.yml`)
Новая задача `package` на `windows-latest` (после зелёных тестов):
- запускает `scripts/package.ps1`;
- проверка зависимостей CLI через `dumpbin` (fail, если есть VC runtime DLL);
- smoke-тест: распаковать zip в `C:\тест папка\`, выполнить там `gcode-analyzer.exe --version` и анализ `samples\sample-01-skymaster-vl1100a.nc --out "C:\тест папка\отчёт.txt"`, проверить, что отчёт не пустой и содержит раздел со временем;
- проверить, что `GcodeAnalyzerGui.exe` есть в архиве и больше 50 МБ (значит, self-contained);
- `actions/upload-artifact` — zip на каждый push (хранение 30 дней);
- при теге `v*` — GitHub Release с zip (`permissions: contents: write`, `gh release create` или `softprops/action-gh-release`).
Изменение CI объясни в `REPORT.md`.

## Готово, когда
- CI зелёный на всех задачах, zip скачивается как артефакт со страницы run.
- В `REPORT.md`: размер zip, список файлов внутри, вывод `dumpbin /dependents` для CLI, результат smoke-теста, как выпустить версию (`git tag v0.8.0 && git push --tags`), и короткий чек-лист для Владимира: скачать zip, распаковать в папку с русским именем, запустить окно, открыть пример, сохранить txt; то же — с флешки.
