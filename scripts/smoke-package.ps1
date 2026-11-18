# Один сценарий проверки архива для локального запуска и CI, PowerShell 5.1.
param([Parameter(Mandatory = $true)][string]$Zip, [string]$Destination = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) {
    $Destination = Join-Path $root ("build\тест папка\" + [Guid]::NewGuid().ToString('N'))
}
Expand-Archive -LiteralPath $Zip -DestinationPath $Destination
$folders = @(Get-ChildItem -LiteralPath $Destination -Directory)
if ($folders.Count -ne 1) { throw 'В архиве должна быть одна папка' }
$app = $folders[0].FullName
$cli = Join-Path $app 'gcode-analyzer.exe'
& $cli --version
if ($LASTEXITCODE -ne 0) { throw '--version failed' }
foreach ($sample in @('sample-01-skymaster-vl1100a', 'sample-02-bridgeport-1300-480')) {
    $report = Join-Path $Destination "$sample-отчёт.txt"
    $json = Join-Path $Destination "$sample-данные.json"
    & $cli (Join-Path $app "samples\$sample.nc") --out $report --json $json
    if ($LASTEXITCODE -ne 0) { throw 'analysis failed' }
    if ((Get-Item -LiteralPath $report).Length -lt 1000) { throw 'report is empty' }
    if (-not (Select-String -LiteralPath $report -Pattern 'Время обработки' -Encoding UTF8 -Quiet)) { throw 'no time section' }
    $data = Get-Content -LiteralPath $json -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($data.schema -ne 1 -or $data.segments.Count -lt 1 -or $data.time.totalSec -le 0) { throw 'invalid JSON analysis' }
    if (@($data.warnings | Where-Object { $_.severity -ne 'info' }).Count -ne 0) { throw 'unexpected sample warnings' }
    Write-Host "OK: $sample, segments=$($data.segments.Count), time=$($data.time.totalSec)"
}
$gui = Get-Item -LiteralPath (Join-Path $app 'GcodeAnalyzerGui.exe')
# Размер сжатого .NET 10 меньше прежних 50 МиБ. Проверяем сам bundle и runtimeconfig.
# Формат .NET: dotnet/runtime, Microsoft.NET.HostModel/Bundle/{Manifest,FileEntry}.cs.
$bytes = [IO.File]::ReadAllBytes($gui.FullName)
$latin = [Text.Encoding]::GetEncoding(28591)
$signature = [byte[]](0x8b,0x12,0x02,0xb9,0x6a,0x61,0x20,0x38,0x72,0x7b,0x93,0x02,0x14,0xd7,0xa0,0x32,
    0x13,0xf5,0xb9,0xe6,0xef,0xae,0x33,0x18,0xee,0x3b,0x2d,0xce,0x24,0xb3,0x6a,0xae)
$marker = $latin.GetString($bytes).IndexOf($latin.GetString($signature), [StringComparison]::Ordinal)
if ($marker -lt 8) { throw 'GUI is not a .NET single-file bundle' }
$headerOffset = [BitConverter]::ToInt64($bytes, $marker - 8)
if ($headerOffset -le 0 -or $headerOffset -ge $bytes.Length) { throw 'invalid bundle header offset' }
$reader = [IO.BinaryReader]::new([IO.MemoryStream]::new($bytes, $false))
try {
    $reader.BaseStream.Position = $headerOffset
    $major = $reader.ReadUInt32()
    $minor = $reader.ReadUInt32()
    if ($major -ne 6 -or $minor -ne 0) { throw "unsupported bundle format $major.$minor" }
    $count = $reader.ReadInt32()
    if ($count -lt 1 -or $count -gt 10000) { throw 'invalid bundle entry count' }
    $reader.ReadString() | Out-Null
    $reader.ReadInt64() | Out-Null # deps.json: положение и размер
    $reader.ReadInt64() | Out-Null
    $configOffset = $reader.ReadInt64()
    $configSize = $reader.ReadInt64()
    $reader.ReadUInt64() | Out-Null
    $entries = @{}
    for ($index = 0; $index -lt $count; ++$index) {
        $offset = $reader.ReadInt64()
        $size = $reader.ReadInt64()
        $compressed = $reader.ReadInt64()
        $reader.ReadByte() | Out-Null
        $entryName = $reader.ReadString()
        $stored = if ($compressed -gt 0) { $compressed } else { $size }
        if ($offset -lt 0 -or $size -le 0 -or $offset + $stored -gt $headerOffset) { throw "invalid bundle entry $entryName" }
        $entries[$entryName] = $true
    }
    foreach ($required in @('GcodeAnalyzerGui.dll', 'System.Private.CoreLib.dll', 'System.Windows.Forms.dll')) {
        if (-not $entries.ContainsKey($required)) { throw "missing embedded assembly $required" }
    }
    if ($configOffset -le 0 -or $configSize -le 0 -or $configOffset + $configSize -gt $headerOffset) { throw 'invalid runtimeconfig location' }
    $config = [Text.Encoding]::UTF8.GetString($bytes, [int]$configOffset, [int]$configSize) | ConvertFrom-Json
    foreach ($framework in @('Microsoft.NETCore.App', 'Microsoft.WindowsDesktop.App')) {
        if (-not ($config.runtimeOptions.includedFrameworks | Where-Object { $_.name -eq $framework -and $_.version -like '10.*' })) {
            throw "missing self-contained .NET 10 framework $framework"
        }
    }
    Write-Host "OK: self-contained .NET 10 / WinForms bundle, $count entries"
} finally { $reader.Dispose() }
if (Get-ChildItem -LiteralPath $app -Recurse -Filter *.pdb) { throw 'pdb in package' }
$expected = if ([IO.Path]::GetFileNameWithoutExtension($Zip) -like '*-compat') { 'off' } else { 'on' }
& (Join-Path $PSScriptRoot 'check-cet.ps1') -Exe $gui.FullName -Expect $expected
& (Join-Path $PSScriptRoot 'check-cet.ps1') -Exe $cli -Expect off
Get-ChildItem -LiteralPath $app -Recurse -File | Select-Object FullName, Length
Write-Host "OK: smoke test - $Zip -> $Destination"
