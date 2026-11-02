// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "observe_protocol.h"
#include "session_fixture.h"
#include <d3d11.h>
#include <cstring>

namespace {
SlObserveShared* shared=nullptr;HANDLE ready=nullptr;
IDXGISwapChain* fixture_chain=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* device_context=nullptr;
alignas(8) unsigned char object[512]{};
alignas(8) unsigned char layers[288]{};
uint64_t __cdecl generation(void*){return 1;}
int __cdecl allowed(void*,int32_t skin){return skin==0||skin==1;}
int __cdecl refresh(void*,void* stack,int force){++shared->refresh_calls;return stack==object+0x80&&force==1;}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_TIMER&&fixture_chain){fixture_chain->Present(0,0);return 0;}
    if(message==SL_O_FIXTURE_QUERY) {
        shared->fixture_module_present=GetModuleHandleW(L"skinlab-observe.dll")?1:0;
        ++shared->fixture_queries;MemoryBarrier();SetEvent(ready);return 0;
    }
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
}
extern "C" __declspec(dllexport) int __cdecl skinlab_get_session_fixture(SlSessionFixtureConfig* output) {
    if(!output||output->struct_size!=sizeof(*output)||!fixture_chain)return 0;
    SlConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=SL_ABI_VERSION;cfg.binding_kind=SL_BINDING_LOCAL_FIXTURE;
    cfg.layout={0x20,0x80,0x38,5,0xa5192d37,11};cfg.object_data=object;cfg.object_size=sizeof(object);
    cfg.expected_generation=1;cfg.generation=generation;cfg.skin_allowed=allowed;cfg.refresh=refresh;
    output->core=cfg;output->chain=fixture_chain;return 1;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,argv[1]);if(!mapping)return 3;
    shared=static_cast<SlObserveShared*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(SlObserveShared)));
    ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(argv[1])+L"-ready").c_str());
    if(!shared||!ready)return 4;
    if(shared->magic!=SL_OBSERVE_MAGIC||shared->version!=SL_OBSERVE_VERSION||shared->struct_size!=sizeof(*shared)
       ||shared->target_pid!=GetCurrentProcessId()||shared->creation_time!=sl_creation_time(GetCurrentProcess()))return 5;
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SkinLabCodeObservationFixture";
    if(!RegisterClassW(&wc))return 6;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"SkinLab observation fixture",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 7;
    if(shared->request_session==1) {
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;
        desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
        if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&desc,&fixture_chain,&device,nullptr,&device_context)))return 9;
        const uint32_t original=0xa6f4a325;std::memcpy(object+0x20,&original,4);
        // Two synthetic opaque layers. Their shape is a fixture contract,
        // never evidence for the layout of a game CharacterStackData entry.
        layers[0]=0x42;layers[sizeof(layers)-1]=0x7d;
        const uintptr_t begin=reinterpret_cast<uintptr_t>(layers),end=begin+sizeof(layers);
        std::memcpy(object+0x80,&begin,8);std::memcpy(object+0x88,&end,8);std::memcpy(object+0x90,&end,8);
        SetTimer(window,1,16,nullptr);
    }
    shared->window=reinterpret_cast<uintptr_t>(window);shared->fixture_tid=GetCurrentThreadId();
    shared->fixture_module_present=GetModuleHandleW(L"skinlab-observe.dll")?1:0;MemoryBarrier();SetEvent(ready);
    MSG message{};BOOL status=0;
    while((status=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    shared->fixture_exited=1;MemoryBarrier();
    // The diagnostic bridge owns another chain reference until process exit.
    if(device_context){device_context->ClearState();device_context->Release();}
    if(fixture_chain)fixture_chain->Release();
    if(device)device->Release();
    CloseHandle(ready);UnmapViewOfFile(shared);CloseHandle(mapping);return status==0?0:8;
}
