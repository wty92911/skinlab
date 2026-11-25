// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Read-only follow-up to the observed update function's resource callbacks.
// No callbacks are invoked, and no cache/reference counts are changed.
void collect_skin_resource_probe(const SlConfig& config,uintptr_t base,uint32_t size,
                                 uint32_t update,SlSkinResourceProbe& out,
                                 SlCodeProbe (&code)[SL_CODE_PROBES]) {
    out={};for(auto& entry:code)entry={};
    if(!skinlab::fields::alive(config)){out.read_status=4;return;}
    if(base>UINTPTR_MAX-size||!update||update>=size||size-update<0x616){out.read_status=3;return;}
    // These are instruction positions inside the independently resolved update,
    // not object offsets. Admit the member/virtual slots only for this body.
    if(!SkinResourceUpdate::admitted(base,size,update)){out.read_status=3;return;}
    out.owner_member=0x130;
    const auto object=reinterpret_cast<uintptr_t>(config.object_data);
    if(object>UINTPTR_MAX-config.layout.character_stack_offset-0x138){out.read_status=3;return;}
    const auto stack=object+config.layout.character_stack_offset;
    uint8_t cache[sizeof(out.cache)]{},again[sizeof(out.cache)]{};
    uintptr_t table=0,table_after=0,entries[11]{},entries_after[11]{};
    if(!local_copy(stack+0xb0,cache,sizeof(cache))){out.read_status=2;return;}
    uintptr_t owner=0;std::memcpy(&owner,cache+0x80,8);
    if(!local_copy(owner,&table,8)||!local_copy(table,entries,sizeof(entries))){out.read_status=2;return;}
    constexpr uint32_t slots[]={0x18,0x20,0x08,0x10,0x28,0x30,0x38,0x48,0x50};
    for(size_t i=0;i<9;++i) {
        out.targets[i]=entries[slots[i]/8];
        if(out.targets[i]<=base||out.targets[i]-base>=size)continue;
        auto& sample=code[i];sample.rva=uint32_t(out.targets[i]-base);
        const auto length=std::min<uint32_t>(sizeof(sample.bytes),size-sample.rva);
        uint8_t bytes[sizeof(sample.bytes)]{};
        if(local_copy(out.targets[i],sample.bytes,length)&&local_copy(out.targets[i],bytes,length)
           &&!std::memcmp(sample.bytes,bytes,length))sample.size=length;
    }
    // Fixed instruction sites in the observed callbacks, for analysis only.
    // These derived callees are sampled, never invoked.
    struct Follow {size_t from,to,offset;uint8_t opcode;};
    constexpr Follow follows[]={{1,9,0x72,0xe8},{4,10,0x0e,0xe9},{0,11,0xd8,0xe8}};
    for(const auto& follow:follows) {
        const auto& entry=code[follow.from];
        if(entry.size<follow.offset+5||entry.bytes[follow.offset]!=follow.opcode)continue;
        int32_t relative=0;std::memcpy(&relative,entry.bytes+follow.offset+1,4);
        const int64_t target=int64_t(entry.rva)+int64_t(follow.offset)+5+relative;
        if(target<=0||target>=size)continue;
        auto& sample=code[follow.to];sample.rva=uint32_t(target);
        const auto length=std::min<uint32_t>(sizeof(sample.bytes),size-sample.rva);
        uint8_t bytes[sizeof(sample.bytes)]{};
        if(local_copy(base+sample.rva,sample.bytes,length)&&local_copy(base+sample.rva,bytes,length)
           &&!std::memcmp(sample.bytes,bytes,length))sample.size=length;
    }
    if(!skinlab::fields::alive(config)){out={};for(auto& entry:code)entry={};out.read_status=4;return;}
    if(!local_copy(stack+0xb0,again,sizeof(again))||std::memcmp(cache,again,sizeof(cache))
       ||!local_copy(owner,&table_after,8)||table_after!=table
       ||!local_copy(table,entries_after,sizeof(entries_after))||std::memcmp(entries,entries_after,sizeof(entries))
       ||!SkinResourceUpdate::admitted(base,size,update)) {
        out={};for(auto& entry:code)entry={};out.read_status=5;return;
    }
    std::memcpy(out.cache,cache,sizeof(cache));out.owner=owner;out.table=table;out.read_status=1;
}
