[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputPath,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\onebrc_baseline.exe'),
    [string[]]$ExtraArguments = @(),
    [string]$ExpectedOutputPath,
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
if ($ExpectedOutputPath) {
    $ExpectedOutputPath = [System.IO.Path]::GetFullPath($ExpectedOutputPath)
    if (-not (Test-Path -LiteralPath $ExpectedOutputPath -PathType Leaf)) {
        throw "Expected output not found: $ExpectedOutputPath"
    }
    $expectedOutput = [System.IO.File]::ReadAllBytes($ExpectedOutputPath)
}

function Invoke-Solution {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.ArgumentList.Add($InputPath)
    foreach ($argument in $ExtraArguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) { throw 'Failed to start benchmark process.' }
    $outputBuffer = [System.IO.MemoryStream]::new()
    $outputTask = $process.StandardOutput.BaseStream.CopyToAsync($outputBuffer)
    $errorTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $outputTask.GetAwaiter().GetResult()
    $standardError = $errorTask.GetAwaiter().GetResult()
    $stopwatch.Stop()
    if ($process.ExitCode -ne 0) {
        throw "Benchmark process failed with $($process.ExitCode): $standardError"
    }
    if ($ExpectedOutputPath -and
        -not [System.Linq.Enumerable]::SequenceEqual($outputBuffer.ToArray(), $expectedOutput)) {
        throw 'Benchmark output did not match the expected output byte-for-byte.'
    }
    [pscustomobject]@{ Seconds = $stopwatch.Elapsed.TotalSeconds }
}

# A warm-up makes the cache state explicit and keeps it outside the measured samples.
$null = Invoke-Solution

$samples = for ($run = 1; $run -le $Runs; ++$run) {
    (Invoke-Solution).Seconds
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
if ([string]::IsNullOrWhiteSpace($cpu)) {
    $cpu = Get-ItemPropertyValue `
        -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0' `
        -Name ProcessorNameString -ErrorAction SilentlyContinue
}
if (-not $memoryBytes) {
    try {
        Add-Type -AssemblyName Microsoft.VisualBasic
        $memoryBytes = ([Microsoft.VisualBasic.Devices.ComputerInfo]::new()).TotalPhysicalMemory
    } catch {
        $memoryBytes = $null
    }
}
if ([string]::IsNullOrWhiteSpace($cpu)) { $cpu = 'unknown' } else { $cpu = $cpu.Trim() }
$memoryGiB = if ($memoryBytes) { [Math]::Round($memoryBytes / 1GB, 1) } else { 'unknown' }
$timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'
$sampleText = ($samples | ForEach-Object { $_.ToString('F6', [Globalization.CultureInfo]::InvariantCulture) }) -join ', '

$markdown = @"
# Local Benchmark

- Timestamp: $timestamp
- Cache mode: $CacheMode (one unmeasured warm-up)
- Executable: ``$Executable``
- Extra arguments: ``$($ExtraArguments -join ' ')``
- Input: ``$InputPath``
- Output verification: $(if ($ExpectedOutputPath) { "byte-for-byte against ``$ExpectedOutputPath``" } else { 'not requested' })
- Input bytes: $bytes
- Runs: $Runs
- Samples (seconds): $sampleText
- Median: $($median.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)) seconds
- Throughput: $($throughput.ToString('F2', [Globalization.CultureInfo]::InvariantCulture)) MiB/s
- CPU: $cpu
- Logical processors: $env:NUMBER_OF_PROCESSORS
- RAM: $memoryGiB GiB
- PowerShell: $($PSVersionTable.PSVersion)

Cold-cache measurements are intentionally not automated: Windows offers no reliable, unprivileged cache eviction API. Record cold runs separately after a reboot.
"@

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null
[System.IO.File]::WriteAllText($OutputPath, $markdown, [System.Text.UTF8Encoding]::new($false))
Write-Host "Median $($median.ToString('F6')) s; $($throughput.ToString('F2')) MiB/s"
Write-Host "Wrote $OutputPath"
