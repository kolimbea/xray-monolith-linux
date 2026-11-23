#include "stdafx.h"
#include "MonitorList.h"
#ifdef __linux__
#include <SDL3/SDL.h>
#include "xr_input.h" // dinput_sdl_feed

// Same reactions as on_message/WndProc below, driven by SDL3 events. Keyboard and mouse go to the
// DirectInput emulation that CInput reads (Common/linux/dinput_sdl.cpp).
void CRenderDevice::on_sdl_event(const SDL_Event& ev)
{
	switch (ev.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_WHEEL:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		dinput_sdl_feed(ev);
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		OnWM_Activate(MAKEWPARAM(WA_ACTIVE, 0), 0);
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		OnWM_Activate(MAKEWPARAM(WA_INACTIVE, 0), 0);
		break;
	case SDL_EVENT_WINDOW_MINIMIZED:
		OnWM_Activate(MAKEWPARAM(WA_INACTIVE, 1), 0);
		break;
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		Engine.Event.Defer("KERNEL:disconnect");
		Engine.Event.Defer("KERNEL:quit");
		break;
	case SDL_EVENT_TEXT_INPUT:
	{
		// WM_CHAR delivers UTF-16 units; feed each code point (BMP only, like the Windows path).
		const unsigned char* p = (const unsigned char*)ev.text.text;
		while (*p)
		{
			u32 cp = *p++;
			int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
			cp &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
			while (extra-- > 0 && (*p & 0xC0) == 0x80)
				cp = (cp << 6) | (*p++ & 0x3F);
			if (cp < 0x10000)
				Device.imgui().InputChar((WPARAM)cp);
		}
		break;
	}
	case SDL_EVENT_KEYMAP_CHANGED:
		Device.imgui().UpdateInputLang();
		break;
	case SDL_EVENT_DISPLAY_ADDED:
	case SDL_EVENT_DISPLAY_REMOVED:
	case SDL_EVENT_DISPLAY_ORIENTATION:
		InterlockedExchange(&g_monitor_list_dirty, 1);
		break;
	default:
		break;
	}
}
#else

bool CRenderDevice::on_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	switch (uMsg)
	{
	case WM_SYSKEYDOWN:
		{
			return true;
		}
	case WM_ACTIVATE:
		{
#ifdef INGAME_EDITOR
        if (editor())
        {
            Device.b_is_Active = TRUE;
            break;
        }
#endif // #ifdef INGAME_EDITOR
			OnWM_Activate(wParam, lParam);
			return (false);
		}
	case WM_SETCURSOR:
		{
#ifdef INGAME_EDITOR
        if (editor())
            break;
#endif // #ifdef INGAME_EDITOR

			result = 1;
			return (true);
		}
	case WM_SYSCOMMAND:
		{
#ifdef INGAME_EDITOR
        if (editor())
            break;
#endif // #ifdef INGAME_EDITOR

			// Prevent moving/sizing and power loss in fullscreen mode
			switch (wParam)
			{
			case SC_MOVE:
			case SC_SIZE:
			case SC_MAXIMIZE:
			case SC_MONITORPOWER:
				result = 1;
				return (true);
			}
			return (false);
		}
	case WM_CLOSE:
		{
			Engine.Event.Defer("KERNEL:disconnect");
			Engine.Event.Defer("KERNEL:quit");								 
#ifdef INGAME_EDITOR
        if (editor())
            break;
#endif // #ifdef INGAME_EDITOR

			result = 0;
			return (true);
		}
	case WM_HOTKEY: // prevent 'ding' sounds caused by Alt+key combinations
	case WM_SYSCHAR:
		result = 0;
		return true;

	case WM_CHAR:
		Device.imgui().InputChar(wParam);
		return false;

	case WM_INPUTLANGCHANGE:
		Device.imgui().UpdateInputLang();
		return false;

	case WM_DISPLAYCHANGE:
		InterlockedExchange(&g_monitor_list_dirty, 1);
		return false;
	}

	return (false);
}

//-----------------------------------------------------------------------------
// Name: WndProc()
// Desc: Static msg handler which passes messages to the application class.
//-----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result;
	if (Device.on_message(hWnd, uMsg, wParam, lParam, result))
		return (result);

	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

#endif // __linux__
