// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
bool check_owner_probe() {
    uint8_t image[4096]{};SlCodeProbe code[SL_CODE_PROBES]{};
    const uint8_t wrapper[]={0x48,0x81,0xc1,0x68,0x05,0x00,0x00,0xe9};
    std::memcpy(image+0x100,wrapper,sizeof(wrapper));
    int32_t displacement=0x300-(0x100+12);std::memcpy(image+0x108,&displacement,4);
    image[0x300]=0xe9;displacement=0x500-(0x300+5);std::memcpy(image+0x301,&displacement,4);image[0x500]=0xc3;
    collect_owner_code(reinterpret_cast<uintptr_t>(image),sizeof(image),0x100,0x200,code);
    if(code[0].rva!=0x100||code[1].rva!=0x200||code[2].rva!=0x300||code[3].rva!=0x500||code[4].size
       ||code[0].size!=sizeof(image)-0x100||code[3].bytes[0]!=0xc3||std::memcmp(image+0x100,wrapper,sizeof(wrapper)))return false;
    image[0x500]=0xe9;displacement=0x100-(0x500+5);std::memcpy(image+0x501,&displacement,4);
    collect_owner_code(reinterpret_cast<uintptr_t>(image),sizeof(image),0x100,0x200,code);
    if(code[4].size)return false;
    displacement=0x2000-(0x100+12);std::memcpy(image+0x108,&displacement,4);
    collect_owner_code(reinterpret_cast<uintptr_t>(image),sizeof(image),0x100,0x200,code);
    return !code[2].size;
}
