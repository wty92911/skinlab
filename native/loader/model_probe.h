// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Read-only diagnostics for the already established CharacterStackData layout.
// A model name/skin here is an observation, never permission to change that
// model, proof of object ownership, or proof a skill uses this stack layer.
void collect_stack_model(uintptr_t address,uint32_t index,SlStackModelProbe& out) {
    out={};out.address=address;out.index=index;
    constexpr size_t stride=skinlab::league::kStackLayerStride;
    uint8_t bytes[stride]{},again[stride]{};
    if(!local_copy(address,bytes,sizeof(bytes))){out.read_status=2;return;}
    uintptr_t name=0;std::memcpy(&name,bytes,8);std::memcpy(&out.model_length,bytes+8,4);
    if(!out.model_length||out.model_length>=sizeof(out.model)){out.read_status=3;return;}
    char model[sizeof(out.model)]{},reread[sizeof(out.model)]{};
    const auto length=size_t(out.model_length)+1;
    if(!local_copy(name,model,length)){out.read_status=2;return;}
    if(model[out.model_length]||std::memchr(model,0,out.model_length)){out.read_status=3;return;}
    for(size_t i=0;i<out.model_length;++i)
        if(static_cast<uint8_t>(model[i])<32||static_cast<uint8_t>(model[i])>126){out.read_status=3;return;}
    if(!local_copy(address,again,sizeof(again))||!local_copy(name,reread,length)){out.read_status=2;return;}
    if(std::memcmp(bytes,again,sizeof(bytes))||std::memcmp(model,reread,length)){out.read_status=5;return;}
    std::memcpy(out.model,model,length);
    std::memcpy(&out.skin,bytes+skinlab::league::kStackLayerSkinOffset,4);
    out.gear=bytes[0x84];out.read_status=1;
}
void collect_stack_models(const SlConfig& config,uintptr_t stack,const uint64_t* expected,SlStackModelsProbe& out) {
    out={};
    if(config.generation(config.context)!=config.expected_generation){out.read_status=4;return;}
    if(config.layout.stack_skin_offset!=skinlab::league::kStackSkinOffset){out.read_status=7;return;}
    if(stack>UINTPTR_MAX-0x18-skinlab::league::kStackLayerStride){out.read_status=3;return;}
    uint64_t header[3]{},again[3]{};
    if(!local_copy(stack,header,sizeof(header))){out.read_status=2;return;}
    if(expected&&std::memcmp(header,expected,sizeof(header))){out.read_status=5;return;}
    constexpr auto stride=skinlab::league::kStackLayerStride;
    if(header[0]>header[1]||header[1]>header[2]||(!header[0]&&header[2])
       ||header[2]-header[0]>65536||(header[1]-header[0])%stride||(header[2]-header[0])%stride){out.read_status=3;return;}
    out.layer_count=uint32_t((header[1]-header[0])/stride);
    collect_stack_model(stack+0x18,0,out.base);
    out.read_status=out.base.read_status;
    out.sampled_layers=std::min<uint32_t>(out.layer_count,SL_MODEL_PROBES);
    for(uint32_t i=0;i<out.sampled_layers;++i) {
        // Retain both the earliest layers and the active top layers when the
        // vector is larger than the bound; no controller-supplied addresses.
        const auto index=out.layer_count>SL_MODEL_PROBES&&i>=SL_MODEL_PROBES/2
            ?out.layer_count-(SL_MODEL_PROBES-i):i;
        collect_stack_model(uintptr_t(header[0]+uint64_t(index)*stride),index,out.layers[i]);
        if(out.layers[i].read_status!=1)out.read_status=out.layers[i].read_status;
    }
    // A later sample must not leave an earlier model/skin/gear observation
    // looking stable after it has changed without reallocating the vector.
    auto equal=[](const SlStackModelProbe& a,const SlStackModelProbe& b) {
        return a.read_status==b.read_status&&a.address==b.address&&a.index==b.index
            &&a.model_length==b.model_length&&a.skin==b.skin&&a.gear==b.gear&&!std::strcmp(a.model,b.model);
    };
    SlStackModelProbe reread{};collect_stack_model(stack+0x18,0,reread);
    if(!equal(out.base,reread)){out={};out.read_status=reread.read_status==2?2:5;return;}
    for(uint32_t i=0;i<out.sampled_layers;++i) {
        collect_stack_model(uintptr_t(out.layers[i].address),out.layers[i].index,reread);
        if(!equal(out.layers[i],reread)){out={};out.read_status=reread.read_status==2?2:5;return;}
    }
    if(config.generation(config.context)!=config.expected_generation){out={};out.read_status=4;return;}
    if(!local_copy(stack,again,sizeof(again))){out={};out.read_status=2;return;}
    if(std::memcmp(header,again,sizeof(header))){out={};out.read_status=5;}
}
