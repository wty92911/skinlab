// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Frame-owned adapter for the counted AI-minion vector and verified weak skin
// source. No candidate/pool-tail enumeration and no cross-frame pet pointers.
struct MinionSkinBinding {
    struct Header {uintptr_t data=0;uint32_t count=0,capacity=0;};
    static constexpr size_t maximum_units=4096;
    uintptr_t slot=0;
    WeakSkinSource source{};
    SummonSkinSync skins{};
    char champion[32]{};
    DWORD expected_thread=0;
    void* refresh_context=nullptr;
    SlRefresh refresh_function=nullptr;
    uint64_t serial=0,next_scan=0;
    uint32_t last_skin=0;
    bool prepared=false,ready=false,selected=false,busy=false;

    bool prepare(uintptr_t base,uint32_t size,uint32_t list_rva,uint32_t source_rva,
                 const char* model,DWORD window_thread,void* context,SlRefresh refresh) {
        prepared=false;
        if(!model||std::strlen(model)>=sizeof(champion)||!list_rva||list_rva>=size||size-list_rva<8)return false;
        bool candidate=false;
        for(const auto& rule:SummonSkinSync::resource_rules)if(!std::strcmp(rule.champion,model)){candidate=true;break;}
        if(!candidate||!window_thread||!refresh||!source.bind(base,size,source_rva))return false;
        slot=base+list_rva;std::strcpy(champion,model);expected_thread=window_thread;
        refresh_context=context;refresh_function=refresh;prepared=true;return true;
    }
    SlStatus initialize(const SlConfig& owner) {
        if(!prepared||GetCurrentThreadId()!=expected_thread)return SL_OK;
        // The seal below is opaque identity data, not a guessed skin field.
        // Keep it disjoint from the two fields the transaction changes.
        if(owner.layout.object_skin_offset<0x84||owner.layout.character_stack_offset<0x88)return SL_UNSUPPORTED_BINDING;
        const auto status=skins.initialize(owner,champion);
        ready=status==SL_OK;return status;
    }
    bool header(uintptr_t& manager,Header& value) const {
        if(!local_copy(slot,&manager,8)||manager>UINTPTR_MAX-24||!local_copy(manager+8,&value,sizeof(value)))return false;
        return value.count<=value.capacity&&value.count<=maximum_units&&value.capacity<=16384
            &&(!value.count||(value.data>=0x10000&&value.data<0x800000000000ULL
               &&value.count<=(UINTPTR_MAX-value.data)/8));
    }
    static bool model(uintptr_t object,const SlLayout& layout,char (&name)[48]) {
        if(object>UINTPTR_MAX-layout.character_stack_offset-0x28)return false;
        const auto stack=object+layout.character_stack_offset;uintptr_t pointer=0;uint32_t length=0;
        if(!local_copy(stack+0x18,&pointer,8)||!local_copy(stack+0x20,&length,4)||!length||length>=sizeof(name)
           ||!local_copy(pointer,name,size_t(length)+1)||name[length]||std::memchr(name,0,length))return false;
        return true;
    }
    struct Frame {
        MinionSkinBinding* binding;
        const SlConfig* owner;
        uintptr_t manager;
        Header header;
        bool valid() const {
            if(GetCurrentThreadId()!=binding->expected_thread||!skinlab::fields::alive(*owner))return false;
            uintptr_t manager_after=0;Header after{};
            return binding->header(manager_after,after)&&manager_after==manager
                &&after.data==header.data&&after.count==header.count&&after.capacity==header.capacity;
        }
    };
    struct Object {
        Frame* frame=nullptr;
        uintptr_t address=0,cell=0,control=0;
        uint64_t nonce=0;
        uint32_t index=0,stamp=0;
        uint8_t seal[0x28]{};
        char name[48]{};
    };
    static uint64_t SL_CALL generation(void* context) {
        const auto& object=*static_cast<Object*>(context);const auto& frame=*object.frame;
        if(!frame.valid())return 0;
        uintptr_t current=0;uint8_t seal[sizeof(object.seal)]{};uint32_t stamp=0;char name[sizeof(object.name)]{};
        if(!local_copy(frame.header.data+size_t(object.index)*8,&current,8)||current!=object.address
           ||!local_copy(current,seal,sizeof(seal))||std::memcmp(seal,object.seal,sizeof(seal))
           ||!local_copy(current+0x80,&stamp,4)||stamp!=object.stamp
           ||!model(current,frame.owner->layout,name)||std::strcmp(name,object.name))return 0;
        const auto link=frame.binding->source.read(current);
        if(link.status!=1||link.source!=reinterpret_cast<uintptr_t>(frame.owner->object_data)
           ||link.cell!=object.cell||link.control!=object.control||!frame.valid())return 0;
        return object.nonce;
    }
    static int SL_CALL allowed(void* context,int32_t skin) {
        const auto& object=*static_cast<Object*>(context);
        const auto& binding=*object.frame->binding;
        // The admitted original may come from a newer compatible catalog.
        // Always permit restoring the observed original number.
        return skin>=0&&(uint32_t(skin)==binding.skins.original_skin||sl_catalog_skin(binding.champion,skin));
    }
    static int SL_CALL refresh(void* context,void* stack,int force) {
        const auto& object=*static_cast<Object*>(context);auto& binding=*object.frame->binding;
        if(reinterpret_cast<uintptr_t>(stack)!=object.address+object.frame->owner->layout.character_stack_offset||force!=1)return 0;
        return binding.refresh_function(binding.refresh_context,stack,force);
    }
    SlStatus synchronize(const SlConfig& owner,bool restoring=false,bool force=false) {
        if(!ready)return SL_OK;
        if(GetCurrentThreadId()!=expected_thread)return SL_WRONG_THREAD;
        if(busy)return SL_BUSY;
        SummonSkinSync::Call call(busy);
        if(!skinlab::fields::alive(owner))return SL_TARGET_CHANGED;
        const auto skin=skinlab::fields::read(owner).skin;
        const auto now=GetTickCount64();
        if(!force&&!restoring&&now<next_scan&&skin==last_skin)return SL_OK;
        next_scan=now+50;last_skin=skin;
        uintptr_t manager=0;Header snapshot{};
        if(!header(manager,snapshot))return SL_INCONSISTENT_STATE;
        uintptr_t units[maximum_units]{},again[maximum_units]{};
        const auto bytes=size_t(snapshot.count)*8;
        if(bytes&&(!local_copy(snapshot.data,units,bytes)||!local_copy(snapshot.data,again,bytes)
           ||std::memcmp(units,again,bytes)))return SL_INCONSISTENT_STATE;
        Frame frame{this,&owner,manager,snapshot};if(!frame.valid())return SL_INCONSISTENT_STATE;
        Object objects[SummonSkinSync::maximum]{};SummonSkinSync::View views[SummonSkinSync::maximum]{};size_t count=0;
        for(uint32_t i=0;i<snapshot.count;++i) {
            const auto address=units[i];char name[48]{};
            if(address<0x10000||address>=0x800000000000ULL||(address&7)
               ||!model(address,owner.layout,name))return SL_INCONSISTENT_STATE;
            if(!skins.accepts(name))continue;
            const auto link=source.read(address);
            // An unreadable/unstable source is not evidence of a foreign pet.
            // In particular, never report successful restoration after merely
            // dropping a supported live entry whose source could not be read.
            if(link.status!=1)return SL_INCONSISTENT_STATE;
            if(link.source!=reinterpret_cast<uintptr_t>(owner.object_data))continue;
            if(count==SummonSkinSync::maximum)return SL_LAYOUT_OUT_OF_RANGE;
            auto& object=objects[count];object.frame=&frame;object.address=address;object.cell=link.cell;object.control=link.control;object.index=i;
            if(!local_copy(address,object.seal,sizeof(object.seal))||!local_copy(address+0x80,&object.stamp,4))return SL_INCONSISTENT_STATE;
            if(!++serial)++serial;
            object.nonce=serial;std::strcpy(object.name,name);
            auto& view=views[count++];view.owner=owner.expected_generation;std::strcpy(view.model,name);view.config=owner;
            view.config.object_data=reinterpret_cast<void*>(address);view.config.context=&object;view.config.expected_generation=object.nonce;
            view.config.generation=generation;view.config.skin_allowed=allowed;view.config.refresh=refresh;view.config.log=nullptr;
        }
        if(!frame.valid())return SL_INCONSISTENT_STATE;
        const auto result=skins.synchronize(owner,views,count,restoring);
        // A pet may disappear while the hero remains valid. Keep restoration
        // available instead of reporting that the local player was replaced.
        return result==SL_TARGET_CHANGED&&skinlab::fields::alive(owner)?SL_RECOVERY_REQUIRED:result;
    }
};
