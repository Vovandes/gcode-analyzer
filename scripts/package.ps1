# Собирает портативный архив dist\gcode-analyzer-<версия>-win-x64.zip.
# Запуск: powershell -ExecutionPolicy Bypass -File scripts/package.ps1 [-Version 0.8.1]
# Совместим с PowerShell 5.1. Нужны: Visual Studio (C++), CMake, .NET 8 SDK.
param([string]$Version = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Invoke-Checked([scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "Команда завершилась с кодом ${LASTEXITCODE}: $Cmd" }
}

# Единый номер версии — файл VERSION; параметр -Version (CI по тегу) перезаписывает его
$versionFile = Join-Path $root 'VERSION'
if ($Version) { [IO.File]::WriteAllText($versionFile, "$Version`n") }
$Version = (Get-Content $versionFile -TotalCount 1).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Некорректная версия в VERSION: '$Version'" }
Write-Host "Версия: $Version"

$name = "gcode-analyzer-$Version"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name-win-x64.zip"
$cliBuild = Join-Path $root 'build\package'
$guiOut = Join-Path $root 'build\package-gui'

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null

# 1. CLI: Release, статический рантайм (задан в CMakeLists.txt), без тестов
Invoke-Checked { cmake -S $root -B $cliBuild -A x64 -DGCA_BUILD_TESTS=OFF }
Invoke-Checked { cmake --build $cliBuild --config Release }
$cliExe = Join-Path $cliBuild 'Release\gcode-analyzer.exe'
& (Join-Path $PSScriptRoot 'check-deps.ps1') -Exe $cliExe

# 2. Окно: self-contained single-file
if (Test-Path $guiOut) { Remove-Item $guiOut -Recurse -Force }
Invoke-Checked {
    dotnet publish (Join-Path $root 'gui\GcodeAnalyzer.Gui\GcodeAnalyzer.Gui.csproj') -c Release -r win-x64 `
        --self-contained true -o $guiOut `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:EnableCompressionInSingleFile=true -p:PublishTrimmed=false -p:DebugType=None -p:DebugSymbols=false
}

# 3. Состав папки
Copy-Item $cliExe $stage
Copy-Item (Join-Path $guiOut 'GcodeAnalyzerGui.exe') $stage
New-Item -ItemType Directory -Path (Join-Path $stage 'samples') | Out-Null
Copy-Item (Join-Path $root 'samples\*.nc') (Join-Path $stage 'samples')
$readme = (Get-Content (Join-Path $PSScriptRoot 'README.template.txt') -Raw -Encoding UTF8).Replace('{VERSION}', $Version)
$readme = $readme -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, (New-Object Text.UTF8Encoding($true)))

# 4. zip (одна папка внутри)
# ZipFile, а не Compress-Archive: в PowerShell 5.1 тот пишет пути с обратным слэшем
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)

Get-ChildItem $stage -Recurse -File | ForEach-Object {
    '{0,12:N0}  {1}' -f $_.Length, $_.FullName.Substring($stage.Length + 1)
}
'Архив: {0} ({1:N1} МБ)' -f $zip, ((Get-Item $zip).Length / 1MB)
