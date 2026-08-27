#pragma once

#include <Windows.h>

#include <string>

namespace windows_shell {

enum class KnownFolder {
    RoamingAppData,
    LocalAppData,
};

bool AddTrayIcon(HWND window, UINT iconId, UINT callbackMessage, HICON icon, const wchar_t* tooltip);
bool RemoveTrayIcon(HWND window, UINT iconId);
bool OpenSettingsEditor(const std::wstring& settingsPath);
std::wstring GetKnownFolderPath(KnownFolder folder);

} // namespace windows_shell
