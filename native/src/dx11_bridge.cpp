#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <skinlab/dx11_bridge.h>
#include <array>
#include <atomic>
#include <cstring>
#include <new>

using Present = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using Resize = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1 = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);

struct SlDx11Runtime {
    SlDx11Config config{};
    DWORD installer = 0;
    HANDLE installer_handle = nullptr;
    std::atomic<DWORD> owner{0}, binding_error{0};
    std::atomic<HANDLE> owner_handle{nullptr};
    void** original = nullptr;
    std::array<void*, 41> table{};
    uint32_t entries = 18;
    std::atomic<bool> attached{false}, lost{false}, fault{false};
    std::atomic<uint32_t> in_flight{0};
    std::atomic<uint64_t> presents{0}, frames{0}, resizes{0}, foreign{0};
    std::atomic<HRESULT> last_present{S_OK};
    std::atomic<HRESULT> lost_result{S_OK};
    uint32_t owner_depth = 0; // accessed only on owner
    bool lost_notified = false;
    ~SlDx11Runtime() {
        const HANDLE render=owner_handle.load();
        if(render && render!=installer_handle)CloseHandle(render);
        if(installer_handle)CloseHandle(installer_handle);
    }
};

namespace {
std::atomic<SlDx11Runtime*> active{nullptr};
constexpr DWORD kBindingInProgress = 0xffffffff;

HANDLE current_thread_handle() {
    HANDLE handle=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&handle,SYNCHRONIZE,FALSE,0)) return nullptr;
    return handle;
}
bool same_live_thread(DWORD id,HANDLE handle) {
    return handle && id==GetCurrentThreadId() && WaitForSingleObject(handle,0)==WAIT_TIMEOUT;
}

// Interface inheritance, UUIDs and member counts from Microsoft's SDK:
// win32metadata/.../RecompiledIdlHeaders/shared/dxgi1_{2,3,4,5}.h.
struct InterfaceShape { GUID iid; uint32_t entries; };
const InterfaceShape shapes[] = {
    {{0x3d585d5a,0xbd4a,0x489e,{0xb1,0xf4,0x3d,0xbc,0xb6,0x45,0x2f,0xfb}},41},
    {{0x94d99bdb,0xf1f8,0x4ab0,{0xb2,0x36,0x7d,0xa0,0x17,0x0e,0xda,0xb1}},40},
    {{0xa8be2ac4,0x199f,0x4946,{0xb3,0x31,0x79,0x59,0x9f,0xb9,0x8d,0xe7}},36},
    {{0x790a45f7,0x0d42,0x4876,{0x98,0x3a,0x0a,0x55,0xcf,0xe6,0xf4,0xaa}},29}
};

bool accessible(const void* address, size_t bytes, bool writable) {
    auto cursor = reinterpret_cast<uintptr_t>(address);
    if (!cursor || cursor + bytes < cursor) return false;
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &page, sizeof(page)) ||
            page.State != MEM_COMMIT || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD p = page.Protect & 0xff;
        const bool can_write = p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
        const bool can_read = can_write || p == PAGE_READONLY || p == PAGE_EXECUTE_READ;
        if (!can_read || (writable && !can_write)) return false;
        const auto next = reinterpret_cast<uintptr_t>(page.BaseAddress) + page.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

bool interface_entries(IDXGISwapChain* chain, uint32_t& entries) {
    entries = 18;
    for (const auto& shape : shapes) {
        IUnknown* alias = nullptr;
        const HRESULT result = chain->QueryInterface(shape.iid, reinterpret_cast<void**>(&alias));
        if (result == E_NOINTERFACE) continue;
        if (FAILED(result) || !alias) return false;
        const bool same = reinterpret_cast<void*>(alias) == reinterpret_cast<void*>(chain);
        alias->Release();
        if (same) { entries = shape.entries; break; }
    }
    return true;
}

