// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_SUMMON_SKIN_H
#define SKINLAB_SUMMON_SKIN_H
// Included in the adapter's private namespace, after skin_fields.h and
// league_fields.h have been included globally, and local_copy is defined.

// Shared synchronizer for frame-owned enumeration/owner adapters. Do not
// construct Views from guessed manager/owner offsets. Views
// and their callback contexts must stay live and serialized for this call;
// nothing in them is retained between frames. Only the frame thread may call.
struct SummonSkinSync {
    struct Rule {const char* champion;const char* model;};
    // Local 16.23 WAD CharacterRecords carry Minion_Summon, and every catalog
    // skin/chroma has a same-number SkinCharacterDataProperties record. These
    // are resource associations, not a claim of runtime support/enumeration.
    inline static constexpr Rule resource_rules[]={
        {"Malzahar","MalzaharVoidling"},{"Naafiri","NaafiriPackmate"},
        {"Ivern","IvernMinion"},{"Yorick","YorickGhoulMelee"},{"Yorick","YorickBigGhoul"},
        {"Annie","AnnieTibbers"},{"Elise","EliseSpiderling"},
        {"Heimerdinger","HeimerTYellow"},{"Heimerdinger","HeimerTBlue"},
        {"Zyra","ZyraThornPlant"},{"Zyra","ZyraGraspingPlant"},{"Zyra","ZyraSeed"},
        {"Shaco","ShacoBox"},{"Belveth","BelvethVoidling"},{"Belveth","BelvethSpore"},
        {"Maokai","MaokaiSproutling"},{"Aphelios","ApheliosTurret"},{"Kalista","KalistaSpawn"},
        {"Kled","KledMount"},{"Zac","ZacRebirthBloblet"},
    };
    struct View {
        uint64_t owner=0; // resolved identity, never team or model-name matching
        char model[48]{};
        SlConfig config{}; // generation uniquely identifies this spawn
    };
    static constexpr size_t maximum=256;
    DWORD thread=0;
    uint64_t owner_identity=0;
    void* owner_object=nullptr;
    char champion[32]{};
    uint32_t original_skin=0;
    bool initialized=false,recovery=false,busy=false;
    struct Call {
        bool& busy;
        explicit Call(bool& value):busy(value){busy=true;}
        ~Call(){busy=false;}
    };

