#pragma once
#include <Windows.h>

namespace overlay_recovery {
enum class Shape { Rectangle, RoundedRectangle, Circle };

// Fixed-size state: no worker, timer, heap allocation, or retained GDI object.
struct State {
    RECT bounds{};
    Shape shape = Shape::Rectangle;
    COLORREF color = 0;
    bool initialized = false;
    bool expectedVisible = false;
    DWORD lastError = ERROR_SUCCESS;
    ULONGLONG lastRefresh = 0;
};

bool FullRefreshDue(const State& state, ULONGLONG now);
bool Apply(HWND window, State& state, const RECT& bounds, Shape shape,
           COLORREF color, bool show, bool force = false);
} // namespace overlay_recovery
