// Win32 window, cursor and message-loop subset over SDL3. HWND is an SDL_Window*.
// Constant values are copied from mingw-w64's winuser.h (fetched 2026-11-23).
#pragma once
#include <cstdint>

struct SDL_Window;
union SDL_Event;

#define GWL_STYLE (-16)
#define SC_MAXIMIZE 0xF030
#define SC_MINIMIZE 0xF020
#define SC_MONITORPOWER 0xF170
#define SC_MOVE 0xF010
#define SC_SIZE 0xF000
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_SWAPBUTTON 23
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SW_SHOW 5
#define SW_HIDE 0
#define SW_MINIMIZE 6
#define VK_CAPITAL 0x14
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_NUMLOCK 0x90
#define VK_SHIFT 0x10
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WM_ACTIVATE 0x0006
#define WM_CHAR 0x0102
#define WM_CLOSE 0x0010
#define WM_COMMAND 0x0111
#define WM_DESTROY 0x0002
#define WM_DISPLAYCHANGE 0x007E
#define WM_ERASEBKGND 0x0014
#define WM_HOTKEY 0x0312
#define WM_INPUTLANGCHANGE 0x0051
#define WM_NCPAINT 0x0085
#define WM_PAINT 0x000F
#define WM_QUIT 0x0012
#define WM_SETCURSOR 0x0020
#define WM_SYSCHAR 0x0106
#define WM_SYSCOMMAND 0x0112
#define WM_SYSKEYDOWN 0x0104
#define WS_BORDER 0x00800000L
#define WS_CHILD 0x40000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_DLGFRAME 0x00400000L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_TOPMOST 0x00000008L
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define HKL_NEXT 1
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define HWND_TOP ((HWND)0)
#define HWND_NOTOPMOST ((HWND)(intptr_t)-2)
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define ENUM_CURRENT_SETTINGS ((uint32_t)-1)
#ifndef LOWORD
#define LOWORD(l) ((uint16_t)(((uintptr_t)(l)) & 0xffff))
#define HIWORD(l) ((uint16_t)((((uintptr_t)(l)) >> 16) & 0xffff))
#endif
#ifndef MAKEWPARAM
#define MAKEWPARAM(l, h) ((uintptr_t)(uint32_t)(((uint16_t)(l)) | ((uint32_t)((uint16_t)(h))) << 16))
#endif

// The engine's main window; set once it is created so SetCursorPos/ClientToScreen have a reference.
void xr_linux_set_main_window(SDL_Window* w);
SDL_Window* xr_linux_main_window();

int ShowCursor(int show);                          // display counter, as on Windows
int ClipCursor(const RECT* screen_rect);           // NULL releases the confinement
int GetClientRect(void* hwnd, RECT* r);
int GetWindowRect(void* hwnd, RECT* r);
int MapWindowPoints(void* from, void* to, POINT* pts, unsigned n); // client(from) -> screen (to == NULL)
int ClientToScreen(void* hwnd, POINT* p);
int ScreenToClient(void* hwnd, POINT* p);
int GetCursorPos(POINT* p);
int SetCursorPos(int x, int y);
int SetForegroundWindow(void* hwnd);
int SetActiveWindow(void* hwnd);
int SetWindowPos(void* hwnd, void* after, int x, int y, int w, int h, unsigned flags);
intptr_t SetWindowLongPtr(void* hwnd, int index, intptr_t value); // GWL_STYLE: WS_POPUP => borderless
long GetWindowLong(void* hwnd, int index);
int IsIconic(void* hwnd);
int ShowWindow(void* hwnd, int cmd);
int GetSystemMetrics(int index);
short GetKeyState(int vk); // only toggle state of VK_CAPITAL / VK_NUMLOCK is meaningful
void PostQuitMessage(int code);
bool xr_linux_quit_requested();

// Monitors: HMONITOR carries an SDL_DisplayID.
typedef struct tagMONITORINFO
{
    uint32_t cbSize;
    RECT rcMonitor;
    RECT rcWork;
    uint32_t dwFlags;
} MONITORINFO;
#define MONITORINFOF_PRIMARY 0x00000001
int GetMonitorInfoA(void* hmon, MONITORINFO* mi);
#define GetMonitorInfo GetMonitorInfoA
void* MonitorFromPoint(POINT p, uint32_t flags);
void* MonitorFromWindow(void* hwnd, uint32_t flags);
void* xr_linux_monitor_from_display(uint32_t display_id);
uint32_t xr_linux_display_from_monitor(void* hmon);
void* GetDesktopWindow(); // GetWindowRect on it yields the primary display bounds

typedef struct _devicemodeA
{
    uint16_t dmSize;
    uint16_t dmDriverExtra;
    uint32_t dmPelsWidth;
    uint32_t dmPelsHeight;
    uint32_t dmDisplayFrequency;
} DEVMODE, DEVMODEA;
int EnumDisplaySettings(const char* device, uint32_t mode, DEVMODE* out); // only ENUM_CURRENT_SETTINGS
#define EnumDisplaySettingsA EnumDisplaySettings
