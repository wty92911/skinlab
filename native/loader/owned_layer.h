// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Private adapter for the observed 16.21 x64 CharacterStackData ABI.
// Requires local_copy, SlConfig, Windows APIs and lux_forms.h in the includer.
using SlLayerPush=void (__fastcall*)(void*,const char*,int32_t,int32_t,
    bool,bool,bool,bool,bool,bool,int8_t,const char*,int32_t,const char*,int32_t,bool,int32_t,const char*);
using SlLayerDestroy=void (__fastcall*)(void*);
struct OwnedStackLayer {
    static constexpr size_t stride=0xa0;
    DWORD thread=0;
    SlLayerPush push=nullptr;
    SlLayerDestroy destroy=nullptr;
    uintptr_t entry=0;
    uint8_t saved[stride]{};
    uint32_t form=0;
    bool owned=false,recovery=false;

    SlStatus guard(const SlConfig& config) const {
        if(thread!=GetCurrentThreadId())return SL_WRONG_THREAD;
        return config.generation(config.context)==config.expected_generation?SL_OK:SL_TARGET_CHANGED;
    }
    bool header(uintptr_t stack,uintptr_t (&values)[3]) const {
        return local_copy(stack,values,sizeof(values))&&values[0]<=values[1]&&values[1]<=values[2]
            &&(values[0]||!values[2])&&values[2]-values[0]<=65536
            &&(values[1]-values[0])%stride==0&&(values[2]-values[0])%stride==0;
    }
    bool matches(uintptr_t stack) const {
        uintptr_t values[3]{};uint8_t bytes[stride]{};
        return header(stack,values)&&values[0]==entry&&values[1]-values[0]==stride
            &&local_copy(entry,bytes,sizeof(bytes))&&std::memcmp(bytes,saved,sizeof(bytes))==0;
    }
    SlStatus remove(const SlConfig& config,uintptr_t stack) {
        auto status=guard(config);if(status!=SL_OK)return status;
        if(!owned)return recovery?SL_RECOVERY_REQUIRED:SL_OK;
        // A game-owned or changed layer is never destroyed as if it were ours.
        if(!destroy||!matches(stack)){recovery=true;return SL_RECOVERY_REQUIRED;}
        std::memcpy(reinterpret_cast<void*>(stack+8),&entry,8);
        destroy(reinterpret_cast<void*>(entry));
        owned=false;recovery=false;form=0;entry=0;
        return guard(config);
    }
    SlStatus select(const SlConfig& config,uintptr_t stack,uint32_t selected) {
        auto status=guard(config);if(status!=SL_OK)return status;
        if(selected>=sl_lux_form_count||!push||!destroy)return SL_UNSUPPORTED_BINDING;
        if(recovery)return SL_RECOVERY_REQUIRED;
        uint32_t skin=0;
        if(!local_copy(stack+0x38,&skin,4)||skin!=7)return SL_INVALID_SKIN;
        if(owned) {
            if(!matches(stack)){recovery=true;return SL_RECOVERY_REQUIRED;}
            if(form==selected)return SL_OK;
        }
        uintptr_t values[3]{};
        if(!header(stack,values))return SL_INCONSISTENT_STATE;
        if(!owned&&values[0]!=values[1])return SL_UNSUPPORTED_BINDING;
        status=remove(config,stack);if(status!=SL_OK)return status;
        if(!selected)return SL_OK;
        // Observed 18-parameter ABI: arg11 is gear, arg18 is another string.
        // Push performs its own update(stack,false). All model strings are
        // copied by the engine into owned AStrings, not borrowed from this DLL.
        push(reinterpret_cast<void*>(stack),sl_lux_forms[selected],7,0,
             false,false,false,false,true,false,-1,"",0,"",0,false,1,"");
        if(guard(config)!=SL_OK){recovery=true;return SL_RECOVERY_REQUIRED;}
        if(!header(stack,values)||values[1]-values[0]!=stride) {
            recovery=true;return SL_RECOVERY_REQUIRED;
        }
        entry=values[0];owned=true;form=selected;
        if(!local_copy(entry,saved,sizeof(saved))) {recovery=true;return SL_RECOVERY_REQUIRED;}
        uintptr_t name=0;uint32_t length=0,layer_skin=0;char model[32]{};
        std::memcpy(&name,saved,8);std::memcpy(&length,saved+8,4);std::memcpy(&layer_skin,saved+0x20,4);
        if(length>=sizeof(model)||!local_copy(name,model,length+1)||model[length]
           ||std::strcmp(model,sl_lux_forms[selected])||layer_skin!=7||saved[0x84]!=0xff) {
            recovery=true;return SL_RECOVERY_REQUIRED;
        }
        return SL_OK;
    }
};
