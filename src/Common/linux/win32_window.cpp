// Win32 window/cursor subset over SDL3 (see win32_window.h).
#include <cstdint>

#include <windows_base.h> // RECT/POINT, same as the engine sees
#include "win32_window.h"

#include <SDL3/SDL.h>

#include <atomic>

namespace
{
    SDL_Window *g_main = nullptr;
    std::atomic<int> g_cursor_count{0}; // Windows starts at 0 (visible) when a mouse is installed
    std::atomic<bool> g_quit{false};
    long g_style = 0;

    void *const kDesktop = (void *)(intptr_t)-3;

    SDL_Window *win(void *hwnd)
    {
        return hwnd && hwnd != kDesktop ? (SDL_Window *)hwnd : g_main;
    }

    bool display_bounds(SDL_DisplayID d, RECT &r, bool usable)
    {
        SDL_Rect b;
        if (!d || !(usable ? SDL_GetDisplayUsableBounds(d, &b) : SDL_GetDisplayBounds(d, &b)))
            return false;
        r = RECT{b.x, b.y, b.x + b.w, b.y + b.h};
        return true;
    }

    void window_origin(SDL_Window *w, int &x, int &y)
    {
        x = y = 0;
        if (w)
            SDL_GetWindowPosition(w, &x, &y); // Wayland reports 0,0; all conversions stay consistent
    }
} // namespace

void xr_linux_set_main_window(SDL_Window *w)
{
    g_main = w;
}
SDL_Window *xr_linux_main_window()
{
    return g_main;
}

int ShowCursor(int show)
{
    const int n = show ? ++g_cursor_count : --g_cursor_count;
    if (n >= 0)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
    return n;
}

int ClipCursor(const RECT *r)
{
    if (!g_main)
        return 0;
    if (!r)
        return SDL_SetWindowMouseRect(g_main, nullptr) ? 1 : 0;
    int ox, oy;
    window_origin(g_main, ox, oy);
    const SDL_Rect rect{r->left - ox, r->top - oy, r->right - r->left, r->bottom - r->top};
    return SDL_SetWindowMouseRect(g_main, &rect) ? 1 : 0;
}

int GetClientRect(void *hwnd, RECT *r)
{
    SDL_Window *w = win(hwnd);
    int cw = 0, ch = 0;
    if (!w || !r || !SDL_GetWindowSizeInPixels(w, &cw, &ch))
        return 0;
    *r = RECT{0, 0, cw, ch};
    return 1;
}

int GetWindowRect(void *hwnd, RECT *r)
{
    if (hwnd == kDesktop)
        return r && display_bounds(SDL_GetPrimaryDisplay(), *r, false) ? 1 : 0;
    SDL_Window *w = win(hwnd);
    int x, y, cw = 0, ch = 0;
    if (!w || !r || !SDL_GetWindowSizeInPixels(w, &cw, &ch))
        return 0;
    window_origin(w, x, y);
    *r = RECT{x, y, x + cw, y + ch};
    return 1;
}

int MapWindowPoints(void *from, void *to, POINT *pts, unsigned n)
{
    int fx, fy, tx = 0, ty = 0;
    window_origin(win(from), fx, fy);
    if (to)
        window_origin((SDL_Window *)to, tx, ty);
    for (unsigned i = 0; i < n; ++i)
    {
        pts[i].x += fx - tx;
        pts[i].y += fy - ty;
    }
    return 1;
}

int ClientToScreen(void *hwnd, POINT *p)
{
    return MapWindowPoints(hwnd, nullptr, p, 1);
}

int ScreenToClient(void *hwnd, POINT *p)
{
    int x, y;
    window_origin(win(hwnd), x, y);
    p->x -= x;
    p->y -= y;
    return 1;
}

int GetCursorPos(POINT *p)
{
    // Window-relative position plus window origin: global coordinates are unavailable on Wayland.
    float x = 0.f, y = 0.f;
    SDL_GetMouseState(&x, &y);
    int ox, oy;
    window_origin(g_main, ox, oy);
    p->x = (int32_t)x + ox;
    p->y = (int32_t)y + oy;
    return 1;
}

int SetCursorPos(int x, int y)
{
    if (!g_main)
        return 0;
    int ox, oy;
    window_origin(g_main, ox, oy);
    SDL_WarpMouseInWindow(g_main, (float)(x - ox), (float)(y - oy));
    return 1;
}

int SetForegroundWindow(void *hwnd)
{
    if (SDL_Window *w = win(hwnd))
        return SDL_RaiseWindow(w) ? 1 : 0;
    return 0;
}

int SetActiveWindow(void *hwnd)
{
    return SetForegroundWindow(hwnd);
}

int SetWindowPos(void *hwnd, void *, int x, int y, int w, int h, unsigned flags)
{
    SDL_Window *sw = win(hwnd);
    if (!sw)
        return 0;
    SDL_SetWindowPosition(sw, x, y);
    if (w > 0 && h > 0)
        SDL_SetWindowSize(sw, w, h);
    if (flags & SWP_SHOWWINDOW)
        SDL_ShowWindow(sw);
    return 1;
}

