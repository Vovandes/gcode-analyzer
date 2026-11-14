param(
    [ValidateRange(100000, 1000000)][int]$Segments = 200000,
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoPath 'build/stage10' }
$targetPath = [IO.Path]::GetFullPath($OutputDirectory)
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoPath 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not ($targetPath + [IO.Path]::DirectorySeparatorChar).StartsWith($buildPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be inside this repository build/ directory.'
}
[IO.Directory]::CreateDirectory($targetPath) | Out-Null
$filePath = Join-Path $targetPath "trajectory-$Segments.nc"
if (Test-Path -LiteralPath $filePath) { throw "File already exists: $filePath" }
$writer = [IO.StreamWriter]::new($filePath, $false, [Text.UTF8Encoding]::new($false))
try {
    $writer.WriteLine('O1000')
    $writer.WriteLine('G90 G21 G17 G54 G94')
    $writer.WriteLine('T1M6')
    $writer.WriteLine('S3000 M3')
    $writer.WriteLine('G92 X0 Y0 Z0')
    for ($index = 0; $index -lt $Segments; $index++) {
        $xValue = $index % 1000
        $yValue = [Math]::Floor($index / 1000)
        # A raster of short segments, ten tools and occasional rapids.
        if (($index % 20000) -eq 0 -and $index -gt 0) {
            $toolValue = 1 + [int]($index / 20000)
            $writer.WriteLine("T${toolValue}M6")
            $writer.WriteLine('S3000 M3')
        }
        $motionCode = if (($index % 1000) -eq 0) { 'G0' } else { 'G1' }
        $writer.WriteLine("$motionCode X$xValue Y$yValue Z0 F600 (synthetic frame $index)")
    }
    $writer.WriteLine('M30')
} finally { $writer.Dispose() }
Write-Output $filePath
