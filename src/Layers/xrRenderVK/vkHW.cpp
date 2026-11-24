// CHW for the Vulkan build: instance, GPU selection, device, SDL3 surface and swapchain (milestone M2).
// Replaces xrRenderDX10/dx10HW.cpp.
#include "stdafx.h"
#include "vkBase.h"

void fill_vid_mode_list(CHW*) { xrvk::not_implemented("fill_vid_mode_list"); }
void free_vid_mode_list() { xrvk::not_implemented("free_vid_mode_list"); }

CHW HW;

CHW::CHW() : m_pFactory(nullptr), m_pAdapter(nullptr), m_pOutput(nullptr), pDevice(nullptr), pContext(nullptr),
    m_pSwapChain(nullptr), pBaseRT(nullptr), pBaseZB(nullptr), m_move_window(true), pAnnotation(nullptr)
{
}

CHW::~CHW() {}

void CHW::CreateDevice(HWND, bool) { xrvk::not_implemented("CHW::CreateDevice"); }
void CHW::DestroyDevice() { xrvk::not_implemented("CHW::DestroyDevice"); }
void CHW::Reset(HWND) { xrvk::not_implemented("CHW::Reset"); }
IDXGIOutput* CHW::FindOutputOnCurrentAdapter(HMONITOR) { xrvk::not_implemented("CHW::FindOutputOnCurrentAdapter"); }
BOOL CHW::support(D3DFORMAT, DWORD, DWORD) { xrvk::not_implemented("CHW::support"); }
void CHW::OnAppActivate() { xrvk::not_implemented("CHW::OnAppActivate"); }
void CHW::OnAppDeactivate() { xrvk::not_implemented("CHW::OnAppDeactivate"); }
