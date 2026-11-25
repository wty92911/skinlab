// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Fixed recipe entry and direct tail jumps only. This reads code; it neither
// calls the candidate nor accepts code/data addresses from the controller.
void collect_owner_code(uintptr_t base,uint32_t image_size,uint32_t entry,uint32_t context,
                        SlCodeProbe (&out)[SL_CODE_PROBES]) {
    for(auto& code:out)code={};
    if(base>UINTPTR_MAX-image_size)return;
    auto sample=[&](uint32_t rva,SlCodeProbe& code) {
        code.rva=rva;if(!rva||rva>=image_size)return;
        uint8_t again[sizeof(code.bytes)]{};
        const auto size=std::min<uint32_t>(sizeof(code.bytes),image_size-rva);
        if(local_copy(base+rva,code.bytes,size)&&local_copy(base+rva,again,size)
           &&!std::memcmp(code.bytes,again,size))code.size=size;
    };
    sample(entry,out[0]);sample(context,out[1]);
    uint32_t previous=entry;
    for(size_t i=2;i<SL_CODE_PROBES;++i) {
        const auto& source=i==2?out[0]:out[i-1];
        if(source.size<12)break;
        const auto* bytes=source.bytes;size_t jump=0;
        if(bytes[0]==0xe9)jump=0;
        else if(bytes[0]==0x48&&bytes[1]==0x81&&bytes[2]==0xc1&&bytes[7]==0xe9)jump=7;
        else if(bytes[0]==0x48&&bytes[1]==0x83&&bytes[2]==0xc1&&bytes[4]==0xe9)jump=4;
        else break;
        int32_t displacement=0;std::memcpy(&displacement,bytes+jump+1,4);
        const int64_t target=int64_t(previous)+int64_t(jump)+5+displacement;
        if(target<=0||target>=image_size)break;
        bool repeated=false;
        for(size_t j=0;j<i;++j)if(j!=1&&out[j].rva==uint32_t(target)){repeated=true;break;}
        if(repeated)break;
        sample(uint32_t(target),out[i]);previous=uint32_t(target);
    }
}
