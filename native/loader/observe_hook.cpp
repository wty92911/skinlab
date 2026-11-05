// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "observe_protocol.h"
#include "observe_hash.h"
#include <skinlab/dx11_bridge.h>
#include <skinlab/league_fields.h>
#include <skinlab/skin_codec.h>
#include "session_fixture.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>

extern "C" __declspec(dllexport) LRESULT CALLBACK skinlab_observe_hook(int,WPARAM,LPARAM);
extern "C" SlStatus SL_CALL skinlab_get_api(uint32_t,SlApi*) noexcept;
namespace {
volatile LONG busy=0;
bool session_active=false;
bool local_copy(uintptr_t address,void* output,size_t size) {
    SIZE_T done=0;
    if(address<0x10000||address>=0x800000000000ULL||size>0x800000000000ULL-address)return false;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&done)&&done==size;
}
void observe_player(SlObserveShared& out,const skinlab::profile::Snapshot& image,const std::vector<skinlab::profile::Result>& results) {
    const skinlab::profile::Candidate *player_rule=nullptr,*stack_rule=nullptr;
    const auto* skin_rule=skinlab::profile::skin_codec(results);
    for(const auto& r:results)if(r.has_candidate) {
        if(r.name=="local_player")player_rule=&r.candidate;
        if(r.name=="character_data_stack")stack_rule=&r.candidate;
    }
    if(!player_rule||!stack_rule||!skin_rule)return;
    if(skin_rule->referenced_stack_offset&&skin_rule->referenced_stack_offset!=stack_rule->field_offset)return;
    out.player_read_status=2;uintptr_t player=0,model=0,after=0;
    if(!local_copy(image.module_base+player_rule->rva,&player,8)||!player) {out.player_read_error=GetLastError();return;}
    const auto stack=player+stack_rule->field_offset;
    out.player_address=player;out.stack_address=stack;
    if(!local_copy(player+skin_rule->field_offset,&out.encoded_skin,4)||!local_copy(stack+skinlab::league::kStackSkinOffset,&out.stack_skin,4)
       ||!local_copy(stack+skinlab::league::kStackModelPointerOffset,&model,8)){out.player_read_error=GetLastError();return;}
    out.decoded_skin=skinlab::codec::decode(out.encoded_skin,skin_rule->codec_kind,skin_rule->rotate_first,
        skin_rule->xor_key,skin_rule->rotate_second,skin_rule->add_key);
    out.skin_fields_agree=out.decoded_skin==out.stack_skin;
    for(size_t i=0;i<sizeof(out.model_name)-1;++i) {
        if(!local_copy(model+i,&out.model_name[i],1)){out.player_read_error=GetLastError();return;}
        if(!out.model_name[i])break;
    }
    if(!local_copy(image.module_base+player_rule->rva,&after,8)){out.player_read_error=GetLastError();return;}
    out.player_stable=after==player;out.player_read_status=1;
}
struct Work {
    HANDLE mapping=nullptr,done=nullptr;
    SlObserveShared* shared=nullptr;
    ~Work(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);if(done)CloseHandle(done);}
};
void copy_text(char (&out)[192],const std::string& text) {
    const size_t count=std::min<size_t>(text.size(),sizeof(out)-1);
    std::memcpy(out,text.data(),count);out[count]=0;
}
#include "session_binding.h"
#include "lux_forms.h"
#include "owned_layer.h"
#include "session_probe.h"
#include "game_session.h"
bool target_scope() {
    wchar_t image[32768];const DWORD count=GetModuleFileNameW(nullptr,image,32768);
    if(!count||count>=32768)return false;
    const wchar_t* name=image;
    for(const wchar_t* p=image;*p;++p)if(*p==L'\\'||*p==L'/')name=p+1;
    return _wcsicmp(name,L"skinlab-observe-fixture.exe")==0||_wcsicmp(name,L"League of Legends.exe")==0;
}
DWORD WINAPI observe(void* parameter) {
    std::shared_ptr<Work> work(static_cast<Work*>(parameter));
    auto& out=*work->shared;out.worker_tid=GetCurrentThreadId();
    try {
        auto image=skinlab::profile::collect_current_module(GetModuleHandleW(nullptr));
        out.image_size=image.image_size;out.complete=image.complete?1:0;
        out.expected_bytes=image.expected_bytes;out.copied_bytes=image.copied_bytes;
        out.gap_count=uint32_t(image.gaps.size());out.collection_ms=image.elapsed_ms;
        copy_text(out.error,image.error);
        if(!image.image_size||image.image_size>128u*1024u*1024u) {
            InterlockedExchange(&out.phase,SL_O_FAILED);
        } else {
            const auto hash=sl_observe_snapshot_hash(image);
            std::memcpy(out.snapshot_hash,hash.data(),hash.size());out.snapshot_hash_present=1;
            const auto results=skinlab::profile::analyze(image,skinlab::profile::recovered_rules(),
                                                        skinlab::profile::read_current_rva,&image);
            if(results.size()!=SL_OBSERVE_RULES)throw std::runtime_error("Recovered rule count changed");
            for(size_t i=0;i<results.size();++i) {
                const auto& r=results[i];auto& dest=out.results[i];
                dest.state=uint32_t(r.state);dest.has_candidate=r.has_candidate?1:0;
                dest.match_count=uint32_t(r.matches.size());dest.match_count_capped=r.match_count_capped?1:0;
                for(size_t m=0;m<r.matches.size()&&m<2;++m)dest.match_rvas[m]=r.matches[m];
                dest.rva=r.candidate.rva;dest.field_offset=r.candidate.field_offset;
                dest.xor_key=r.candidate.xor_key;dest.rotate_first=r.candidate.rotate_first;dest.rotate_second=r.candidate.rotate_second;
                dest.codec_kind=r.candidate.codec_kind;dest.add_key=r.candidate.add_key;dest.referenced_stack_offset=r.candidate.referenced_stack_offset;
                copy_text(dest.reason,r.reason);
            }
            observe_player(out,image,results);
            if(out.request_session==2) {
                auto sites=session_probe_sites(image,results);
                out.stack_probe.stack_reference_count=sites.stack_reference_count;
                if(out.player_read_status==1&&out.player_stable&&out.stack_address)resolve_gear_probe(sites,uintptr_t(out.stack_address));
                collect_code_probe(sites,out.stack_probe.code);
            }
            if(out.request_session==1) {
                out.result_count=uint32_t(results.size());
                if(start_session(work,image,results))return 0;
            }

            out.result_count=uint32_t(results.size());InterlockedExchange(&out.phase,SL_O_COMPLETE);
        }
    } catch(const std::exception& error) {copy_text(out.error,error.what());InterlockedExchange(&out.phase,SL_O_FAILED);}
      catch(...) {copy_text(out.error,"Unhandled observation exception");InterlockedExchange(&out.phase,SL_O_FAILED);}
    // All report writes precede publication. Nothing writes the mapping after
    // signalling. The module is pinned, so neither this worker nor a still
    // returning GetMessage callback can return into an unmapped image.
    InterlockedExchange(&busy,0);SetEvent(work->done);return 0;
}
void begin(const MSG& message) {
    if(!message.wParam||uint64_t(message.wParam)>UINT32_MAX||!message.lParam)return;
    std::unique_ptr<Work> work(new Work);
    const auto name=sl_observe_name(uint32_t(message.wParam),uint64_t(message.lParam));
    work->mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());
    if(!work->mapping)return;
    work->shared=static_cast<SlObserveShared*>(MapViewOfFile(work->mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(SlObserveShared)));
    if(!work->shared)return;
    auto& out=*work->shared;
    const uint64_t creation=sl_creation_time(GetCurrentProcess());
    if(out.magic!=SL_OBSERVE_MAGIC||out.version!=SL_OBSERVE_VERSION||out.struct_size!=sizeof(out)
       ||out.host_pid!=uint32_t(message.wParam)||out.nonce!=uint64_t(message.lParam)
       ||out.target_pid!=GetCurrentProcessId()||out.window!=reinterpret_cast<uintptr_t>(message.hwnd)
       ||!creation||out.creation_time!=creation||InterlockedCompareExchange(&out.phase,SL_O_RUNNING,SL_O_ARMED)!=SL_O_ARMED)return;
    work->done=OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L"-done").c_str());
    if(!work->done)return;
    if(InterlockedCompareExchange(&busy,1,0)!=0) {
        copy_text(out.error,"An observation worker is already running");out.win32_error=ERROR_BUSY;
        InterlockedExchange(&out.phase,SL_O_FAILED);SetEvent(work->done);return;
    }
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&skinlab_observe_hook),&pinned)) {
        out.win32_error=GetLastError();copy_text(out.error,"Cannot retain observer module for process lifetime");
        InterlockedExchange(&busy,0);InterlockedExchange(&out.phase,SL_O_FAILED);SetEvent(work->done);return;
    }
    out.module_pinned=1;out.hook_tid=GetCurrentThreadId();
    HANDLE thread=CreateThread(nullptr,0,observe,work.get(),0,nullptr);
    if(!thread) {
        out.win32_error=GetLastError();copy_text(out.error,"Cannot create observation worker; module remains pinned");
        InterlockedExchange(&busy,0);InterlockedExchange(&out.phase,SL_O_FAILED);SetEvent(work->done);return;
    }
    // Ownership has passed to the new worker; it may already have completed.
    work.release();CloseHandle(thread);
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK skinlab_observe_hook(int code,WPARAM remove,LPARAM parameter) {
    if(code>=0&&remove==PM_REMOVE&&parameter) {
        const auto& message=*reinterpret_cast<const MSG*>(parameter);
        if(message.message>=0xc000) {
            static const UINT command=RegisterWindowMessageW(SL_OBSERVE_MESSAGE);
            if(command&&message.message==command&&target_scope()) {
                try{begin(message);}catch(...){/* Never unwind across the Windows hook boundary. */}
            }
        }
    }
    // Do not consume or alter any message, including our own registered command.
    return CallNextHookEx(nullptr,code,remove,parameter);
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
