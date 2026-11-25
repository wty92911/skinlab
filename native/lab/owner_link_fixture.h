// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
bool check_owner_link() {
    uint8_t image[4096]{},object[1536]{},control[16]{};
    const uint8_t wrapper[]={0x48,0x81,0xc1,0x68,0x05,0x00,0x00,0xe9};
    std::memcpy(image+0x100,wrapper,sizeof(wrapper));
    int32_t displacement=0x400-(0x100+12);std::memcpy(image+0x108,&displacement,4);
    std::memcpy(image+0x400,WeakSkinSource::body,sizeof(WeakSkinSource::body));
    WeakSkinSource reader{};const auto base=reinterpret_cast<uintptr_t>(image);
    if(!reader.bind(base,sizeof(image),0x100)||reader.member!=0x568)return false;
    uintptr_t cell=0x10000000,other=0x20000000;
    const uintptr_t holder[]={reinterpret_cast<uintptr_t>(&cell),reinterpret_cast<uintptr_t>(control)};
    std::memcpy(object+0x568,holder,sizeof(holder));int32_t count=2;std::memcpy(control+8,&count,4);
    auto link=reader.read(reinterpret_cast<uintptr_t>(object));
    int32_t after=0;std::memcpy(&after,control+8,4);
    if(link.status!=1||link.source!=cell||link.strong!=2||after!=2)return false;
    count=0;std::memcpy(control+8,&count,4);link=reader.read(reinterpret_cast<uintptr_t>(object));
    if(link.status!=1||link.source)return false;
    count=2;std::memcpy(control+8,&count,4);
    struct Change {uint8_t* object;uintptr_t* cell;uintptr_t* other;} change{object,&cell,&other};
    fixture_read_context=&change;
    fixture_read_callback=[](void* context,uintptr_t address) {
        auto& value=*static_cast<Change*>(context);
        if(address==reinterpret_cast<uintptr_t>(value.cell)) {
            fixture_read_callback=nullptr;const auto replacement=reinterpret_cast<uintptr_t>(value.other);
            std::memcpy(value.object+0x568,&replacement,8);
        }
    };
    link=reader.read(reinterpret_cast<uintptr_t>(object));fixture_read_callback=nullptr;fixture_read_context=nullptr;
    if(link.status!=5||link.source)return false;
    image[0x400]^=1;
    if(reader.bind(base,sizeof(image),0x100)||reader.member)return false;
    image[0x400]^=1;
    const uint32_t invalid=65535;std::memcpy(image+0x103,&invalid,4);
    return !reader.bind(base,sizeof(image),0x100)&&!reader.member;
}
