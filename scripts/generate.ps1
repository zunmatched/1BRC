[CmdletBinding()]
param(
    [ValidateSet('1M', '10M', '100M', '1B')]
    [string]$Scale = '1M',
    [ValidateSet('random', 'single', 'unique10000')]
    [string]$Mode = 'random',
    [UInt64]$Seed = 457973798,
    [string]$Generator = (Join-Path $PSScriptRoot '..\build\onebrc_generate.exe'),
    [string]$OutputPath
)

$rowsByScale = @{
    '1M' = [UInt64]1000000
    '10M' = [UInt64]10000000
    '100M' = [UInt64]100000000
    '1B' = [UInt64]1000000000
}

if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot "..\data\measurements-$($Scale.ToLower())-$Mode.txt"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$Generator = [System.IO.Path]::GetFullPath($Generator)

if (-not (Test-Path -LiteralPath $Generator -PathType Leaf)) {
    throw "Generator not found at '$Generator'. Build the Release targets first."
}

$outputDirectory = Split-Path -Parent $OutputPath
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

& $Generator $rowsByScale[$Scale] $OutputPath $Seed $Mode
if ($LASTEXITCODE -ne 0) {
    throw "Generator exited with code $LASTEXITCODE."
}

$file = Get-Item -LiteralPath $OutputPath
Write-Host "Generated $($rowsByScale[$Scale]) rows at $OutputPath ($($file.Length) bytes)."
