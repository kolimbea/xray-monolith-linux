// Queries of the Vulkan backend (Query in vkObjects.h): D3D11_QUERY_EVENT on the frame timeline and
// D3D11_QUERY_OCCLUSION on Vulkan occlusion queries that follow the context's rendering scopes.
#include "vkObjects.h"

#include <algorithm>

namespace xrvk
{
    bool Query::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Asynchronous), __uuidof(ID3D11Query)});
    }

    Query::~Query()
    {
        if (Core *c = core(); c && pool)
        {
            VkQueryPool p = pool;
            c->defer([c, p]
                     {
                         vkDestroyQueryPool(c->device, p, nullptr);
                     });
        }
    }

    void Context::begin_query_slot(VkCommandBuffer cb, Query *q)
    {
        if (q->used >= Query::kSlots)
        {
            static bool once = false;
            if (!once)
                log("! vk: occlusion query spans more than %u rendering scopes; the rest is not counted", Query::kSlots);
            once = true;
            return;
        }
        vkCmdBeginQuery(cb, q->pool, q->used++, core()->features.occlusionQueryPrecise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
        q->slot_open = true;
    }
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
    if (!pQueryDesc)
        return E_INVALIDARG;
    if (pQueryDesc->Query != D3D11_QUERY_EVENT && pQueryDesc->Query != D3D11_QUERY_OCCLUSION)
    {
        log("! vk: CreateQuery: query type %d is not supported", int(pQueryDesc->Query));
        return E_INVALIDARG;
    }
    if (!ppQuery)
        return S_FALSE;
    auto *q = new Query();
    q->xrvk_device = this;
    q->desc = *pQueryDesc;
    if (q->desc.Query == D3D11_QUERY_OCCLUSION)
    {
        VkQueryPoolCreateInfo pi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        pi.queryType = VK_QUERY_TYPE_OCCLUSION;
        pi.queryCount = Query::kSlots;
        XRVK_CHECK(vkCreateQueryPool(core()->device, &pi, nullptr, &q->pool));
        vkResetQueryPool(core()->device, q->pool, 0, Query::kSlots);
    }
    *ppQuery = q;
    return S_OK;
}

UINT ID3D11Asynchronous::GetDataSize()
{
    return static_cast<Query *>(static_cast<ID3D11Query *>(this))->desc.Query == D3D11_QUERY_EVENT ? sizeof(BOOL) : sizeof(UINT64);
}

void ID3D11DeviceContext::Begin(ID3D11Asynchronous *pAsync)
{
    auto *c = impl(this);
    auto *q = static_cast<Query *>(static_cast<ID3D11Query *>(pAsync));
    if (!q || q->desc.Query != D3D11_QUERY_OCCLUSION)
        return; // D3D11: Begin is ignored for event queries
    Core *core_ = core();
    if (q->active) // Begin twice: D3D11 restarts the query
        ID3D11DeviceContext::End(pAsync);
    if (q->end_value > core_->completed_value())
    {
        // the previous use is still in flight: its slots cannot be reset from the host yet
        core_->flush();
        VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wi.semaphoreCount = 1;
        wi.pSemaphores = &core_->timeline;
        wi.pValues = &q->end_value;
        XRVK_CHECK(vkWaitSemaphores(core_->device, &wi, UINT64_MAX));
    }
    vkResetQueryPool(core_->device, q->pool, 0, Query::kSlots);
    q->used = 0;
    q->active = true;
    q->end_value = 0;
    c->active_queries.push_back(q);
    if (c->rendering)
        c->begin_query_slot(core_->cmd(), q);
}

void ID3D11DeviceContext::End(ID3D11Asynchronous *pAsync)
{
    auto *c = impl(this);
    auto *q = static_cast<Query *>(static_cast<ID3D11Query *>(pAsync));
    if (!q)
        return;
    if (q->desc.Query == D3D11_QUERY_OCCLUSION)
    {
        if (q->slot_open)
        {
            vkCmdEndQuery(core()->cmd(), q->pool, q->used - 1);
            q->slot_open = false;
        }
        q->active = false;
        c->active_queries.erase(std::remove(c->active_queries.begin(), c->active_queries.end(), q), c->active_queries.end());
    }
    q->end_value = core()->recording_value();
}

HRESULT ID3D11DeviceContext::GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
    auto *q = static_cast<Query *>(static_cast<ID3D11Query *>(pAsync));
    if (!q || q->active || !q->end_value)
        return DXGI_ERROR_INVALID_CALL;
    Core *c = core();
    if (q->end_value > c->completed_value())
    {
        // D3D11 flushes unless told not to, so a query of the frame being recorded completes eventually
        if (q->end_value == c->recording_value() && !(GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH))
            c->flush();
        return S_FALSE;
    }
    if (q->desc.Query == D3D11_QUERY_EVENT)
    {
        if (pData && DataSize >= sizeof(BOOL))
            *static_cast<BOOL *>(pData) = TRUE;
        return S_OK;
    }
    UINT64 samples = 0;
    if (q->used)
    {
        uint64_t r[Query::kSlots] = {};
        XRVK_CHECK(vkGetQueryPoolResults(c->device, q->pool, 0, q->used, sizeof(r), r, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
        for (uint32_t i = 0; i < q->used; ++i)
            samples += r[i];
    }
    if (pData && DataSize >= sizeof(UINT64))
        *static_cast<UINT64 *>(pData) = samples;
    else if (pData && DataSize >= sizeof(UINT))
        *static_cast<UINT *>(pData) = UINT(samples);
    return S_OK;
}
