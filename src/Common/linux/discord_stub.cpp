// The Discord Game SDK binary is not shipped for this build; report "not running" so the engine's
// existing fallback (x_ray.cpp: "Failed to create Discord RPC", use_discord = false) takes over.
#include "discord/ffi.h"

extern "C" enum EDiscordResult DiscordCreate(DiscordVersion, struct DiscordCreateParams*, struct IDiscordCore** result)
{
    if (result)
        *result = nullptr;
    return DiscordResult_NotRunning;
}
