// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Synthetic diagnostic samples, not evidence that Quinn uses these layers.
struct ModelProbeFixture {
    alignas(8) uint8_t stack[512]{},entries[10*skinlab::league::kStackLayerStride]{};
    SlConfig config{};
    uint32_t checks=0,change_at=0,lose_at=0;
    static uint64_t SL_CALL generation(void* context) {
        auto& f=*static_cast<ModelProbeFixture*>(context);++f.checks;
        if(f.change_at==f.checks)f.header(f.entries,0);
        return f.lose_at==f.checks?0:9;
    }
    void header(uint8_t* data,size_t count) {
        const uint64_t begin=reinterpret_cast<uintptr_t>(data),end=begin+count*skinlab::league::kStackLayerStride;
        const uint64_t values[]={begin,end,end};std::memcpy(stack,values,sizeof(values));
    }
    static void layer(uint8_t* data,const char* model,uint32_t skin,uint8_t gear) {
        const auto length=uint32_t(std::strlen(model));
        std::memcpy(data,&model,8);std::memcpy(data+8,&length,4);
        std::memcpy(data+skinlab::league::kStackLayerSkinOffset,&skin,4);data[0x84]=gear;
    }
    void initialize(size_t count=1) {
        config.layout.stack_skin_offset=skinlab::league::kStackSkinOffset;
        config.expected_generation=9;config.generation=generation;config.context=this;
        header(entries,count);layer(stack+0x18,"Quinn",6,0xff);
        for(size_t i=0;i<10;++i)layer(entries+i*skinlab::league::kStackLayerStride,"QuinnValor",uint32_t(i+5),uint8_t(i));
    }
    SlStackModelsProbe collect(const uint64_t* expected=nullptr) {
        SlStackModelsProbe out{};collect_stack_models(config,reinterpret_cast<uintptr_t>(stack),expected,out);return out;
    }
};
bool check_model_probe() {
    ModelProbeFixture f{};f.initialize();
    uint8_t stack_before[sizeof(f.stack)]{},entries_before[sizeof(f.entries)]{};
    std::memcpy(stack_before,f.stack,sizeof(f.stack));std::memcpy(entries_before,f.entries,sizeof(f.entries));
    auto out=f.collect();
    if(out.read_status!=1||out.layer_count!=1||out.sampled_layers!=1
       ||out.base.read_status!=1||std::strcmp(out.base.model,"Quinn")||out.base.skin!=6||out.base.gear!=0xff
       ||out.layers[0].read_status!=1||std::strcmp(out.layers[0].model,"QuinnValor")||out.layers[0].skin!=5||out.layers[0].gear!=0
       ||std::memcmp(stack_before,f.stack,sizeof(f.stack))||std::memcmp(entries_before,f.entries,sizeof(f.entries)))return false;
    // A layer may be replaced/reallocated between probes; retain no old address.
    auto* replacement=f.entries+5*skinlab::league::kStackLayerStride;
    ModelProbeFixture::layer(replacement,"OtherChampion",28,0xff);f.header(replacement,1);out=f.collect();
    if(out.read_status!=1||out.layers[0].address!=reinterpret_cast<uintptr_t>(replacement)
       ||std::strcmp(out.layers[0].model,"OtherChampion")||out.layers[0].skin!=28)return false;
    f.header(f.entries,10);out=f.collect();
    if(out.read_status!=1||out.layer_count!=10||out.sampled_layers!=8)return false;
    const uint32_t indices[]={0,1,2,3,6,7,8,9};
    for(size_t i=0;i<8;++i)if(out.layers[i].index!=indices[i]||out.layers[i].skin!=indices[i]+5)return false;
    // Empty layers still expose the base model, skin and gear.
    std::memset(f.stack,0,24);out=f.collect();
    if(out.read_status!=1||out.layer_count||out.sampled_layers||std::strcmp(out.base.model,"Quinn"))return false;
    f.header(f.entries,1);const uint32_t invalid_length=96;
    std::memcpy(f.entries+8,&invalid_length,4);out=f.collect();
    if(out.read_status!=3||out.layers[0].read_status!=3||*out.layers[0].model)return false;
    ModelProbeFixture::layer(f.entries,"QuinnValor",5,0xff);const uintptr_t invalid_name=1;
    std::memcpy(f.entries,&invalid_name,8);out=f.collect();
    if(out.read_status!=2||out.layers[0].read_status!=2||*out.layers[0].model)return false;
    const uint64_t invalid_header[]={1,2,3};std::memcpy(f.stack,invalid_header,sizeof(invalid_header));out=f.collect();
    if(out.read_status!=3||out.sampled_layers||*out.base.model)return false;
    f.header(f.entries,1);ModelProbeFixture::layer(f.entries,"QuinnValor",5,0xff);
    const uint64_t stale_header[]={0,0,0};out=f.collect(stale_header);
    if(out.read_status!=5||out.sampled_layers||*out.base.model)return false;
    fixture_read_context=&f;
    fixture_read_callback=[](void* context,uintptr_t address) {
        auto& state=*static_cast<ModelProbeFixture*>(context);
        if(address==reinterpret_cast<uintptr_t>(state.entries)) {
            fixture_read_callback=nullptr;
            const uint32_t changed=28;std::memcpy(state.stack+0x38,&changed,4);
        }
    };
    out=f.collect();fixture_read_callback=nullptr;fixture_read_context=nullptr;
    if(out.read_status!=5||out.sampled_layers||*out.base.model)return false;
    f.checks=0;f.change_at=2;out=f.collect();
    if(out.read_status!=5||out.sampled_layers||*out.base.model)return false;
    f.change_at=0;f.checks=0;f.lose_at=2;out=f.collect();
    if(out.read_status!=4||out.sampled_layers||*out.base.model)return false;
    f.lose_at=0;f.config.layout.stack_skin_offset=0x40;out=f.collect();
    return out.read_status==7&&!out.sampled_layers&&!*out.base.model;
}
