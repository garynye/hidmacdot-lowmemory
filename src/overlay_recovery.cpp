#include "overlay_recovery.h"
#include <algorithm>

namespace overlay_recovery {
bool FullRefreshDue(const State& state, ULONGLONG now) {
    return !state.initialized || now - state.lastRefresh >= 5 * 60 * 1000;
}

bool Apply(HWND window, State& state, const RECT& bounds, Shape shape,
           COLORREF color, bool show, bool force) {
    state.expectedVisible = show;
    if (!IsWindow(window)) {
        state.lastError = ERROR_INVALID_WINDOW_HANDLE;
        return false;
    }
    const bool wasVisible = IsWindowVisible(window) != FALSE;
    const bool sizeChanged = !state.initialized ||
        bounds.right - bounds.left != state.bounds.right - state.bounds.left ||
        bounds.bottom - bounds.top != state.bounds.bottom - state.bounds.top;
    const bool shapeChanged = sizeChanged || shape != state.shape;
    const bool appearanceChanged = shapeChanged || color != state.color;
    RECT actual{};
    if (!GetWindowRect(window, &actual)) {
        state.lastError = GetLastError();
        return false;
    }
    if (shapeChanged) {
        HRGN region = nullptr;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (shape == Shape::RoundedRectangle) {
            region = CreateRoundRectRgn(0, 0, width, height,
                (std::max)(1, width / 4), (std::max)(1, height / 4));
        } else if (shape == Shape::Circle) {
            region = CreateEllipticRgn(0, 0, width, height);
        }
        if (shape != Shape::Rectangle && !region) {
            state.lastError = ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }
        if (!SetWindowRgn(window, region, FALSE)) {
            state.lastError = GetLastError();
            if (region) DeleteObject(region);
            return false;
        }
        // Windows owns a successfully assigned region.
    }
    if (!EqualRect(&actual, &bounds) || show || force) {
        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        if (EqualRect(&actual, &bounds)) flags |= SWP_NOMOVE | SWP_NOSIZE;
        if (!show) flags |= SWP_NOZORDER;
        // SWP_NOZORDER would ignore HWND_TOPMOST and prevent recovery.
        if (!SetWindowPos(window, HWND_TOPMOST, bounds.left, bounds.top,
                          bounds.right - bounds.left, bounds.bottom - bounds.top, flags)) {
            state.lastError = GetLastError();
            return false;
        }
    }
    if (show && !wasVisible) ShowWindow(window, SW_SHOWNOACTIVATE);
    else if (!show && wasVisible) ShowWindow(window, SW_HIDE);
    if (appearanceChanged || force || (show && !wasVisible)) {
        InvalidateRect(window, nullptr, FALSE);
        if (show) UpdateWindow(window);
    }
    state.bounds = bounds;
    state.shape = shape;
    state.color = color;
    state.initialized = true;
    state.lastError = ERROR_SUCCESS;
    if (force || !state.lastRefresh) state.lastRefresh = GetTickCount64();
    return true;
}
} // namespace overlay_recovery
