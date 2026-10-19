GcodeAnalyzer {VERSION} — анализатор управляющих программ Fanuc (фрезерование)
==============================================================================

Ничего устанавливать не нужно: ни Visual C++, ни .NET. Работает на Windows 10 x64.
Папку можно распаковать куда угодно (в том числе на флешку, в путь с русскими буквами и пробелами).

1. Окно (самый простой способ)
   Запустите GcodeAnalyzerGui.exe. Нажмите «Открыть…» (или перетащите файл .nc в окно),
   затем «Анализировать». Результат можно показать в окне или сохранить в .txt
   («Сохранить как txt»). Для открытия txt в Блокноте включите «BOM для Блокнота».
   Примеры программ лежат в папке samples.

2. Консоль (cmd или PowerShell), перейдите в эту папку:
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc --out report.txt
   gcode-analyzer.exe samples\sample-02-bridgeport-1300-480.nc --rapid 15000 --out report.txt
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc --machine auto
   gcode-analyzer.exe --help          (все аргументы)
   gcode-analyzer.exe --version
   gcode-analyzer.exe --list-machines (профили станков)

3. Если Windows предупреждает
   Файлы не подписаны, поэтому после скачивания Windows может заблокировать их.
   - «Защитник Windows предупреждает»: нажмите «Подробнее» -> «Выполнить в любом случае».
   - Файл заблокирован после скачивания: щёлкните правой кнопкой по zip-архиву ДО распаковки ->
     «Свойства» -> внизу отметьте «Разблокировать» -> «ОК», затем распакуйте заново.
   - Окно пишет, что не найден gcode-analyzer.exe: он должен лежать рядом с GcodeAnalyzerGui.exe.
     Не переносите файлы по отдельности.