struct Call {
    SlDx11Runtime& state;
    bool owner, outer;
    explicit Call(SlDx11Runtime& value, bool may_bind=false) : state(value), owner(false), outer(false) {
        ++state.in_flight;
        if(may_bind && value.config.thread_binding==1 && value.owner==0) {
            DWORD empty=0;
            if(value.owner.compare_exchange_strong(empty,kBindingInProgress)) {
                const HANDLE handle=current_thread_handle();
                if(handle) {value.owner_handle=handle;value.binding_error=0;value.owner=GetCurrentThreadId();}
                else {value.binding_error=GetLastError();value.owner=0;}
            }
        }
        owner=same_live_thread(value.owner.load(),value.owner_handle.load());
        if (owner) outer = ++state.owner_depth == 1;
        else ++state.foreign;
    }
    ~Call() {
        if (owner) --state.owner_depth;
        --state.in_flight;
    }
    bool callbacks() const { return owner && outer && !state.fault && !state.lost; }
};

template<typename Callback> void invoke(SlDx11Runtime& state, Callback&& callback) noexcept {
    try { callback(); } catch (...) { state.fault = true; }
}

SlDx11Runtime* lookup(IDXGISwapChain* chain) {
    auto* state = active.load();
    return state && state->config.chain == chain ? state : nullptr;
}

void device_result(SlDx11Runtime& state, const Call& call, HRESULT result) {
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        state.lost_result = result;
        state.lost = true;
    }
    if (call.owner && call.outer && state.lost && !state.fault && !state.lost_notified && state.config.on_device_lost) {
        state.lost_notified = true;
        invoke(state, [&] { state.config.on_device_lost(state.config.context, state.config.chain, state.lost_result.load()); });
    }
}

template<typename Original> HRESULT present_common(IDXGISwapChain* chain, UINT flags, Original&& original) {
    auto* state = lookup(chain);
    if (!state) return DXGI_ERROR_INVALID_CALL;
    Call call(*state,!(flags & DXGI_PRESENT_TEST));
    ++state->presents;
    if(call.owner && call.outer && !(flags & DXGI_PRESENT_TEST) && state->config.on_control_frame)
        invoke(*state,[&]{state->config.on_control_frame(state->config.context,chain);});
    if (call.callbacks() && !(flags & DXGI_PRESENT_TEST) && state->config.on_frame) {
        ++state->frames;
        invoke(*state, [&] { state->config.on_frame(state->config.context, chain); });
    }
    const HRESULT result = original(*state);
    state->last_present = result;
    device_result(*state, call, result);
    return result;
}

HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain* chain, UINT interval, UINT flags) {
    return present_common(chain, flags, [&](SlDx11Runtime& s) { return reinterpret_cast<Present>(s.original[8])(chain, interval, flags); });
}
HRESULT STDMETHODCALLTYPE present1_hook(IDXGISwapChain* chain, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
    return present_common(chain, flags, [&](SlDx11Runtime& s) { return reinterpret_cast<Present1>(s.original[22])(chain, interval, flags, parameters); });
}

template<typename Original> HRESULT resize_common(IDXGISwapChain* chain, Original&& original) {
    auto* state = lookup(chain);
    if (!state) return DXGI_ERROR_INVALID_CALL;
    Call call(*state);
    ++state->resizes;
    if (call.callbacks() && state->config.before_resize)
        invoke(*state, [&] { state->config.before_resize(state->config.context, chain); });
    const HRESULT result = original(*state);
    if (call.callbacks() && state->config.after_resize)
        invoke(*state, [&] { state->config.after_resize(state->config.context, chain, result); });
    device_result(*state, call, result);
    return result;
}
HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    return resize_common(chain, [&](SlDx11Runtime& s) { return reinterpret_cast<Resize>(s.original[13])(chain,count,width,height,format,flags); });
}
HRESULT STDMETHODCALLTYPE resize1_hook(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* masks, IUnknown* const* queues) {
    return resize_common(chain, [&](SlDx11Runtime& s) { return reinterpret_cast<Resize1>(s.original[39])(chain,count,width,height,format,flags,masks,queues); });
}
}

