# Проверяет, что CLI не зависит от DLL рантайма Visual C++ (dumpbin /dependents). Совместим с PowerShell 5.1.
param([Parameter(Mandatory = $true)][string]$Exe)

$ErrorActionPreference = 'Stop'

function Find-Dumpbin {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'dumpbin.exe не найден (нужна Visual Studio с компонентом C++)' }
    $vs = & $vswhere -latest -products * -property installationPath
    $found = Get-ChildItem -Path (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe |
        Where-Object { $_.FullName -match 'Hostx64[\\/]x64' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw 'dumpbin.exe не найден в установке Visual Studio' }
    return $found.FullName
}

$dumpbin = Find-Dumpbin
$output = & $dumpbin /dependents $Exe | Out-String
Write-Host $output

if ($output -match '(?im)^\s*(VCRUNTIME140\w*|MSVCP140\w*|api-ms-win-crt-[\w-]+)\.dll\s*$') {
    throw "CLI зависит от рантайма VC: $($Matches[0].Trim())"
}
Write-Host 'OK: зависимостей от VC runtime нет'
