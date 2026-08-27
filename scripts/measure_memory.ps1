[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [ValidateRange(1, 20)]
    [int]$Runs = 5,
    [ValidateRange(0, 300)]
    [int]$WarmupSeconds = 15,
    [ValidateRange(1, 600)]
    [int]$SampleSeconds = 60,
    [ValidateRange(100, 10000)]
    [int]$SampleIntervalMilliseconds = 1000,
    [switch]$AllowHotkeys,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$nativeSource = @'
using System;
using System.Runtime.InteropServices;

public sealed class DotHiderMemorySnapshot
{
    public ulong WorkingSetBytes { get; set; }
    public ulong PrivateWorkingSetBytes { get; set; }
    public ulong PrivateBytes { get; set; }
    public uint PageFaultCount { get; set; }
}

public static class DotHiderMemoryNative
{
    private const uint MemCommit = 0x1000;
    private const ulong PageSize = 4096;
    private const ulong WorkingSetValid = 0x1;
    private const ulong WorkingSetShared = 0x8000;

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryBasicInformation
    {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public UIntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WorkingSetExInformation
    {
        public IntPtr VirtualAddress;
        public UIntPtr VirtualAttributes;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessMemoryCountersEx
    {
        public uint cb;
        public uint PageFaultCount;
        public UIntPtr PeakWorkingSetSize;
        public UIntPtr WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage;
        public UIntPtr QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage;
        public UIntPtr QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage;
        public UIntPtr PeakPagefileUsage;
        public UIntPtr PrivateUsage;
    }

    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern UIntPtr VirtualQueryEx(
        IntPtr process,
        IntPtr address,
        out MemoryBasicInformation information,
        UIntPtr informationLength);

    [DllImport("psapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryWorkingSetEx(
        IntPtr process,
        [In, Out] WorkingSetExInformation[] information,
        uint informationLength);

    [DllImport("psapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessMemoryInfo(
        IntPtr process,
        out ProcessMemoryCountersEx counters,
        uint size);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static DotHiderMemorySnapshot Capture(IntPtr process)
    {
        var counters = new ProcessMemoryCountersEx();
        counters.cb = (uint)Marshal.SizeOf(typeof(ProcessMemoryCountersEx));
        if (!GetProcessMemoryInfo(process, out counters, counters.cb))
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }

        ulong privateWorkingSetBytes = 0;
        ulong address = 0;
        var informationSize = (UIntPtr)Marshal.SizeOf(typeof(MemoryBasicInformation));
        var itemSize = Marshal.SizeOf(typeof(WorkingSetExInformation));

        while (true)
        {
            MemoryBasicInformation information;
            var bytesReturned = VirtualQueryEx(
                process,
                new IntPtr(unchecked((long)address)),
                out information,
                informationSize).ToUInt64();
            if (bytesReturned == 0)
            {
                break;
            }

            var regionBase = unchecked((ulong)information.BaseAddress.ToInt64());
            var regionSize = information.RegionSize.ToUInt64();
            var nextAddress = regionBase + regionSize;

            if (information.State == MemCommit && regionSize > 0)
            {
                var pageCount = (regionSize + PageSize - 1) / PageSize;
                for (ulong offset = 0; offset < pageCount; offset += 2048)
                {
                    var count = (int)Math.Min(2048, pageCount - offset);
                    var pages = new WorkingSetExInformation[count];
                    for (var index = 0; index < count; ++index)
                    {
                        var pageAddress = regionBase + (offset + (ulong)index) * PageSize;
                        pages[index].VirtualAddress = new IntPtr(unchecked((long)pageAddress));
                    }

                    if (!QueryWorkingSetEx(process, pages, (uint)(count * itemSize)))
                    {
                        continue;
                    }

                    foreach (var page in pages)
                    {
                        var attributes = page.VirtualAttributes.ToUInt64();
                        if ((attributes & WorkingSetValid) != 0 &&
                            (attributes & WorkingSetShared) == 0)
                        {
                            privateWorkingSetBytes += PageSize;
                        }
                    }
                }
            }

            if (nextAddress <= address)
            {
                break;
            }
            address = nextAddress;
        }

        return new DotHiderMemorySnapshot
        {
            WorkingSetBytes = counters.WorkingSetSize.ToUInt64(),
            PrivateWorkingSetBytes = privateWorkingSetBytes,
            PrivateBytes = counters.PrivateUsage.ToUInt64(),
            PageFaultCount = counters.PageFaultCount
        };
    }

    public static bool RequestClose(uint processId)
    {
        IntPtr matchingWindow = IntPtr.Zero;
        EnumWindows((window, parameter) =>
        {
            uint windowProcessId;
            GetWindowThreadProcessId(window, out windowProcessId);
            if (windowProcessId == processId)
            {
                matchingWindow = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        return matchingWindow != IntPtr.Zero &&
            PostMessage(matchingWindow, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
'@

Add-Type -TypeDefinition $nativeSource

if (-not $ExecutablePath) {
    $ExecutablePath = Join-Path $PSScriptRoot "..\x64\Release\DotHiderNative.exe"
}

function Get-Percentile {
    param(
        [double[]]$Values,
        [ValidateRange(0, 1)]
        [double]$Percentile
    )

    if ($Values.Count -eq 0) {
        return 0
    }

    $sorted = @($Values | Sort-Object)
    $index = [Math]::Ceiling(($sorted.Count - 1) * $Percentile)
    return $sorted[$index]
}

function Stop-BenchmarkProcess {
    param([System.Diagnostics.Process]$Process)

    if ($Process.HasExited) {
        return
    }

    [void][DotHiderMemoryNative]::RequestClose([uint32]$Process.Id)
    if (-not $Process.WaitForExit(3000)) {
        $Process.Kill()
        [void]$Process.WaitForExit(3000)
    }
}

$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$benchmarkRoot = Join-Path $tempBase ("DotHiderNative-memory-" + [Guid]::NewGuid().ToString("N"))
$resolvedBenchmarkRoot = [IO.Path]::GetFullPath($benchmarkRoot)

if (-not $resolvedBenchmarkRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
    -not ([IO.Path]::GetFileName($resolvedBenchmarkRoot)).StartsWith("DotHiderNative-memory-", [StringComparison]::Ordinal)) {
    throw "Refusing to use an unsafe benchmark directory: $resolvedBenchmarkRoot"
}

$roamingPath = Join-Path $resolvedBenchmarkRoot "Roaming"
$localPath = Join-Path $resolvedBenchmarkRoot "Local"
$tempPath = Join-Path $resolvedBenchmarkRoot "Temp"
$settingsDirectory = Join-Path $roamingPath "DotHiderNative"
$settingsPath = Join-Path $settingsDirectory "settings.ini"

$hotkeysValue = if ($AllowHotkeys) { "true" } else { "false" }
$settingsContent = @"
[Overlay]
monitor=primary
anchor=TopRight
shape=Rectangle
color=Black
width=9
height=9
topInset=2
rightInset=2
scaleLogicalSettings=true

[Behavior]
targetProcesses=JumpDesktop,JumpClient
visibilityMode=TargetFullscreenWindow
fullscreenTolerancePx=8
calibrationMode=false
persistCalibrationMode=false
showOnlyWhenTargetForeground=true

[Hotkeys]
enableHotkeys=$hotkeysValue
smallStep=1
largeStep=10

[Diagnostics]
enableMemoryLogging=false
"@

$runSummaries = @()
$allSamples = @()

try {
    [void](New-Item -ItemType Directory -Path $settingsDirectory -Force)
    [void](New-Item -ItemType Directory -Path $localPath -Force)
    [void](New-Item -ItemType Directory -Path $tempPath -Force)
    Set-Content -LiteralPath $settingsPath -Value $settingsContent -Encoding ASCII

    for ($run = 1; $run -le $Runs; ++$run) {
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $resolvedExecutable
        $startInfo.WorkingDirectory = Split-Path -Path $resolvedExecutable -Parent
        $startInfo.UseShellExecute = $false
        $startInfo.EnvironmentVariables["APPDATA"] = $roamingPath
        $startInfo.EnvironmentVariables["LOCALAPPDATA"] = $localPath
        $startInfo.EnvironmentVariables["TEMP"] = $tempPath
        $startInfo.EnvironmentVariables["TMP"] = $tempPath

        $process = [Diagnostics.Process]::Start($startInfo)
        if (-not $process) {
            throw "Failed to start benchmark process."
        }

        try {
            if ($WarmupSeconds -gt 0) {
                Start-Sleep -Seconds $WarmupSeconds
            }

            $runSamples = @()
            $sampleCount = [Math]::Max(1, [Math]::Floor(($SampleSeconds * 1000) / $SampleIntervalMilliseconds))
            $previousPageFaults = $null
            $previousCpuMilliseconds = $null

            for ($sample = 1; $sample -le $sampleCount; ++$sample) {
                if ($process.HasExited) {
                    throw "Benchmark process exited unexpectedly with code $($process.ExitCode)."
                }

                $process.Refresh()
                $snapshot = [DotHiderMemoryNative]::Capture($process.Handle)
                $cpuMilliseconds = $process.TotalProcessorTime.TotalMilliseconds
                $pageFaultDelta = if ($null -eq $previousPageFaults) { 0 } else { $snapshot.PageFaultCount - $previousPageFaults }
                $cpuDelta = if ($null -eq $previousCpuMilliseconds) { 0 } else { $cpuMilliseconds - $previousCpuMilliseconds }

                $sampleResult = [PSCustomObject]@{
                    Run = $run
                    Sample = $sample
                    PrivateWorkingSetKiB = [Math]::Round($snapshot.PrivateWorkingSetBytes / 1KB, 3)
                    PrivateBytesKiB = [Math]::Round($snapshot.PrivateBytes / 1KB, 3)
                    WorkingSetKiB = [Math]::Round($snapshot.WorkingSetBytes / 1KB, 3)
                    CpuDeltaMilliseconds = [Math]::Round($cpuDelta, 3)
                    PageFaultDelta = $pageFaultDelta
                    Threads = $process.Threads.Count
                    Handles = $process.HandleCount
                }
                $runSamples += $sampleResult
                $allSamples += $sampleResult
                $previousPageFaults = $snapshot.PageFaultCount
                $previousCpuMilliseconds = $cpuMilliseconds

                if ($sample -lt $sampleCount) {
                    Start-Sleep -Milliseconds $SampleIntervalMilliseconds
                }
            }

            $privateWorkingSetValues = [double[]]@($runSamples.PrivateWorkingSetKiB)
            $runSummaries += [PSCustomObject]@{
                Run = $run
                MedianPrivateWorkingSetKiB = Get-Percentile -Values $privateWorkingSetValues -Percentile 0.50
                P95PrivateWorkingSetKiB = Get-Percentile -Values $privateWorkingSetValues -Percentile 0.95
                MedianPrivateBytesKiB = Get-Percentile -Values ([double[]]@($runSamples.PrivateBytesKiB)) -Percentile 0.50
                MedianWorkingSetKiB = Get-Percentile -Values ([double[]]@($runSamples.WorkingSetKiB)) -Percentile 0.50
                MaxThreads = ($runSamples.Threads | Measure-Object -Maximum).Maximum
                MaxHandles = ($runSamples.Handles | Measure-Object -Maximum).Maximum
                TotalCpuMilliseconds = [Math]::Round(($runSamples.CpuDeltaMilliseconds | Measure-Object -Sum).Sum, 3)
                TotalPageFaults = ($runSamples.PageFaultDelta | Measure-Object -Sum).Sum
            }
        }
        finally {
            Stop-BenchmarkProcess -Process $process
            $process.Dispose()
        }
    }

    $aggregatePrivateWorkingSet = [double[]]@($allSamples.PrivateWorkingSetKiB)
    $report = [PSCustomObject]@{
        Executable = $resolvedExecutable
        Runs = $Runs
        WarmupSeconds = $WarmupSeconds
        SampleSeconds = $SampleSeconds
        SampleIntervalMilliseconds = $SampleIntervalMilliseconds
        HotkeysEnabled = [bool]$AllowHotkeys
        Aggregate = [PSCustomObject]@{
            MedianPrivateWorkingSetKiB = Get-Percentile -Values $aggregatePrivateWorkingSet -Percentile 0.50
            P95PrivateWorkingSetKiB = Get-Percentile -Values $aggregatePrivateWorkingSet -Percentile 0.95
            MedianPrivateBytesKiB = Get-Percentile -Values ([double[]]@($allSamples.PrivateBytesKiB)) -Percentile 0.50
            MedianWorkingSetKiB = Get-Percentile -Values ([double[]]@($allSamples.WorkingSetKiB)) -Percentile 0.50
        }
        RunSummaries = $runSummaries
    }

    $runSummaries | Format-Table -AutoSize
    $report.Aggregate | Format-List

    if ($OutputPath) {
        $resolvedOutputPath = [IO.Path]::GetFullPath($OutputPath)
        $outputDirectory = Split-Path -Path $resolvedOutputPath -Parent
        if ($outputDirectory) {
            [void](New-Item -ItemType Directory -Path $outputDirectory -Force)
        }
        $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8
        Write-Host "Wrote benchmark report to $resolvedOutputPath"
    }
}
finally {
    if (Test-Path -LiteralPath $resolvedBenchmarkRoot) {
        Remove-Item -LiteralPath $resolvedBenchmarkRoot -Recurse -Force
    }
}
