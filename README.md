# DotHiderNative

Native Win32 utility that hides the orange privacy dot used by Jump Desktop with a tiny black overlay.

## What it does

- Creates a tiny topmost Win32 overlay window.
- Tracks foreground-window changes using `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` and refreshes visibility every 2 seconds.
- Shows the overlay according to `visibilityMode` (default: `TargetFullscreenWindow`) for target windows on the selected monitor, or when calibration mode is enabled.
- Supports global hotkeys for live nudge-based calibration.
- Persists nudge offsets to `%AppData%\DotHiderNative\settings.ini`.
- Stores a native tray icon menu for quick control.

## Build

```powershell
.\build.ps1
```

The build script locates MSBuild (via `vswhere` when available) and builds `Release|x64`.

## Run

```powershell
I:\...\x64\Release\DotHiderNative.exe
```

Start the executable directly. On first run, it creates:

- `%AppData%\DotHiderNative\settings.ini` (preferred when writable)
- `%TEMP%\DotHiderNative\settings.ini` (fallback if `%APPDATA%` is not writable)

Both locations use default values on first launch.

## Tray behavior

- Tray icon is created on startup and stays available for app control.
- Right-click the tray icon for:
  - Toggle Calibration Mode
  - Reload Settings
  - Open Settings File
  - Show Diagnostics Snapshot
  - Exit

## Calibration

1. Enable calibration mode with `Ctrl+Alt+D` (or tray menu).
2. Move the overlay with arrow hotkeys while calibration is enabled.
3. Large-step movement uses `Shift`:
   - `Ctrl+Alt+Shift+Arrow`
4. Toggle calibration mode off with `Ctrl+Alt+D` when alignment is done.

`topInset` and `rightInset` are written back to `settings.ini` immediately on each nudge.

## Default settings

- monitor=primary
- anchor=TopRight
- shape=Rectangle
- color=Black
- width=9
- height=9
- topInset=2
- rightInset=2
- scaleLogicalSettings=true
- targetProcesses=JumpDesktop,JumpClient
- visibilityMode=TargetFullscreenWindow
- fullscreenTolerancePx=8
- calibrationMode=false
- persistCalibrationMode=false
- showOnlyWhenTargetForeground=true
- smallStep=1
- largeStep=10
- enableHotkeys=true
- enableMemoryLogging=false

## Visibility behavior

- `TargetFullscreenWindow` (default): show overlay when any visible top-level target process window covers the selected monitor as a full-screen window.
- `ForegroundTarget`: show overlay only when a target process is the foreground process.
- `Always`: always show overlay when calibration mode is off.

Recommended behavior:

- Use `TargetFullscreenWindow` for normal use.
- Use Calibration Mode only for manual positioning.
- Turn Calibration Mode off after adjustment.

## Keyboard controls

- `Ctrl+Alt+Left` move left (small)  
- `Ctrl+Alt+Right` move right (small)  
- `Ctrl+Alt+Up` move up (small)  
- `Ctrl+Alt+Down` move down (small)  
- `Ctrl+Alt+Shift+Left` move left (large)  
- `Ctrl+Alt+Shift+Right` move right (large)  
- `Ctrl+Alt+Shift+Up` move up (large)  
- `Ctrl+Alt+Shift+Down` move down (large)  
- `Ctrl+Alt+D` toggle calibration mode  
- `Ctrl+Alt+R` reload settings

When calibration mode is disabled, nudge hotkeys do nothing; `Ctrl+Alt+D` and `Ctrl+Alt+R` remain active.

## Memory measurement

- `enableMemoryLogging` is disabled by default.
- To enable memory logging:
  1. Edit `settings.ini` and set `enableMemoryLogging=true`.
  2. Press `Ctrl+Alt+R` to reload, or restart the app.
- Inspect the generated diagnostics file:

```powershell
Get-Content $env:TEMP\DotHiderNative\diagnostics.log    # default fallback location
Get-Content $env:APPDATA\DotHiderNative\diagnostics.log # preferred location
```

The diagnostics log includes:
- working set (MB)
- private bytes (MB)
- handle count
- overlay position/size and current offsets
- calibration state
