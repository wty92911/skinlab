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
extern "C" SlStatus SL_CALL skinlab_get_api(uint32_t,SlApi*) noexcept;

namespace {
bool local_copy(uintptr_t address,void* output,size_t size) {
    SIZE_T done=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&done)&&done==size;
}
#include "owned_layer.h"
#include "form_skin.h"
#include "skin_gear.h"
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
struct GearFixture {
    alignas(8) uint8_t object[512]{};
    uint64_t generation=9;
    uint32_t refreshes=0;
    bool fail_next=false;
    SlConfig config{};SlApi api{};SlRuntime* core=nullptr;
    SkinGearSync gears{};
    uintptr_t stack() {return reinterpret_cast<uintptr_t>(object+0x80);}
    uint8_t& gear() {return object[0x80+SkinGearSync::gear_offset];}
    uint32_t skin() {uint32_t value=0;std::memcpy(&value,object+0xb8,4);return value;}
    static uint64_t SL_CALL identity(void* context) {return static_cast<GearFixture*>(context)->generation;}
    static int SL_CALL allowed(void*,int32_t skin) {return skin>=0&&skin<1000;}
    static int SL_CALL refresh(void* context,void* stack,int force) {
        auto& f=*static_cast<GearFixture*>(context);bool changed=false;
        if(reinterpret_cast<uintptr_t>(stack)!=f.stack()||force!=1
           ||f.gears.synchronize(f.config,f.stack(),changed)!=SL_OK)return 0;
        ++f.refreshes;
        if(f.fail_next){f.fail_next=false;return 0;}
        return f.gears.synchronize(f.config,f.stack(),changed,false)==SL_OK&&!changed;
    }
    bool initialize(uint32_t original,uint8_t initial,const char* champion="Katarina") {
        const char* model="Katarina";
        std::memcpy(object+0x98,&model,sizeof(model));
        std::memcpy(object+0x20,&original,4);std::memcpy(object+0xb8,&original,4);gear()=initial;
        config.struct_size=sizeof(config);config.abi_version=SL_ABI_VERSION;config.binding_kind=SL_BINDING_LOCAL_FIXTURE;
        config.layout={0x20,0x80,0x38,0,0,0,0,0,0};config.object_data=object;config.object_size=sizeof(object);
        config.context=this;config.expected_generation=9;config.generation=identity;config.skin_allowed=allowed;config.refresh=refresh;
        gears.champion=champion;api.struct_size=sizeof(api);
        return skinlab_get_api(SL_ABI_VERSION,&api)==SL_OK&&gears.initialize(config,stack())==SL_OK&&api.create(&config,&core)==SL_OK;
    }
    SlStatus apply(uint32_t selected) {
        auto result=gears.begin(config,stack(),selected);
        if(result==SL_OK)result=api.apply(core,int32_t(selected));
        gears.end();return result;
    }
    SlStatus restore() {
        auto result=gears.begin(config,stack(),gears.original_skin,true);
        if(result==SL_OK)result=api.restore(core);
        gears.end();return result;
    }
    ~GearFixture() {
        if(!core)return;
        gears.end();gears.begin(config,stack(),gears.original_skin,true);
        api.destroy(&core);gears.end();
        if(core)api.abandon_destroyed_target(&core);
    }
};
bool check_skin_gear() {
    GearFixture f{};
    if(!f.initialize(0,0xff)||f.apply(29)!=SL_OK||f.skin()!=29||f.gear()!=0||f.refreshes!=1)return false;
    f.gear()=4; // A game-driven knife change must survive chroma selection.
    if(f.apply(30)!=SL_OK||f.skin()!=30||f.gear()!=4)return false;
    f.fail_next=true;
    // Use the actual core rollback: the failed ordinary skin wrote ff, then
    // its rollback callback must recover knife 4 rather than initializing 0.
    if(f.apply(1)!=SL_UPDATE_FAILED||f.skin()!=30||f.gear()!=4||f.refreshes!=4)return false;
    if(f.apply(1)!=SL_OK||f.gear()!=0xff||f.restore()!=SL_OK||f.skin()!=0||f.gear()!=0xff)return false;
    // Same-skin frame repair is separate from core.apply's intentional no-op.
    if(f.apply(29)!=SL_OK)return false;
    f.gear()=0xff;bool changed=false;
    if(f.gears.begin(f.config,f.stack(),29)!=SL_OK
       ||f.gears.synchronize(f.config,f.stack(),changed,false)!=SL_OK||!changed||f.gear()!=0xff
       ||f.gears.synchronize(f.config,f.stack(),changed)!=SL_OK||!changed||f.gear()!=0
       ||!GearFixture::refresh(&f,reinterpret_cast<void*>(f.stack()),1))return false;
    f.gears.end();const auto refreshes=f.refreshes;f.gear()=5;
    if(f.gears.begin(f.config,f.stack(),29)!=SL_OK
       ||f.gears.synchronize(f.config,f.stack(),changed)!=SL_OK||changed||f.gear()!=5||f.refreshes!=refreshes)return false;
    f.gears.end();if(f.restore()!=SL_OK)return false;
    // Original Battle Queen can already have a nondefault or invalid gear;
    // restore preserves its exact byte, including the reported ff state.
    for(const uint8_t original:{uint8_t(3),uint8_t(0xff)}) {
        GearFixture own{};
        if(!own.initialize(29,original)||own.apply(0)!=SL_OK||own.gear()!=0xff
           ||own.apply(36)!=SL_OK||own.gear()!=0
           ||own.restore()!=SL_OK||own.skin()!=29||own.gear()!=original)return false;
    }
    GearFixture other{};
    if(!other.initialize(0,0xff,"Viego")||other.apply(29)!=SL_OK||other.gear()!=0xff||other.restore()!=SL_OK)return false;
    // No write through borrowed models, invalid identity, or a different thread.
    const char* borrowed="OtherHero";std::memcpy(f.object+0x98,&borrowed,sizeof(borrowed));
    if(f.gears.begin(f.config,f.stack(),29)!=SL_INCONSISTENT_STATE||f.gear()!=0xff)return false;
    const char* model="Katarina";std::memcpy(f.object+0x98,&model,sizeof(model));
    f.generation=10;
    if(f.gears.begin(f.config,f.stack(),29)!=SL_TARGET_CHANGED||f.gear()!=0xff)return false;
    f.generation=9;f.gears.thread^=1;
    if(f.gears.begin(f.config,f.stack(),29)!=SL_WRONG_THREAD||f.gear()!=0xff)return false;
    f.gears.thread=GetCurrentThreadId();
    // A game-owned extra layer is outside this base-gear rule.
    const uintptr_t header[]={0x10000,0x100a0,0x100a0};std::memcpy(f.object+0x80,header,sizeof(header));
    if(f.gears.begin(f.config,f.stack(),29)!=SL_OK)return false;
    uint32_t selected=29;std::memcpy(f.object+0xb8,&selected,4);
    if(f.gears.synchronize(f.config,f.stack(),changed)!=SL_INCONSISTENT_STATE||f.gear()!=0xff)return false;
    std::memset(f.object+0x80,0,sizeof(header));selected=0;std::memcpy(f.object+0xb8,&selected,4);f.gears.end();
    // Readable storage alone is not permission to patch it.
    auto* page=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!page)return false;
    std::memcpy(page,f.object,sizeof(f.object));
    SlConfig page_config=f.config;page_config.object_data=page;
    const auto page_stack=reinterpret_cast<uintptr_t>(page+0x80);
    SkinGearSync readonly{};readonly.champion="Katarina";DWORD protection=0,ignored=0;
    bool ok=readonly.initialize(page_config,page_stack)==SL_OK&&readonly.begin(page_config,page_stack,29)==SL_OK;
    selected=29;std::memcpy(page+0xb8,&selected,4);
    ok=ok&&VirtualProtect(page,4096,PAGE_READONLY,&protection);
    if(ok)ok=readonly.synchronize(page_config,page_stack,changed)==SL_INCONSISTENT_STATE&&page[0x80+SkinGearSync::gear_offset]==0xff;
    if(protection)VirtualProtect(page,4096,protection,&ignored);
    VirtualFree(page,0,MEM_RELEASE);return ok;
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
    if(!check_skin_gear())return 12;
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
