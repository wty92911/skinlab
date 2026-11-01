// Shared command queue. Commands: 1=apply, 2=restore, 3=restore-and-stop.
// States: 0=starting, 1=ready, 2=active, 3=stopped, 4=recovery, 5=target changed.
struct GameSession:SessionBinding {
    SlConfig config{};
    HANDLE controller=nullptr;
    bool initialized=false,ready_sent=false;
    char model[96]{};
    ~GameSession(){if(controller)CloseHandle(controller);}
    static uint64_t SL_CALL generation(void* context) {
        auto* session=static_cast<GameSession*>(context);
        // Skin/model changes are expected. Identity is the current local
        // player pointer, not an unchanging model-name string.
        return SessionBinding::generation(static_cast<SessionBinding*>(session));
    }
    static int SL_CALL refresh(void* context,void* stack,int force) {
        return SessionBinding::refresh(static_cast<SessionBinding*>(static_cast<GameSession*>(context)),stack,force);
    }
    static int SL_CALL catalog(void* context,int32_t skin) {
        (void)context;
        // Public skin IDs are hero*1000 + skin number. The frontend supplies
        // catalog numbers; don't require a new native build for each new skin.
        return skin>=0&&skin<1000;
    }
    void publish(SlStatus result,LONG sequence) {
        auto& out=*work->shared;out.session_result=result;
        if(result==SL_TARGET_CHANGED)out.session_state=5;
        else if(result==SL_RECOVERY_REQUIRED)out.session_state=4;
        if(core&&result==SL_OK) {
            SlState state{};state.struct_size=sizeof(state);
            const auto read=api.state(core,&state);
            if(read==SL_OK){out.session_skin=state.current_skin;out.session_state=(state.flags&SL_STATE_ACTIVE)?2:1;}
            else {out.session_result=read;if(read==SL_TARGET_CHANGED)out.session_state=5;}
        }
        InterlockedExchange(&out.completed_sequence,sequence);
    }
    static void session_frame(void* context,IDXGISwapChain*) {
        auto& p=*static_cast<GameSession*>(context);auto& out=*p.work->shared;
        if(p.finished)return;
        out.render_tid=GetCurrentThreadId();
        if(!p.initialized) {
            p.initialized=true;const auto result=p.api.create(&p.config,&p.core);
            p.publish(result,0);p.ready_sent=true;
            if(result!=SL_OK){p.finished=true;InterlockedExchange(&out.phase,SL_O_FAILED);}
            else InterlockedExchange(&out.phase,SL_O_COMPLETE);
            SetEvent(p.work->done);
            if(result!=SL_OK)return;
        }
        // Never wait for the controller on the render thread. A tiny shared
        // lock makes sequence+payload a coherent command, including stop after
        // an earlier timeout. Engine work runs after releasing this lock.
        if(InterlockedCompareExchange(&out.command_lock,1,0)!=0)return;
        const LONG sequence=out.command_sequence;
        const auto requested=out.requested_skin;
        const auto queued_command=out.session_command;
        InterlockedExchange(&out.command_lock,0);
        const bool disconnected=p.controller&&WaitForSingleObject(p.controller,0)==WAIT_OBJECT_0;
        if(sequence==InterlockedCompareExchange(&out.completed_sequence,0,0)&&!disconnected)return;
        const auto command=disconnected?3:queued_command;
        SlStatus result=SL_INVALID_ARGUMENT;
        if(command==1)result=p.api.apply(p.core,static_cast<int32_t>(requested));
        else if(command==2)result=p.api.restore(p.core);
        else if(command==3) {
            SlState state{};state.struct_size=sizeof(state);
            result=p.api.restore(p.core);
            if(result==SL_OK&&p.api.state(p.core,&state)==SL_OK)out.session_skin=state.current_skin;
            if(result==SL_OK)result=p.api.destroy(&p.core);
            if(result==SL_OK){p.finished=true;out.session_state=3;}
        }
        p.publish(result,sequence);
        if(disconnected&&result!=SL_OK) {
            // Keep recovery responsibility. Stop is retried only by an explicit
            // command; do not repeatedly call an engine function each frame.
            if(p.controller){CloseHandle(p.controller);p.controller=nullptr;}
        }
    }
};
bool start_session(const std::shared_ptr<Work>& work,const skinlab::profile::Snapshot& image,const std::vector<skinlab::profile::Result>& results) {
    if(session_active)throw std::runtime_error("A session is already active in this module");
    std::unique_ptr<GameSession> pending(new GameSession);
    auto* s=pending.get();s->work=work;
    SlSessionFixtureConfig fixture{};fixture.struct_size=sizeof(fixture);
    const auto provider=reinterpret_cast<SlGetSessionFixture>(GetProcAddress(GetModuleHandleW(nullptr),"skinlab_get_session_fixture"));
    IDXGISwapChain* chain=nullptr;
    if(provider&&provider(&fixture)) {
        s->config=fixture.core;chain=fixture.chain;std::strcpy(s->model,"Fixture");
        std::strcpy(work->shared->model_name,"Fixture");
    } else {
        auto& out=*work->shared;
        if(out.player_read_status!=1||!out.player_stable||!out.skin_fields_agree)
            throw std::runtime_error("Cannot read consistent current player skin data");
        auto find=[&](const char* name)->skinlab::profile::Candidate {
            for(const auto& r:results)if(r.name==name&&r.has_candidate)return r.candidate;
            throw std::runtime_error("Required session recipe is not unique/reread-equal");
        };
        const auto player=find("local_player"),stack=find("character_data_stack"),skin=find("skin_id_encoding");
        const auto update=find("character_data_stack_update"),getter=find("material_registry_getter"),swap=find("swap_chain");
        s->slot=image.module_base+player.rva;s->player=out.player_address;s->update=image.module_base+update.rva;
        std::strncpy(s->model,out.model_name,sizeof(s->model)-1);
        s->layout={skin.field_offset,stack.field_offset,skinlab::league::kStackSkinOffset,skin.rotate_first,skin.xor_key,skin.rotate_second};
        auto& cfg=s->config;cfg.struct_size=sizeof(cfg);cfg.abi_version=SL_ABI_VERSION;cfg.binding_kind=SL_BINDING_LEAGUE_EXPERIMENT;
        cfg.layout=s->layout;cfg.object_data=reinterpret_cast<void*>(s->player);cfg.object_size=s->layout.character_stack_offset+skinlab::league::kMinimumStackSkinExtent;
        cfg.context=s;cfg.expected_generation=s->player;cfg.generation=GameSession::generation;cfg.skin_allowed=GameSession::catalog;cfg.refresh=GameSession::refresh;
        const uintptr_t registry=reinterpret_cast<uintptr_t(__fastcall*)()>(image.module_base+getter.rva)();
        uintptr_t pointer=0;
        if(!local_copy(registry+swap.field_offset,&pointer,8))throw std::runtime_error("Cannot read session swapchain");
        chain=reinterpret_cast<IDXGISwapChain*>(pointer);
    }
    s->api.struct_size=sizeof(s->api);
    if(skinlab_get_api(SL_ABI_VERSION,&s->api)!=SL_OK)throw std::runtime_error("Session core API unavailable");
    s->controller=OpenProcess(SYNCHRONIZE,FALSE,work->shared->host_pid);
    if(!s->controller)throw std::runtime_error("Cannot retain controller lifetime handle");
    SlDx11Config cfg{};cfg.struct_size=sizeof(cfg);cfg.binding_kind=s->config.binding_kind;
    cfg.chain=chain;cfg.context=s;cfg.thread_binding=1;cfg.on_control_frame=GameSession::session_frame;
    const auto result=sl_dx11_attach(&cfg,&s->bridge);
    if(result!=SL_OK)throw std::runtime_error("Cannot attach session frame bridge");
    session_active=true;pending.release();return true;
}
