////////////////////////////////////////////////////////////////////////////
// Module : os_clipboard.cpp
// Created : 21.02.2008
// Author : Evgeniy Sokolov
// Description : os clipboard class implementation
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop
#include "os_clipboard.h"
#ifdef __linux__
// SDL3 clipboard (Wayland/X11). The engine works in CP1251, SDL in UTF-8.
#include <SDL3/SDL_clipboard.h>

namespace
{
xr_string cp1251_to_utf8(LPCSTR src)
{
	const int wn = MultiByteToWideChar(1251, 0, src, -1, nullptr, 0);
	if (wn <= 0)
		return xr_string();
	xr_vector<wchar_t> w(wn);
	MultiByteToWideChar(1251, 0, src, -1, w.data(), wn);
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return xr_string();
	xr_string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, &out[0], n, nullptr, nullptr);
	out.resize(n - 1);
	return out;
}

xr_string utf8_to_cp1251(LPCSTR src)
{
	const int wn = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
	if (wn <= 0)
		return xr_string();
	xr_vector<wchar_t> w(wn);
	MultiByteToWideChar(CP_UTF8, 0, src, -1, w.data(), wn);
	const int n = WideCharToMultiByte(1251, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return xr_string();
	xr_string out(n, '\0');
	WideCharToMultiByte(1251, 0, w.data(), -1, &out[0], n, nullptr, nullptr);
	out.resize(n - 1);
	return out;
}

xr_string clipboard_cp1251()
{
	char* text = SDL_GetClipboardText(); // never NULL; "" when empty or on error
	xr_string out = text ? utf8_to_cp1251(text) : xr_string();
	SDL_free(text);
	return out;
}
} // namespace

void os_clipboard::copy_to_clipboard(LPCSTR buf)
{
	if (!SDL_SetClipboardText(cp1251_to_utf8(buf).c_str()))
		Msg("! clipboard: %s", SDL_GetError());
}

void os_clipboard::paste_from_clipboard(LPSTR buffer, u32 const& buffer_size)
{
	VERIFY(buffer);
	VERIFY(buffer_size > 0);
	const xr_string clip = clipboard_cp1251();
	strncpy(buffer, clip.c_str(), buffer_size - 1);
	buffer[buffer_size - 1] = 0;
	for (u32 i = 0; i < strlen(buffer); ++i)
	{
		char c = buffer[i];
		// CP1251 bytes >= 0x80 are letters; the C locale's isprint() would reject them all.
		if (((unsigned char)c < 0x80 && isprint((unsigned char)c) == 0) || c == '\t' || c == '\n')
			buffer[i] = ' ';
	}
}

void os_clipboard::update_clipboard(LPCSTR string)
{
	xr_string joined = clipboard_cp1251();
	joined += string;
	copy_to_clipboard(joined.c_str());
}
#else

void os_clipboard::copy_to_clipboard(LPCSTR buf)
{
	if (!OpenClipboard(0))
		return;
	u32 handle_size = (xr_strlen(buf) + 1) * sizeof(char);
	HGLOBAL handle = GlobalAlloc(GHND, handle_size);
	if (!handle)
	{
		CloseClipboard();
		return;
	}

	char* memory = (char*)GlobalLock(handle);
	xr_strcpy(memory, handle_size, buf);
	GlobalUnlock(handle);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, handle);
	CloseClipboard();
}

void os_clipboard::paste_from_clipboard(LPSTR buffer, u32 const& buffer_size)
{
	VERIFY(buffer);
	VERIFY(buffer_size > 0);

	if (!OpenClipboard(0))
		return;

	HGLOBAL hmem = GetClipboardData(CF_TEXT);
	if (!hmem)
		return;

	LPCSTR clipdata = (LPCSTR)GlobalLock(hmem);
	strncpy_s(buffer, buffer_size, clipdata, buffer_size - 1);
	buffer[buffer_size - 1] = 0;
	for (u32 i = 0; i < strlen(buffer); ++i)
	{
		char c = buffer[i];
		if (((isprint(c) == 0) && (c != char(-1))) || c == '\t' || c == '\n') // "я" = -1
		{
			buffer[i] = ' ';
		}
	}

	GlobalUnlock(hmem);
	CloseClipboard();
}

void os_clipboard::update_clipboard(LPCSTR string)
{
	if (!OpenClipboard(0))
		return;

	HGLOBAL handle = GetClipboardData(CF_TEXT);
	if (!handle)
	{
		CloseClipboard();
		copy_to_clipboard(string);
		return;
	}

	LPSTR memory = (LPSTR)GlobalLock(handle);
	int memory_length = (int)strlen(memory);
	int string_length = (int)strlen(string);
	int buffer_size = (memory_length + string_length + 1) * sizeof(char);
#ifndef _EDITOR
	LPSTR buffer = (LPSTR)_alloca(buffer_size);
#else // #ifndef _EDITOR
    LPSTR buffer = (LPSTR)xr_alloc<char>( buffer_size );
#endif // #ifndef _EDITOR
	xr_strcpy(buffer, buffer_size, memory);
	GlobalUnlock(handle);

	xr_strcat(buffer, buffer_size, string);
	CloseClipboard();
	copy_to_clipboard(buffer);
#ifdef _EDITOR
    xr_free (buffer);
#endif // #ifdef _EDITOR
}

#endif // __linux__
