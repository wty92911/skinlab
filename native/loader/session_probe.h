// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Included in observe_hook.cpp's private namespace. No game writes or calls.
struct SessionProbeSites {
    uintptr_t image_base=0;
    uint32_t image_size=0;
    uint32_t rvas[3]{};
};
void collect_stack_probe(const SlConfig& config,const SessionProbeSites& sites,
                         LONG sequence,uint32_t refresh_calls,SlStackProbe& out) {
    out={};out.schema_version=1;out.sequence=uint32_t(sequence);
    out.render_tid=GetCurrentThreadId();out.refresh_calls=refresh_calls;
    // The core's callback validates player identity before any object read.
    if(config.generation(config.context)!=config.expected_generation) {out.read_status=4;return;}
    // Capture verified recipe sites even if the unverified stack header cannot
    // be interpreted, so an invalid candidate remains diagnosable.
    for(size_t i=0;i<3;++i) {
        auto& code=out.code[i];code.rva=sites.rvas[i];
        if(!sites.image_base||!code.rva||code.rva>=sites.image_size
           ||sites.image_base>UINTPTR_MAX-sites.image_size)continue;
        const auto size=std::min<uint32_t>(sizeof(code.bytes),sites.image_size-code.rva);
        if(local_copy(sites.image_base+code.rva,code.bytes,size))code.size=size;
    }
    const auto object=reinterpret_cast<uintptr_t>(config.object_data);
    if(object>UINTPTR_MAX-config.layout.character_stack_offset) {out.read_status=2;return;}
    const auto stack=object+config.layout.character_stack_offset;
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
    const bool readable=local_copy(stack,reread,sizeof(out.prefix));
    bool stable=readable&&std::memcmp(reread,out.prefix,sizeof(out.prefix))==0;
    if(readable&&stable&&count) {
        stable=local_copy(uintptr_t(out.begin),reread,count)&&std::memcmp(reread,out.head,count)==0;
        if(stable)stable=local_copy(uintptr_t(out.end-count),reread,count)&&std::memcmp(reread,out.tail,count)==0;
    }
    if(config.generation(config.context)!=config.expected_generation) {
        out.read_status=4;out.head_size=out.tail_size=0;return;
    }
    if(!stable) {out.read_status=readable?5:2;out.head_size=out.tail_size=0;return;}
    out.read_status=1;
}
