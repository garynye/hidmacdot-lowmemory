# DotHiderNative

DotHiderNative is a tiny native Windows utility that covers the orange privacy dot shown by Jump Desktop with a configurable topmost overlay. It runs in the notification area, watches for Jump Desktop full-screen windows, and uses no bundled framework or background service.

## Requirements

- Windows 10 or Windows 11, x64
- Jump Desktop for the default automatic visibility behavior
- No administrator access, installer, or Visual C++ runtime download is required

The published executable is currently unsigned. Windows SmartScreen may therefore show a warning even when the file matches the release checksum.

## Download and install

Open the [latest GitHub release](https://github.com/garynye/hidmacdot-lowmemory/releases/latest) and choose either:

- `DotHiderNative-v1.0.0-windows-x64.exe` for the smallest direct download, or
- `DotHiderNative-v1.0.0-windows-x64.zip` if you also want this README and the license in one package.

For the ZIP package, extract it before running the app. Put `DotHiderNative.exe` in a permanent folder such as `%LOCALAPPDATA%\Programs\DotHiderNative`; the app does not need to live under `Program Files`.

Double-click the executable. DotHiderNative has no main window: a tray icon appears in the Windows notification area. If SmartScreen blocks the first launch, verify the SHA-256 checksum from `SHA256SUMS.txt`, then select **More info** and **Run anyway** only if it matches the GitHub release.

### Start automatically with Windows

1. Press `Win+R`, enter `shell:startup`, and press Enter.
2. Right-drag `DotHiderNative.exe` into the Startup folder.
3. Choose **Create shortcuts here**.

Keep the executable in its permanent folder after creating the shortcut.

### Update

1. Right-click the DotHiderNative tray icon and choose **Exit**.
2. Download the newer release and replace the existing executable.
3. Start it again. Your settings remain in your Windows profile.

### Uninstall

Exit DotHiderNative, remove its Startup shortcut if you created one, and delete the executable. To remove preferences and diagnostics too, delete `%APPDATA%\DotHiderNative`. If that folder does not exist, check the fallback locations described below.

## Quick start

1. Start `DotHiderNative.exe`.
2. Open Jump Desktop or Jump Client full screen on the selected monitor.
3. Confirm that the black overlay covers the orange privacy dot.
4. If alignment needs adjustment, press `Ctrl+Alt+D`, use the arrow hotkeys, then press `Ctrl+Alt+D` again.

The default `TargetFullscreenWindow` mode keeps the overlay hidden until a visible Jump Desktop or Jump Client window covers the selected monitor.

## Tray menu

Right-click the tray icon to access:

- **Toggle Calibration Mode**: show the overlay so it can be positioned.
- **Reload Settings**: reload `settings.ini` without restarting.
- **Open Settings File**: open the active configuration in Notepad++ or Notepad.
- **Show Diagnostics Snapshot**: display current memory and overlay geometry.
- **Exit**: close the overlay and remove the tray icon.

## Display resolution, scaling, and calibration

Screen resolution and Windows display scaling are different settings. A 3840x2160 display is often set to 150% or 200% scaling, while a 1920x1080 display is often set to 100%. DotHiderNative uses the selected monitor's effective DPI, not its resolution alone.

With the default `scaleLogicalSettings=true`, `width`, `height`, `topInset`, and `rightInset` are logical values that scale automatically per monitor. The default logical size of 9x9 becomes approximately:

| Windows scaling | Effective DPI | Physical overlay size |
| --- | ---: | ---: |
| 100% | 96 | 9x9 px |
| 125% | 120 | 11x11 px |
| 150% | 144 | 14x14 px |
| 200% | 192 | 18x18 px |

This normally means that switching between 4K, 1440p, and 1080p displays requires calibration rather than a separate resolution preset.

### Calibrate on any resolution

1. Right-click the tray icon and select **Toggle Calibration Mode**, or press `Ctrl+Alt+D`.
2. Use `Ctrl+Alt+Arrow` to move one logical unit at a time.
3. Use `Ctrl+Alt+Shift+Arrow` to move ten logical units at a time.
4. If the overlay itself is too large or small, open the settings file and adjust `width` and `height`.
5. Turn calibration mode off when finished.

Arrow-key changes update `topInset` and `rightInset` immediately in `settings.ini`. With a top-right anchor, Left increases `rightInset`, Right decreases it, Up decreases `topInset`, and Down increases it. Insets cannot go below zero.

### Select another monitor

The default `monitor=primary` follows the Windows primary display. You can instead use a zero-based monitor index such as `monitor=1`, or a Windows display device name such as `monitor=\\.\DISPLAY2`.

To list display device names from PowerShell:

```powershell
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Screen]::AllScreens | Select-Object DeviceName, Primary, Bounds
```

Device names are more explicit than indexes, whose enumeration order may change. After editing `monitor`, use **Reload Settings** or press `Ctrl+Alt+R`.

### Use exact physical pixels

Set `scaleLogicalSettings=false` when you want `width`, `height`, and inset values to mean exact physical pixels instead of DPI-scaled logical units. Recalibrate after changing this setting.

DotHiderNative automatically repositions itself when Windows reports a resolution, monitor, or per-monitor DPI change.

## Settings file

On first launch, DotHiderNative creates `DotHiderNative\settings.ini` in the first writable base location, in this order:

1. `%APPDATA%`
2. `%TEMP%`
3. `%LOCALAPPDATA%`

The normal location is `%APPDATA%\DotHiderNative\settings.ini`. Use **Open Settings File** from the tray menu to open the exact active file rather than guessing which fallback was selected.

Default configuration:

```ini
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
enableHotkeys=true
smallStep=1
largeStep=10

[Diagnostics]
enableMemoryLogging=false
```

Supported overlay values:

- `monitor`: `primary`, a zero-based monitor index, or a device name such as `\\.\DISPLAY2`.
- `anchor`: currently positions at the selected monitor's top-right corner.
- `shape`: `Rectangle`, `RoundedRectangle`, or `Circle`.
- `color`: `Black`, `White`, `Red`, `Green`, or `Blue`.
- `width`, `height`: positive logical units, or pixels when scaling is disabled.
- `topInset`, `rightInset`: non-negative offsets from the selected monitor's top-right edge.
- `scaleLogicalSettings`: apply the selected monitor's effective DPI to size and inset values.

### Visibility modes

- `TargetFullscreenWindow` (recommended default): show when any visible top-level target-process window covers the selected monitor within `fullscreenTolerancePx`.
- `ForegroundTarget`: show only while a target process owns the foreground window.
- `Always`: show whenever calibration mode is off.

`targetProcesses` is a comma-separated list of executable names. `.exe` is optional. `showOnlyWhenTargetForeground` remains as compatibility behavior for older settings files that do not specify `visibilityMode`.

`persistCalibrationMode=false` prevents the app from accidentally starting in calibration mode after a restart.

## Keyboard controls

| Shortcut | Action |
| --- | --- |
| `Ctrl+Alt+D` | Toggle calibration mode |
| `Ctrl+Alt+R` | Reload settings |
| `Ctrl+Alt+Arrow` | Move one configured small step while calibrating |
| `Ctrl+Alt+Shift+Arrow` | Move one configured large step while calibrating |

Nudge hotkeys do nothing outside calibration mode. Set `enableHotkeys=false` if these global shortcuts conflict with another application.

## Measured memory use

The final v1.0.0 Release x64 executable was measured on August 28, 2026, on Windows 11 Pro build 26200 with an Intel Core i5-12600K:

| Metric | Aggregate result |
| --- | ---: |
| Median private working set | 724 KiB (0.71 MiB) |
| P95 private working set | 772 KiB (0.75 MiB) |
| Median private bytes | 1,088 KiB (1.06 MiB) |
| Median total working set | 7,624 KiB (7.45 MiB) |

These are measured values, not a memory limit. Results vary with Windows version, display configuration, loaded system components, and activity.

Private working set is the primary figure because it approximates resident RAM unique to this process. Total working set also includes Windows DLL pages that can be shared with other processes.

### Reproduce the benchmark

Build the app, then run the isolated benchmark from PowerShell:

```powershell
.\scripts\measure_memory.ps1
```

The standard run performs five launches, waits 15 seconds after each launch, and samples once per second for 60 seconds. It uses temporary AppData, LocalAppData, and Temp directories, disables benchmark-instance hotkeys, and removes its temporary profile when finished.

To test another binary or retain JSON output:

```powershell
.\scripts\measure_memory.ps1 `
  -ExecutablePath .\path\to\DotHiderNative.exe `
  -OutputPath .\out\memory-report.json
```

## Diagnostics

Choose **Show Diagnostics Snapshot** from the tray menu to see:

- working set and private bytes
- handle count
- physical overlay position and size
- saved inset values
- calibration state

For continuous diagnostic logging, set `enableMemoryLogging=true` and reload settings. The log is stored beside the active settings file as `diagnostics.log`.

Typical locations:

```powershell
Get-Content "$env:APPDATA\DotHiderNative\diagnostics.log"
Get-Content "$env:TEMP\DotHiderNative\diagnostics.log"
Get-Content "$env:LOCALAPPDATA\DotHiderNative\diagnostics.log"
```

## Troubleshooting

- **No tray icon:** check the notification-area overflow menu and make sure another copy is not already running.
- **Overlay never appears:** enable calibration mode. If it appears then, confirm `targetProcesses`, `visibilityMode`, the selected monitor, and whether Jump Desktop is truly full screen.
- **Overlay appears on the wrong monitor:** set `monitor` to the intended device name and reload settings.
- **Overlay is the wrong size:** keep logical scaling enabled and adjust `width`/`height`, or disable it for exact pixels.
- **Hotkeys do not respond:** confirm `enableHotkeys=true`, enable calibration for movement keys, and check whether another app owns the shortcut.
- **Settings edits seem ignored:** save the file and choose **Reload Settings** or press `Ctrl+Alt+R`.
- **A display change leaves stale placement:** reload settings; if the issue persists, exit and restart the app.

## Build from source

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload, then run:

```powershell
.\build.ps1 -Configuration Release -Platform x64
```

The build script locates MSBuild through `vswhere` when available. The output is `x64\Release\DotHiderNative.exe`.

## License

DotHiderNative is available under the [MIT License](LICENSE).
