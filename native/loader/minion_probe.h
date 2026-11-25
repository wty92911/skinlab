// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Bounded read-only discovery. Pointer-shaped manager fields and resource model
// matches are candidates, never a verified enumeration, spawn ID or owner.
void collect_minion_probe(const SlConfig& config,uintptr_t slot,uint32_t slot_rva,
                          uintptr_t context,uint32_t context_rva,uintptr_t callee,uint32_t callee_rva,
                          const char* champion,SlMinionProbe& out,const WeakSkinSource* sources=nullptr) {
    out={};out.slot_rva=slot_rva;out.context_rva=context_rva;out.callee_rva=callee_rva;
    out.player=reinterpret_cast<uintptr_t>(config.object_data);
    if(!slot)return;
    if(config.generation(config.context)!=config.expected_generation){out.read_status=4;return;}
    uint8_t code_again[sizeof(out.context)]{};
    if(context&&local_copy(context,out.context,sizeof(out.context))&&local_copy(context,code_again,sizeof(code_again))
       &&!std::memcmp(out.context,code_again,sizeof(code_again)))out.context_size=sizeof(out.context);
    if(callee&&local_copy(callee,out.callee,sizeof(out.callee))&&local_copy(callee,code_again,sizeof(code_again))
       &&!std::memcmp(out.callee,code_again,sizeof(code_again)))out.callee_size=sizeof(out.callee);
    uintptr_t manager=0;
    if(!local_copy(slot,&manager,8)||!local_copy(manager,out.manager_prefix,sizeof(out.manager_prefix))){out.read_status=2;return;}
    out.manager=manager;
    uint8_t player_again[sizeof(out.player_prefix)]{};
    if(local_copy(reinterpret_cast<uintptr_t>(config.object_data),out.player_prefix,sizeof(out.player_prefix))
       &&local_copy(reinterpret_cast<uintptr_t>(config.object_data),player_again,sizeof(player_again))
       &&!std::memcmp(out.player_prefix,player_again,sizeof(player_again)))out.player_size=sizeof(out.player_prefix);
    else std::memset(out.player_prefix,0,sizeof(out.player_prefix));
    const auto started=GetTickCount64();uintptr_t seen[1024]{};size_t seen_count=0;
    for(uint32_t offset=0;offset<sizeof(out.manager_prefix);offset+=8) {
        if(GetTickCount64()-started>40){out.budget_exhausted=1;break;}
        uintptr_t address=0;std::memcpy(&address,out.manager_prefix+offset,8);
        if(address<0x10000||address>=0x800000000000ULL||(address&7))continue;
        auto& array=out.arrays[out.array_count];uint8_t again[sizeof(array.bytes)]{};
        if(!local_copy(address,array.bytes,sizeof(array.bytes))||!local_copy(address,again,sizeof(again))
           ||std::memcmp(array.bytes,again,sizeof(again)))continue;
        array.address=address;array.field_offset=offset;array.size=sizeof(array.bytes);++out.array_count;
        for(size_t i=0;i<sizeof(array.bytes);i+=8) {
            if(out.tries>=1024||GetTickCount64()-started>40){out.budget_exhausted=1;break;}
            uintptr_t object=0;std::memcpy(&object,array.bytes+i,8);
            if(object<0x10000||object>=0x800000000000ULL||(object&7)||object==reinterpret_cast<uintptr_t>(config.object_data))continue;
            bool duplicate=false;for(size_t j=0;j<seen_count;++j)if(seen[j]==object){duplicate=true;break;}
            if(duplicate)continue;
            seen[seen_count++]=object;++out.tries;
            if(object>UINTPTR_MAX-config.layout.character_stack_offset-0x28)continue;
            const auto stack=object+config.layout.character_stack_offset;
            uintptr_t name=0;uint32_t length=0;char model[96]{};
            if(!local_copy(stack+0x18,&name,8)||!local_copy(stack+0x20,&length,4)||!length||length>=sizeof(model)
               ||!local_copy(name,model,size_t(length)+1)||model[length]||std::memchr(model,0,length))continue;
            bool candidate=false;
            for(const auto& rule:SummonSkinSync::resource_rules)
                if(!std::strcmp(champion,rule.champion)&&!std::strcmp(model,rule.model)){candidate=true;break;}
            if(!candidate||out.object_count>=4)continue;
            auto& sample=out.objects[out.object_count++];sample.address=object;std::memcpy(sample.model,model,size_t(length)+1);
            if(!local_copy(object,sample.prefix,sizeof(sample.prefix))){sample.read_status=2;continue;}
            uint8_t reread[sizeof(sample.prefix)]{};
            if(!local_copy(object,reread,sizeof(reread))){std::memset(sample.prefix,0,sizeof(sample.prefix));sample.read_status=2;continue;}
            if(std::memcmp(sample.prefix,reread,sizeof(reread))){std::memset(sample.prefix,0,sizeof(sample.prefix));sample.read_status=5;continue;}
            uintptr_t captured_name=0;uint32_t captured_length=0;char model_again[sizeof(model)]{};
            // Bind the name to this captured prefix instead of a prior read
            // through an address the game may have replaced in the meantime.
            const auto at=size_t(config.layout.character_stack_offset)+0x18;
            if(at+12>sizeof(sample.prefix)){
                std::memset(sample.prefix,0,sizeof(sample.prefix));sample.read_status=3;continue;
            }
            std::memcpy(&captured_name,sample.prefix+at,8);std::memcpy(&captured_length,sample.prefix+at+8,4);
            if(captured_name!=name||captured_length!=length||!local_copy(name,model_again,size_t(length)+1)
               ||std::memcmp(model,model_again,size_t(length)+1)){
                std::memset(sample.prefix,0,sizeof(sample.prefix));sample.read_status=5;continue;
            }
            sample.prefix_size=sizeof(sample.prefix);sample.read_status=1;
        }
        if(out.budget_exhausted)break;
    }
    uintptr_t after=0;uint8_t reread[sizeof(out.manager_prefix)]{};
    if(config.generation(config.context)!=config.expected_generation){out={};out.read_status=4;return;}
    if(!local_copy(slot,&after,8)||!local_copy(manager,reread,sizeof(reread))){out={};out.read_status=2;return;}
    if(after!=manager||std::memcmp(out.manager_prefix,reread,sizeof(reread))){out={};out.read_status=5;return;}
    // Report membership in the captured pointer/count header separately from
    // discovery in capacity/pool tails. This alone is not a lifetime lock.
    uintptr_t data=0;uint32_t count=0,capacity=0;
    std::memcpy(&data,out.manager_prefix+8,8);std::memcpy(&count,out.manager_prefix+16,4);std::memcpy(&capacity,out.manager_prefix+20,4);
    if(sources)out.source_member=sources->member;
    if(count<=capacity&&capacity<=4096)for(uint32_t i=0;i<out.array_count;++i) {
        const auto& array=out.arrays[i];
        if(array.field_offset!=8||array.address!=data||count>array.size/8)continue;
        for(uint32_t j=0;j<out.object_count;++j) {
            auto& object=out.objects[j];
            for(uint32_t k=0;k<count;++k) {
                uintptr_t item=0;std::memcpy(&item,array.bytes+size_t(k)*8,8);
                if(item==object.address){object.counted_member=1;break;}
            }
            if(object.counted_member&&sources) {
                const auto link=sources->read(uintptr_t(object.address));
                object.source_status=link.status;object.source_strong=link.strong;
                object.source_cell=link.cell;object.source_control=link.control;object.source_object=link.source;
            }
        }
    }
    out.read_status=1;
}
