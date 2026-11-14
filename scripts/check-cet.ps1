# Проверяет CET у PE-файла. Совместим с Windows PowerShell 5.1.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][ValidateSet('on', 'off')][string]$Expect
)
$ErrorActionPreference = 'Stop'
function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Не найден vswhere.exe (нужна Visual Studio с C++)' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Не найдена Visual Studio с компонентом C++' }
    $found = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe |
        Where-Object { $_.FullName -match 'Hostx64[\\/]x64' } |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw 'Не найден dumpbin.exe для x64' }
    return $found.FullName
}
$dumpbin = Find-Dumpbin
$headers = & $dumpbin /headers $Exe | Out-String
if ($LASTEXITCODE -ne 0) { throw "dumpbin /headers завершился с кодом $LASTEXITCODE" }
# Полные заголовки, включая DLL characteristics, остаются в журнале упаковки.
Write-Host $headers
$enabled = $headers -match '(?im)^\s*CET compatible\s*$'
$actual = if ($enabled) { 'on' } else { 'off' }
if ($actual -ne $Expect) { throw "CET: ожидалось $Expect, получено $actual для $Exe" }
Write-Host "OK: CET $actual - $Exe"
