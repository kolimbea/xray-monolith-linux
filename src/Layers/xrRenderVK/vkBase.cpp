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
// Flushed at once: backend lines are rare, and a run that is killed (tests, timeouts) must keep them.
void engine_log(const char* line)
{
    Msg("%s", line);
    FlushLog();
}
struct LogHook
{
    LogHook() { xrvk::set_log(engine_log); }
} g_log_hook;
} // namespace

// Referenced by the link (-Wl,-u, CMakeLists.txt) so this object, and with it the log hook above, is
// pulled from the static renderer library although nothing else calls into it.
extern "C" int xrvk_engine_log_hook = 1;
