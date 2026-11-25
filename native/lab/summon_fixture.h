// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Synthetic object identities/layouts. No fixture offset establishes a live
// game manager, owner, object lifetime, enumeration or update calling contract.
struct SummonFixture {
    struct Object {
        alignas(8) uint8_t data[512]{};
        char model[48]{};
        uint64_t id=0,owner=9;
        uint32_t refreshes=0,failures=0;
        bool live=true,lose_identity=false,clobber=false;
        void (*after_refresh)(void*)=nullptr;
        void (*before_allowed)(void*)=nullptr;
        void* callback_context=nullptr;
        SlConfig config{};
        static uint64_t SL_CALL generation(void* context) {
            const auto& object=*static_cast<Object*>(context);return object.live?object.id:0;
        }
        static int SL_CALL allowed(void* context,int32_t skin) {
            auto& object=*static_cast<Object*>(context);
            const auto callback=object.before_allowed;object.before_allowed=nullptr;
            if(callback)callback(object.callback_context);
            return skin>=0&&skin<1000;
        }
        static int SL_CALL refresh(void* context,void* stack,int force) {
            auto& object=*static_cast<Object*>(context);++object.refreshes;
            if(stack!=object.data+0x80||force!=1)return 0;
            if(object.lose_identity){object.live=false;object.lose_identity=false;}
            if(object.clobber){object.data[0x20]^=1;object.clobber=false;}
            const auto callback=object.after_refresh;object.after_refresh=nullptr;
            if(callback)callback(object.callback_context);
            if(object.failures){--object.failures;return 0;}
            return 1;
        }
        void initialize(const char* name,uint64_t identity,uint32_t skin) {
            std::strcpy(model,name);id=identity;
            const char* pointer=model;const auto length=uint32_t(std::strlen(model));
            std::memcpy(data+0x98,&pointer,8);std::memcpy(data+0xa0,&length,4);
            config.struct_size=sizeof(config);config.abi_version=SL_ABI_VERSION;config.binding_kind=SL_BINDING_LOCAL_FIXTURE;
            config.layout={0x20,0x80,0x38,28,0x5050669b,0,2,0x57e1606e,0x0d37fa35};
            config.object_data=data;config.object_size=sizeof(data);config.expected_generation=id;
            config.context=this;config.generation=generation;config.skin_allowed=allowed;config.refresh=refresh;
            set_skin(skin);
        }
        void set_skin(uint32_t skin) {skinlab::fields::write(config,skinlab::fields::encoded(config.layout,skin));}
        bool is_skin(uint32_t skin) const {return skinlab::fields::same(skinlab::fields::read(config),skinlab::fields::encoded(config.layout,skin));}
        SummonSkinSync::View view() const {
            SummonSkinSync::View value{};value.owner=owner;std::strcpy(value.model,model);value.config=config;return value;
        }
    };
    Object hero{},first{},second{};
    SummonSkinSync sync{};
    bool initialize() {
        hero.initialize("Malzahar",9,18);first.initialize("MalzaharVoidling",100,18);second.initialize("MalzaharVoidling",101,18);
        return sync.initialize(hero.config,"Malzahar")==SL_OK;
    }
    SlStatus frame(bool restore=false) {
        const SummonSkinSync::View views[]={first.view(),second.view()};
        return sync.synchronize(hero.config,views,2,restore);
    }
};
bool check_summon_skin() {
    // Existing and later-born summons, game rewrites, repeated frames, chromas,
    // restoration to the owner's original chroma rather than a pet's birth skin.
    SummonFixture f{};if(!f.initialize())return false;
    f.hero.set_skin(11);
    if(f.frame()!=SL_OK||!f.first.is_skin(11)||!f.second.is_skin(11))return false;
    if(f.frame()!=SL_OK||f.first.refreshes!=1||f.second.refreshes!=1)return false;
    f.second.id=102;f.second.config.expected_generation=102;f.second.set_skin(11);
    f.first.set_skin(0);f.hero.set_skin(28);
    if(f.frame()!=SL_OK||!f.first.is_skin(28)||!f.second.is_skin(28))return false;
    f.hero.set_skin(18);
    if(f.frame(true)!=SL_OK||!f.first.is_skin(18)||!f.second.is_skin(18))return false;

    // Another owner's identical model and an own borrowed/unknown model stay
    // untouched; matching by team or string alone would fail this case.
    f.hero.set_skin(11);f.first.owner=77;
    const char* borrowed="AnnieTibbers";std::strcpy(f.second.model,borrowed);
    const uint32_t length=uint32_t(std::strlen(borrowed));std::memcpy(f.second.data+0xa0,&length,4);
    if(f.frame()!=SL_OK||!f.first.is_skin(18)||!f.second.is_skin(18))return false;
    // A complete but malformed batch must fail before changing its first pet.
    SummonFixture malformed{};if(!malformed.initialize())return false;
    malformed.hero.set_skin(11);malformed.second.data[0x20]^=1;
    if(malformed.frame()!=SL_INCONSISTENT_STATE||!malformed.first.is_skin(18)||malformed.first.refreshes)return false;
    malformed.second.set_skin(18);
    auto duplicate=malformed.first.view();SummonSkinSync::View copies[]={duplicate,duplicate};
    if(malformed.sync.synchronize(malformed.hero.config,copies,2)!=SL_INCONSISTENT_STATE||malformed.first.refreshes)return false;
    if(malformed.sync.synchronize(malformed.hero.config,nullptr,1)!=SL_INVALID_ARGUMENT
       ||malformed.sync.synchronize(malformed.hero.config,copies,SummonSkinSync::maximum+1)!=SL_INVALID_ARGUMENT)return false;

    // A failed second update rolls the first back as well, then automatic
    // frames stop. Explicit restore obtains fresh views and retries recovery.
    SummonFixture failed{};if(!failed.initialize())return false;
    failed.hero.set_skin(11);failed.second.failures=1;
    if(failed.frame()!=SL_UPDATE_FAILED||!failed.first.is_skin(18)||!failed.second.is_skin(18)
       ||failed.first.refreshes!=2||failed.second.refreshes!=2||!failed.sync.recovery)return false;
    if(failed.frame()!=SL_RECOVERY_REQUIRED||failed.first.refreshes!=2)return false;
    failed.hero.set_skin(18);
    if(failed.frame(true)!=SL_OK||failed.sync.recovery)return false;
    failed.hero.set_skin(11);failed.second.failures=2;
    if(failed.frame()!=SL_RECOVERY_REQUIRED||!failed.first.is_skin(18))return false;
    // Inconsistent fields left by a failed engine update can still be restored
    // when identity, model, empty layer container and writable storage agree.
    failed.second.data[0x20]^=1;failed.hero.set_skin(18);
    if(failed.frame(true)!=SL_OK||!failed.second.is_skin(18)||failed.sync.recovery)return false;

    // The target may disappear during refresh. Do not write rollback bytes
    // through the stale identity. A replacement at the same address is fresh.
    SummonFixture gone{};if(!gone.initialize())return false;
    gone.hero.set_skin(11);gone.second.lose_identity=true;
    if(gone.frame()!=SL_RECOVERY_REQUIRED||gone.second.refreshes!=1||!gone.first.is_skin(18))return false;
    auto remaining=gone.first.view();gone.hero.set_skin(18);
    if(gone.sync.synchronize(gone.hero.config,&remaining,1,true)!=SL_OK)return false;
    gone.second.live=true;gone.second.id=200;gone.second.config.expected_generation=200;gone.second.set_skin(18);
    gone.hero.set_skin(11);
    if(gone.frame()!=SL_OK||!gone.second.is_skin(11))return false;
    gone.hero.set_skin(18);if(gone.frame(true)!=SL_OK)return false;

    // Check actual commit bytes, lifetime and thread guards, unsupported layers,
    // and genuinely read-only storage rather than just an in-range pointer.
    SummonFixture guarded{};if(!guarded.initialize())return false;
    guarded.hero.set_skin(11);guarded.first.clobber=true;
    if(guarded.frame()!=SL_UPDATE_FAILED||!guarded.first.is_skin(18)||guarded.second.refreshes)return false;
    guarded.hero.set_skin(18);if(guarded.frame(true)!=SL_OK)return false;
    guarded.hero.live=false;
    if(guarded.frame()!=SL_TARGET_CHANGED)return false;
    guarded.hero.live=true;guarded.sync.thread^=1;
    if(guarded.frame()!=SL_WRONG_THREAD)return false;
    guarded.sync.thread=GetCurrentThreadId();guarded.hero.set_skin(11);
    const uintptr_t header[]={0x10000,0x100a0,0x100a0};std::memcpy(guarded.second.data+0x80,header,sizeof(header));
    if(guarded.frame()!=SL_INCONSISTENT_STATE||guarded.first.refreshes!=2)return false;
    std::memset(guarded.second.data+0x80,0,sizeof(header));

    // Engine callbacks can change a later object or the hero without replacing
    // its identity. Revalidate their bytes/structure immediately before writes.
    SummonFixture changed{};if(!changed.initialize())return false;
    changed.hero.set_skin(11);changed.first.callback_context=&changed;
    changed.first.before_allowed=[](void* context){static_cast<SummonFixture*>(context)->hero.set_skin(28);};
    if(changed.frame()!=SL_INCONSISTENT_STATE||changed.first.refreshes||changed.second.refreshes)return false;
    changed.hero.set_skin(11);
    changed.first.after_refresh=[](void* context){static_cast<SummonFixture*>(context)->hero.set_skin(28);};
    if(changed.frame()!=SL_INCONSISTENT_STATE||!changed.first.is_skin(18)||changed.second.refreshes)return false;
    changed.hero.set_skin(18);if(changed.frame(true)!=SL_OK)return false;
    changed.hero.set_skin(11);
    changed.first.after_refresh=[](void* context){static_cast<SummonFixture*>(context)->second.set_skin(28);};
    if(changed.frame()!=SL_INCONSISTENT_STATE||!changed.first.is_skin(18)||!changed.second.is_skin(28)||changed.second.refreshes)return false;
    changed.hero.set_skin(18);if(changed.frame(true)!=SL_OK)return false;

    struct Reentry {SummonFixture* fixture;SlStatus status=SL_OK;} reentry{&changed};
    changed.hero.set_skin(11);changed.first.callback_context=&reentry;
    changed.first.after_refresh=[](void* context){auto& attempt=*static_cast<Reentry*>(context);attempt.status=attempt.fixture->frame();};
    if(changed.frame()!=SL_OK||reentry.status!=SL_BUSY||!changed.first.is_skin(11)||!changed.second.is_skin(11))return false;
    changed.hero.set_skin(18);if(changed.frame(true)!=SL_OK)return false;

    auto* page=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!page)return false;
    std::memcpy(page,guarded.first.data,sizeof(guarded.first.data));
    auto view=guarded.first.view();view.config.object_data=page;DWORD protection=0,ignored=0;
    bool ok=VirtualProtect(page,4096,PAGE_READONLY,&protection)!=0;
    if(ok)ok=guarded.sync.synchronize(guarded.hero.config,&view,1)==SL_INCONSISTENT_STATE;
    if(protection)VirtualProtect(page,4096,protection,&ignored);
    VirtualFree(page,0,MEM_RELEASE);return ok;
}
