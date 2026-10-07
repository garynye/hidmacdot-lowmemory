#include "overlay_recovery.h"
#include <cstdio>
#include <algorithm>
#include <string>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
    std::printf("PASS: %s\n", message);
}

void Pump(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(20);
    } while (GetTickCount64() < end);
}

HWND CreateTestWindow(const RECT& bounds) {
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"STATIC", L"DotHider recovery test", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) throw std::runtime_error("CreateWindowEx failed");
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    return window;
}

bool Above(HWND first, HWND second) {
    for (HWND current = GetTopWindow(nullptr); current; current = GetWindow(current, GW_HWNDNEXT)) {
        if (current == first) return true;
        if (current == second) return false;
    }
    return false;
}

struct WindowSearch { DWORD pid; HWND found = nullptr; };
BOOL CALLBACK FindOverlay(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<WindowSearch*>(data);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (pid == search.pid && std::wstring(name) == L"DotHiderNativeOverlay") {
        search.found = window;
        return FALSE;
    }
    return TRUE;
}

void ModuleTests() {
    using namespace overlay_recovery;
    State state;
    Check(FullRefreshDue(state, 0), "first reconciliation forces refresh");
    state.initialized = true;
    state.lastRefresh = 1000;
    Check(!FullRefreshDue(state, 300999), "no premature five-minute refresh");
    Check(FullRefreshDue(state, 301000), "five-minute refresh becomes due");
    const RECT bounds{50, 50, 68, 68};
    HWND window = CreateTestWindow(bounds);
    HWND other = CreateTestWindow(bounds);
    state = {};
    try {
        Check(Apply(window, state, bounds, Shape::Rectangle, RGB(0, 0, 0), true), "initial show");
        const HWND foreground = GetForegroundWindow();
        SetWindowPos(other, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        Check(Above(other, window), "competing topmost window reproduces occlusion");
        Check(Apply(window, state, bounds, Shape::Rectangle, 0, true) && Above(window, other), "stacking order recovers");
        ShowWindow(window, SW_HIDE);
        SetWindowPos(window, HWND_NOTOPMOST, 400, 400, 2, 2, SWP_NOACTIVATE);
        Check(Apply(window, state, bounds, Shape::Rectangle, 0, true), "hidden displaced overlay recovers");
        RECT actual{};
        GetWindowRect(window, &actual);
        Check(IsWindowVisible(window) && EqualRect(&actual, &bounds), "visibility and geometry restored");
        Check(GetForegroundWindow() == foreground, "recovery does not steal focus");
        Check(Apply(window, state, bounds, Shape::Circle, 0, true), "circle shape applies");
        ValidateRect(window, nullptr);
        const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        DWORD handlesBefore = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
        bool succeeded = true;
        for (int i = 0; i < 1000; ++i) succeeded &= Apply(window, state, bounds, Shape::Circle, 0, true);
        DWORD handlesAfter = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
        Check(succeeded && gdi == GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) &&
            handlesBefore == handlesAfter, "1000 steady reconciliations do not leak handles or GDI objects");
        Check(!GetUpdateRect(window, nullptr, FALSE), "unchanged checks do not repaint");
        Check(Apply(window, state, bounds, Shape::RoundedRectangle, RGB(255, 0, 0), true, true), "forced shape and color refresh");
        Check(Apply(window, state, bounds, Shape::Rectangle, 0, false) && !IsWindowVisible(window), "intentional hide preserved");
        Check(!Apply(nullptr, state, bounds, Shape::Rectangle, 0, true) &&
            state.lastError == ERROR_INVALID_WINDOW_HANDLE, "invalid window reports recovery error");
    } catch (...) {
        DestroyWindow(other); DestroyWindow(window); throw;
    }
    DestroyWindow(other); DestroyWindow(window);
}

