// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Game-owned transformation layers. Never push, pop, rename or retain them:
// the game may replace/reallocate the vector on every transformation.
struct FormSkinSync {
    static constexpr size_t stride=skinlab::league::kStackLayerStride;
    static constexpr size_t skin_offset=skinlab::league::kStackLayerSkinOffset,max_layers=65536/stride;
    struct Rule {const char* champion;const char* alternate;};
    // Each alternate has a CharacterRecord referencing the base champion and
    // skins/skinN.bin for every catalog skin/chroma in the local WADs. This
    // covers game-created model layers; gear and borrowed models are separate.
    inline static constexpr Rule rules[]={
        {"Nidalee","NidaleeCougar"},
        {"Elise","EliseSpider"},
        {"Gnar","GnarBig"},
        {"Shyvana","ShyvanaDragon"},
        {"Kled","KledRider"},
    };
    const char* champion=nullptr;

    bool enabled() const {
        if(champion)for(const auto& rule:rules)if(!std::strcmp(champion,rule.champion))return true;
        return false;
    }
    bool accepts(const char* model) const {
        if(champion)for(const auto& rule:rules)
            if(!std::strcmp(champion,rule.champion)
               &&(!std::strcmp(model,rule.champion)||!std::strcmp(model,rule.alternate)))return true;
        return false;
    }
    static bool writable(uintptr_t address) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)))return false;
        const auto protection=info.Protect;
        return info.State==MEM_COMMIT&&!(protection&(PAGE_GUARD|PAGE_NOACCESS))
            &&(protection&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
            &&info.RegionSize>=sizeof(uint32_t)
            &&address-reinterpret_cast<uintptr_t>(info.BaseAddress)<=info.RegionSize-sizeof(uint32_t);
    }
    SlStatus synchronize(const SlConfig& config,uintptr_t stack,uint32_t skin,bool& changed,bool commit=true) const {
        changed=false;
        if(!enabled())return SL_OK;
        if(config.generation(config.context)!=config.expected_generation)return SL_TARGET_CHANGED;
        uintptr_t header[3]{};
        if(!local_copy(stack,header,sizeof(header))||header[0]>header[1]||header[1]>header[2]
           ||(!header[0]&&header[2])||header[2]-header[0]>65536
           ||(header[1]-header[0])%stride||(header[2]-header[0])%stride)return SL_INCONSISTENT_STATE;
        if(header[0]==header[1])return SL_OK;
        struct Patch {uintptr_t entry;uint8_t prefix[skin_offset+sizeof(uint32_t)];};
        Patch patches[max_layers]{};size_t count=0;
        for(uintptr_t entry=header[0];entry<header[1];entry+=stride) {
            Patch patch{};patch.entry=entry;
            if(!local_copy(entry,patch.prefix,sizeof(patch.prefix)))return SL_INCONSISTENT_STATE;
            uintptr_t name=0;uint32_t length=0,current=0;
            std::memcpy(&name,patch.prefix,8);std::memcpy(&length,patch.prefix+8,4);
            std::memcpy(&current,patch.prefix+skin_offset,4);
            char model[32]{};
            if(!length||length>=sizeof(model))continue;
            if(!local_copy(name,model,size_t(length)+1)||model[length])return SL_INCONSISTENT_STATE;
            if(!accepts(model)||current==skin)continue;
            if(!writable(entry+skin_offset))return SL_INCONSISTENT_STATE;
            patches[count++]=patch;
        }
        // Validate all candidates before the first write; no stale layer address
        // survives this call. The adapter invokes this only on the frame thread.
        uintptr_t after[3]{};
        if(config.generation(config.context)!=config.expected_generation)return SL_TARGET_CHANGED;
        if(!local_copy(stack,after,sizeof(after))||std::memcmp(header,after,sizeof(header)))return SL_INCONSISTENT_STATE;
        for(size_t i=0;i<count;++i) {
            uint8_t prefix[sizeof(Patch::prefix)]{};
            if(!local_copy(patches[i].entry,prefix,sizeof(prefix))
               ||std::memcmp(prefix,patches[i].prefix,sizeof(prefix)))return SL_INCONSISTENT_STATE;
        }
        changed=count!=0;
        if(commit)for(size_t i=0;i<count;++i)
            std::memcpy(reinterpret_cast<void*>(patches[i].entry+skin_offset),&skin,sizeof(skin));
        return SL_OK;
    }
};
