// DirectInput 8 subset over SDL3; see the declarations at the end of include/dinput.h.
#include "include/dinput.h"
#include "include/mmsystem.h"

#include <SDL3/SDL.h>

#include <deque>
#include <mutex>

#include "dinput_sdl_table.inc"

namespace
{
    constexpr int kKeyboard = 1, kMouse = 2;

    std::mutex g_lock;
    struct Queue
    {
        std::deque<DIDEVICEOBJECTDATA> events;
        size_t capacity = 64;
        bool acquired = false;
    };
    Queue g_queues[3]; // indexed by kind
    uint32_t g_sequence = 0;
    int g_sdl_to_dik[SDL_SCANCODE_COUNT];
    int g_dik_to_sdl[256];

    struct TableInit
    {
        TableInit()
        {
            for (const auto &p : k_sdl_dik)
            {
                g_sdl_to_dik[p.sdl] = p.dik;
                g_dik_to_sdl[p.dik & 0xFF] = p.sdl;
            }
        }
    } g_table_init;

    void push(int kind, uint32_t ofs, uint32_t data)
    {
        std::lock_guard<std::mutex> g(g_lock);
        Queue &q = g_queues[kind];
        if (!q.acquired)
            return; // DirectInput drops input for unacquired devices
        if (q.events.size() >= q.capacity)
            q.events.pop_front(); // buffer overflow: oldest data is lost, as with DirectInput
        DIDEVICEOBJECTDATA d{};
        d.dwOfs = ofs;
        d.dwData = data;
        d.dwTimeStamp = timeGetTime(); // same clock as CInput::dwCurTime (TimerAsync_MMT)
        d.dwSequence = ++g_sequence;
        q.events.push_back(d);
    }

    class Device final : public IDirectInputDevice8
    {
    public:
        explicit Device(int kind) : kind_(kind)
        {
        }

        long SetDataFormat(const DIDATAFORMAT *fmt) override
        {
            return fmt && fmt->kind == kind_ ? DI_OK : DIERR_INVALIDPARAM;
        }

        long SetCooperativeLevel(void *window, uint32_t flags) override
        {
            window_ = (SDL_Window *)window;
            exclusive_ = (flags & DISCL_EXCLUSIVE) != 0;
            return DI_OK;
        }

        long SetProperty(const DIGUID_ *prop, const DIPROPHEADER *ph) override
        {
            if (prop == DIPROP_BUFFERSIZE && ph)
            {
                std::lock_guard<std::mutex> g(g_lock);
                g_queues[kind_].capacity = ((const DIPROPDWORD *)ph)->dwData;
                return DI_OK;
            }
            return DIERR_INVALIDPARAM;
        }

        long GetProperty(const DIGUID_ *prop, DIPROPHEADER *ph) override
        {
            if (prop != DIPROP_KEYNAME || kind_ != kKeyboard || !ph)
                return DIERR_INVALIDPARAM;
            auto *ps = (DIPROPSTRING *)ph;
            ps->wsz[0] = 0;
            const int sc = dinput_sdl_dik_to_scancode((int)ps->diph.dwObj);
            const char *name = sc ? SDL_GetScancodeName((SDL_Scancode)sc) : "";
            size_t i = 0;
            for (; name[i] && i + 1 < sizeof(ps->wsz) / sizeof(ps->wsz[0]); ++i)
                ps->wsz[i] = (unsigned char)name[i]; // SDL scancode names are ASCII
            ps->wsz[i] = 0;
            return DI_OK;
        }

        long Acquire() override
        {
            {
                std::lock_guard<std::mutex> g(g_lock);
                g_queues[kind_].acquired = true;
            }
            // Exclusive mouse on Windows hides the cursor and delivers raw relative motion.
            if (kind_ == kMouse && window_)
                SDL_SetWindowRelativeMouseMode(window_, exclusive_);
            return DI_OK;
        }

        long Unacquire() override
        {
            {
                std::lock_guard<std::mutex> g(g_lock);
                g_queues[kind_].acquired = false;
                g_queues[kind_].events.clear();
            }
            if (kind_ == kMouse && window_)
                SDL_SetWindowRelativeMouseMode(window_, false);
            return DI_OK;
        }

