// Small Win32 native utility. WPF/.NET are intentionally avoided to keep memory
// footprint low and startup time small for a tiny background overlay helper.

#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shcore.h>
#include <psapi.h>
#include <cstring>
#include <cwctype>
#include <cmath>
#include <cstdarg>
#include <algorithm>
#include <string>
#include <vector>

namespace {

enum class VisibilityMode {
    ForegroundTarget,
    TargetFullscreenWindow,
    Always,
};

constexpr wchar_t kAppName[] = L"DotHiderNative";
constexpr wchar_t kWindowClass[] = L"DotHiderNativeOverlay";
constexpr wchar_t kSettingsDir[] = L"DotHiderNative";
constexpr wchar_t kSettingsFileName[] = L"settings.ini";
constexpr wchar_t kDiagnosticsFileName[] = L"diagnostics.log";
constexpr int kMenuToggleCalibration = 40001;
constexpr int kMenuReloadSettings = 40002;
constexpr int kMenuOpenSettings = 40003;
constexpr int kMenuShowDiagnostics = 40004;
constexpr int kMenuExit = 40005;
constexpr int kHotkeyLeftSmall = 50001;
constexpr int kHotkeyRightSmall = 50002;
constexpr int kHotkeyUpSmall = 50003;
constexpr int kHotkeyDownSmall = 50004;
constexpr int kHotkeyLeftLarge = 50005;
constexpr int kHotkeyRightLarge = 50006;
constexpr int kHotkeyUpLarge = 50007;
constexpr int kHotkeyDownLarge = 50008;
constexpr int kHotkeyToggleCalibration = 50009;
constexpr int kHotkeyReload = 50010;
constexpr UINT kTrayIconMessage = WM_APP + 10;
constexpr UINT kForegroundUpdateMessage = WM_APP + 11;
constexpr UINT_PTR kVisibilityRefreshTimerId = 1;
constexpr UINT kVisibilityRefreshIntervalMs = 2000;

struct AppSettings {
    std::wstring monitor = L"primary";
    std::wstring anchor = L"TopRight";
    std::wstring shape = L"Rectangle";
    COLORREF color = RGB(0, 0, 0);
    int width = 9;
    int height = 9;
    int topInset = 2;
    int rightInset = 2;
    bool scaleLogicalSettings = true;

    std::vector<std::wstring> targetProcesses = {L"jumpdesktop", L"jumpclient"};
    VisibilityMode visibilityMode = VisibilityMode::TargetFullscreenWindow;
    int fullscreenTolerancePx = 8;
    bool calibrationMode = false;
    bool persistCalibrationMode = false;
    bool showOnlyWhenTargetForeground = true;

    bool enableHotkeys = true;
    int smallStep = 1;
    int largeStep = 10;

    bool enableMemoryLogging = false;
};

struct MonitorInfo {
    HMONITOR handle = nullptr;
    RECT bounds{};
    bool primary = false;
    std::wstring deviceName;
};

struct Geometry {
    int logicalWidth = 0;
    int logicalHeight = 0;
    int logicalTopInset = 0;
    int logicalRightInset = 0;
    int physicalWidth = 0;
    int physicalHeight = 0;
    int physicalTopInset = 0;
    int physicalRightInset = 0;
    int x = 0;
    int y = 0;
};

std::wstring GetAppDataPath();
bool EnsureDirectory(const std::wstring& path);

std::wstring g_settingsPath;
std::wstring g_logPath;
std::wstring g_settingsBaseDir;
AppSettings g_settings;
HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWINEVENTHOOK g_foregroundHook = nullptr;
NOTIFYICONDATAW g_trayData{};
bool g_trayInstalled = false;
Geometry g_geometry;
MonitorInfo g_activeMonitor;
UINT g_dpiX = 96;
UINT g_dpiY = 96;
HRGN g_windowRgn = nullptr;

SIZE_T GetCurrentProcessHandleCount() {
    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        return static_cast<SIZE_T>(handleCount);
    }
    return 0;
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring Trim(std::wstring value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t' || value.front() == L'\r' || value.front() == L'\n')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'\r' || value.back() == L'\n')) {
        value.pop_back();
    }
    return value;
}

bool HasSuffixI(std::wstring value, const std::wstring& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    auto tail = value.substr(value.size() - suffix.size());
    return ToLower(tail) == ToLower(suffix);
}

std::wstring GetAppDataPath() {
    DWORD needed = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (needed > 0) {
        std::wstring buffer(needed, L'\0');
        DWORD got = GetEnvironmentVariableW(L"APPDATA", buffer.data(), needed);
        if (got > 0) {
            buffer.resize(got);
            if (!buffer.empty() && (buffer.back() == L'\\' || buffer.back() == L'/')) {
                buffer.resize(buffer.size() - 1);
            }
            return buffer;
        }
    }

    needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (needed > 0) {
        std::wstring buffer(needed, L'\0');
        DWORD got = GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), needed);
        if (got > 0) {
            buffer.resize(got);
            if (!buffer.empty() && (buffer.back() == L'\\' || buffer.back() == L'/')) {
                buffer.resize(buffer.size() - 1);
            }
            return buffer + L"\\AppData\\Roaming";
        }
    }

    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::wstring(path);
    }
    return {};
}

