#include "stdafx.h"
#include "vkBase.h"
#include "vkCore.h"

namespace xrvk
{
void not_implemented(const char* what)
{
    Msg("! vk: %s not implemented", what);
    FlushLog();
    Debug.fatal(DEBUG_INFO, "Vulkan renderer: %s is not implemented yet", what);
    abort(); // fatal does not return; keeps [[noreturn]] honest if it ever did
}

} // namespace xrvk


namespace
{
// The Vulkan core logs through the engine log in the game (stderr in standalone tests).
void engine_log(const char* line) { Msg("%s", line); }
struct LogHook
{
    LogHook() { xrvk::set_log(engine_log); }
} g_log_hook;
} // namespace
