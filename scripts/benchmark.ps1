[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputPath,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\onebrc_baseline.exe'),
    [ValidateRange(5, 100)]
    [int]$Runs = 5,
    [ValidateSet('Warm')]
    [string]$CacheMode = 'Warm',
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\results\baseline.local.md')
)

$InputPath = [System.IO.Path]::GetFullPath($InputPath)
$Executable = [System.IO.Path]::GetFullPath($Executable)
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    throw "Input file not found: $InputPath"
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}

# A warm-up makes the cache state explicit and keeps it outside the measured samples.
& $Executable $InputPath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Warm-up failed with exit code $LASTEXITCODE."
}

$samples = for ($run = 1; $run -le $Runs; ++$run) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    & $Executable $InputPath | Out-Null
    $stopwatch.Stop()
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark run $run failed with exit code $LASTEXITCODE."
    }
    $stopwatch.Elapsed.TotalSeconds
}

$ordered = @($samples | Sort-Object)
$middle = [Math]::Floor($ordered.Count / 2)
$median = if ($ordered.Count % 2 -eq 1) {
    $ordered[$middle]
} else {
    ($ordered[$middle - 1] + $ordered[$middle]) / 2
}
$bytes = (Get-Item -LiteralPath $InputPath).Length
$throughput = $bytes / $median / 1MB
$cpu = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Name)
$memoryBytes = (Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue).TotalPhysicalMemory
if ([string]::IsNullOrWhiteSpace($cpu)) { $cpu = 'unknown' }
$memoryGiB = if ($memoryBytes) { [Math]::Round($memoryBytes / 1GB, 1) } else { 'unknown' }
$timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'
$sampleText = ($samples | ForEach-Object { $_.ToString('F6', [Globalization.CultureInfo]::InvariantCulture) }) -join ', '

$markdown = @"
# Local Benchmark

- Timestamp: $timestamp
- Cache mode: $CacheMode (one unmeasured warm-up)
- Executable: ``$Executable``
- Input: ``$InputPath``
- Input bytes: $bytes
- Runs: $Runs
- Samples (seconds): $sampleText
- Median: $($median.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)) seconds
- Throughput: $($throughput.ToString('F2', [Globalization.CultureInfo]::InvariantCulture)) MiB/s
- CPU: $cpu
- RAM: $memoryGiB GiB
- PowerShell: $($PSVersionTable.PSVersion)

Cold-cache measurements are intentionally not automated: Windows offers no reliable, unprivileged cache eviction API. Record cold runs separately after a reboot.
"@

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null
[System.IO.File]::WriteAllText($OutputPath, $markdown, [System.Text.UTF8Encoding]::new($false))
Write-Host "Median $($median.ToString('F6')) s; $($throughput.ToString('F2')) MiB/s"
Write-Host "Wrote $OutputPath"