std::wstring ComposeSettingsPath() {
    const std::wstring appData = g_settingsBaseDir.empty() ? GetAppDataPath() : g_settingsBaseDir;
    if (appData.empty()) return {};
    return appData + L"\\" + kSettingsDir + L"\\" + kSettingsFileName;
}

std::wstring ComposeDiagnosticsPath() {
    const std::wstring appData = g_settingsBaseDir.empty() ? GetAppDataPath() : g_settingsBaseDir;
    if (appData.empty()) return {};
    return appData + L"\\" + kSettingsDir + L"\\" + kDiagnosticsFileName;
}

std::wstring ResolveWritableSettingsBase() {
    std::wstring candidates[3];
    candidates[0] = GetAppDataPath();

    WCHAR tempPath[MAX_PATH] = {};
    DWORD tempLen = GetTempPathW(MAX_PATH, tempPath);
    if (tempLen > 0 && tempLen < MAX_PATH) {
        candidates[1] = std::wstring(tempPath);
        while (!candidates[1].empty() &&
               (candidates[1].back() == L'\\' || candidates[1].back() == L'/')) {
            candidates[1].resize(candidates[1].size() - 1);
        }
    }

    WCHAR localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        candidates[2] = std::wstring(localAppData);
    }

    for (const auto& candidate : candidates) {
        if (candidate.empty()) continue;
        const std::wstring settingsDir = candidate + L"\\" + kSettingsDir;
        if (EnsureDirectory(settingsDir)) return candidate;
    }
    return {};
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS) {
        return true;
    }
    return false;
}

void EnsureFiles() {
    if (g_settingsBaseDir.empty()) {
        return;
    }
    std::wstring settingsDir = g_settingsBaseDir + L"\\" + kSettingsDir;
    if (!EnsureDirectory(settingsDir)) {
        return;
    }
}

void AppendLogLine(const std::wstring& line) {
    if (!g_settings.enableMemoryLogging) return;
    if (g_logPath.empty()) {
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t full[2048];
    swprintf_s(full, L"%04d-%02d-%02d %02d:%02d:%02d.%03d - %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line.c_str());

    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD bytesWritten = 0;
    WriteFile(file, full, static_cast<DWORD>(wcslen(full) * sizeof(wchar_t)), &bytesWritten, nullptr);
    CloseHandle(file);
}

void Logf(const wchar_t* format, ...) {
    if (!g_settings.enableMemoryLogging) return;
    wchar_t message[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, 1024, _TRUNCATE, format, args);
    va_end(args);
    AppendLogLine(message);
}

void LogMemorySnapshot(const wchar_t* reason) {
    if (!g_settings.enableMemoryLogging) return;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    SIZE_T workingSet = 0;
    SIZE_T privateBytes = 0;
    SIZE_T handleCount = GetCurrentProcessHandleCount();

    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters))) {
        workingSet = counters.WorkingSetSize;
        privateBytes = counters.PagefileUsage;
    }

    Logf(L"memory snapshot [%s] workingSetMB=%.2f privateBytesMB=%.2f handleCount=%llu overlay=%d,%d,%d,%d topInset=%d rightInset=%d calibration=%s",
         reason,
         workingSet / (1024.0 * 1024.0),
         privateBytes / (1024.0 * 1024.0),
         static_cast<unsigned long long>(handleCount),
         g_geometry.x, g_geometry.y, g_geometry.physicalWidth, g_geometry.physicalHeight,
         g_settings.topInset, g_settings.rightInset,
         g_settings.calibrationMode ? L"true" : L"false");
}

std::wstring ReadProfileValue(const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue) {
    wchar_t buffer[512];
    DWORD len = GetPrivateProfileStringW(section.c_str(), key.c_str(), defaultValue.c_str(), buffer, 512, g_settingsPath.c_str());
    return std::wstring(buffer, len);
}

int ReadProfileInt(const std::wstring& section, const std::wstring& key, int defaultValue) {
    const std::wstring raw = ReadProfileValue(section, key, std::to_wstring(defaultValue));
    try {
        return std::stoi(raw);
    } catch (...) {
        return defaultValue;
    }
}

bool ReadProfileBool(const std::wstring& section, const std::wstring& key, bool defaultValue) {
    const std::wstring raw = ToLower(Trim(ReadProfileValue(section, key, defaultValue ? L"true" : L"false")));
    if (raw == L"1" || raw == L"true" || raw == L"yes" || raw == L"on") return true;
    if (raw == L"0" || raw == L"false" || raw == L"no" || raw == L"off") return false;
    return defaultValue;
}