        long GetDeviceData(uint32_t cb, DIDEVICEOBJECTDATA *out, uint32_t *count, uint32_t) override
        {
            if (cb != sizeof(DIDEVICEOBJECTDATA) || !count)
                return DIERR_INVALIDPARAM;
            std::lock_guard<std::mutex> g(g_lock);
            Queue &q = g_queues[kind_];
            if (!q.acquired)
            {
                *count = 0;
                return DIERR_NOTACQUIRED;
            }
            uint32_t n = 0;
            while (n < *count && !q.events.empty())
            {
                out[n++] = q.events.front();
                q.events.pop_front();
            }
            *count = n;
            return DI_OK;
        }

        unsigned long Release() override
        {
            delete this;
            return 0;
        }

    private:
        int kind_;
        SDL_Window *window_ = nullptr;
        bool exclusive_ = false;
    };

    class DirectInput final : public IDirectInput8
    {
    public:
        long CreateDevice(const DIGUID_ &guid, LPDIRECTINPUTDEVICE8 *out, void *) override
        {
            if (!out || (guid.Data1 != kKeyboard && guid.Data1 != kMouse))
                return DIERR_INVALIDPARAM;
            *out = new Device(guid.Data1);
            return DI_OK;
        }
        unsigned long Release() override
        {
            delete this;
            return 0;
        }
    };
} // namespace

const DIDATAFORMAT c_dfDIKeyboard{kKeyboard};
const DIDATAFORMAT c_dfDIMouse{kMouse};
const DIDATAFORMAT c_dfDIMouse2{kMouse};
const DIGUID_ GUID_SysKeyboard{kKeyboard, 0, 0, {}};
const DIGUID_ GUID_SysMouse{kMouse, 0, 0, {}};
const DIGUID_ IID_IDirectInput8{0, 0, 0, {}};

long DirectInput8Create(void *, uint32_t, const DIGUID_ &, void **out, void *)
{
    if (!out)
        return DIERR_INVALIDPARAM;
    *out = static_cast<IDirectInput8 *>(new DirectInput());
    return DI_OK;
}

int dinput_sdl_scancode_to_dik(int sc)
{
    return sc > 0 && sc < SDL_SCANCODE_COUNT ? g_sdl_to_dik[sc] : 0;
}
int dinput_sdl_dik_to_scancode(int dik)
{
    return dik > 0 && dik < 256 ? g_dik_to_sdl[dik] : 0;
}

void dinput_sdl_feed(const SDL_Event &e)
{
    switch (e.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (e.key.repeat)
            break; // DirectInput reports state changes only; holds come from KBState
        const int dik = dinput_sdl_scancode_to_dik(e.key.scancode);
        if (dik)
            push(kKeyboard, (uint32_t)dik, e.type == SDL_EVENT_KEY_DOWN ? 0x80u : 0u);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        // DirectInput gives integer counts; keep sub-pixel remainders so slow motion is not lost.
        static float rx = 0.f, ry = 0.f;
        rx += e.motion.xrel;
        ry += e.motion.yrel;
        const int dx = (int)rx, dy = (int)ry;
        rx -= dx;
        ry -= dy;
        if (dx)
            push(kMouse, DIMOFS_X, (uint32_t)dx);
        if (dy)
            push(kMouse, DIMOFS_Y, (uint32_t)dy);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // WHEEL_DELTA (120) per notch, like DirectInput's DIMOFS_Z.
        const float notches = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        const int z = (int)(notches * 120.f);
        if (z)
            push(kMouse, DIMOFS_Z, (uint32_t)z);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        // SDL: 1 left, 2 middle, 3 right, 4/5 X1/X2. DirectInput: 0 left, 1 right, 2 middle, 3/4 X.
        int b;
        switch (e.button.button)
        {
        case SDL_BUTTON_LEFT:
            b = 0;
            break;
        case SDL_BUTTON_RIGHT:
            b = 1;
            break;
        case SDL_BUTTON_MIDDLE:
            b = 2;
            break;
        default:
            b = e.button.button - 1;
            break; // X1 -> 3, X2 -> 4, further buttons follow
        }
        if (b >= 0 && b < 8)
            push(kMouse, (uint32_t)(DIMOFS_BUTTON0 + b), e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0x80u : 0u);
        break;
    }
    default:
        break;
    }
}
