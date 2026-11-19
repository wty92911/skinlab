// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "observe_protocol.h"
#include "session_fixture.h"
#include <d3d11.h>
#include <cstring>
#include <skinlab/league_fields.h>
#include "lux_forms.h"

namespace {
bool local_copy(uintptr_t address,void* output,size_t size) {
    SIZE_T done=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&done)&&done==size;
}
#include "owned_layer.h"
#include "form_skin.h"
struct LayerFixture {
    alignas(8) uint8_t stack[512]{},entries[320]{};
    uint64_t generation=9;
    uint32_t pushes=0,destroys=0;
    bool arguments=true;
};
LayerFixture* layer_fixture=nullptr;
void __fastcall fixture_push(void* stack,const char* model,int32_t skin,int32_t a4,
    bool a5,bool a6,bool a7,bool a8,bool a9,bool a10,int8_t gear,const char* a12,
    int32_t a13,const char* a14,int32_t a15,bool a16,int32_t a17,const char* a18) {
    auto& f=*layer_fixture;++f.pushes;
    f.arguments=f.arguments&&stack==f.stack&&skin==7&&!a4&&!a5&&!a6&&!a7&&!a8&&a9&&!a10
        &&gear==-1&&!*a12&&!a13&&!*a14&&!a15&&!a16&&a17==1&&!*a18;
    const uintptr_t values[]={reinterpret_cast<uintptr_t>(f.entries),reinterpret_cast<uintptr_t>(f.entries)+0xa0,
        reinterpret_cast<uintptr_t>(f.entries)+sizeof(f.entries)};
    std::memcpy(f.stack,values,sizeof(values));
    const auto length=uint32_t(std::strlen(model));
    std::memcpy(f.entries,&model,8);std::memcpy(f.entries+8,&length,4);
    std::memcpy(f.entries+0x20,&skin,4);f.entries[0x84]=0xff;
}
void __fastcall fixture_destroy(void* entry) {
    auto& f=*layer_fixture;++f.destroys;
    uintptr_t values[3]{};std::memcpy(values,f.stack,sizeof(values));
    f.arguments=f.arguments&&entry==f.entries&&values[0]==values[1];
    std::memset(entry,0,0xa0);
}
uint64_t __cdecl layer_generation(void* context){return static_cast<LayerFixture*>(context)->generation;}
bool check_owned_layer() {
    LayerFixture fixture{};layer_fixture=&fixture;
    SlConfig config{};config.context=&fixture;config.generation=layer_generation;config.expected_generation=9;
    const auto stack=reinterpret_cast<uintptr_t>(fixture.stack);
    uint32_t skin=7;std::memcpy(fixture.stack+0x38,&skin,4);
    OwnedStackLayer layer{};layer.thread=GetCurrentThreadId();layer.push=fixture_push;layer.destroy=fixture_destroy;
    if(layer.select(config,stack,1)!=SL_OK||!layer.owned||fixture.pushes!=1||!fixture.arguments)return false;
    if(layer.select(config,stack,1)!=SL_OK||fixture.pushes!=1)return false;
    if(layer.select(config,stack,2)!=SL_OK||fixture.pushes!=2||fixture.destroys!=1||!fixture.arguments)return false;
    fixture.entries[0x7f]=1;
    if(layer.remove(config,stack)!=SL_RECOVERY_REQUIRED||fixture.destroys!=1)return false;
    fixture.entries[0x7f]=0;
    if(layer.remove(config,stack)!=SL_OK||layer.owned||fixture.destroys!=2||!fixture.arguments)return false;
    uintptr_t values[3]{};std::memcpy(values,fixture.stack,sizeof(values));
    if(values[0]!=values[1])return false;
    // Reject a foreign layer without changing its bytes or calling its destructor.
    values[1]+=0xa0;std::memcpy(fixture.stack,values,sizeof(values));
    if(layer.select(config,stack,1)!=SL_UNSUPPORTED_BINDING||fixture.pushes!=2||fixture.destroys!=2)return false;
    values[1]=values[0];std::memcpy(fixture.stack,values,sizeof(values));
    fixture.generation=10;
    if(layer.select(config,stack,1)!=SL_TARGET_CHANGED||fixture.pushes!=2)return false;
    fixture.generation=9;skin=0;std::memcpy(fixture.stack+0x38,&skin,4);
    return layer.select(config,stack,1)==SL_INVALID_SKIN&&fixture.pushes==2&&fixture.arguments;
}
bool check_form_skin(const FormSkinSync::Rule& rule) {
    LayerFixture fixture{};
    SlConfig config{};config.context=&fixture;config.generation=layer_generation;config.expected_generation=9;
    const auto stack=reinterpret_cast<uintptr_t>(fixture.stack);
    FormSkinSync sync{};sync.champion=rule.champion;bool changed=false;
    if(sync.synchronize(config,stack,11,changed)!=SL_OK||changed)return false;
    auto set_header=[&](uint8_t* entries,size_t size) {
        const auto begin=reinterpret_cast<uintptr_t>(entries);
        const uintptr_t header[]={begin,begin+size,begin+size};
        std::memcpy(fixture.stack,header,sizeof(header));
    };
    auto set_layer=[](uint8_t* entry,const char* model,uint32_t skin) {
        std::memset(entry,0x5a,FormSkinSync::stride);
        const auto length=uint32_t(std::strlen(model));
        std::memcpy(entry,&model,8);std::memcpy(entry+8,&length,4);
        std::memcpy(entry+FormSkinSync::skin_offset,&skin,4);
    };
    auto get_skin=[](uint8_t* entry) {
        uint32_t skin=0;std::memcpy(&skin,entry+FormSkinSync::skin_offset,4);return skin;
    };
    // Transform after applying: preserve model/gear/other entry bytes, update
    // only the form's skin. An unrelated top layer must stay game-owned.
    set_layer(fixture.entries,rule.alternate,0);
    set_layer(fixture.entries+FormSkinSync::stride,"OtherChampion",42);
    set_header(fixture.entries,sizeof(fixture.entries));
    uint8_t expected[sizeof(fixture.entries)]{};std::memcpy(expected,fixture.entries,sizeof(expected));
    const uint32_t selected=11;std::memcpy(expected+FormSkinSync::skin_offset,&selected,4);
    if(sync.synchronize(config,stack,selected,changed)!=SL_OK||!changed
       ||std::memcmp(expected,fixture.entries,sizeof(expected)))return false;
    // Stable frames do not request another refresh; inspect never writes.
    if(sync.synchronize(config,stack,selected,changed)!=SL_OK||changed)return false;
    if(sync.synchronize(config,stack,18,changed,false)!=SL_OK||!changed||get_skin(fixture.entries)!=selected)return false;
    if(sync.synchronize(config,stack,18,changed)!=SL_OK||!changed||get_skin(fixture.entries)!=18)return false;
    // Original can be a chroma. Restoration targets the *current* layer, even
    // after the game has reallocated it or changed back to the human model.
    alignas(8) uint8_t replacement[FormSkinSync::stride]{};
    set_layer(replacement,rule.champion,0);set_header(replacement,sizeof(replacement));
    if(sync.synchronize(config,stack,54,changed)!=SL_OK||!changed
       ||get_skin(replacement)!=54||get_skin(fixture.entries)!=18)return false;
    // Other champions and borrowed transformations are never covered by this rule.
    sync.champion="Viego";
    if(sync.synchronize(config,stack,1,changed)!=SL_OK||changed||get_skin(replacement)!=54)return false;
    sync.champion=rule.champion;
    // Model rules are scoped to their champion, even when another supported
    // champion's form appears in the same stack (e.g. a borrowed appearance).
    for(const auto& other:FormSkinSync::rules)if(std::strcmp(other.champion,rule.champion)) {
        set_layer(replacement,other.alternate,0);
        if(sync.synchronize(config,stack,1,changed)!=SL_OK||changed||get_skin(replacement))return false;
    }
    // A malformed later entry must reject the entire operation before writes.
    set_header(fixture.entries,sizeof(fixture.entries));
    uintptr_t bad_name=1;std::memcpy(fixture.entries+FormSkinSync::stride,&bad_name,8);
    if(sync.synchronize(config,stack,1,changed)!=SL_INCONSISTENT_STATE||changed||get_skin(fixture.entries)!=18)return false;
    const uintptr_t malformed[]={1,2,3};std::memcpy(fixture.stack,malformed,sizeof(malformed));
    if(sync.synchronize(config,stack,1,changed)!=SL_INCONSISTENT_STATE||changed)return false;
    fixture.generation=10;
    return sync.synchronize(config,stack,1,changed)==SL_TARGET_CHANGED&&!changed;
}
SlObserveShared* shared=nullptr;HANDLE ready=nullptr;
IDXGISwapChain* fixture_chain=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* device_context=nullptr;
alignas(8) unsigned char object[512]{};
alignas(8) unsigned char layers[288]{};
uint64_t __cdecl generation(void*){return 1;}
int __cdecl allowed(void*,int32_t skin){return skin==0||skin==1;}
bool refresh_succeeds=true;
int __cdecl refresh(void*,void* stack,int force){++shared->refresh_calls;return refresh_succeeds&&stack==object+0x80&&force==1;}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_TIMER&&fixture_chain){fixture_chain->Present(0,0);return 0;}
    if(message==SL_O_FIXTURE_REFRESH_RESULT){refresh_succeeds=wp!=0;return 0;}
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
    cfg.layout=shared->fixture_codec==2?SlLayout{0x20,0x80,0x38,28,0x5050669b,0,2,0x57e1606e,0x0d37fa35}:shared->fixture_codec==1?SlLayout{0x20,0x80,0x38,25,0x5f0510b1,14,1,0x54cd83ae,0}:SlLayout{0x20,0x80,0x38,5,0xa5192d37,11,0,0,0};
    cfg.object_data=object;cfg.object_size=sizeof(object);
    cfg.expected_generation=1;cfg.generation=generation;cfg.skin_allowed=allowed;cfg.refresh=refresh;
    output->core=cfg;output->chain=fixture_chain;return 1;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    if(!check_owned_layer())return 10;
    for(const auto& rule:FormSkinSync::rules)if(!check_form_skin(rule))return 11;
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
        const uint32_t original=shared->fixture_codec==2?0x835493c3:shared->fixture_codec==1?0x3e179126:0xa6f4a325;std::memcpy(object+0x20,&original,4);
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
    for(;;) {
        // Observation exercises PM_NOREMOVE followed by PM_REMOVE; session
        // checks exercise GetMessage's PM_REMOVE-only path independently.
        if(shared->request_session!=1&&!PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE)) {
            if(!WaitMessage())return 8;
            continue;
        }
        status=GetMessageW(&message,nullptr,0,0);
        if(status<=0)break;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    shared->fixture_exited=1;MemoryBarrier();
    // The diagnostic bridge owns another chain reference until process exit.
    if(device_context){device_context->ClearState();device_context->Release();}
    if(fixture_chain)fixture_chain->Release();
    if(device)device->Release();
    CloseHandle(ready);UnmapViewOfFile(shared);CloseHandle(mapping);return status==0?0:8;
}
