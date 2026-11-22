// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// A command-scoped companion to the core's skin transaction. No engine calls;
// the caller runs synchronize before/after its existing frame-thread refresh.
struct SkinGearSync {
    // 16.23 update selects stack+0x18 (or the top 0xA0-byte layer), then
    // reads its +0x84 gear byte. This is a member offset, not a code RVA.
    static constexpr uintptr_t gear_offset=0x18+0x84;
    static constexpr uint8_t default_gear=0xff,gear_count=6;
    const char* champion=nullptr;
    DWORD thread=0;
    uint32_t original_skin=0,before_skin=0,target_skin=0;
    uint8_t original_gear=default_gear,before_gear=default_gear,target_gear=default_gear;
    bool initialized=false,transaction=false,restoring=false;

    bool enabled() const {return champion&&!std::strcmp(champion,"Katarina");}
    static bool special(uint32_t skin) {return skin>=29&&skin<=36;}
    SlStatus guard(const SlConfig& config,uintptr_t stack) const {
        if(thread!=GetCurrentThreadId())return SL_WRONG_THREAD;
        if(stack!=reinterpret_cast<uintptr_t>(config.object_data)+config.layout.character_stack_offset)return SL_INVALID_ARGUMENT;
        return config.generation(config.context)==config.expected_generation?SL_OK:SL_TARGET_CHANGED;
    }
    SlStatus read(const SlConfig& config,uintptr_t stack,uint32_t& skin,uint8_t& gear) const {
        const auto status=guard(config,stack);if(status!=SL_OK)return status;
        uintptr_t name=0;char model[sizeof("Katarina")]{};
        if(!local_copy(stack+skinlab::league::kStackModelPointerOffset,&name,sizeof(name))
           ||!local_copy(name,model,sizeof(model))||std::memcmp(model,"Katarina",sizeof(model))
           ||!local_copy(stack+config.layout.stack_skin_offset,&skin,sizeof(skin))
           ||!local_copy(stack+gear_offset,&gear,sizeof(gear)))return SL_INCONSISTENT_STATE;
        return guard(config,stack);
    }
    SlStatus initialize(const SlConfig& config,uintptr_t stack) {
        if(!enabled())return SL_OK;
        if(initialized)return guard(config,stack);
        thread=GetCurrentThreadId();
        const auto status=read(config,stack,original_skin,original_gear);
        if(status==SL_OK)initialized=true;
        return status;
    }
    SlStatus begin(const SlConfig& config,uintptr_t stack,uint32_t selected,bool restore=false) {
        if(!enabled())return SL_OK;
        if(!initialized)return SL_INVALID_ARGUMENT;
        if(transaction)return SL_BUSY;
        const auto status=read(config,stack,before_skin,before_gear);if(status!=SL_OK)return status;
        if(restore&&selected!=original_skin)return SL_INVALID_ARGUMENT;
        target_skin=selected;restoring=restore;target_gear=before_gear;
        if(restore)target_gear=original_gear;
        else if(special(selected))target_gear=before_gear<gear_count?before_gear:0;
        else if(special(before_skin))target_gear=special(original_skin)?default_gear:original_gear;
        transaction=true;return SL_OK;
    }
    SlStatus synchronize(const SlConfig& config,uintptr_t stack,bool& changed,bool commit=true) const {
        changed=false;
        if(!enabled())return SL_OK;
        if(!initialized||!transaction)return SL_INVALID_ARGUMENT;
        uint32_t skin=0;uint8_t gear=0;
        auto status=read(config,stack,skin,gear);if(status!=SL_OK)return status;
        uint8_t desired=0;
        if(skin==target_skin) {
            desired=target_gear;
            // The game can naturally advance the six knives. Only initialize
            // an invalid gear; a restore and a rollback require exact bytes.
            if(!restoring&&special(skin)&&gear<gear_count)desired=gear;
        } else if(skin==before_skin)desired=before_gear;
        else return SL_INCONSISTENT_STATE;
        changed=gear!=desired;
        if(!changed||!commit)return SL_OK;
        // No observed rule for game-owned extra layers: do not patch through
        // them or reinterpret their gear as the base layer's gear.
        uintptr_t header[3]{};
        if(!local_copy(stack,header,sizeof(header))||header[0]!=header[1]
           ||header[1]>header[2]||(!header[0]&&header[2])||header[2]-header[0]>65536
           ||(header[2]-header[0])%skinlab::league::kStackLayerStride)return SL_INCONSISTENT_STATE;
        MEMORY_BASIC_INFORMATION info{};const auto address=stack+gear_offset;
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||info.State!=MEM_COMMIT
           ||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
           ||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return SL_INCONSISTENT_STATE;
        uint32_t after_skin=0;uint8_t after_gear=0;
        status=read(config,stack,after_skin,after_gear);if(status!=SL_OK)return status;
        if(after_skin!=skin||after_gear!=gear)return SL_INCONSISTENT_STATE;
        std::memcpy(reinterpret_cast<void*>(address),&desired,sizeof(desired));
        status=read(config,stack,after_skin,after_gear);if(status!=SL_OK)return status;
        return after_skin==skin&&after_gear==desired?SL_OK:SL_RECOVERY_REQUIRED;
    }
    void end() {transaction=false;}
};
