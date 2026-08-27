#include "windows_input.h"

#include <Windows.h>

namespace windows_input {
namespace {

using ImmDisableIme = BOOL(WINAPI*)(DWORD);

} // namespace

bool DisableImeForCurrentThread() {
    HMODULE module = LoadLibraryExW(L"imm32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;

    const auto disableIme = reinterpret_cast<ImmDisableIme>(GetProcAddress(module, "ImmDisableIME"));
    const bool disabled = disableIme && disableIme(GetCurrentThreadId()) != FALSE;
    FreeLibrary(module);
    return disabled;
}

} // namespace windows_input