std::vector<std::wstring> ParseTargetProcesses(const std::wstring& raw) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (start < raw.size()) {
        size_t comma = raw.find(L",", start);
        std::wstring token = ToLower(Trim(raw.substr(start, comma == std::wstring::npos ? raw.size() - start : comma - start)));
        if (token.size() > 4 && HasSuffixI(token, L".exe")) {
            token = token.substr(0, token.size() - 4);
        }
        if (!token.empty()) {
            result.push_back(token);
        }
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    if (result.empty()) {
        result.push_back(L"jumpdesktop");
        result.push_back(L"jumpclient");
    }
    return result;
}

VisibilityMode ParseVisibilityMode(const std::wstring& raw, bool showOnlyWhenTargetForegroundFallback) {
    const std::wstring mode = ToLower(Trim(raw));
    if (mode.empty()) {
        return showOnlyWhenTargetForegroundFallback ? VisibilityMode::TargetFullscreenWindow : VisibilityMode::Always;
    }

    if (mode == L"foregroundtarget") return VisibilityMode::ForegroundTarget;
    if (mode == L"targetfullscreenwindow") return VisibilityMode::TargetFullscreenWindow;
    if (mode == L"always") return VisibilityMode::Always;

    return showOnlyWhenTargetForegroundFallback ? VisibilityMode::TargetFullscreenWindow : VisibilityMode::Always;
}

const wchar_t* VisibilityModeToString(VisibilityMode mode) {
    switch (mode) {
        case VisibilityMode::ForegroundTarget:
            return L"ForegroundTarget";
        case VisibilityMode::TargetFullscreenWindow:
            return L"TargetFullscreenWindow";
        case VisibilityMode::Always:
            return L"Always";
        default:
            return L"TargetFullscreenWindow";
    }
}

COLORREF ParseColor(const std::wstring& color) {
    if (ToLower(color) == L"black") return RGB(0, 0, 0);
    if (ToLower(color) == L"white") return RGB(255, 255, 255);
    if (ToLower(color) == L"red") return RGB(255, 0, 0);
    if (ToLower(color) == L"green") return RGB(0, 255, 0);
    if (ToLower(color) == L"blue") return RGB(0, 0, 255);
    return RGB(0, 0, 0);
}

void WriteProfileValue(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), g_settingsPath.c_str());
}

void PersistOverlayOffsets() {
    WriteProfileValue(L"Overlay", L"topInset", std::to_wstring(g_settings.topInset));
    WriteProfileValue(L"Overlay", L"rightInset", std::to_wstring(g_settings.rightInset));
}

void PersistCalibrationMode() {
    WriteProfileValue(L"Behavior", L"calibrationMode", g_settings.calibrationMode ? L"true" : L"false");
}

