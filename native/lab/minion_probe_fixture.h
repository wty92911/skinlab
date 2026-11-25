// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
bool check_minion_probe() {
    alignas(8) uint8_t hero[16384]{},pet[16384]{},manager[256]{};
    uintptr_t list[256]{},slot=reinterpret_cast<uintptr_t>(manager);uint64_t identity=9;
    const char* name="MalzaharVoidling";const auto length=uint32_t(std::strlen(name));
    std::memcpy(pet+0x98,&name,8);std::memcpy(pet+0xa0,&length,4);
    const uint32_t skin=18;std::memcpy(pet+0xb8,&skin,4);
    list[0]=reinterpret_cast<uintptr_t>(pet);list[1]=list[0];list[2]=1;
    const auto array=reinterpret_cast<uintptr_t>(list);std::memcpy(manager+8,&array,8);
    SlConfig config{};config.layout.character_stack_offset=0x80;config.object_data=hero;
    config.expected_generation=9;config.context=&identity;
    config.generation=[](void* context)->uint64_t{return *static_cast<uint64_t*>(context);};
    SlMinionProbe out{};
    collect_minion_probe(config,reinterpret_cast<uintptr_t>(&slot),1,0,0,0,0,"Malzahar",out);
    if(out.read_status!=1||out.array_count!=1||out.object_count!=1||out.player_size!=sizeof(hero)
       ||out.budget_exhausted||out.objects[0].address!=list[0]||out.objects[0].read_status!=1
       ||out.objects[0].prefix_size!=sizeof(pet)||std::strcmp(out.objects[0].model,name)
       ||std::memcmp(out.objects[0].prefix,pet,sizeof(pet))||list[0]!=list[1])return false;
    collect_minion_probe(config,reinterpret_cast<uintptr_t>(&slot),1,0,0,0,0,"Quinn",out);
    if(out.read_status!=1||out.object_count)return false;
    identity=10;collect_minion_probe(config,reinterpret_cast<uintptr_t>(&slot),1,0,0,0,0,"Malzahar",out);
    if(out.read_status!=4||out.array_count||out.object_count)return false;
    identity=9;slot=1;collect_minion_probe(config,reinterpret_cast<uintptr_t>(&slot),1,0,0,0,0,"Malzahar",out);
    return out.read_status==2&&!out.array_count&&!out.object_count;
}
