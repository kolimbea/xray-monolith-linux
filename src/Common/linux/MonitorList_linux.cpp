// Linux version of xrEngine/MonitorList.cpp: the same vid_monitor token list ("Auto" + one entry
// per display), built from SDL3 displays instead of EnumDisplayMonitors/QueryDisplayConfig.
// HMONITOR values carry the SDL_DisplayID (see win32_window.h).
#include "stdafx.h"
#include "MonitorList.h"

#include <SDL3/SDL_video.h>

ENGINE_API xr_token *vid_monitor_token = nullptr;
ENGINE_API xr_string vid_monitor_name = "Auto";
ENGINE_API volatile long g_monitor_list_dirty = 0;

static xr_vector<HMONITOR> s_monitor_handles;

void fill_vid_monitor_list()
{
    if (vid_monitor_token != nullptr)
        return;

    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    if (!ids || count <= 0)
    {
        Msg("! vid_monitor: SDL_GetDisplays failed (%s), list will be Auto-only", SDL_GetError());
        count = 0;
    }

    // Same naming rule as Windows: a display name gets an index suffix only if it is not unique.
    xr_vector<xr_string> labels;
    xr_map<xr_string, u32> label_count;
    for (int i = 0; i < count; ++i)
    {
        const char *n = SDL_GetDisplayName(ids[i]);
        labels.push_back(n && *n ? n : "Display");
        label_count[labels.back()]++;
    }

    vid_monitor_token = xr_alloc<xr_token>(count + 2);
    vid_monitor_token[0].name = xr_strdup("Auto");
    vid_monitor_token[0].id = 0;
    s_monitor_handles.push_back(NULL);

    for (int i = 0; i < count; ++i)
    {
        xr_string label = labels[i];
        if (label_count[label] > 1)
            label += " (" + xr_string(std::to_string(i + 1).c_str()) + ")";
        vid_monitor_token[i + 1].name = xr_strdup(label.c_str());
        vid_monitor_token[i + 1].id = i + 1;
        s_monitor_handles.push_back(xr_linux_monitor_from_display(ids[i]));
    }
    vid_monitor_token[count + 1].name = nullptr;
    vid_monitor_token[count + 1].id = -1;
    SDL_free(ids);

    Msg("* vid_monitor: enumerated %d monitor(s)", count);
}

void free_vid_monitor_list()
{
    if (!vid_monitor_token)
        return;
    for (int i = 0; vid_monitor_token[i].name; ++i)
        xr_free(vid_monitor_token[i].name);
    xr_free(vid_monitor_token);
    vid_monitor_token = nullptr;
    s_monitor_handles.clear();
}

HMONITOR ResolveSelectedMonitor()
{
    const xr_string &name = vid_monitor_name;
    if (name.empty() || name == "Auto" || !vid_monitor_token)
        return NULL;
    for (u32 i = 1; vid_monitor_token[i].name; ++i)
        if (xr_strcmp(vid_monitor_token[i].name, name.c_str()) == 0 && i < (u32)s_monitor_handles.size())
            return s_monitor_handles[i];
    Msg("! vid_monitor: '%s' not found on this system, using Auto", name.c_str());
    return NULL;
}

void refresh_vid_monitor_list()
{
    Msg("* vid_monitor: live-refresh triggered");
    free_vid_monitor_list();
    fill_vid_monitor_list();
}