SlStatus sl_dx11_attach(const SlDx11Config* config, SlDx11Runtime** output) {
    if (!config || !output || *output || config->struct_size != sizeof(*config) || !config->chain) return SL_INVALID_ARGUMENT;
    if (config->binding_kind != SL_BINDING_LOCAL_FIXTURE && config->binding_kind != SL_BINDING_LEAGUE_EXPERIMENT) return SL_UNSUPPORTED_BINDING;
    if (config->thread_binding>1) return SL_INVALID_ARGUMENT;
    if (active.load()) return SL_BUSY;
    if (!accessible(config->chain, sizeof(void*), true)) return SL_INVALID_ARGUMENT;
    auto* state = new(std::nothrow) SlDx11Runtime;
    if (!state) return SL_ALLOCATION_FAILED;
    state->config = *config;
    state->installer = GetCurrentThreadId();
    state->installer_handle = current_thread_handle();
    if(!state->installer_handle){delete state;return SL_ALLOCATION_FAILED;}
    if(config->thread_binding==0){state->owner_handle=state->installer_handle;state->owner=state->installer;}
    state->original = *reinterpret_cast<void***>(config->chain);
    if (!interface_entries(config->chain, state->entries) || !accessible(state->original, sizeof(void*) * state->entries, false)) {
        delete state; return SL_UNSUPPORTED_BINDING;
    }
    std::memcpy(state->table.data(), state->original, sizeof(void*) * state->entries);
    state->table[8] = reinterpret_cast<void*>(&present_hook);
    state->table[13] = reinterpret_cast<void*>(&resize_hook);
    if (state->entries >= 29) state->table[22] = reinterpret_cast<void*>(&present1_hook);
    if (state->entries >= 40) state->table[39] = reinterpret_cast<void*>(&resize1_hook);
    config->chain->AddRef();
    SlDx11Runtime* empty = nullptr;
    if (!active.compare_exchange_strong(empty, state)) { config->chain->Release(); delete state; return SL_BUSY; }
    auto* slot = reinterpret_cast<void* volatile*>(config->chain);
    if (InterlockedCompareExchangePointer(slot, state->table.data(), state->original) != state->original) {
        active = nullptr; config->chain->Release(); delete state; return SL_TARGET_CHANGED;
    }
    state->attached = true;
    *output = state;
    return SL_OK;
}

SlStatus sl_dx11_stats(SlDx11Runtime* state, SlDx11Stats* output) {
    if (!state || !output || output->struct_size != sizeof(*output)) return SL_INVALID_ARGUMENT;
    const bool owns_table = state->attached &&
        *reinterpret_cast<void***>(state->config.chain) == state->table.data();
    const DWORD render=state->owner.load();
    const HANDLE render_handle=state->owner_handle.load();
    const bool owner_alive=render_handle && WaitForSingleObject(render_handle,0)==WAIT_TIMEOUT;
    *output = SlDx11Stats{sizeof(*output), render==kBindingInProgress?0:render, state->installer, state->binding_error,
                         state->entries, state->attached, owner_alive, owns_table, state->lost, state->fault,
                         state->presents, state->frames, state->resizes, state->foreign, state->last_present};
    return SL_OK;
}

SlStatus sl_dx11_detach(SlDx11Runtime* state) {
    if (!state) return SL_INVALID_ARGUMENT;
    if (!same_live_thread(state->installer,state->installer_handle) &&
        !same_live_thread(state->owner.load(),state->owner_handle.load())) return SL_WRONG_THREAD;
    if (!state->attached) return SL_OK;
    if (state->in_flight) return SL_BUSY;
    if (!accessible(state->config.chain, sizeof(void*), true)) return SL_TARGET_CHANGED;
    auto* slot = reinterpret_cast<void* volatile*>(state->config.chain);
    if (InterlockedCompareExchangePointer(slot, state->original, state->table.data()) != state->table.data()) return SL_TARGET_CHANGED;
    state->attached = false;
    active = nullptr;
    state->config.chain->Release();
    return SL_OK;
}

SlStatus sl_dx11_destroy(SlDx11Runtime** runtime) {
    if (!runtime) return SL_INVALID_ARGUMENT;
    if (!*runtime) return SL_OK;
    const SlStatus result = sl_dx11_detach(*runtime);
    if (result != SL_OK) return result;
    delete *runtime;
    *runtime = nullptr;
    return SL_OK;
}
