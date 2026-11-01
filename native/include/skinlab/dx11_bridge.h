#ifndef SKINLAB_DX11_BRIDGE_H
#define SKINLAB_DX11_BRIDGE_H
#include <dxgi1_2.h>
#include <skinlab/api.h>

// The caller owns a valid swap-chain and stops all its users while attaching,
// detaching or destroying this bridge. Callbacks run on the selected render
// thread. This is a quiescence contract, not concurrent teardown protection.
// One attached interface pointer per linked bridge module is supported.
// Attach outside DllMain. The selected render thread must remain alive until
// core cleanup. Stats require stable runtime/object lifetime;
// ownership inspection is a snapshot, not concurrent hook synchronization.
struct SlDx11Config {
    uint32_t struct_size;
    SlBindingKind binding_kind; // only LOCAL_FIXTURE until game binding is verified
    IDXGISwapChain* chain;
    void* context;
    void (*on_frame)(void*, IDXGISwapChain*);
    void (*before_resize)(void*, IDXGISwapChain*);
    void (*after_resize)(void*, IDXGISwapChain*, HRESULT);
    void (*on_device_lost)(void*, IDXGISwapChain*, HRESULT);
    // 0: attaching thread; 1: first non-TEST Present claims the render thread.
    uint32_t thread_binding = 0;
    // Runs on real owner frames even after draw/device faults, so restoration
    // can finish. Must be bounded; exceptions are contained as draw faults.
    void (*on_control_frame)(void*, IDXGISwapChain*) = nullptr;
};

struct SlDx11Stats {
    uint32_t struct_size;
    uint32_t owner_thread;
    uint32_t installer_thread;
    uint32_t binding_error;
    uint32_t vtable_entries;
    bool attached;
    bool owner_alive;
    bool owns_vtable;
    bool device_lost;
    bool callback_fault;
    uint64_t present_calls;
    uint64_t frame_callbacks;
    uint64_t resize_calls;
    uint64_t foreign_thread_calls;
    HRESULT last_present_result;
};

struct SlDx11Runtime;
SlStatus sl_dx11_attach(const SlDx11Config* config, SlDx11Runtime** output);
SlStatus sl_dx11_stats(SlDx11Runtime* runtime, SlDx11Stats* output);
// Failed detach/destroy retains the runtime and its chain reference. A vtable
// ownership conflict must be resolved by the caller before either can succeed.
SlStatus sl_dx11_detach(SlDx11Runtime* runtime);
SlStatus sl_dx11_destroy(SlDx11Runtime** runtime);
#endif
