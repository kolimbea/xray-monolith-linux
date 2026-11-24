// Linux stand-in for <dinput.h>. The engine keeps DirectInput key codes (DIK_*) as its key identity
// (saved key bindings, Lua scripts use them), so input on Linux maps SDL scancodes onto these values.
// DIK_* values, DISCL_* flags and the structs below are copied from mingw-w64's dinput.h
// (Wine project, LGPL-2.1+), fetched 2026-11-23 from mingw-w64-headers/include/dinput.h.
#pragma once
#include <cstddef>
#include <cstdint>
#if __has_include(<windows_base.h>)
#include <windows_base.h> // same GUID as the rest of the engine (dxvk-native)
#endif

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#define DIK_ESCAPE          0x01
#define DIK_1               0x02
#define DIK_2               0x03
#define DIK_3               0x04
#define DIK_4               0x05
#define DIK_5               0x06
#define DIK_6               0x07
#define DIK_7               0x08
#define DIK_8               0x09
#define DIK_9               0x0A
#define DIK_0               0x0B
#define DIK_MINUS           0x0C    /* - on main keyboard */
#define DIK_EQUALS          0x0D
#define DIK_BACK            0x0E    /* backspace */
#define DIK_TAB             0x0F
#define DIK_Q               0x10
#define DIK_W               0x11
#define DIK_E               0x12
#define DIK_R               0x13
#define DIK_T               0x14
#define DIK_Y               0x15
#define DIK_U               0x16
#define DIK_I               0x17
#define DIK_O               0x18
#define DIK_P               0x19
#define DIK_LBRACKET        0x1A
#define DIK_RBRACKET        0x1B
#define DIK_RETURN          0x1C    /* Enter on main keyboard */
#define DIK_LCONTROL        0x1D
#define DIK_A               0x1E
#define DIK_S               0x1F
#define DIK_D               0x20
#define DIK_F               0x21
#define DIK_G               0x22
#define DIK_H               0x23
#define DIK_J               0x24
#define DIK_K               0x25
#define DIK_L               0x26
#define DIK_SEMICOLON       0x27
#define DIK_APOSTROPHE      0x28
#define DIK_GRAVE           0x29    /* accent grave */
#define DIK_LSHIFT          0x2A
#define DIK_BACKSLASH       0x2B
#define DIK_Z               0x2C
#define DIK_X               0x2D
#define DIK_C               0x2E
#define DIK_V               0x2F
#define DIK_B               0x30
#define DIK_N               0x31
#define DIK_M               0x32
#define DIK_COMMA           0x33
#define DIK_PERIOD          0x34    /* . on main keyboard */
#define DIK_SLASH           0x35    /* / on main keyboard */
#define DIK_RSHIFT          0x36
#define DIK_MULTIPLY        0x37    /* * on numeric keypad */
#define DIK_LMENU           0x38    /* left Alt */
#define DIK_SPACE           0x39
#define DIK_CAPITAL         0x3A
#define DIK_F1              0x3B
#define DIK_F2              0x3C
#define DIK_F3              0x3D
#define DIK_F4              0x3E
#define DIK_F5              0x3F
#define DIK_F6              0x40
#define DIK_F7              0x41
#define DIK_F8              0x42
#define DIK_F9              0x43
#define DIK_F10             0x44
#define DIK_NUMLOCK         0x45
#define DIK_SCROLL          0x46    /* Scroll Lock */
#define DIK_NUMPAD7         0x47
#define DIK_NUMPAD8         0x48
#define DIK_NUMPAD9         0x49
#define DIK_SUBTRACT        0x4A    /* - on numeric keypad */
#define DIK_NUMPAD4         0x4B
#define DIK_NUMPAD5         0x4C
#define DIK_NUMPAD6         0x4D
#define DIK_ADD             0x4E    /* + on numeric keypad */
#define DIK_NUMPAD1         0x4F
#define DIK_NUMPAD2         0x50
#define DIK_NUMPAD3         0x51
#define DIK_NUMPAD0         0x52
#define DIK_DECIMAL         0x53    /* . on numeric keypad */
#define DIK_OEM_102         0x56    /* < > | on UK/Germany keyboards */
#define DIK_F11             0x57
#define DIK_F12             0x58
#define DIK_F13             0x64    /*                     (NEC PC98) */
#define DIK_F14             0x65    /*                     (NEC PC98) */
#define DIK_F15             0x66    /*                     (NEC PC98) */
#define DIK_KANA            0x70    /* (Japanese keyboard)            */
#define DIK_ABNT_C1         0x73    /* / ? on Portugese (Brazilian) keyboards */
#define DIK_CONVERT         0x79    /* (Japanese keyboard)            */
#define DIK_NOCONVERT       0x7B    /* (Japanese keyboard)            */
#define DIK_YEN             0x7D    /* (Japanese keyboard)            */
#define DIK_ABNT_C2         0x7E    /* Numpad . on Portugese (Brazilian) keyboards */
#define DIK_NUMPADEQUALS    0x8D    /* = on numeric keypad (NEC PC98) */
#define DIK_PREVTRACK       0x90    /* Previous Track (DIK_CIRCUMFLEX on Japanese keyboard) */
#define DIK_CIRCUMFLEX      0x90    /* (Japanese keyboard)            */
#define DIK_AT              0x91    /*                     (NEC PC98) */
#define DIK_COLON           0x92    /*                     (NEC PC98) */
#define DIK_UNDERLINE       0x93    /*                     (NEC PC98) */
#define DIK_KANJI           0x94    /* (Japanese keyboard)            */
#define DIK_STOP            0x95    /*                     (NEC PC98) */
#define DIK_AX              0x96    /*                     (Japan AX) */
#define DIK_UNLABELED       0x97    /*                        (J3100) */
#define DIK_NEXTTRACK       0x99    /* Next Track */
#define DIK_NUMPADENTER     0x9C    /* Enter on numeric keypad */
#define DIK_RCONTROL        0x9D
#define DIK_MUTE	    0xA0    /* Mute */
#define DIK_CALCULATOR      0xA1    /* Calculator */
#define DIK_PLAYPAUSE       0xA2    /* Play / Pause */
#define DIK_MEDIASTOP       0xA4    /* Media Stop */
#define DIK_VOLUMEDOWN      0xAE    /* Volume - */
#define DIK_VOLUMEUP        0xB0    /* Volume + */
#define DIK_WEBHOME         0xB2    /* Web home */
#define DIK_NUMPADCOMMA     0xB3    /* , on numeric keypad (NEC PC98) */
#define DIK_DIVIDE          0xB5    /* / on numeric keypad */
#define DIK_SYSRQ           0xB7
#define DIK_RMENU           0xB8    /* right Alt */
#define DIK_PAUSE           0xC5    /* Pause */
#define DIK_HOME            0xC7    /* Home on arrow keypad */
#define DIK_UP              0xC8    /* UpArrow on arrow keypad */
#define DIK_PRIOR           0xC9    /* PgUp on arrow keypad */
#define DIK_LEFT            0xCB    /* LeftArrow on arrow keypad */
#define DIK_RIGHT           0xCD    /* RightArrow on arrow keypad */
#define DIK_END             0xCF    /* End on arrow keypad */
#define DIK_DOWN            0xD0    /* DownArrow on arrow keypad */
#define DIK_NEXT            0xD1    /* PgDn on arrow keypad */
#define DIK_INSERT          0xD2    /* Insert on arrow keypad */
#define DIK_DELETE          0xD3    /* Delete on arrow keypad */
#define DIK_LWIN            0xDB    /* Left Windows key */
#define DIK_RWIN            0xDC    /* Right Windows key */
#define DIK_APPS            0xDD    /* AppMenu key */
#define DIK_POWER           0xDE
#define DIK_SLEEP           0xDF
#define DIK_WAKE            0xE3    /* System Wake */
#define DIK_WEBSEARCH       0xE5    /* Web Search */
#define DIK_WEBFAVORITES    0xE6    /* Web Favorites */
#define DIK_WEBREFRESH      0xE7    /* Web Refresh */
#define DIK_WEBSTOP         0xE8    /* Web Stop */
#define DIK_WEBFORWARD      0xE9    /* Web Forward */
#define DIK_WEBBACK         0xEA    /* Web Back */
#define DIK_MYCOMPUTER      0xEB    /* My Computer */
#define DIK_MAIL            0xEC    /* Mail */
#define DIK_MEDIASELECT     0xED    /* Media Select */
#define DIK_BACKSPACE       DIK_BACK            /* backspace */
#define DIK_NUMPADSTAR      DIK_MULTIPLY        /* * on numeric keypad */
#define DIK_LALT            DIK_LMENU           /* left Alt */
#define DIK_CAPSLOCK        DIK_CAPITAL         /* CapsLock */
#define DIK_NUMPADMINUS     DIK_SUBTRACT        /* - on numeric keypad */
#define DIK_NUMPADPLUS      DIK_ADD             /* + on numeric keypad */
#define DIK_NUMPADPERIOD    DIK_DECIMAL         /* . on numeric keypad */
#define DIK_NUMPADSLASH     DIK_DIVIDE          /* / on numeric keypad */
#define DIK_RALT            DIK_RMENU           /* right Alt */
#define DIK_UPARROW         DIK_UP              /* UpArrow on arrow keypad */
#define DIK_PGUP            DIK_PRIOR           /* PgUp on arrow keypad */
#define DIK_LEFTARROW       DIK_LEFT            /* LeftArrow on arrow keypad */
#define DIK_RIGHTARROW      DIK_RIGHT           /* RightArrow on arrow keypad */
#define DIK_DOWNARROW       DIK_DOWN            /* DownArrow on arrow keypad */
#define DIK_PGDN            DIK_NEXT            /* PgDn on arrow keypad */