void EnsureSettingsFile() {
    if (g_settingsPath.empty()) {
        return;
    }
    DWORD attrib = GetFileAttributesW(g_settingsPath.c_str());
    if (attrib != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    HANDLE file = CreateFileW(g_settingsPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const char defaultIni[] =
        "[Overlay]\r\n"
        "monitor=primary\r\n"
        "anchor=TopRight\r\n"
        "shape=Rectangle\r\n"
        "color=Black\r\n"
        "width=9\r\n"
        "height=9\r\n"
        "topInset=2\r\n"
        "rightInset=2\r\n"
        "scaleLogicalSettings=true\r\n"
        "\r\n"
        "[Behavior]\r\n"
        "targetProcesses=JumpDesktop,JumpClient\r\n"
        "visibilityMode=TargetFullscreenWindow\r\n"
        "fullscreenTolerancePx=8\r\n"
        "calibrationMode=false\r\n"
        "persistCalibrationMode=false\r\n"
        "showOnlyWhenTargetForeground=true\r\n"
        "\r\n"
        "[Hotkeys]\r\n"
        "enableHotkeys=true\r\n"
        "smallStep=1\r\n"
        "largeStep=10\r\n"
        "\r\n"
        "[Diagnostics]\r\n"
        "enableMemoryLogging=false\r\n";

    DWORD written = 0;
    WriteFile(file, defaultIni, static_cast<DWORD>(sizeof(defaultIni) - 1), &written, nullptr);
    CloseHandle(file);
}

void LoadSettings() {
    EnsureSettingsFile();

    g_settings.monitor = ReadProfileValue(L"Overlay", L"monitor", g_settings.monitor);
    g_settings.anchor = ReadProfileValue(L"Overlay", L"anchor", g_settings.anchor);
    g_settings.shape = ReadProfileValue(L"Overlay", L"shape", g_settings.shape);
    g_settings.color = ParseColor(ReadProfileValue(L"Overlay", L"color", L"Black"));
    g_settings.width = ReadProfileInt(L"Overlay", L"width", g_settings.width);
    g_settings.height = ReadProfileInt(L"Overlay", L"height", g_settings.height);
    g_settings.topInset = ReadProfileInt(L"Overlay", L"topInset", g_settings.topInset);
    g_settings.rightInset = ReadProfileInt(L"Overlay", L"rightInset", g_settings.rightInset);
    g_settings.scaleLogicalSettings = ReadProfileBool(L"Overlay", L"scaleLogicalSettings", g_settings.scaleLogicalSettings);

    g_settings.targetProcesses = ParseTargetProcesses(ReadProfileValue(L"Behavior", L"targetProcesses", L"JumpDesktop,JumpClient"));
    g_settings.fullscreenTolerancePx = ReadProfileInt(L"Behavior", L"fullscreenTolerancePx", g_settings.fullscreenTolerancePx);
    g_settings.calibrationMode = ReadProfileBool(L"Behavior", L"calibrationMode", g_settings.calibrationMode);
    g_settings.persistCalibrationMode = ReadProfileBool(L"Behavior", L"persistCalibrationMode", g_settings.persistCalibrationMode);
    g_settings.showOnlyWhenTargetForeground = ReadProfileBool(L"Behavior", L"showOnlyWhenTargetForeground", g_settings.showOnlyWhenTargetForeground);
    g_settings.visibilityMode = ParseVisibilityMode(
        ReadProfileValue(L"Behavior", L"visibilityMode", L""),
        g_settings.showOnlyWhenTargetForeground);

    if (!g_settings.persistCalibrationMode) {
        g_settings.calibrationMode = false;
    }

    g_settings.enableHotkeys = ReadProfileBool(L"Hotkeys", L"enableHotkeys", g_settings.enableHotkeys);
    g_settings.smallStep = ReadProfileInt(L"Hotkeys", L"smallStep", g_settings.smallStep);
    g_settings.largeStep = ReadProfileInt(L"Hotkeys", L"largeStep", g_settings.largeStep);

    g_settings.enableMemoryLogging = ReadProfileBool(L"Diagnostics", L"enableMemoryLogging", g_settings.enableMemoryLogging);

    if (g_settings.width <= 0) g_settings.width = 1;
    if (g_settings.height <= 0) g_settings.height = 1;
    if (g_settings.topInset < 0) g_settings.topInset = 0;
    if (g_settings.rightInset < 0) g_settings.rightInset = 0;
    if (g_settings.fullscreenTolerancePx < 0) g_settings.fullscreenTolerancePx = 0;
    if (g_settings.smallStep < 1) g_settings.smallStep = 1;
    if (g_settings.largeStep < 1) g_settings.largeStep = 10;
}

int ScaleForDpi(int logicalValue) {
    if (!g_settings.scaleLogicalSettings) return logicalValue;
    double scaled = static_cast<double>(logicalValue) * g_dpiX / 96.0;
    return static_cast<int>(std::llround(scaled));
}

BOOL CALLBACK EnumerateMonitors(HMONITOR hMon, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(data);
    if (!monitors) return TRUE;
    MONITORINFOEXW info;
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMon, reinterpret_cast<MONITORINFO*>(&info))) return TRUE;
    MonitorInfo m;
    m.handle = hMon;
    m.bounds = info.rcMonitor;
    m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.deviceName = info.szDevice;
    monitors->push_back(m);
    return TRUE;
}

