// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Included in observe_hook.cpp's private namespace. No game writes or calls.
struct SessionProbeSites {
    uintptr_t image_base=0;
    uint32_t image_size=0;
    uint32_t stack_reference_count=0;
    uint32_t rvas[SL_CODE_PROBES]{};
};
SessionProbeSites session_probe_sites(const skinlab::profile::Snapshot& image,
                                      const std::vector<skinlab::profile::Result>& results) {
    SessionProbeSites sites{};sites.image_base=image.module_base;sites.image_size=image.image_size;
    for(const auto& r:results)if(r.has_candidate) {
        if(r.name=="character_data_stack") {
            // Structural diagnostics only: LEA reg64,[base+stack], any register.
            // Keep the first six code contexts and the total hit count; neither
            // uniqueness nor these byte matches admit a field or codec.
            for(const auto& region:image.regions)for(size_t i=0;i+7<=region.bytes.size();++i) {
                const auto* code=region.bytes.data()+i;uint32_t offset=0;std::memcpy(&offset,code+3,4);
                if((code[0]&0xf8)==0x48&&code[1]==0x8d&&(code[2]&0xc0)==0x80
                   &&(code[2]&7)!=4&&offset==r.candidate.field_offset) {
                    const auto index=sites.stack_reference_count++;
                    if(index<SL_STACK_CONTEXTS) {
                        const auto at=region.rva+uint32_t(i);sites.rvas[5+index]=at>=256?at-256:at;
                    }
                }
            }
        }
        if(r.name=="character_data_stack_update")sites.rvas[1]=r.candidate.rva;
        if(r.name=="material_registry_getter")sites.rvas[11]=r.candidate.rva;
        if(r.name=="swap_chain"&&!r.matches.empty())sites.rvas[12]=r.matches[0]>=64?r.matches[0]-64:r.matches[0];
        if(r.name=="character_data_stack_push") {
            sites.rvas[0]=r.candidate.rva;
            if(!r.matches.empty())sites.rvas[2]=r.matches[0]>=1024?r.matches[0]-1024:r.matches[0];
        }
    }
    // This code-coordinate is independently observed in 16.21's push body.
    // It identifies a direct callee for analysis, never permission to call it.
    uint8_t call[5]{};
    if(sites.rvas[0]&&sites.rvas[0]<=sites.image_size&&sites.image_size-sites.rvas[0]>0xa1
       &&local_copy(sites.image_base+sites.rvas[0]+0x9c,call,sizeof(call))&&call[0]==0xe8) {
        int32_t displacement=0;std::memcpy(&displacement,call+1,4);
        const int64_t rva=int64_t(sites.rvas[0])+0xa1+displacement;
        if(rva>0&&rva<sites.image_size)sites.rvas[3]=uint32_t(rva);
    }
    return sites;
}
void resolve_gear_probe(SessionProbeSites& sites,uintptr_t stack) {
    uintptr_t owner=0,table=0,entry=0,again=0;
    if(stack>UINTPTR_MAX-0x130||!local_copy(stack+0x130,&owner,8)
       ||!local_copy(owner,&table,8)||table>UINTPTR_MAX-0x38
       ||!local_copy(table+0x38,&entry,8)||!local_copy(stack+0x130,&again,8)||again!=owner)return;
    if(entry>sites.image_base&&entry-sites.image_base<sites.image_size)sites.rvas[4]=uint32_t(entry-sites.image_base);
}
void collect_code_probe(const SessionProbeSites& sites,SlCodeProbe (&codes)[SL_CODE_PROBES]) {
    uint8_t reread[sizeof(codes[0].bytes)]{};
    for(size_t i=0;i<SL_CODE_PROBES;++i) {
        auto& code=codes[i];code={};code.rva=sites.rvas[i];
        if(!sites.image_base||!code.rva||code.rva>=sites.image_size
           ||sites.image_base>UINTPTR_MAX-sites.image_size)continue;
        const auto size=std::min<uint32_t>(sizeof(code.bytes),sites.image_size-code.rva);
        if(local_copy(sites.image_base+code.rva,code.bytes,size)
           &&local_copy(sites.image_base+code.rva,reread,size)
           &&std::memcmp(code.bytes,reread,size)==0)code.size=size;
    }
}
void collect_stack_probe(const SlConfig& config,const SessionProbeSites& sites,
                         LONG sequence,uint32_t refresh_calls,SlStackProbe& out) {
    out={};out.schema_version=1;out.sequence=uint32_t(sequence);
    out.stack_reference_count=sites.stack_reference_count;
    out.render_tid=GetCurrentThreadId();out.refresh_calls=refresh_calls;
    // The core's callback validates player identity before any object read.
    if(config.generation(config.context)!=config.expected_generation) {out.read_status=4;return;}
    // Capture verified recipe sites even if the unverified stack header cannot
    // be interpreted, so an invalid candidate remains diagnosable.
    const auto object=reinterpret_cast<uintptr_t>(config.object_data);
    if(object>UINTPTR_MAX-config.layout.character_stack_offset) {out.read_status=2;return;}
    const auto stack=object+config.layout.character_stack_offset;
    auto expanded=sites;
    if(config.binding_kind==SL_BINDING_LEAGUE_EXPERIMENT)resolve_gear_probe(expanded,stack);
    collect_code_probe(expanded,out.code);
    if(!local_copy(stack,out.prefix,sizeof(out.prefix))) {out.read_status=2;return;}
    out.prefix_size=sizeof(out.prefix);
    // Only admit an ordered, bounded vector extent. Do not assume an entry
    // stride or follow pointers inside entries whose layouts are unverified.
    std::memcpy(&out.begin,out.prefix,8);
    std::memcpy(&out.end,out.prefix+8,8);
    std::memcpy(&out.capacity,out.prefix+16,8);
    if((!out.begin&&(out.end||out.capacity))||out.begin>out.end||out.end>out.capacity
       ||out.capacity-out.begin>65536) {out.read_status=3;return;}
    const auto count=uint32_t(std::min<uint64_t>(out.end-out.begin,sizeof(out.head)));
    if(count) {
        if(!local_copy(uintptr_t(out.begin),out.head,count)
           ||!local_copy(uintptr_t(out.end-count),out.tail,count)) {out.read_status=2;return;}
        out.head_size=count;out.tail_size=count;
    }
    uint8_t reread[256]{};
    bool readable=local_copy(stack,reread,sizeof(out.prefix));
    bool stable=readable&&std::memcmp(reread,out.prefix,sizeof(out.prefix))==0;
    if(readable&&stable&&count) {
        readable=local_copy(uintptr_t(out.begin),reread,count);
        stable=readable&&std::memcmp(reread,out.head,count)==0;
        if(stable) {
            readable=local_copy(uintptr_t(out.end-count),reread,count);
            stable=readable&&std::memcmp(reread,out.tail,count)==0;
        }
    }
    if(readable&&stable) {
        readable=local_copy(stack,reread,sizeof(out.prefix));
        stable=readable&&std::memcmp(reread,out.prefix,sizeof(out.prefix))==0;
    }
    if(config.generation(config.context)!=config.expected_generation) {
        out.read_status=4;out.head_size=out.tail_size=0;return;
    }
    if(!stable) {out.read_status=readable?5:2;out.head_size=out.tail_size=0;return;}
    out.read_status=1;
}
