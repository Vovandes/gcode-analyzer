GcodeAnalyzer {VERSION} — анализатор управляющих программ ЧПУ
==============================================================================

Ничего устанавливать не нужно: ни Visual C++, ни .NET. Работает на Windows 10/11 x64.
Окно включает .NET 10. Сборка: {VARIANT}.
Папку можно распаковать куда угодно (в том числе на флешку, в путь с русскими буквами и пробелами).

1. Окно (самый простой способ)
   Запустите GcodeAnalyzerGui.exe. Нажмите «Открыть…» (или перетащите файл .nc в окно),
   затем «Анализировать». Результат можно показать в окне или сохранить в .txt
   («Сохранить как txt»). Для открытия txt в Блокноте включите «BOM для Блокнота».
   «Сохранить PDF…» создаёт цветной отчёт А4 с таблицами, видами XY и XZ
   в 300 dpi и шпаргалкой для постпроцессора. На вкладке «Сравнение» PDF
   содержит наложение траекторий и отчёт о различиях старой и новой УП.
   Примеры программ лежат в папке samples.
   Кнопка «Открыть профиль…» выбирает INI-файл станка; проверки показаны на вкладке
   «Пределы станка». Нарушения выделены красным, включая обводку траектории.
   Профили лежат в machines рядом с exe и читаются независимо от текущей папки.
   kede-template.ini — незаполненный шаблон, паспортные данные нужно уточнить.
   kede-uni800ut.ini — профиль KEDE UNI 800 UT для диалекта GNC62.
   Пустые ускорения: «оценка без ускорений»; прежнее время сохранено рядом с новым.

2. Консоль (cmd или PowerShell), перейдите в эту папку:
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc --out report.txt
   gcode-analyzer.exe samples\sample-01-skymaster-vl1100a.nc --out report.txt --json analysis.json
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

4. Библиотека PDF
   PDFsharp-MigraDoc-GDI 6.2.4 (MIT), https://docs.pdfsharp.net/

Copyright (c) 2001-2026 empira Software GmbH, Troisdorf (Cologne Area), Germany

MIT License
Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
