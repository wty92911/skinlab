// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
struct MinionBindingFixture {
    struct Object {
        alignas(8) uint8_t data[0x600]{},control[16]{};
        uintptr_t source=0;
        uint32_t refreshes=0,failures=0;
    };
    Object hero{},pets[4]{};
    struct Manager {uintptr_t unused=0,data=0;uint32_t count=0,capacity=0;} manager;
    uintptr_t slot=0,units[4]{};
    SlConfig owner{};
    MinionSkinBinding binding{};
    uint64_t identity=9;
    void (*after_refresh)(MinionBindingFixture&,size_t)=nullptr;
    static uint64_t SL_CALL generation(void* context) {return static_cast<MinionBindingFixture*>(context)->identity;}
    static int SL_CALL allowed(void*,int32_t skin) {return sl_catalog_skin("Malzahar",skin);}
    static int SL_CALL refresh(void* context,void* stack,int force) {
        auto& fixture=*static_cast<MinionBindingFixture*>(context);
        if(force!=1)return 0;
        for(size_t i=0;i<4;++i)if(stack==fixture.pets[i].data+0x100) {
            auto& object=fixture.pets[i];++object.refreshes;
            if(fixture.after_refresh) {
                auto callback=fixture.after_refresh;fixture.after_refresh=nullptr;callback(fixture,i);
            }
            if(object.failures){--object.failures;return 0;}
            return 1;
        }
        return 0;
    }
    SlConfig config(Object& object) const {auto out=owner;out.object_data=object.data;return out;}
    void set_skin(Object& object,uint32_t skin) {skinlab::fields::write(config(object),skinlab::fields::encoded(owner.layout,skin));}
    bool is_skin(Object& object,uint32_t skin) const {
        return skinlab::fields::same(skinlab::fields::read(config(object)),skinlab::fields::encoded(owner.layout,skin));
    }
    void link(Object& object,uintptr_t source) {
        object.source=source;
        const uintptr_t holder[]={reinterpret_cast<uintptr_t>(&object.source),reinterpret_cast<uintptr_t>(object.control)};
        std::memcpy(object.data+0x568,holder,sizeof(holder));
        const int32_t strong=1;std::memcpy(object.control+8,&strong,4);
    }
    bool initialize(uint32_t original=0) {
        owner.struct_size=sizeof(owner);owner.abi_version=SL_ABI_VERSION;owner.binding_kind=SL_BINDING_LOCAL_FIXTURE;
        owner.layout={0x88,0x100,0x38,28,0x5050669b,0,2,0x57e1606e,0x0d37fa35};
        owner.object_data=hero.data;owner.object_size=sizeof(hero.data);owner.expected_generation=identity;
        owner.context=this;owner.generation=generation;owner.skin_allowed=allowed;owner.refresh=refresh;
        set_skin(hero,original);
        for(size_t i=0;i<4;++i) {
            auto& pet=pets[i];const char* model="MalzaharVoidling";const auto length=uint32_t(std::strlen(model));
            std::memcpy(pet.data+0x118,&model,8);std::memcpy(pet.data+0x120,&length,4);
            set_skin(pet,original);link(pet,reinterpret_cast<uintptr_t>(hero.data));units[i]=reinterpret_cast<uintptr_t>(pet.data);
        }
        // An identical model belonging to someone else is inside the live count.
        link(pets[1],0x10000000);
        manager.data=reinterpret_cast<uintptr_t>(units);manager.count=2;manager.capacity=4;
        slot=reinterpret_cast<uintptr_t>(&manager);
        binding.slot=reinterpret_cast<uintptr_t>(&slot);binding.source.member=0x568;
        binding.prepared=true;std::strcpy(binding.champion,"Malzahar");binding.expected_thread=GetCurrentThreadId();
        binding.refresh_context=this;binding.refresh_function=refresh;
        return binding.initialize(owner)==SL_OK&&binding.ready;
    }
    SlStatus frame(bool restoring=false) {return binding.synchronize(owner,restoring,true);}
};
bool check_minion_binding() {
    MinionBindingFixture newer{};if(!newer.initialize(999))return false;
    newer.set_skin(newer.hero,18);
    if(newer.frame()!=SL_OK||!newer.is_skin(newer.pets[0],18))return false;
    newer.set_skin(newer.hero,999);
    if(newer.frame(true)!=SL_OK||!newer.is_skin(newer.pets[0],999))return false;
    MinionBindingFixture f{};if(!f.initialize())return false;
    f.set_skin(f.hero,18);
    if(f.frame()!=SL_OK||!f.is_skin(f.pets[0],18)||!f.is_skin(f.pets[1],0)
       ||!f.is_skin(f.pets[2],0)||f.pets[0].refreshes!=1)return false;
    if(f.frame()!=SL_OK||f.pets[0].refreshes!=1)return false;
    // Capacity tails never participate. A newly counted instance does.
    f.manager.count=3;
    if(f.frame()!=SL_OK||!f.is_skin(f.pets[2],18)||!f.is_skin(f.pets[3],0))return false;
    f.set_skin(f.hero,5);f.manager.count=4;
    if(f.frame()!=SL_OK||!f.is_skin(f.pets[0],5)||!f.is_skin(f.pets[2],5)||!f.is_skin(f.pets[3],5))return false;
    f.set_skin(f.hero,0);
    if(f.frame(true)!=SL_OK||!f.is_skin(f.pets[0],0)||!f.is_skin(f.pets[2],0)||!f.is_skin(f.pets[3],0))return false;
    f.set_skin(f.hero,18);if(f.frame()!=SL_OK)return false;
    f.set_skin(f.hero,0);
    uintptr_t model_pointer=0;std::memcpy(&model_pointer,f.pets[0].data+0x118,8);
    const uintptr_t unreadable=1;std::memcpy(f.pets[0].data+0x118,&unreadable,8);
    if(f.frame(true)!=SL_INCONSISTENT_STATE||!f.is_skin(f.pets[0],18))return false;
    std::memcpy(f.pets[0].data+0x118,&model_pointer,8);
    std::memcpy(f.pets[0].data+0x570,&unreadable,8);
    if(f.frame(true)!=SL_INCONSISTENT_STATE||!f.is_skin(f.pets[0],18))return false;
    f.link(f.pets[0],reinterpret_cast<uintptr_t>(f.hero.data));
    if(f.frame(true)!=SL_OK||!f.is_skin(f.pets[0],0))return false;
    // Failed updates roll back earlier pets; automatic writes stay suspended.
    f.set_skin(f.hero,18);f.pets[2].failures=1;
    if(f.frame()!=SL_UPDATE_FAILED||!f.is_skin(f.pets[0],0)||!f.is_skin(f.pets[2],0)
       ||!f.is_skin(f.pets[3],0)||!f.binding.skins.recovery)return false;
    const auto calls=f.pets[0].refreshes;
    if(f.frame()!=SL_RECOVERY_REQUIRED||f.pets[0].refreshes!=calls)return false;
    f.set_skin(f.hero,0);if(f.frame(true)!=SL_OK||f.binding.skins.recovery)return false;
    // A changed identity or ownership during the callback forbids stale writes.
    f.set_skin(f.hero,18);
    const auto second_calls=f.pets[2].refreshes;
    f.after_refresh=[](MinionBindingFixture& fixture,size_t index){fixture.pets[index].data[0x80]^=1;};
    if(f.frame()!=SL_RECOVERY_REQUIRED||f.pets[2].refreshes!=second_calls)return false;
    f.set_skin(f.hero,0);if(f.frame(true)!=SL_OK)return false;
    f.set_skin(f.hero,18);
    f.after_refresh=[](MinionBindingFixture& fixture,size_t index){fixture.link(fixture.pets[index],0x10000000);};
    if(f.frame()!=SL_RECOVERY_REQUIRED)return false;
    // The foreign replacement is left alone during recovery. Fresh ownership
    // at the same address is discovered on the next explicit transaction.
    f.set_skin(f.hero,0);if(f.frame(true)!=SL_OK||!f.is_skin(f.pets[0],18))return false;
    f.link(f.pets[0],reinterpret_cast<uintptr_t>(f.hero.data));
    if(f.frame(true)!=SL_OK||!f.is_skin(f.pets[0],0))return false;
    // Header changes during an engine call prevent further writes this frame.
    f.set_skin(f.hero,18);
    f.after_refresh=[](MinionBindingFixture& fixture,size_t){fixture.manager.count=2;};
    if(f.frame()!=SL_RECOVERY_REQUIRED)return false;
    f.set_skin(f.hero,0);if(f.frame(true)!=SL_OK)return false;
    f.manager.count=5;
    if(f.frame()!=SL_INCONSISTENT_STATE)return false;
    f.manager.count=2;f.binding.expected_thread^=1;
    if(f.frame()!=SL_WRONG_THREAD)return false;
    f.binding.expected_thread=GetCurrentThreadId();f.identity=10;
    return f.frame()==SL_TARGET_CHANGED;
}