void IntegrationTests(const wchar_t* executable, DWORD soakSeconds) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    HWND target = CreateTestWindow(monitor.rcMonitor);
    ShowWindow(target, SW_SHOWNOACTIVATE);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + std::wstring(executable) + L"\"";
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        DestroyWindow(target);
        throw std::runtime_error("candidate launch failed");
    }
    HWND overlay = nullptr;
    HWND other = nullptr;
    try {
        for (int i = 0; i < 50 && !overlay; ++i) {
            WindowSearch search{process.dwProcessId};
            EnumWindows(FindOverlay, reinterpret_cast<LPARAM>(&search));
            overlay = search.found;
            Pump(100);
        }
        Check(overlay != nullptr, "isolated app creates overlay");
        Pump(2500);
        Check(IsWindowVisible(overlay) != FALSE, "synthetic fullscreen target detected");
        RECT expected{};
        GetWindowRect(overlay, &expected);
        other = CreateTestWindow(expected);
        SetWindowPos(other, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        Check(Above(other, overlay), "integration reproduces topmost competition");
        const HWND foreground = GetForegroundWindow();
        Pump(4200);
        Check(Above(overlay, other), "existing timer restores stacking");
        ShowWindow(overlay, SW_HIDE);
        SetWindowPos(overlay, HWND_NOTOPMOST, -500, -500, 1, 1, SWP_NOACTIVATE);
        Pump(4200);
        RECT actual{};
        GetWindowRect(overlay, &actual);
        Check(IsWindowVisible(overlay) && EqualRect(&actual, &expected), "existing timer restores hidden displaced overlay");
        Check(GetForegroundWindow() == foreground, "timer recovery preserves foreground app");
        ShowWindow(target, SW_HIDE);
        Pump(4200);
        Check(!IsWindowVisible(overlay), "cover hides when fullscreen target disappears");
        PostMessageW(overlay, WM_COMMAND, 40001, 0);
        Pump(200);
        Check(IsWindowVisible(overlay) != FALSE, "calibration overrides target hiding");
        PostMessageW(overlay, WM_COMMAND, 40001, 0);
        Pump(200);
        Check(!IsWindowVisible(overlay), "leaving calibration restores intentional hiding");
        wchar_t profile[MAX_PATH]{};
        GetEnvironmentVariableW(L"APPDATA", profile, MAX_PATH);
        const std::wstring settingsPath = std::wstring(profile) + L"\\DotHiderNative\\settings.ini";
        Check(WritePrivateProfileStringW(L"Overlay", L"width", L"18", settingsPath.c_str()) != FALSE,
              "isolated settings update succeeds");
        PostMessageW(overlay, WM_COMMAND, 40002, 0);
        Pump(200);
        GetWindowRect(overlay, &actual);
        Check(!IsWindowVisible(overlay) && actual.right - actual.left > expected.right - expected.left,
              "Reload Settings updates geometry while respecting hidden state");
        ShowWindow(target, SW_SHOWNOACTIVATE);
        Pump(4200);
        Check(IsWindowVisible(overlay) != FALSE, "cover returns when target reconnects");
        PostMessageW(overlay, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        PostMessageW(overlay, WM_DISPLAYCHANGE, 0, 0);
        Pump(500);
        Check(IsWindowVisible(overlay) != FALSE, "resume and display messages preserve cover");
        if (soakSeconds) {
            const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(soakSeconds) * 1000;
            DWORD minimumHandles = MAXDWORD, maximumHandles = 0;
            DWORD minimumGdi = MAXDWORD, maximumGdi = 0;
            while (GetTickCount64() < end) {
                Pump(1000);
                DWORD handles = 0;
                if (!GetProcessHandleCount(process.hProcess, &handles) ||
                    WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT || !IsWindowVisible(overlay)) {
                    throw std::runtime_error("soak lost overlay/process");
                }
                const DWORD gdi = GetGuiResources(process.hProcess, GR_GDIOBJECTS);
                minimumHandles = (std::min)(minimumHandles, handles);
                maximumHandles = (std::max)(maximumHandles, handles);
                minimumGdi = (std::min)(minimumGdi, gdi);
                maximumGdi = (std::max)(maximumGdi, gdi);
            }
            std::printf("Soak %lu seconds: handles %lu..%lu, GDI %lu..%lu\n",
                soakSeconds, minimumHandles, maximumHandles, minimumGdi, maximumGdi);
            Check(maximumHandles - minimumHandles <= 2 && maximumGdi - minimumGdi <= 1, "soak resource counts remain bounded");
        }
    } catch (...) {
        if (other) DestroyWindow(other);
        DestroyWindow(target);
        if (overlay) PostMessageW(overlay, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); throw;
    }
    if (other) DestroyWindow(other);
    DestroyWindow(target);
    PostMessageW(overlay, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--target-only") {
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
            HWND target = CreateTestWindow(monitor.rcMonitor);
            ShowWindow(target, SW_SHOWNOACTIVATE);
            Pump(20 * 60 * 1000);
            DestroyWindow(target);
        } else {
            ModuleTests();
            if (argc > 1) IntegrationTests(argv[1], argc > 2 ? std::stoul(argv[2]) : 0);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    return 0;
}
