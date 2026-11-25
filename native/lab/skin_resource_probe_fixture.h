// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
bool check_skin_resource_probe() {
    uint8_t image[8192]{},object[0x300]{};uintptr_t entries[11]{},table=reinterpret_cast<uintptr_t>(entries);
    const auto base=reinterpret_cast<uintptr_t>(image);
    const auto& notify18=SkinResourceUpdate::notify18;
    const auto& notify20=SkinResourceUpdate::notify20;
    std::memcpy(image+0x100+0x5b7,notify18,sizeof(notify18));std::memcpy(image+0x100+0x605,notify20,sizeof(notify20));
    std::memcpy(image+0x100,SkinResourceUpdate::entry,sizeof(SkinResourceUpdate::entry));
    std::memcpy(image+0x100+0x576,SkinResourceUpdate::notification_flow,sizeof(SkinResourceUpdate::notification_flow));
    SkinResourceUpdate update{};update.notifications=SkinResourceUpdate::admitted(base,sizeof(image),0x100);
    if(!update.notifications)return false;
    struct Flags {bool called=false,initialize=true;} flags;
    auto engine=+[](void* context,bool initialize){auto& f=*static_cast<Flags*>(context);f.called=true;f.initialize=initialize;};
    update.invoke(reinterpret_cast<uintptr_t>(engine),&flags);
    if(!flags.called||flags.initialize)return false;
    image[0x100+0x5b7]^=1;
    update.notifications=SkinResourceUpdate::admitted(base,sizeof(image),0x100);flags={};
    update.invoke(reinterpret_cast<uintptr_t>(engine),&flags);
    if(update.notifications||!flags.called||!flags.initialize)return false;
    image[0x100+0x5b7]^=1;
    // Keeping both virtual-call windows is insufficient: changed conditional
    // control flow or argument storage must reject the parameter mapping.
    image[0x100+0x576+8]^=1;
    if(SkinResourceUpdate::admitted(base,sizeof(image),0x100))return false;
    image[0x100+0x576+8]^=1;image[0x100]^=1;
    if(SkinResourceUpdate::admitted(base,sizeof(image),0x100))return false;
    image[0x100]^=1;
    for(auto& entry:entries)entry=base+0x1000;
    image[0x1000]=0xc3;
    const auto owner=reinterpret_cast<uintptr_t>(&table);std::memcpy(object+0x100+0x130,&owner,8);
    uint64_t identity=9;SlConfig config{};config.object_data=object;config.layout.character_stack_offset=0x100;
    config.context=&identity;config.expected_generation=9;
    config.generation=[](void* context)->uint64_t{return *static_cast<uint64_t*>(context);};
    SlSkinResourceProbe probe{};SlCodeProbe code[SL_CODE_PROBES]{};
    collect_skin_resource_probe(config,base,sizeof(image),0x100,probe,code);
    if(probe.read_status!=1||probe.owner!=owner||probe.table!=table||probe.owner_member!=0x130)return false;
    for(size_t i=0;i<9;++i)if(code[i].rva!=0x1000||code[i].size!=4096||code[i].bytes[0]!=0xc3)return false;
    // Synthetic callbacks are never executed; the sample byte is just RET.
    if(code[9].size||entries[3]!=base+0x1000)return false;
    image[0x100+0x5b7]^=1;
    collect_skin_resource_probe(config,base,sizeof(image),0x100,probe,code);
    if(probe.read_status!=3||code[0].size)return false;
    image[0x100+0x5b7]^=1;
    struct Change {uintptr_t* entries;uintptr_t trigger;} change{entries,base+0x1000};
    fixture_read_context=&change;fixture_read_callback=[](void* context,uintptr_t address) {
        auto& value=*static_cast<Change*>(context);
        if(address==value.trigger){fixture_read_callback=nullptr;value.entries[3]=0;}
    };
    collect_skin_resource_probe(config,base,sizeof(image),0x100,probe,code);
    fixture_read_callback=nullptr;fixture_read_context=nullptr;
    if(probe.read_status!=5||code[0].size||probe.owner)return false;
    identity=10;collect_skin_resource_probe(config,base,sizeof(image),0x100,probe,code);
    return probe.read_status==4&&!code[0].size;
}