bool IsInteger(std::wstring value, int& out) {
    if (value.empty()) return false;
    try {
        out = std::stoi(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool QueryMonitorDpi(HMONITOR monitor, UINT& dpiX, UINT& dpiY) {
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, UINT, UINT*, UINT*);
    const UINT kEffectiveDpi = 0;

    HMODULE shcore = GetModuleHandleW(L"Shcore.dll");
    bool ownsShcoreHandle = false;
    if (!shcore) {
        shcore = LoadLibraryW(L"Shcore.dll");
        ownsShcoreHandle = true;
    }
    if (!shcore) return false;

    auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
    if (!fn) {
        if (ownsShcoreHandle) {
            FreeLibrary(shcore);
        }
        return false;
    }

    const HRESULT hr = fn(monitor, kEffectiveDpi, &dpiX, &dpiY);
    if (ownsShcoreHandle) {
        FreeLibrary(shcore);
    }
    return SUCCEEDED(hr);
}

bool ResolveMonitor(const std::wstring& selector, MonitorInfo& out) {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitors, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) return false;

    const std::wstring sel = ToLower(Trim(selector));
    std::wstring primary = L"primary";

    for (const auto& monitor : monitors) {
        if (monitor.primary && sel == primary) {
            out = monitor;
            return true;
        }
    }

    int index = -1;
    if (IsInteger(sel, index)) {
        if (index >= 0 && index < static_cast<int>(monitors.size())) {
            out = monitors[static_cast<size_t>(index)];
            return true;
        }
    }

    for (const auto& monitor : monitors) {
        if (ToLower(monitor.deviceName) == sel) {
            out = monitor;
            return true;
        }
    }

    for (const auto& monitor : monitors) {
        if (monitor.primary) {
            out = monitor;
            return true;
        }
    }
    out = monitors[0];
    return true;
}

void UpdateMonitorAndScale() {
    if (!ResolveMonitor(g_settings.monitor, g_activeMonitor)) return;
    g_dpiX = 96;
    g_dpiY = 96;
    if (!QueryMonitorDpi(g_activeMonitor.handle, g_dpiX, g_dpiY)) {
        HDC dc = GetDC(nullptr);
        if (dc) {
            g_dpiX = GetDeviceCaps(dc, LOGPIXELSX);
            g_dpiY = GetDeviceCaps(dc, LOGPIXELSY);
            ReleaseDC(nullptr, dc);
        }
    }
    if (g_dpiX == 0) g_dpiX = 96;
    if (g_dpiY == 0) g_dpiY = 96;

    g_geometry.logicalWidth = g_settings.width;
    g_geometry.logicalHeight = g_settings.height;
    g_geometry.logicalTopInset = g_settings.topInset;
    g_geometry.logicalRightInset = g_settings.rightInset;

    int scaledWidth = ScaleForDpi(g_geometry.logicalWidth);
    int scaledHeight = ScaleForDpi(g_geometry.logicalHeight);
    int scaledTopInset = ScaleForDpi(g_geometry.logicalTopInset);
    int scaledRightInset = ScaleForDpi(g_geometry.logicalRightInset);

    g_geometry.physicalWidth = scaledWidth > 1 ? scaledWidth : 1;
    g_geometry.physicalHeight = scaledHeight > 1 ? scaledHeight : 1;
    g_geometry.physicalTopInset = scaledTopInset > 0 ? scaledTopInset : 0;
    g_geometry.physicalRightInset = scaledRightInset > 0 ? scaledRightInset : 0;

    // DPI-aware scaling is needed because the legacy WPF settings are logical units,
    // and users expect width/height/inset values to describe UI size in logical points.
    if (ToLower(g_settings.anchor) == L"topright") {
        g_geometry.x = g_activeMonitor.bounds.right - g_geometry.physicalWidth - g_geometry.physicalRightInset;
        g_geometry.y = g_activeMonitor.bounds.top + g_geometry.physicalTopInset;
    } else {
        g_geometry.x = g_activeMonitor.bounds.right - g_geometry.physicalWidth - g_geometry.physicalRightInset;
        g_geometry.y = g_activeMonitor.bounds.top + g_geometry.physicalTopInset;
    }
}

bool IsProcessMatchTarget(const std::wstring& exeName) {
    const std::wstring lowered = ToLower(exeName);
    for (const auto& target : g_settings.targetProcesses) {
        if (lowered == target) return true;
    }
    return false;
}

std::wstring QueryProcessExecutableByPid(DWORD pid) {
    if (pid == 0) return {};
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        Logf(L"process access denied for pid=%lu", pid);
        return {};
    }

    WCHAR buffer[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    std::wstring exe;
    if (QueryFullProcessImageNameW(proc, 0, buffer, &len)) {
        exe = buffer;
        auto slash = exe.find_last_of(L"\\\\/");
        if (slash != std::wstring::npos) {
            exe = exe.substr(slash + 1);
        }
    }
    CloseHandle(proc);

    if (HasSuffixI(exe, L".exe")) {
        exe.resize(exe.size() - 4);
    }
    return ToLower(exe);
}

bool IsTargetProcessPid(DWORD pid) {
    const std::wstring exe = QueryProcessExecutableByPid(pid);
    if (exe.empty()) return false;
    return IsProcessMatchTarget(exe);
}

std::wstring QueryForegroundExecutable() {
    HWND fg = GetForegroundWindow();
    if (!fg) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return QueryProcessExecutableByPid(pid);
}

bool IsWindowCloaked(HWND hwnd) {
    using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    constexpr DWORD DWMWA_CLOAKED = 14;

    static DwmGetWindowAttributeFn fn = nullptr;
    static bool initialized = false;
    if (!initialized) {
        HMODULE module = GetModuleHandleW(L"dwmapi.dll");
        if (!module) {
            module = LoadLibraryW(L"dwmapi.dll");
        }
        if (module) {
            fn = reinterpret_cast<DwmGetWindowAttributeFn>(GetProcAddress(module, "DwmGetWindowAttribute"));
        }
        initialized = true;
    }

    if (!fn) return false;

    DWORD cloaked = 0;
    return SUCCEEDED(fn(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

bool IsTargetTopLevelVisibleWindow(HWND hwnd) {
    if (!hwnd || hwnd == g_hwnd) return false;
    if (!IsWindowVisible(hwnd)) return false;
    if (IsIconic(hwnd)) return false;
    if (IsWindowCloaked(hwnd)) return false;
    return true;
}

bool DoesWindowCoverSelectedMonitor(HWND hwnd) {
    if (!hwnd) return false;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return false;

    const int tolerance = g_settings.fullscreenTolerancePx;
    return rect.left <= (g_activeMonitor.bounds.left + tolerance)
        && rect.top <= (g_activeMonitor.bounds.top + tolerance)
        && rect.right >= (g_activeMonitor.bounds.right - tolerance)
        && rect.bottom >= (g_activeMonitor.bounds.bottom - tolerance);
}

bool HasTargetFullscreenWindowOnSelectedMonitor() {
    if (g_settings.targetProcesses.empty()) return false;

    struct SearchState {
        bool found = false;
        DWORD matchingPid = 0;
        HWND matchingWindow = nullptr;
    } state;

    auto callback = [](HWND hwnd, LPARAM lParam) -> BOOL {
        SearchState* searchState = reinterpret_cast<SearchState*>(lParam);
        if (!searchState || searchState->found) return FALSE;

        if (!IsTargetTopLevelVisibleWindow(hwnd)) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!IsTargetProcessPid(pid)) return TRUE;

        if (!DoesWindowCoverSelectedMonitor(hwnd)) return TRUE;

        searchState->found = true;
        searchState->matchingPid = pid;
        searchState->matchingWindow = hwnd;
        return FALSE;
    };

    EnumWindows(callback, reinterpret_cast<LPARAM>(&state));

    if (state.found && g_settings.enableMemoryLogging) {
        std::wstring exe = QueryProcessExecutableByPid(state.matchingPid);
        Logf(L"fullscreen target hit pid=%lu exe=%s hwnd=%p", state.matchingPid, exe.c_str(), state.matchingWindow);
    }

    return state.found;
}

bool IsTargetForegroundProcess() {
    const std::wstring exe = QueryForegroundExecutable();
    if (exe.empty()) return false;
    return IsProcessMatchTarget(exe);
}

void RefreshVisibilityState() {
    bool targetMatch = false;

    if (g_settings.calibrationMode) {
        targetMatch = true;
    } else if (g_settings.visibilityMode == VisibilityMode::Always) {
        targetMatch = true;
    } else if (g_settings.visibilityMode == VisibilityMode::ForegroundTarget) {
        targetMatch = IsTargetForegroundProcess();
    } else {
        targetMatch = HasTargetFullscreenWindowOnSelectedMonitor();
    }

    Logf(L"visibility check mode=%s targetMatch=%s calibration=%s",
         VisibilityModeToString(g_settings.visibilityMode),
         targetMatch ? L"true" : L"false",
         g_settings.calibrationMode ? L"true" : L"false");

    ShowOrHideOverlay(targetMatch);
}

void ApplyOverlayShape() {
    if (!g_hwnd) return;
    if (g_windowRgn) {
        DeleteObject(g_windowRgn);
        g_windowRgn = nullptr;
    }
    if (g_geometry.physicalWidth <= 0 || g_geometry.physicalHeight <= 0) return;

    const std::wstring shape = ToLower(g_settings.shape);
    if (shape == L"rectangle") {
        SetWindowRgn(g_hwnd, nullptr, TRUE);
        return;
    }

    // SetWindowRgn is used so the overlay remains click-through and only the desired
    // shape area is visible/active.
    HRGN region = nullptr;
    if (shape == L"roundedrectangle" || shape == L"rounded_rectangle" || shape == L"roundrectangle") {
        const int radiusX = (g_geometry.physicalWidth / 4 > 1) ? (g_geometry.physicalWidth / 4) : 1;
        const int radiusY = (g_geometry.physicalHeight / 4 > 1) ? (g_geometry.physicalHeight / 4) : 1;
        region = CreateRoundRectRgn(0, 0, g_geometry.physicalWidth, g_geometry.physicalHeight, radiusX, radiusY);
    } else {
        region = CreateEllipticRgn(0, 0, g_geometry.physicalWidth, g_geometry.physicalHeight);
    }

    if (!region) return;

    if (SetWindowRgn(g_hwnd, region, TRUE)) {
        g_windowRgn = nullptr;
    } else {
        DeleteObject(region);
        g_windowRgn = nullptr;
        SetWindowRgn(g_hwnd, nullptr, TRUE);
    }
}

void ApplyOverlayPlacement() {
    if (!g_hwnd) return;
    ApplyOverlayShape();
    SetWindowPos(g_hwnd, HWND_TOPMOST,
                 g_geometry.x, g_geometry.y,
                 g_geometry.physicalWidth, g_geometry.physicalHeight,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    InvalidateRect(g_hwnd, nullptr, FALSE);
    if (IsWindowVisible(g_hwnd)) {
        UpdateWindow(g_hwnd);
    }
}

void ShowOrHideOverlay(bool show) {
    if (!g_hwnd) return;
    bool currentlyVisible = IsWindowVisible(g_hwnd) != FALSE;
    if (show == currentlyVisible) {
        if (show) {
            ApplyOverlayPlacement();
        }
        return;
    }

    if (show) {
        ApplyOverlayPlacement();
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        Logf(L"overlay show");
        LogMemorySnapshot(L"overlay show");
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
        Logf(L"overlay hide");
        LogMemorySnapshot(L"overlay hide");
    }
}

void RegisterOrUpdateHotkeys();

void PersistAndReposition() {
    UpdateMonitorAndScale();
    ApplyOverlayPlacement();
    RefreshVisibilityState();
}

void SetCalibrationMode(bool enable) {
    if (g_settings.calibrationMode == enable) return;
    g_settings.calibrationMode = enable;
    if (g_settings.persistCalibrationMode) {
        PersistCalibrationMode();
    }
    Logf(L"calibration mode=%s", enable ? L"true" : L"false");
    RefreshVisibilityState();
}

void NudgeBy(int dxLogical, int dyLogical) {
    g_settings.topInset += dyLogical;
    g_settings.rightInset += dxLogical;

    if (g_settings.topInset < 0) g_settings.topInset = 0;
    if (g_settings.rightInset < 0) g_settings.rightInset = 0;

    PersistOverlayOffsets();
    PersistAndReposition();
    Logf(L"hotkey nudge dx=%d dy=%d -> topInset=%d rightInset=%d", dxLogical, dyLogical, g_settings.topInset, g_settings.rightInset);
    LogMemorySnapshot(L"hotkey nudge");
}

void RegisterOrUpdateHotkeys() {
    const UINT MOD_CAL = MOD_CONTROL | MOD_ALT;
    const UINT MOD_CAL_SHIFT = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    auto reg = [](int id, UINT mods, UINT vk) {
        RegisterHotKey(g_hwnd, id, mods, vk);
    };
    auto unreg = [](int id) {
        UnregisterHotKey(g_hwnd, id);
    };

    unreg(kHotkeyLeftSmall);
    unreg(kHotkeyRightSmall);
    unreg(kHotkeyUpSmall);
    unreg(kHotkeyDownSmall);
    unreg(kHotkeyLeftLarge);
    unreg(kHotkeyRightLarge);
    unreg(kHotkeyUpLarge);
    unreg(kHotkeyDownLarge);
    unreg(kHotkeyToggleCalibration);
    unreg(kHotkeyReload);

    if (!g_settings.enableHotkeys) return;

    // Hotkeys are a low-memory alternative to a live settings GUI for precise calibration.
    reg(kHotkeyLeftSmall, MOD_CAL, VK_LEFT);
    reg(kHotkeyRightSmall, MOD_CAL, VK_RIGHT);
    reg(kHotkeyUpSmall, MOD_CAL, VK_UP);
    reg(kHotkeyDownSmall, MOD_CAL, VK_DOWN);
    reg(kHotkeyLeftLarge, MOD_CAL_SHIFT, VK_LEFT);
    reg(kHotkeyRightLarge, MOD_CAL_SHIFT, VK_RIGHT);
    reg(kHotkeyUpLarge, MOD_CAL_SHIFT, VK_UP);
    reg(kHotkeyDownLarge, MOD_CAL_SHIFT, VK_DOWN);
    reg(kHotkeyToggleCalibration, MOD_CAL, 'D');
    reg(kHotkeyReload, MOD_CAL, 'R');
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | (g_settings.calibrationMode ? MF_CHECKED : MF_UNCHECKED), kMenuToggleCalibration, L"Toggle Calibration Mode");
    AppendMenuW(menu, MF_STRING, kMenuReloadSettings, L"Reload Settings");
    AppendMenuW(menu, MF_STRING, kMenuOpenSettings, L"Open Settings File");
    AppendMenuW(menu, MF_STRING, kMenuShowDiagnostics, L"Show Diagnostics Snapshot");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void OpenSettingsFile() {
    EnsureSettingsFile();
    std::wstring quoted = L"\"" + g_settingsPath + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"notepad++.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void ShowDiagnosticsSnapshot() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    SIZE_T workingSet = 0;
    SIZE_T privateBytes = 0;
    SIZE_T handleCount = GetCurrentProcessHandleCount();
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        workingSet = counters.WorkingSetSize;
        privateBytes = counters.PagefileUsage;
    }

    wchar_t text[1024];
    swprintf_s(text,
               L"Working Set MB: %.2f\r\n"
               L"Private Bytes MB: %.2f\r\n"
               L"Handle Count: %llu\r\n"
               L"Overlay: x=%d y=%d w=%d h=%d\r\n"
               L"topInset=%d\r\n"
               L"rightInset=%d\r\n"
               L"calibrationMode=%s",
               workingSet / (1024.0 * 1024.0),
               privateBytes / (1024.0 * 1024.0),
               static_cast<unsigned long long>(handleCount),
               g_geometry.x, g_geometry.y, g_geometry.physicalWidth, g_geometry.physicalHeight,
               g_settings.topInset, g_settings.rightInset,
               g_settings.calibrationMode ? L"true" : L"false");

    MessageBoxW(g_hwnd, text, L"DotHiderNative Diagnostics", MB_OK | MB_ICONINFORMATION);
    Logf(L"diagnostics snapshot shown");
    LogMemorySnapshot(L"diagnostics snapshot shown");
}

void LoadAndApplySettings() {
    LoadSettings();
    Logf(L"settings load");
    UpdateMonitorAndScale();
    RegisterOrUpdateHotkeys();
    PersistAndReposition();
}

void CALLBACK ForegroundChangeHookProc(
    HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_hwnd) {
        PostMessageW(g_hwnd, kForegroundUpdateMessage, 0, 0);
    }
}

void CreateTrayIcon() {
    if (g_trayInstalled) return;

    std::memset(&g_trayData, 0, sizeof(g_trayData));
    g_trayData.cbSize = sizeof(g_trayData);
    g_trayData.hWnd = g_hwnd;
    g_trayData.uID = 1;
    g_trayData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayData.uCallbackMessage = kTrayIconMessage;
    g_trayData.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    wcsncpy_s(g_trayData.szTip, L"DotHiderNative", _TRUNCATE);

    if (Shell_NotifyIconW(NIM_ADD, &g_trayData)) {
        g_trayInstalled = true;
        Logf(L"tray icon created");
    }
}

void DestroyTrayIcon() {
    if (!g_trayInstalled) return;
    Shell_NotifyIconW(NIM_DELETE, &g_trayData);
    g_trayInstalled = false;
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_CREATE:
            return 0;
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case kMenuToggleCalibration:
                    SetCalibrationMode(!g_settings.calibrationMode);
                    break;
                case kMenuReloadSettings:
                    LoadAndApplySettings();
                    break;
                case kMenuOpenSettings:
                    OpenSettingsFile();
                    break;
                case kMenuShowDiagnostics:
                    ShowDiagnosticsSnapshot();
                    break;
                case kMenuExit:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }
        case kTrayIconMessage: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        }
        case kForegroundUpdateMessage:
            RefreshVisibilityState();
            return 0;
        case WM_TIMER:
            if (wParam == kVisibilityRefreshTimerId) {
                RefreshVisibilityState();
            }
            return 0;
        case WM_HOTKEY: {
            if (!g_settings.enableHotkeys) return 0;

            if (wParam == kHotkeyToggleCalibration) {
                SetCalibrationMode(!g_settings.calibrationMode);
                return 0;
            }
            if (wParam == kHotkeyReload) {
                LoadAndApplySettings();
                return 0;
            }

            if (!g_settings.calibrationMode) return 0;

            switch (wParam) {
                case kHotkeyLeftSmall:
                    NudgeBy(g_settings.smallStep, 0);
                    break;
                case kHotkeyRightSmall:
                    NudgeBy(-g_settings.smallStep, 0);
                    break;
                case kHotkeyUpSmall:
                    NudgeBy(0, -g_settings.smallStep);
                    break;
                case kHotkeyDownSmall:
                    NudgeBy(0, g_settings.smallStep);
                    break;
                case kHotkeyLeftLarge:
                    NudgeBy(g_settings.largeStep, 0);
                    break;
                case kHotkeyRightLarge:
                    NudgeBy(-g_settings.largeStep, 0);
                    break;
                case kHotkeyUpLarge:
                    NudgeBy(0, -g_settings.largeStep);
                    break;
                case kHotkeyDownLarge:
                    NudgeBy(0, g_settings.largeStep);
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH brush = CreateSolidBrush(g_settings.color);
            FillRect(hdc, &client, brush);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY: {
            for (int id : { kHotkeyLeftSmall, kHotkeyRightSmall, kHotkeyUpSmall, kHotkeyDownSmall,
                           kHotkeyLeftLarge, kHotkeyRightLarge, kHotkeyUpLarge, kHotkeyDownLarge,
                           kHotkeyToggleCalibration, kHotkeyReload }) {
                UnregisterHotKey(hwnd, id);
            }
            KillTimer(hwnd, kVisibilityRefreshTimerId);
            if (g_foregroundHook) {
                UnhookWinEvent(g_foregroundHook);
                g_foregroundHook = nullptr;
            }
            DestroyTrayIcon();
            Logf(L"exit");
            LogMemorySnapshot(L"exit");
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    g_instance = hInstance;
    g_settingsBaseDir = ResolveWritableSettingsBase();
    g_settingsPath = ComposeSettingsPath();
    g_logPath = ComposeDiagnosticsPath();
    if (g_settingsPath.empty() || g_logPath.empty()) {
        return 1;
    }
    EnsureFiles();
    LoadSettings();

    Logf(L"startup");
    LogMemorySnapshot(L"startup");

    if (g_settings.enableMemoryLogging) {
        EnsureFiles();
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassExW failed", L"DotHiderNative", MB_OK | MB_ICONERROR);
        return 1;
    }

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    g_hwnd = CreateWindowExW(
        exStyle,
        kWindowClass,
        kAppName,
        WS_POPUP,
        0, 0, 1, 1,
        nullptr,
        nullptr,
        hInstance,
        nullptr);
    if (!g_hwnd) {
        MessageBoxW(nullptr, L"CreateWindowExW failed", L"DotHiderNative", MB_OK | MB_ICONERROR);
        return 1;
    }
    UpdateMonitorAndScale();
    ApplyOverlayPlacement();
    Logf(L"overlay created");

    CreateTrayIcon();

    RegisterOrUpdateHotkeys();
    SetTimer(g_hwnd, kVisibilityRefreshTimerId, kVisibilityRefreshIntervalMs, nullptr);
    g_foregroundHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        ForegroundChangeHookProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    RefreshVisibilityState();

    LogMemorySnapshot(L"after initialization");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