#define DISCL_EXCLUSIVE		0x00000001
#define DISCL_NONEXCLUSIVE	0x00000002
#define DISCL_FOREGROUND	0x00000004
#define DISCL_BACKGROUND	0x00000008
#define DISCL_NOWINKEY          0x00000010

typedef struct DIDEVICEOBJECTDATA
{
    uint32_t dwOfs;
    uint32_t dwData;
    uint32_t dwTimeStamp;
    uint32_t dwSequence;
    uintptr_t uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

typedef struct DIMOUSESTATE2
{
    int32_t lX;
    int32_t lY;
    int32_t lZ;
    uint8_t rgbButtons[8];
} DIMOUSESTATE2;
typedef DIMOUSESTATE2 DIMOUSESTATE; // same prefix layout (DIMOUSESTATE has 4 buttons)

#define DIMOFS_X offsetof(DIMOUSESTATE2, lX)
#define DIMOFS_Y offsetof(DIMOUSESTATE2, lY)
#define DIMOFS_Z offsetof(DIMOUSESTATE2, lZ)
#define DIMOFS_BUTTON0 (offsetof(DIMOUSESTATE2, rgbButtons) + 0)
#define DIMOFS_BUTTON1 (offsetof(DIMOUSESTATE2, rgbButtons) + 1)
#define DIMOFS_BUTTON2 (offsetof(DIMOUSESTATE2, rgbButtons) + 2)
#define DIMOFS_BUTTON3 (offsetof(DIMOUSESTATE2, rgbButtons) + 3)
#define DIMOFS_BUTTON4 (offsetof(DIMOUSESTATE2, rgbButtons) + 4)
#define DIMOFS_BUTTON5 (offsetof(DIMOUSESTATE2, rgbButtons) + 5)
#define DIMOFS_BUTTON6 (offsetof(DIMOUSESTATE2, rgbButtons) + 6)
#define DIMOFS_BUTTON7 (offsetof(DIMOUSESTATE2, rgbButtons) + 7)

// ---------------------------------------------------------------------------------------------
// Minimal DirectInput 8 object model over SDL3 (implemented in Common/linux/dinput_sdl.cpp).
// Only what xrEngine/Xr_input.cpp calls: buffered keyboard/mouse data, acquire, coop level
// (exclusive mouse = SDL relative mode on the window passed as HWND), key names.
// The main loop must hand SDL input events to dinput_sdl_feed().
// ---------------------------------------------------------------------------------------------
union SDL_Event;
void dinput_sdl_feed(const SDL_Event& e);

typedef long HRESULT_DI;
#define DI_OK 0L
#define DIERR_INPUTLOST ((long)0x8007001EL)
#define DIERR_NOTACQUIRED ((long)0x8007000CL)
#define DIERR_INVALIDPARAM ((long)0x80070057L)

struct DIDATAFORMAT
{
    int kind; // 1 keyboard, 2 mouse
};
extern const DIDATAFORMAT c_dfDIKeyboard;
extern const DIDATAFORMAT c_dfDIMouse;
extern const DIDATAFORMAT c_dfDIMouse2;

#ifndef GUID_DEFINED
#define GUID_DEFINED
// Windows layout. Device GUIDs below use Data1 as the device kind (1 keyboard, 2 mouse).
typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
#endif
typedef GUID DIGUID_;
extern const DIGUID_ GUID_SysKeyboard;
extern const DIGUID_ GUID_SysMouse;
extern const DIGUID_ IID_IDirectInput8;

struct DIPROPHEADER
{
    uint32_t dwSize;
    uint32_t dwHeaderSize;
    uint32_t dwObj;
    uint32_t dwHow;
};
struct DIPROPDWORD
{
    DIPROPHEADER diph;
    uint32_t dwData;
};
struct DIPROPSTRING
{
    DIPROPHEADER diph;
    wchar_t wsz[260];
};
#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPROP_BUFFERSIZE ((const DIGUID_*)1)
#define DIPROP_KEYNAME ((const DIGUID_*)20)

struct DIDEVCAPS
{
    uint32_t dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs;
};
struct DIDEVICEINSTANCE
{
    uint32_t dwSize;
};
struct DIDEVICEOBJECTINSTANCE
{
    uint32_t dwSize;
};

struct IDirectInputDevice8
{
    virtual ~IDirectInputDevice8() = default;
    virtual unsigned long AddRef() { return 1; }
    virtual long SetDataFormat(const DIDATAFORMAT* fmt) = 0;
    virtual long SetCooperativeLevel(void* window, uint32_t flags) = 0;
    virtual long SetProperty(const DIGUID_* prop, const DIPROPHEADER* ph) = 0;
    virtual long GetProperty(const DIGUID_* prop, DIPROPHEADER* ph) = 0;
    virtual long Acquire() = 0;
    virtual long Unacquire() = 0;
    virtual long GetDeviceData(uint32_t cb, DIDEVICEOBJECTDATA* out, uint32_t* count, uint32_t flags) = 0;
    virtual unsigned long Release() = 0;
};
typedef IDirectInputDevice8* LPDIRECTINPUTDEVICE8;

struct IDirectInput8
{
    virtual ~IDirectInput8() = default;
    virtual unsigned long AddRef() { return 1; }
    virtual long CreateDevice(const DIGUID_& guid, LPDIRECTINPUTDEVICE8* out, void* outer) = 0;
    virtual unsigned long Release() = 0;
};
typedef IDirectInput8* LPDIRECTINPUT8;
long DirectInput8Create(void* inst, uint32_t version, const DIGUID_& iid, void** out, void* outer);

// SDL scancode -> DIK code (0 when the key has no DirectInput equivalent), and back.
int dinput_sdl_scancode_to_dik(int sdl_scancode);
int dinput_sdl_dik_to_scancode(int dik);
