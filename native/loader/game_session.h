// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Shared command queue: 1=apply, 2=restore, 3=stop, 4=inspect, 5=Lux model form.
// States: 0=starting, 1=ready, 2=active, 3=stopped, 4=recovery, 5=target changed.
struct GameSession:SessionBinding {
    SlConfig config{};
    HANDLE controller=nullptr;
    bool initialized=false,ready_sent=false;
    char model[96]{};
    SessionProbeSites probe_sites{};
    OwnedStackLayer layer{};
    FormSkinSync form_skins{};
    SkinGearSync skin_gears{};
    bool gear_selected=false;
    SlStatus form_status=SL_OK;
    ~GameSession(){if(controller)CloseHandle(controller);}
    static uint64_t SL_CALL generation(void* context) {
        auto* session=static_cast<GameSession*>(context);
        // Skin/model changes are expected. Identity is the current local
        // player pointer, not an unchanging model-name string.
        return SessionBinding::generation(static_cast<SessionBinding*>(session));
    }
    static int SL_CALL refresh(void* context,void* stack,int force) {
        auto& session=*static_cast<GameSession*>(context);
        uint32_t skin=0;bool changed=false;
        if(session.form_skins.enabled()
           &&(!local_copy(reinterpret_cast<uintptr_t>(stack)+session.config.layout.stack_skin_offset,&skin,4)
              ||session.form_skins.synchronize(session.config,reinterpret_cast<uintptr_t>(stack),skin,changed)!=SL_OK))return 0;
        if(session.skin_gears.synchronize(session.config,reinterpret_cast<uintptr_t>(stack),changed)!=SL_OK)return 0;
        if(!SessionBinding::refresh(static_cast<SessionBinding*>(&session),stack,force))return 0;
        // A failed commit must enter the core's normal rollback/recovery path.
        if(session.form_skins.synchronize(session.config,reinterpret_cast<uintptr_t>(stack),skin,changed,false)!=SL_OK||changed)return 0;
        return session.skin_gears.synchronize(session.config,reinterpret_cast<uintptr_t>(stack),changed,false)==SL_OK&&!changed;
    }
    SlStatus finish_gear(SlStatus result) {
        // The core skips refresh for an unchanged skin and an inactive restore.
        // Gear can still need initialization/restoration in those two cases.
        if(result==SL_OK) {
            const auto stack=reinterpret_cast<uintptr_t>(config.object_data)+config.layout.character_stack_offset;
            bool changed=false;
            result=skin_gears.synchronize(config,stack,changed);
            if(result==SL_OK&&changed&&!refresh(this,reinterpret_cast<void*>(stack),1))result=SL_RECOVERY_REQUIRED;
            if(result!=SL_OK&&result!=SL_TARGET_CHANGED)result=SL_RECOVERY_REQUIRED;
        }
        skin_gears.end();return result;
    }
    SlStatus synchronize_form() {
        if((!form_skins.enabled()&&!skin_gears.enabled())||form_status!=SL_OK)return form_status;
        SlState state{};state.struct_size=sizeof(state);
        auto result=api.state(core,&state);
        if(result!=SL_OK)return result;
        if(!(state.flags&SL_STATE_ACTIVE)&&!gear_selected)return SL_OK;
        if(state.flags&SL_STATE_RECOVERY_REQUIRED)return SL_RECOVERY_REQUIRED;
        if(state.current_skin<0||uint32_t(state.current_skin)!=state.current_stack_skin)return SL_INCONSISTENT_STATE;
        const auto stack=reinterpret_cast<uintptr_t>(config.object_data)+config.layout.character_stack_offset;
        bool changed=false;
        result=form_skins.synchronize(config,stack,state.current_stack_skin,changed);
        if(result!=SL_OK)return result;
        result=skin_gears.begin(config,stack,state.current_stack_skin);
        if(result!=SL_OK)return result;
        // No per-frame refresh when the game has kept the selected skin.
        if(changed&&!refresh(this,reinterpret_cast<void*>(stack),1))result=SL_RECOVERY_REQUIRED;
        result=finish_gear(result);
        if(result!=SL_OK)return result;
        return config.generation(config.context)==config.expected_generation?SL_OK:SL_TARGET_CHANGED;
    }
    static int SL_CALL catalog(void* context,int32_t skin) {
        (void)context;
        // Public skin IDs are hero*1000 + skin number. The frontend supplies
        // catalog numbers; don't require a new native build for each new skin.
        return skin>=0&&skin<1000;
    }
    SlStatus clear_form() {
        const bool had_layer=layer.owned;
        const auto stack=reinterpret_cast<uintptr_t>(config.object_data)+config.layout.character_stack_offset;
        const auto result=layer.remove(config,stack);
        if(result!=SL_OK)return result;
        if(had_layer) {
            refresh(this,reinterpret_cast<void*>(stack),1);
            if(layer.guard(config)!=SL_OK)return SL_TARGET_CHANGED;
        }
        work->shared->session_form=0;return SL_OK;
    }
    void publish(SlStatus result,LONG sequence) {
        if(result==SL_OK&&form_status!=SL_OK)result=form_status;
        auto& out=*work->shared;out.session_result=result;
        out.session_form=layer.recovery?sl_lux_form_count:(layer.owned?layer.form:0);
        if(result==SL_TARGET_CHANGED)out.session_state=5;
        else if(result==SL_RECOVERY_REQUIRED)out.session_state=4;
        if(core&&result==SL_OK) {
            SlState state{};state.struct_size=sizeof(state);
            const auto read=api.state(core,&state);
            if(read==SL_OK){out.session_skin=state.current_skin;out.session_encoded_skin=state.current_encoded_skin;out.session_state=(state.flags&SL_STATE_ACTIVE)?2:1;}
            else {out.session_result=read;if(read==SL_TARGET_CHANGED)out.session_state=5;}
        }
        InterlockedExchange(&out.completed_sequence,sequence);
    }
    static void session_frame(void* context,IDXGISwapChain*) {
        auto& p=*static_cast<GameSession*>(context);auto& out=*p.work->shared;
        if(p.finished)return;
        out.render_tid=GetCurrentThreadId();
        if(!p.initialized) {
            p.initialized=true;auto result=p.api.create(&p.config,&p.core);
            p.layer.thread=GetCurrentThreadId();
            const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
            if(result==SL_OK)result=p.skin_gears.initialize(p.config,stack);
            // Initialization has not applied anything; destroy this inactive
            // core without leaving a failed session holding an unused runtime.
            if(result!=SL_OK&&p.core)p.api.destroy(&p.core);
            uintptr_t values[3]{};uint8_t gear=0;
            out.forms_available=p.layer.push&&p.layer.destroy&&p.layer.header(stack,values)&&values[0]==values[1]
                &&local_copy(stack+0x9c,&gear,1)&&gear==0xff;
            p.publish(result,0);p.ready_sent=true;
            InterlockedExchange(&out.startup_stage,SL_START_READY);
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
        if(sequence==InterlockedCompareExchange(&out.completed_sequence,0,0)&&!disconnected) {
            if(p.form_status==SL_OK) {
                const auto status=p.synchronize_form();
                if(status!=SL_OK) {
                    // Stop automatic writes after a failure; restore/stop can
                    // retry recovery explicitly, including on disconnection.
                    p.form_status=status==SL_TARGET_CHANGED?status:SL_RECOVERY_REQUIRED;
                    p.publish(p.form_status,sequence);
                }
            }
            return;
        }
        const auto command=disconnected?3:queued_command;
        SlStatus result=SL_INVALID_ARGUMENT;
        if(command==1) {
            result=p.form_status==SL_OK?p.clear_form():p.form_status;
            const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
            if(result==SL_OK)result=p.skin_gears.begin(p.config,stack,requested);
            if(result==SL_OK)result=p.api.apply(p.core,static_cast<int32_t>(requested));
            result=p.finish_gear(result);
            if(result==SL_OK) {
                p.gear_selected=p.skin_gears.enabled();
                result=p.synchronize_form();
                if(result!=SL_OK)p.form_status=result=result==SL_TARGET_CHANGED?result:SL_RECOVERY_REQUIRED;
            }
        } else if(command==2) {
            result=p.clear_form();
            const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
            if(result==SL_OK)result=p.skin_gears.begin(p.config,stack,p.skin_gears.original_skin,true);
            if(result==SL_OK)result=p.api.restore(p.core);
            result=p.finish_gear(result);
            if(result==SL_OK){p.form_status=SL_OK;p.gear_selected=false;}
        }
        else if(command==3) {
            SlState state{};state.struct_size=sizeof(state);
            result=p.clear_form();
            const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
            if(result==SL_OK)result=p.skin_gears.begin(p.config,stack,p.skin_gears.original_skin,true);
            if(result==SL_OK)result=p.api.restore(p.core);
            result=p.finish_gear(result);
            if(result==SL_OK){p.form_status=SL_OK;p.gear_selected=false;}
            if(result==SL_OK&&p.api.state(p.core,&state)==SL_OK){out.session_skin=state.current_skin;out.session_encoded_skin=state.current_encoded_skin;}
            if(result==SL_OK)result=p.api.destroy(&p.core);
            if(result==SL_OK){p.finished=true;out.session_state=3;}
        } else if(command==4) {
            out.stack_probe={};out.stack_probe.schema_version=1;out.stack_probe.sequence=uint32_t(sequence);
            out.stack_probe.render_tid=GetCurrentThreadId();out.stack_probe.refresh_calls=out.refresh_calls;
            SlState state{};state.struct_size=sizeof(state);
            result=p.api.state(p.core,&state);
            if(result==SL_OK)collect_stack_probe(p.config,p.probe_sites,sequence,out.refresh_calls,out.stack_probe);
            else out.stack_probe.read_status=result==SL_TARGET_CHANGED?4:6;
        } else if(command==5) {
            SlState state{};state.struct_size=sizeof(state);
            result=p.api.state(p.core,&state);
            if(result==SL_OK) {
                if(!out.forms_available)result=SL_UNSUPPORTED_BINDING;
                else if(std::strcmp(p.model,"Lux")||state.current_skin!=7)result=SL_INVALID_SKIN;
                else if(!requested) {
                    const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
                    uintptr_t values[3]{};
                    if(!p.layer.owned&&(!p.layer.header(stack,values)||values[0]!=values[1]))result=SL_UNSUPPORTED_BINDING;
                    else result=p.clear_form();
                }
                else {
                    const auto stack=reinterpret_cast<uintptr_t>(p.config.object_data)+p.config.layout.character_stack_offset;
                    const bool same=p.layer.owned&&p.layer.form==requested;
                    result=p.layer.select(p.config,stack,requested);
                    if(result==SL_OK) {
                        out.session_form=requested;
                        if(!same)++out.refresh_calls; // push invokes update internally
                    }
                }
            }
        }
        if(p.skin_gears.enabled()&&(result==SL_RECOVERY_REQUIRED||result==SL_TARGET_CHANGED))p.form_status=result;
        p.publish(result,sequence);
        if(disconnected&&result!=SL_OK) {
            // Keep recovery responsibility. Stop is retried only by an explicit
            // command; do not repeatedly call an engine function each frame.
            if(p.controller){CloseHandle(p.controller);p.controller=nullptr;}
        }
    }
};
// One bridge remains installed until target exit. Only its frame thread owns
// the current session; workers publish a fresh candidate through this slot.
// A stopped session can be replaced without unloading or stacking frame hooks.
struct SessionBridge {
    IDXGISwapChain* chain=nullptr;
    SlDx11Runtime* runtime=nullptr;
    std::unique_ptr<GameSession> current;
    std::atomic<GameSession*> pending{nullptr};
    ~SessionBridge(){delete pending.load();}
    static void frame(void* context,IDXGISwapChain* chain) {
        auto& bridge=*static_cast<SessionBridge*>(context);
        if(bridge.current)GameSession::session_frame(bridge.current.get(),chain);
        std::unique_ptr<GameSession> next(bridge.pending.exchange(nullptr));
        if(!next)return;
        const auto* previous=bridge.current.get();
        const char* error=nullptr;
        if(previous) {
            if(previous->layer.recovery||previous->work->shared->session_state==4)
                error="Previous session restoration is incomplete; cannot reconnect";
            else if(!previous->finished||previous->core||previous->layer.owned||previous->work->shared->session_state!=3)
                error="A session is already active in this module";
        }
        if(WaitForSingleObject(next->controller,0)!=WAIT_TIMEOUT)
            error="Controller exited before session startup";
        if(error) {
            auto& out=*next->work->shared;copy_text(out.error,error);
            InterlockedExchange(&out.phase,SL_O_FAILED);SetEvent(next->work->done);return;
        }
        bridge.current=std::move(next);
        GameSession::session_frame(bridge.current.get(),chain);
    }
};
// Accessed only by the single observation worker; frame-owned state is private.
SessionBridge* session_bridge=nullptr;
bool start_session(const std::shared_ptr<Work>& work,const skinlab::profile::Snapshot& image,const std::vector<skinlab::profile::Result>& results) {
    std::unique_ptr<GameSession> pending(new GameSession);
    auto* s=pending.get();s->work=work;
    SlSessionFixtureConfig fixture{};fixture.struct_size=sizeof(fixture);
    const auto provider=reinterpret_cast<SlGetSessionFixture>(reinterpret_cast<void(*)()>(GetProcAddress(GetModuleHandleW(nullptr),"skinlab_get_session_fixture")));
    IDXGISwapChain* chain=nullptr;
    if(provider&&provider(&fixture)) {
        s->config=fixture.core;chain=fixture.chain;std::strcpy(s->model,"Fixture");
        std::strcpy(work->shared->model_name,"Fixture");
    } else {
        auto& out=*work->shared;
        auto find=[&](const char* name)->skinlab::profile::Candidate {
            for(const auto& r:results)if(r.name==name&&r.has_candidate)return r.candidate;
            throw std::runtime_error("Required session recipe is not unique/reread-equal");
        };
        const auto player=find("local_player"),stack=find("character_data_stack");
        const auto* codec=skinlab::profile::skin_codec(results);
        if(!codec||(codec->referenced_stack_offset&&codec->referenced_stack_offset!=stack.field_offset))
            throw std::runtime_error("Required session recipe codec is not unique/stack-consistent");
        const auto skin=*codec;
        const auto update=find("character_data_stack_update"),getter=find("material_registry_getter"),swap=find("swap_chain");
        if(out.player_read_status!=1||!out.player_stable||!out.skin_fields_agree)
            throw std::runtime_error("Cannot read consistent current player skin data");
        s->probe_sites=session_probe_sites(image,results);
        // Push/update are resolved by the shared recipes; the destructor is
        // resolved from push's direct call. 16.21 through 16.23 retain the same
        // 18-argument push ABI and layer layout. Relocated call operands must
        // not suppress the form controls after an otherwise compatible patch.
        if(!std::strcmp(out.model_name,"Lux")
           &&s->probe_sites.rvas[0]&&s->probe_sites.rvas[1]&&s->probe_sites.rvas[3]) {
            s->layer.push=reinterpret_cast<SlLayerPush>(image.module_base+s->probe_sites.rvas[0]);
            s->layer.destroy=reinterpret_cast<SlLayerDestroy>(image.module_base+s->probe_sites.rvas[3]);
        }
        s->slot=image.module_base+player.rva;s->player=out.player_address;s->update=image.module_base+update.rva;
        std::memcpy(s->model,out.model_name,strnlen(out.model_name,sizeof(s->model)-1));
        s->form_skins.champion=s->model;
        s->skin_gears.champion=s->model;
        s->layout={skin.field_offset,stack.field_offset,skinlab::league::kStackSkinOffset,skin.rotate_first,skin.xor_key,skin.rotate_second,skin.codec_kind,skin.add_key,skin.xor_key_second};
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
    InterlockedExchange(&work->shared->startup_stage,SL_START_FRAME);
    if(session_bridge) {
        if(session_bridge->chain!=chain)throw std::runtime_error("Session swapchain changed; cannot reconnect");
        auto* candidate=pending.release();GameSession* empty=nullptr;
        if(!session_bridge->pending.compare_exchange_strong(empty,candidate)) {
            delete candidate;throw std::runtime_error("An observation worker is already running");
        }
    } else {
        std::unique_ptr<SessionBridge> bridge(new SessionBridge);
        bridge->chain=chain;bridge->pending=pending.release();
        SlDx11Config cfg{};cfg.struct_size=sizeof(cfg);cfg.binding_kind=s->config.binding_kind;
        cfg.chain=chain;cfg.context=bridge.get();cfg.thread_binding=1;cfg.on_control_frame=SessionBridge::frame;
        // Present may arrive as soon as attach changes the vtable.
        const auto result=sl_dx11_attach(&cfg,&bridge->runtime);
        if(result!=SL_OK)throw std::runtime_error("Cannot attach session frame bridge");
        session_bridge=bridge.release();
    }
    return true;
}
