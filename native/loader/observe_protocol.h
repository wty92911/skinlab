// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_OBSERVE_PROTOCOL_H
#define SKINLAB_OBSERVE_PROTOCOL_H
#include <skinlab/profile_scan.h>
#include <string>

constexpr uint64_t SL_OBSERVE_MAGIC=0x534c4f4253303031ULL;
constexpr uint32_t SL_OBSERVE_VERSION=17,SL_OBSERVE_RULES=18,SL_STACK_CONTEXTS=6,SL_CODE_PROBES=13;
constexpr uint32_t SL_STACK_PROBE_VERSION=2,SL_MODEL_PROBES=8;
constexpr wchar_t SL_OBSERVE_MESSAGE[]=L"SkinLab.NativeSession.74576016-9FBC-486A-98EB-C98B41960012";
enum : LONG {SL_O_ARMED=1,SL_O_RUNNING,SL_O_COMPLETE,SL_O_FAILED};
enum : LONG {SL_START_MESSAGE,SL_START_COLLECT,SL_START_HASH,SL_START_SCAN,SL_START_BIND,SL_START_FRAME,SL_START_READY};
enum : UINT {SL_O_FIXTURE_QUERY=WM_APP+0x630,SL_O_FIXTURE_REFRESH_RESULT};
struct SlObserveResult {
    uint32_t state,has_candidate,match_count,match_count_capped;
    uint32_t match_rvas[2];
    uint32_t rva,field_offset,xor_key,rotate_first,rotate_second;
    uint32_t codec_kind,add_key,referenced_stack_offset;
    uint32_t xor_key_second;
    char reason[192];
};
// Explicit read-only adaptation probe, collected on the session's frame thread.
// Raw spans do not assert a stack-entry type, stride, gear offset or push ABI.
struct SlCodeProbe {uint32_t rva,size;uint8_t bytes[4096];};
struct SlStackModelProbe {
    uint32_t read_status,index,model_length,skin,gear;
    uint64_t address;
    char model[96];
};
struct SlStackModelsProbe {
    uint32_t read_status,layer_count,sampled_layers;
    SlStackModelProbe base,layers[SL_MODEL_PROBES];
};
struct SlMinionArrayProbe {
    uint32_t field_offset,size;
    uint64_t address;
    uint8_t bytes[2048];
};
struct SlMinionObjectProbe {
    uint32_t read_status,prefix_size;
    uint32_t counted_member,source_status,source_strong;
    uint64_t address;
    uint64_t source_cell,source_control,source_object;
    char model[96];
    uint8_t prefix[16384];
};
// Candidate spans only: no manager/member/owner layout is asserted here.
struct SlMinionProbe {
    uint32_t read_status,slot_rva,context_rva,callee_rva,context_size,callee_size;
    uint32_t array_count,object_count,tries,budget_exhausted,player_size;
    uint64_t manager,player;
    uint32_t source_member;
    uint8_t manager_prefix[256],player_prefix[16384],context[1024],callee[1024];
    SlMinionArrayProbe arrays[32];
    SlMinionObjectProbe objects[4];
    SlCodeProbe owner_candidate,owner_context;
};
struct SlSkinResourceProbe {
    uint32_t read_status,owner_member;
    uint64_t owner,table,targets[9];
    uint8_t cache[0x88]; // observed stack +0xB0..+0x138, no inferred type
};
struct SlStackProbe {
    uint32_t schema_version,sequence,render_tid,refresh_calls,read_status,stack_reference_count;
    uint32_t prefix_size,head_size,tail_size;
    uint64_t begin,end,capacity;
    uint8_t prefix[176],head[256],tail[256];
    SlCodeProbe code[SL_CODE_PROBES]; // five original roles, six stack contexts, registry getter, swap context
    SlStackModelsProbe models; // bounded read-only decoding of the known 0xA0 layout
    SlMinionProbe minions;
    SlSkinResourceProbe resources;
};
// Scalars and fixed arrays only. No function pointers, paths, memory read
// addresses or executable commands are accepted from the controller.
struct SlObserveShared {
    uint64_t magic;
    uint32_t version,struct_size,host_pid,target_pid;
    uint64_t nonce,creation_time,window;
    uint32_t request_session,render_tid,refresh_calls;
    volatile LONG command_lock,command_sequence,completed_sequence;
    uint32_t session_command,requested_skin,session_result,session_skin,session_state,session_form,forms_available;
    uint32_t session_encoded_skin;
    volatile LONG phase;
    volatile LONG startup_stage;
    uint32_t hook_remove;
    uint32_t fixture_tid,fixture_module_present,fixture_queries,fixture_exited,fixture_codec;
    uint32_t hook_tid,worker_tid,module_pinned,win32_error;
    uint32_t image_size,complete,gap_count,result_count;
    uint32_t snapshot_hash_present;
    uint8_t snapshot_hash[32];
    uint32_t player_read_status,player_read_error,player_stable,skin_fields_agree;
    uint32_t encoded_skin,decoded_skin,stack_skin;
    uint64_t player_address,stack_address;
    char model_name[96];
    uint64_t expected_bytes,copied_bytes,collection_ms;
    char error[192];
    SlObserveResult results[SL_OBSERVE_RULES];
    SlStackProbe stack_probe;
    uint32_t companions_available;
};
static_assert(sizeof(SlObserveShared)<256*1024,"Observation mapping unexpectedly large");
inline std::wstring sl_observe_name(uint32_t host_pid,uint64_t nonce) {
    return L"Local\\SkinLabObserve-"+std::to_wstring(host_pid)+L"-"+std::to_wstring(nonce);
}
inline uint64_t sl_filetime(FILETIME value) {return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
inline uint64_t sl_creation_time(HANDLE process) {
    FILETIME creation{},exit{},kernel{},user{};
    return GetProcessTimes(process,&creation,&exit,&kernel,&user)?sl_filetime(creation):0;
}
#endif
