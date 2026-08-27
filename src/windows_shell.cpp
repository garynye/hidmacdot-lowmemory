#include "windows_shell.h"

#include <shellapi.h>
#include <shlobj.h>

namespace windows_shell {
namespace {

class ShellModule final {
public:
    ShellModule()
        : handle_(LoadLibraryExW(L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
    }

    ~ShellModule() {
        if (handle_) {
            FreeLibrary(handle_);
        }
    }

    ShellModule(const ShellModule&) = delete;
    ShellModule& operator=(const ShellModule&) = delete;

    explicit operator bool() const {
        return handle_ != nullptr;
    }

    FARPROC Resolve(const char* functionName) const {
        return handle_ ? GetProcAddress(handle_, functionName) : nullptr;
    }

private:
    HMODULE handle_ = nullptr;
};

using ShellNotifyIconW = BOOL(WINAPI*)(DWORD, PNOTIFYICONDATAW);
using ShellExecuteW = HINSTANCE(WINAPI*)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
using SHGetFolderPathW = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);

ShellNotifyIconW ResolveShellNotifyIcon(const ShellModule& module) {
    return reinterpret_cast<ShellNotifyIconW>(module.Resolve("Shell_NotifyIconW"));
}

} // namespace

bool AddTrayIcon(HWND window, UINT iconId, UINT callbackMessage, HICON icon, const wchar_t* tooltip) {
    ShellModule module;
    if (!module) return false;

    const ShellNotifyIconW notifyIcon = ResolveShellNotifyIcon(module);
    if (!notifyIcon) return false;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = iconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = callbackMessage;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, ARRAYSIZE(data.szTip), tooltip, _TRUNCATE);
    return notifyIcon(NIM_ADD, &data) != FALSE;
}

bool RemoveTrayIcon(HWND window, UINT iconId) {
    ShellModule module;
    if (!module) return false;

    const ShellNotifyIconW notifyIcon = ResolveShellNotifyIcon(module);
    if (!notifyIcon) return false;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = iconId;
    return notifyIcon(NIM_DELETE, &data) != FALSE;
}

bool OpenSettingsEditor(const std::wstring& settingsPath) {
    ShellModule module;
    if (!module) return false;

    const auto shellExecute = reinterpret_cast<ShellExecuteW>(module.Resolve("ShellExecuteW"));
    if (!shellExecute) return false;

    const std::wstring quotedPath = L"\"" + settingsPath + L"\"";
    HINSTANCE result = shellExecute(nullptr, L"open", L"notepad++.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        result = shellExecute(nullptr, L"open", L"notepad.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return reinterpret_cast<INT_PTR>(result) > 32;
}

std::wstring GetKnownFolderPath(KnownFolder folder) {
    ShellModule module;
    if (!module) return {};

    const auto getFolderPath = reinterpret_cast<SHGetFolderPathW>(module.Resolve("SHGetFolderPathW"));
    if (!getFolderPath) return {};

    const int folderId = folder == KnownFolder::RoamingAppData ? CSIDL_APPDATA : CSIDL_LOCAL_APPDATA;
    wchar_t path[MAX_PATH] = {};
    if (FAILED(getFolderPath(nullptr, folderId, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return {};
    }
    return path;
}

} // namespace windows_shell
