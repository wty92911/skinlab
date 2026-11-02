// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Included inside observe_hook.cpp's private namespace, after Work/copy_text.
// Shared object identity and refresh callback for a live session.
struct SessionBinding {
    std::shared_ptr<Work> work;
    uintptr_t slot=0,player=0,update=0;
    SlLayout layout{};
    SlApi api{};SlRuntime* core=nullptr;SlDx11Runtime* bridge=nullptr;
    bool finished=false;
    static uint64_t SL_CALL generation(void* context) {
        auto& p=*static_cast<SessionBinding*>(context);uintptr_t current=0;uint32_t value=0;
        return local_copy(p.slot,&current,8)&&current==p.player&&
            local_copy(current+p.layout.object_skin_offset,&value,4)&&
            local_copy(current+p.layout.character_stack_offset+skinlab::league::kStackSkinOffset,&value,4)?p.player:0;
    }
    static int SL_CALL refresh(void* context,void* stack,int force) {
        auto& p=*static_cast<SessionBinding*>(context);++p.work->shared->refresh_calls;
        reinterpret_cast<void(__fastcall*)(void*,bool)>(p.update)(stack,force!=0);return 1;
    }
};
