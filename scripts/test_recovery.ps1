param(
    [string]$ExecutablePath = "$PSScriptRoot\..\out\recovery-candidate\DotHiderNative.exe",
    [ValidateRange(0, 7200)][int]$SoakSeconds = 0
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
& "$repoRoot\build.ps1" -ProjectPath "$repoRoot\tests\overlay_recovery_tests.vcxproj" -OutputDirectory "$repoRoot\out\recovery-test-build"
$testExecutable = Join-Path $repoRoot 'out\recovery-test-build\overlay_recovery_tests.exe'
$candidate = (Resolve-Path -LiteralPath $ExecutablePath).Path
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))
$profile = Join-Path $outputRoot ('recovery-profile-' + [Guid]::NewGuid().ToString('N'))
$savedEnvironment = @{}
foreach ($name in @('APPDATA', 'LOCALAPPDATA', 'TEMP', 'TMP')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $settingsDirectory = Join-Path $profile 'DotHiderNative'
    New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
    # Each run gets a unique process name so concurrent benchmarks/tests cannot
    # accidentally satisfy this instance's fullscreen visibility condition.
    $targetName = 'recovery_target_' + [Guid]::NewGuid().ToString('N')
    $isolatedTestExecutable = Join-Path $profile ($targetName + '.exe')
    Copy-Item -LiteralPath $testExecutable -Destination $isolatedTestExecutable
    $settings = @'
[Overlay]
monitor=primary
shape=Circle
width=9
height=9
topInset=2
rightInset=2
scaleLogicalSettings=true
[Behavior]
targetProcesses=overlay_recovery_tests
visibilityMode=TargetFullscreenWindow
calibrationMode=false
[Hotkeys]
enableHotkeys=false
[Diagnostics]
enableMemoryLogging=false
'@
    $settings.Replace('targetProcesses=overlay_recovery_tests', "targetProcesses=$targetName") |
        Set-Content -LiteralPath (Join-Path $settingsDirectory 'settings.ini') -Encoding ASCII
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $profile, 'Process')
    }
    & $isolatedTestExecutable $candidate $SoakSeconds
    if ($LASTEXITCODE -ne 0) { throw "Recovery tests failed with exit code $LASTEXITCODE." }
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    $resolvedProfile = [IO.Path]::GetFullPath($profile)
    if (-not $resolvedProfile.StartsWith($outputRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedProfile)).StartsWith('recovery-profile-')) {
        throw "Unsafe recovery profile cleanup path: $resolvedProfile"
    }
    if (Test-Path -LiteralPath $resolvedProfile) {
        Remove-Item -LiteralPath $resolvedProfile -Recurse -Force
    }
}
