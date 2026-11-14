# Собирает портативный архив dist\gcode-analyzer-<версия>-win-x64.zip.
# Запуск: powershell -ExecutionPolicy Bypass -File scripts/package.ps1 [-Variant standard|compat|all]
# Совместим с PowerShell 5.1. Нужны: Visual Studio (C++), CMake, .NET 10 SDK.
param([string]$Version = '', [ValidateSet('standard', 'compat', 'all')][string]$Variant = 'all')

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
$cliBuild = Join-Path $root 'build\package'
$runDirectory = Join-Path $root ("build\package-staging\" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dist -Force | Out-Null

# 1. CLI: Release, статический рантайм (задан в CMakeLists.txt), без тестов
Invoke-Checked { cmake -S $root -B $cliBuild -A x64 -DGCA_BUILD_TESTS=OFF }
Invoke-Checked { cmake --build $cliBuild --config Release }
$cliExe = Join-Path $cliBuild 'Release\gcode-analyzer.exe'
& (Join-Path $PSScriptRoot 'check-deps.ps1') -Exe $cliExe
& (Join-Path $PSScriptRoot 'check-cet.ps1') -Exe $cliExe -Expect off

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$variants = if ($Variant -eq 'all') { @('standard', 'compat') } else { @($Variant) }
foreach ($item in $variants) {
    # 2. Окно: отдельная публикация каждого варианта.
    $guiOut = Join-Path $root "build\package-gui-$item"
    # SDK проверяет время singlefilehost, но не смену CETCompat: кэш должен быть отдельным.
    $guiIntermediate = Join-Path $runDirectory "$item\obj\"
    $cet = if ($item -eq 'compat') { 'false' } else { 'true' }
    $expected = if ($item -eq 'compat') { 'off' } else { 'on' }
    Invoke-Checked {
        dotnet publish (Join-Path $root 'gui\GcodeAnalyzer.Gui\GcodeAnalyzer.Gui.csproj') -c Release -r win-x64 `
            --self-contained true -o $guiOut `
            -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
            -p:EnableCompressionInSingleFile=true -p:PublishTrimmed=false -p:DebugType=None -p:DebugSymbols=false `
            "-p:CETCompat=$cet" "-p:IntermediateOutputPath=$guiIntermediate"
    }
    $guiExe = Join-Path $guiOut 'GcodeAnalyzerGui.exe'
    & (Join-Path $PSScriptRoot 'check-cet.ps1') -Exe $guiExe -Expect $expected

    # 3. Новое staging-дерево сохраняет предыдущие сборки и посторонние файлы dist.
    $stage = Join-Path $runDirectory "$item\$name"
    New-Item -ItemType Directory -Path (Join-Path $stage 'samples') -Force | Out-Null
    Copy-Item -LiteralPath $cliExe -Destination $stage
    Copy-Item -LiteralPath $guiExe -Destination $stage
    Copy-Item (Join-Path $root 'samples\*.nc') (Join-Path $stage 'samples')
    Copy-Item -LiteralPath (Join-Path $root 'machines') -Destination (Join-Path $stage 'machines') -Recurse
    $description = if ($item -eq 'compat') { 'вариант для старой Windows 10 без поддержки CET' } else { 'стандартный вариант (.NET 10, CET включён)' }
    $readme = (Get-Content (Join-Path $PSScriptRoot 'README.template.txt') -Raw -Encoding UTF8).Replace('{VERSION}', $Version).Replace('{VARIANT}', $description)
    $readme = $readme -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, (New-Object Text.UTF8Encoding($true)))

    # 4. ZipArchive даёт пути с прямым слэшем, одна папка внутри.
    # Перезаписывается только запрошенный архив; другие файлы не удаляются.
    $suffix = if ($item -eq 'compat') { '-compat' } else { '' }
    $zip = Join-Path $dist "$name-win-x64$suffix.zip"
    $stream = [IO.File]::Open($zip, [IO.FileMode]::Create, [IO.FileAccess]::Write)
    try {
        $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
                $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, "$name/$relative", [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
    Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        '{0,12:N0}  {1}' -f $_.Length, $_.FullName.Substring($stage.Length + 1)
    }
    'Архив: {0} ({1:N1} МБ)' -f $zip, ((Get-Item $zip).Length / 1MB)
}
