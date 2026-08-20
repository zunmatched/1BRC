[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputPath,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\onebrc_bounded_memory.exe'),
    [string[]]$ExtraArguments = @(),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\results\memory.local.md')
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
if (-not $process.Start()) {
    throw 'Failed to start the solution process.'
}
$standardOutputTask = $process.StandardOutput.ReadToEndAsync()
$standardErrorTask = $process.StandardError.ReadToEndAsync()

$peakWorkingSet = [Int64]0
$peakCommittedBytes = [Int64]0
$peakVirtualBytes = [Int64]0
while (-not $process.HasExited) {
    try {
        $process.Refresh()
        $peakWorkingSet = [Math]::Max($peakWorkingSet, $process.WorkingSet64)
        $peakCommittedBytes = [Math]::Max($peakCommittedBytes, $process.PeakPagedMemorySize64)
        $peakVirtualBytes = [Math]::Max($peakVirtualBytes, $process.PeakVirtualMemorySize64)
    } catch {
        # The process can exit between HasExited and Refresh; final values are sampled below.
    }
    Start-Sleep -Milliseconds 5
}
$process.WaitForExit()
$stopwatch.Stop()

try {
    $process.Refresh()
    $peakWorkingSet = [Math]::Max($peakWorkingSet, $process.PeakWorkingSet64)
    $peakCommittedBytes = [Math]::Max($peakCommittedBytes, $process.PeakPagedMemorySize64)
    $peakVirtualBytes = [Math]::Max($peakVirtualBytes, $process.PeakVirtualMemorySize64)
} catch {
    # Polling values remain valid if final process properties are no longer available.
}

$standardOutput = $standardOutputTask.GetAwaiter().GetResult()
$standardError = $standardErrorTask.GetAwaiter().GetResult()
if ($process.ExitCode -ne 0) {
    throw "Solution exited with $($process.ExitCode): $standardError"
}

$bytes = (Get-Item -LiteralPath $InputPath).Length
$timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'
$markdown = @"
# Local Memory Measurement

- Timestamp: $timestamp
- Executable: ``$Executable``
- Input: ``$InputPath``
- Extra arguments: ``$($ExtraArguments -join ' ')``
- Input bytes: $bytes
- Exit code: $($process.ExitCode)
- Elapsed: $($stopwatch.Elapsed.TotalSeconds.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)) seconds
- Peak working set: $peakWorkingSet bytes ($([Math]::Round($peakWorkingSet / 1MB, 2)) MiB)
- Peak committed bytes: $peakCommittedBytes bytes ($([Math]::Round($peakCommittedBytes / 1MB, 2)) MiB)
- Peak virtual bytes: $peakVirtualBytes bytes ($([Math]::Round($peakVirtualBytes / 1MB, 2)) MiB)
- Output bytes: $([System.Text.Encoding]::UTF8.GetByteCount($standardOutput))

Values describe the solution process only and exclude the filesystem cache maintained by Windows.
"@

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null
[System.IO.File]::WriteAllText($OutputPath, $markdown, [System.Text.UTF8Encoding]::new($false))
Write-Host "Peak working set $([Math]::Round($peakWorkingSet / 1MB, 2)) MiB; committed $([Math]::Round($peakCommittedBytes / 1MB, 2)) MiB; virtual $([Math]::Round($peakVirtualBytes / 1MB, 2)) MiB"
Write-Host "Wrote $OutputPath"
