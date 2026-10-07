# v1.0.1 overlay recovery validation

Validated October 7, 2026 on Windows 11 Pro build 26300, Intel Core i5-12600K,
PowerShell 7.6.5, MSBuild 17.14.40, and MSVC 14.44.35207 (Release x64).

## Repair and boundaries

The original placement call passed HWND_TOPMOST with SWP_NOZORDER, so it could
not recover stacking order. Its timer also evaluated fullscreen coverage against
cached monitor geometry. The native regression reproduced the first defect:
v1.0.0 failed `existing timer restores stacking`; the repaired app passed.
This confirms a recoverable defect, but does not establish that every reported
intermittent disappearance has the same cause.

The existing two-second timer now resolves monitor bounds, evaluates visibility,
and restores overlay placement/stacking without activation. Display/DPI and wake
notifications, plus a five-minute deadline on that timer, refresh DPI. Monitor
enumeration uses stack state; named selectors do not throw numeric-conversion
exceptions each tick. Unchanged checks do not recreate regions or invalidate
painting. The original startup allocation order is preserved.

No new process, thread, timer, dependency, privilege, IPC endpoint, or settings
key is introduced. Region ownership transfers to Windows only on successful
SetWindowRgn; failed assignments release the caller-owned region. Recovery
errors and expected/actual visibility are available in the existing snapshot.
Continuous logging remains disabled by default.

## Resource comparison

Both binaries used isolated profiles, disabled hotkeys/logging, five launches,
15-second warmup, and one-second samples for 60 seconds per launch. Active mode
used an invisible fullscreen window owned by a synthetic target process.
Always mode reflects the behavior of the user's existing legacy configuration.
Baseline Always mode was also checked in an earlier two-launch run.

| Scenario | Build | Median private WS KiB | P95 private WS KiB | Median private bytes KiB | Median CPU ms per sampled run |
| --- | --- | ---: | ---: | ---: | ---: |
| Idle automatic | v1.0.0 | 724 | 772 | 1100 | 15.625 |
| Idle automatic | v1.0.1 | 716 | 764 | 1092 | 15.625 |
| Active fullscreen | v1.0.0 | 724 | 772 | 1096 | 31.250 |
| Active fullscreen | v1.0.1 | 716 | 764 | 1088 | 31.250 |
| Always, repeated baseline | v1.0.0 | 700 | 748 | 1056 | 0 |
| Always | v1.0.1 | 696 | 748 | 1056 | 0 |

The requested private-memory acceptance gate passed. CPU medians were unchanged;
individual runs varied in 15.625 ms increments (idle: baseline/fix 0..46.875 ms;
active: baseline 15.625..46.875 ms, fix 0..62.5 ms; Always: baseline 0..62.5 ms,
fix 0 ms). These measurements cannot establish literally zero extra CPU work.
Maximum sampled thread count remained four. Benchmark handle maxima were
unchanged or lower (idle 123/123, active 127/123, Always 119/119).
Page faults stayed low, with no repeated DLL-load churn on ordinary ticks.

Total working set, which also includes shareable Windows/library pages, was
16..28 KiB higher in the final aggregate measurements; the primary private resident
RAM metric decreased. These are measurements on this machine, not fixed limits.

Full JSON reports remain locally under `out/recovery-baseline/` and
`out/recovery-release-final/`; generated binaries and reports are Git-ignored.
The final source compile's code/writable-data/unwind sections match the measured
binary. Read-only-data differences are confined to two embedded linker timestamps.
The release ZIP and direct executable contain the exact measured binary.

SHA-256:

- Baseline executable: `c57a226c3e6d8e435aa2d1c2960bfaf7d1db9292732f2d39e15f8d2a141a499e`
- Packaged v1.0.1 executable: `de42d1eab29f69be6b2bdd8b0124eda7c47e1d7f840ea28a369ce5d11da1ea56`

## Commands and outcomes

PowerShell, from the repository root:

```powershell
# Final source compile: passed, zero warnings/errors.
.\build.ps1 -Configuration Release -Platform x64 -OutputDirectory .\out\recovery-handoff

# Expanded native regressions and six-minute soak: all 29 assertions passed.
.\scripts\test_recovery.ps1 -ExecutablePath .\out\recovery-release-final\DotHiderNative.exe -SoakSeconds 360

# Final memory/CPU comparisons: all passed.
.\scripts\measure_memory.ps1 -ExecutablePath .\out\recovery-release-final\DotHiderNative.exe -OutputPath .\out\recovery-release-final\memory-idle.json
.\scripts\measure_memory.ps1 -ExecutablePath .\out\recovery-release-final\DotHiderNative.exe -TargetProcesses overlay_recovery_tests -OutputPath .\out\recovery-release-final\memory-active.json
.\scripts\measure_memory.ps1 -ExecutablePath .\out\recovery-release-final\DotHiderNative.exe -VisibilityMode Always -OutputPath .\out\recovery-release-final\memory-always.json

# Packaging and ZIP hash verification: passed.
.\scripts\package_release.ps1 -Version 1.0.1 -ExecutablePath .\out\recovery-release-final\DotHiderNative.exe
```

For reproducing the active benchmark, build the native test project and launch
its executable with `--target-only` in a hidden window before the measurement.
That owned, invisible target exits after 20 minutes; close it after the benchmark
if it is still running. The runner uses distinct process names for integration
tests, so parallel test instances cannot satisfy each other's target conditions.

The native tests cover stacking competition, hidden/displaced overlays, focus,
intentional hiding, target return, calibration, settings reload, shapes/color,
invalid-window errors, repaint avoidance, and simulated display/wake messages.
One thousand unchanged checks did not grow handle/GDI counts. The real six-minute
soak crossed the five-minute refresh deadline: handles stayed 87, GDI objects 1.
PowerShell syntax checks and `git diff --check` passed.

## Manual checks still required

The existing user instance and profile were left untouched. Exit that instance
through its tray menu, then launch
`out\v1.0.1\DotHiderNative-v1.0.1-windows-x64.exe`.

1. Open Jump Desktop full screen, Alt+Tab repeatedly, reconnect the session, and
   toggle full screen. The cover should return within four seconds after settling,
   without Reload Settings or a focus change.
2. Sleep/wake, change Windows scaling/resolution, and disconnect/reconnect the
   configured monitor. Check alignment, automatic recovery, and mouse passthrough.
3. Toggle calibration with Ctrl+Alt+D, nudge with Ctrl+Alt+Arrow, leave calibration,
   and use Reload Settings. Confirm saved alignment and visibility behavior.
4. Leave a real session open for two hours, including five-minute refreshes. Confirm
   no blink, input interruption, disappearance, or growing RAM/handle counts.
5. If a failure remains, capture Show Diagnostics Snapshot before Reload Settings.

Physical display changes, actual sleep/wake, visual flicker, and a two-hour real
Jump Desktop session were not automated. A two-hour synthetic soak is available
with `-SoakSeconds 7200` but was not run. During validation, v1.0.1 was packaged
locally before GitHub publication. The original executable is retained in
`out\recovery-baseline\DotHiderNative.exe` for rollback.