intptr_t SetWindowLongPtr(void *hwnd, int index, intptr_t value)
{
    if (index != GWL_STYLE)
        return 0;
    const intptr_t old = g_style;
    g_style = (long)value;
    if (SDL_Window *w = win(hwnd))
    {
        SDL_SetWindowBordered(w, !(value & WS_POPUP));
        if (value & WS_VISIBLE)
            SDL_ShowWindow(w);
    }
    return old;
}

long GetWindowLong(void *, int index)
{
    return index == GWL_STYLE ? g_style : 0;
}

int IsIconic(void *hwnd)
{
    SDL_Window *w = win(hwnd);
    return w && (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) ? 1 : 0;
}

int ShowWindow(void *hwnd, int cmd)
{
    SDL_Window *w = win(hwnd);
    if (!w)
        return 0;
    if (cmd == SW_HIDE)
        SDL_HideWindow(w);
    else if (cmd == SW_MINIMIZE)
        SDL_MinimizeWindow(w);
    else
        SDL_ShowWindow(w);
    return 1;
}

int GetSystemMetrics(int index)
{
    if (index == SM_SWAPBUTTON)
        return 0; // SDL already reports the logical (possibly swapped) button
    const SDL_DisplayID d = g_main ? SDL_GetDisplayForWindow(g_main) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *m = d ? SDL_GetDesktopDisplayMode(d) : nullptr;
    if (!m)
        return 0;
    if (index == SM_CXSCREEN)
        return m->w;
    if (index == SM_CYSCREEN)
        return m->h;
    return 0;
}

short GetKeyState(int vk)
{
    const SDL_Keymod mod = SDL_GetModState();
    if (vk == VK_CAPITAL)
        return (mod & SDL_KMOD_CAPS) ? 1 : 0;
    if (vk == VK_NUMLOCK)
        return (mod & SDL_KMOD_NUM) ? 1 : 0;
    if (vk == VK_SHIFT)
        return (mod & SDL_KMOD_SHIFT) ? (short)0x8000 : 0;
    if (vk == VK_CONTROL)
        return (mod & SDL_KMOD_CTRL) ? (short)0x8000 : 0;
    if (vk == VK_MENU)
        return (mod & SDL_KMOD_ALT) ? (short)0x8000 : 0;
    return 0;
}

void PostQuitMessage(int)
{
    g_quit = true;
}
bool xr_linux_quit_requested()
{
    return g_quit;
}

void* xr_linux_monitor_from_display(uint32_t d) { return (void*)(uintptr_t)d; }
uint32_t xr_linux_display_from_monitor(void* h) { return (uint32_t)(uintptr_t)h; }

int GetMonitorInfoA(void* hmon, MONITORINFO* mi)
{
    const SDL_DisplayID d = (SDL_DisplayID)(uintptr_t)hmon;
    if (!mi || !display_bounds(d, mi->rcMonitor, false))
        return 0;
    if (!display_bounds(d, mi->rcWork, true))
        mi->rcWork = mi->rcMonitor;
    mi->dwFlags = d == SDL_GetPrimaryDisplay() ? MONITORINFOF_PRIMARY : 0;
    return 1;
}

void* MonitorFromPoint(POINT p, uint32_t)
{
    const SDL_Point pt{p.x, p.y};
    SDL_DisplayID d = SDL_GetDisplayForPoint(&pt);
    if (!d)
        d = SDL_GetPrimaryDisplay(); // MONITOR_DEFAULTTOPRIMARY (the only flag the engine passes)
    return (void*)(uintptr_t)d;
}

void* MonitorFromWindow(void* hwnd, uint32_t)
{
    SDL_Window* w = win(hwnd);
    SDL_DisplayID d = w ? SDL_GetDisplayForWindow(w) : 0;
    return (void*)(uintptr_t)(d ? d : SDL_GetPrimaryDisplay());
}

void* GetDesktopWindow() { return kDesktop; }

int EnumDisplaySettings(const char*, uint32_t mode, DEVMODE* out)
{
    if (mode != ENUM_CURRENT_SETTINGS || !out)
        return 0;
    const SDL_DisplayID d = g_main ? SDL_GetDisplayForWindow(g_main) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = d ? SDL_GetCurrentDisplayMode(d) : nullptr;
    if (!m)
        return 0;
    out->dmPelsWidth = (uint32_t)m->w;
    out->dmPelsHeight = (uint32_t)m->h;
    out->dmDisplayFrequency = m->refresh_rate > 0.f ? (uint32_t)(m->refresh_rate + 0.5f) : 60u;
    return 1;
}

int DestroyWindow(void* hwnd)
{
    if (!hwnd || hwnd == g_main || hwnd == kDesktop)
        return 0;
    SDL_DestroyWindow((SDL_Window*)hwnd);
    return 1;
}

int UpdateWindow(void*) { return 1; } // SDL windows have no pending WM_PAINT to flush
