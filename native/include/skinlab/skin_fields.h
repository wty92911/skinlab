// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_SKIN_FIELDS_H
#define SKINLAB_SKIN_FIELDS_H
#include <skinlab/api.h>
#include <skinlab/skin_codec.h>
#include <cstring>
#include <limits>

// Storage, callbacks and identity changes are serialized by the frame adapter.
// A generation check does not make arbitrary/stale memory safe to dereference.
namespace skinlab { namespace fields {
struct Snapshot {uint32_t encoded,skin;};
inline bool same(const Snapshot& a,const Snapshot& b) {return a.encoded==b.encoded&&a.skin==b.skin;}
inline SlStatus validate(const SlConfig& config) {
    if(config.struct_size<sizeof(config))return SL_INVALID_ARGUMENT;
    if(config.abi_version!=SL_ABI_VERSION)return SL_ABI_MISMATCH;
    if(!codec::valid(config.layout.codec_kind,config.layout.rotation_a,config.layout.rotation_b))return SL_INVALID_ARGUMENT;
    if(config.binding_kind!=SL_BINDING_LOCAL_FIXTURE&&config.binding_kind!=SL_BINDING_LEAGUE_EXPERIMENT)return SL_UNSUPPORTED_BINDING;
    if(!config.object_data||!config.expected_generation||!config.generation||!config.skin_allowed||!config.refresh)return SL_INVALID_ARGUMENT;
    const uint64_t encoded=config.layout.object_skin_offset;
    const uint64_t stack=uint64_t(config.layout.character_stack_offset)+config.layout.stack_skin_offset;
    const uint64_t end=(encoded>stack?encoded:stack)+sizeof(uint32_t);
    if(end>config.object_size||end>std::numeric_limits<uintptr_t>::max()-reinterpret_cast<uintptr_t>(config.object_data)
       ||(encoded<stack+4&&stack<encoded+4))return SL_LAYOUT_OUT_OF_RANGE;
    return SL_OK;
}
inline bool alive(const SlConfig& config) {return config.generation(config.context)==config.expected_generation;}
inline Snapshot read(const SlConfig& config) {
    const auto* data=static_cast<const uint8_t*>(config.object_data);Snapshot out{};
    std::memcpy(&out.encoded,data+config.layout.object_skin_offset,4);
    std::memcpy(&out.skin,data+size_t(config.layout.character_stack_offset)+config.layout.stack_skin_offset,4);
    return out;
}
inline void write(const SlConfig& config,const Snapshot& value) {
    auto* data=static_cast<uint8_t*>(config.object_data);
    std::memcpy(data+config.layout.object_skin_offset,&value.encoded,4);
    std::memcpy(data+size_t(config.layout.character_stack_offset)+config.layout.stack_skin_offset,&value.skin,4);
}
inline Snapshot encoded(const SlLayout& layout,uint32_t skin) {
    return {codec::encode(skin,layout.codec_kind,layout.rotation_a,layout.xor_key,layout.rotation_b,layout.add_key,layout.xor_key_second),skin};
}
inline uint32_t decoded(const SlLayout& layout,const Snapshot& value) {
    return codec::decode(value.encoded,layout.codec_kind,layout.rotation_a,layout.xor_key,layout.rotation_b,layout.add_key,layout.xor_key_second);
}
inline bool refresh(const SlConfig& config) {
    auto* stack=static_cast<uint8_t*>(config.object_data)+config.layout.character_stack_offset;
    return config.refresh(config.context,stack,1)!=0;
}
// Both the hero core and dependent objects use this exact update/rollback path.
struct Result {SlStatus status;bool target_lost=false;};
inline Result replace(const SlConfig& config,const Snapshot& before,const Snapshot& desired) {
    if(!alive(config))return {SL_TARGET_CHANGED};
    write(config,desired);
    const bool refreshed=refresh(config);
    if(!alive(config))return {SL_RECOVERY_REQUIRED,true};
    if(refreshed&&same(read(config),desired))return {SL_OK};
    write(config,before);
    if(!refresh(config)||!alive(config)||!same(read(config),before))return {SL_RECOVERY_REQUIRED};
    return {SL_UPDATE_FAILED};
}
inline SlStatus restore(const SlConfig& config,const Snapshot& original) {
    if(!alive(config))return SL_TARGET_CHANGED;
    write(config,original);
    return refresh(config)&&alive(config)&&same(read(config),original)?SL_OK:SL_RECOVERY_REQUIRED;
}
} }
#endif