    bool accepts(const char* model) const {
        for(const auto& rule:resource_rules)
            if(!std::strcmp(champion,rule.champion)&&!std::strcmp(model,rule.model))return true;
        return false;
    }
    SlStatus guard(const SlConfig& owner) const {
        if(!initialized)return SL_INVALID_ARGUMENT;
        if(thread!=GetCurrentThreadId())return SL_WRONG_THREAD;
        if(owner.object_data!=owner_object||owner.expected_generation!=owner_identity)return SL_TARGET_CHANGED;
        const auto status=skinlab::fields::validate(owner);
        if(status!=SL_OK)return status;
        return skinlab::fields::alive(owner)?SL_OK:SL_TARGET_CHANGED;
    }
    SlStatus initialize(const SlConfig& owner,const char* model) {
        if(initialized||busy)return SL_BUSY;
        Call call(busy);
        if(!model||std::strlen(model)>=sizeof(champion))return SL_INVALID_ARGUMENT;
        const auto status=skinlab::fields::validate(owner);
        if(status!=SL_OK)return status;
        if(!skinlab::fields::alive(owner))return SL_TARGET_CHANGED;
        const auto before=skinlab::fields::read(owner);
        if(skinlab::fields::decoded(owner.layout,before)!=before.skin)return SL_INCONSISTENT_STATE;
        // Initialize before the first hero apply. A later-born pet may already
        // inherit the selected local skin; restoring its birth snapshot would
        // leave it changed. Restoration follows the owner's original number.
        original_skin=before.skin;owner_identity=owner.expected_generation;owner_object=owner.object_data;
        std::strcpy(champion,model);thread=GetCurrentThreadId();initialized=true;return SL_OK;
    }
    static bool writable(uintptr_t address) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)))return false;
        return info.State==MEM_COMMIT&&!(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
            &&(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
            &&info.RegionSize>=4&&address-reinterpret_cast<uintptr_t>(info.BaseAddress)<=info.RegionSize-4;
    }
    static SlStatus inspect(const View& view,skinlab::fields::Snapshot& out,bool consistent=true) {
        const auto& cfg=view.config;auto result=skinlab::fields::validate(cfg);
        if(result!=SL_OK)return result;
        if(cfg.layout.stack_skin_offset!=skinlab::league::kStackSkinOffset)return SL_UNSUPPORTED_BINDING;
        if(!skinlab::fields::alive(cfg))return SL_TARGET_CHANGED;
        const auto object=reinterpret_cast<uintptr_t>(cfg.object_data);
        const auto stack=object+cfg.layout.character_stack_offset;
        uintptr_t name=0,header[3]{};uint32_t length=0;char model[sizeof(View::model)]{};
        if(!local_copy(stack,header,sizeof(header))||header[0]!=header[1]||header[1]>header[2]
           ||(!header[0]&&header[2])||header[2]-header[0]>65536
           ||(header[2]-header[0])%skinlab::league::kStackLayerStride
           ||!local_copy(stack+skinlab::league::kStackModelPointerOffset,&name,8)
           ||!local_copy(stack+skinlab::league::kStackModelPointerOffset+8,&length,4)
           ||!length||length>=sizeof(model)||!local_copy(name,model,size_t(length)+1)
           ||model[length]||std::strcmp(model,view.model)
           ||!local_copy(object+cfg.layout.object_skin_offset,&out.encoded,4)
           ||!local_copy(stack+cfg.layout.stack_skin_offset,&out.skin,4)
           ||(consistent&&skinlab::fields::decoded(cfg.layout,out)!=out.skin)
           ||!writable(object+cfg.layout.object_skin_offset)||!writable(stack+cfg.layout.stack_skin_offset))return SL_INCONSISTENT_STATE;
        return skinlab::fields::alive(cfg)?SL_OK:SL_TARGET_CHANGED;
    }
    SlStatus synchronize(const SlConfig& owner,const View* views,size_t count,bool restoring=false) {
        if(initialized&&thread!=GetCurrentThreadId())return SL_WRONG_THREAD;
        if(busy)return SL_BUSY;
        Call call(busy);
        auto result=guard(owner);if(result!=SL_OK)return result;
        if(recovery&&!restoring)return SL_RECOVERY_REQUIRED;
        if(count>maximum||(!views&&count))return SL_INVALID_ARGUMENT;
        const auto selected=skinlab::fields::read(owner);
        if(skinlab::fields::decoded(owner.layout,selected)!=selected.skin)return SL_INCONSISTENT_STATE;
        if(restoring&&selected.skin!=original_skin)return SL_INCONSISTENT_STATE;
        struct Patch {const View* view=nullptr;skinlab::fields::Snapshot before{},desired{};};
        Patch patches[maximum]{};size_t used=0;
        auto same_owner=[&]() {
            const auto status=guard(owner);
            if(status!=SL_OK)return status;
            return skinlab::fields::same(skinlab::fields::read(owner),selected)?SL_OK:SL_INCONSISTENT_STATE;
        };
        // Validate the entire batch before the first write. Source adapters
        // must enumerate all live objects, without truncation, on this frame.
        for(size_t i=0;i<count;++i) {
            const auto& view=views[i];
            if(view.owner!=owner_identity)continue;
            if(!std::memchr(view.model,0,sizeof(view.model)))return SL_INCONSISTENT_STATE;
            if(!accepts(view.model))continue;
            if(view.config.object_data==owner_object)return SL_INCONSISTENT_STATE;
            for(size_t j=0;j<used;++j)
                if(patches[j].view->config.object_data==view.config.object_data
                   ||patches[j].view->config.expected_generation==view.config.expected_generation)return SL_INCONSISTENT_STATE;
            Patch patch{};patch.view=&view;result=inspect(view,patch.before,!restoring);
            if(result!=SL_OK)return result;
            if(selected.skin>INT32_MAX||!view.config.skin_allowed(view.config.context,int32_t(selected.skin)))return SL_INVALID_SKIN;
            patch.desired=skinlab::fields::encoded(view.config.layout,selected.skin);patches[used++]=patch;
        }
        // Recheck after skin_allowed callbacks, before changing any pet.
        result=same_owner();if(result!=SL_OK)return result;
        for(size_t i=0;i<used;++i) {
            skinlab::fields::Snapshot current{};result=inspect(*patches[i].view,current,!restoring);
            if(result!=SL_OK)return result;
            if(!skinlab::fields::same(current,patches[i].before))return SL_INCONSISTENT_STATE;
        }
        size_t completed=0;
        for(;completed<used;++completed) {
            const auto& patch=patches[completed];
            if(skinlab::fields::same(patch.before,patch.desired))continue;
            result=same_owner();
            skinlab::fields::Snapshot current{};
            if(result==SL_OK)result=inspect(*patch.view,current,!restoring);
            if(result==SL_OK&&!skinlab::fields::same(current,patch.before))result=SL_INCONSISTENT_STATE;
            if(result==SL_OK)result=skinlab::fields::replace(patch.view->config,patch.before,patch.desired).status;
            if(result!=SL_OK)break;
        }
        if(result==SL_OK) {
            result=same_owner();
            if(result==SL_OK)for(size_t i=0;i<used;++i) {
                skinlab::fields::Snapshot current{};result=inspect(*patches[i].view,current);
                if(result!=SL_OK||!skinlab::fields::same(current,patches[i].desired)) {
                    if(result==SL_OK)result=SL_INCONSISTENT_STATE;
                    break;
                }
            }
            if(result==SL_OK)result=same_owner();
        }
        if(result==SL_OK){if(restoring)recovery=false;return SL_OK;}
        // Roll back previous pets in reverse order. Never retain their pointers
        // for a later frame; an explicit restore uses a fresh enumeration.
        bool rolled_back=true;
        while(completed) {
            const auto& patch=patches[--completed];
            if(skinlab::fields::same(patch.before,patch.desired))continue;
            skinlab::fields::Snapshot current{};
            if(inspect(*patch.view,current)!=SL_OK||!skinlab::fields::same(current,patch.desired)
               ||skinlab::fields::restore(patch.view->config,patch.before)!=SL_OK)rolled_back=false;
        }
        recovery=true;
        return rolled_back&&result!=SL_RECOVERY_REQUIRED?result:SL_RECOVERY_REQUIRED;
    }
};
#endif
