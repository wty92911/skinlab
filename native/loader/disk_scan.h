// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Included by observe_host.cpp. Reads a PE as data; never loads or executes it.
struct DiskImage {
    HANDLE file;
    struct Section {uint32_t rva,size,raw,raw_size;};
    std::vector<Section> sections;
    skinlab::profile::Snapshot snapshot;
    uint64_t length=0;
    explicit DiskImage(HANDLE input):file(input) {
        LARGE_INTEGER size{};os(GetFileSizeEx(file,&size)!=0,"Read disk image size");
        if(size.QuadPart<=0||size.QuadPart>512ll*1024*1024)throw std::runtime_error("Invalid disk image size");
        length=uint64_t(size.QuadPart);
        const auto dos=header<IMAGE_DOS_HEADER>(0);
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<LONG(sizeof(dos))||dos.e_lfanew>1024*1024)
            throw std::runtime_error("Invalid disk DOS header");
        const auto nt=header<IMAGE_NT_HEADERS64>(uint32_t(dos.e_lfanew));
        if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC
           ||nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64)
           ||!nt.FileHeader.NumberOfSections||nt.FileHeader.NumberOfSections>96)
            throw std::runtime_error("Unsupported disk PE header");
        snapshot.image_size=nt.OptionalHeader.SizeOfImage;
        const uint64_t table=uint64_t(dos.e_lfanew)+4+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        if(!snapshot.image_size||snapshot.image_size>128u*1024u*1024u
           ||nt.OptionalHeader.SizeOfHeaders>snapshot.image_size
           ||table+uint64_t(nt.FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>nt.OptionalHeader.SizeOfHeaders)
            throw std::runtime_error("Disk PE image or section table exceeds bounds");
        struct Entry {Section section;bool read,code;};std::vector<Entry> entries;
        for(uint32_t i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto s=header<IMAGE_SECTION_HEADER>(table+i*sizeof(IMAGE_SECTION_HEADER));
            const auto extent=std::max(s.Misc.VirtualSize,s.SizeOfRawData);
            if(!extent)continue;
            if(s.VirtualAddress<nt.OptionalHeader.SizeOfHeaders||s.VirtualAddress>snapshot.image_size
               ||extent>snapshot.image_size-s.VirtualAddress||s.PointerToRawData>length
               ||s.SizeOfRawData>length-s.PointerToRawData)
                throw std::runtime_error("Disk PE section exceeds bounds");
            entries.push_back({{s.VirtualAddress,extent,s.PointerToRawData,s.SizeOfRawData},
                (s.Characteristics&IMAGE_SCN_MEM_READ)!=0,(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0});
        }
        std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.section.rva<b.section.rva;});
        uint64_t end=0;
        for(const auto& entry:entries) {
            const auto& s=entry.section;
            if(s.rva<end)throw std::runtime_error("Overlapping disk PE sections");
            end=uint64_t(s.rva)+s.size;sections.push_back(s);
            snapshot.ranges.push_back({s.rva,s.size,entry.read,entry.code});
            if(entry.code) {
                if(!entry.read)throw std::runtime_error("Unreadable disk code section");
                // Zero-fill describes the PE's disk image, not unpacked runtime code.
                skinlab::profile::Region region{s.rva,std::vector<uint8_t>(s.size)};
                read(s.raw,region.bytes.data(),s.raw_size);
                snapshot.expected_bytes+=s.size;snapshot.copied_bytes+=s.size;
                snapshot.regions.push_back(std::move(region));
            }
        }
        if(snapshot.regions.empty())throw std::runtime_error("No disk executable sections");
        snapshot.complete=true;
    }
    void read(uint64_t offset,void* output,size_t size) {
        if(offset>length||size>length-offset||size>MAXDWORD)throw std::runtime_error("Disk read exceeds bounds");
        LARGE_INTEGER position{};position.QuadPart=LONGLONG(offset);DWORD count=0;
        os(SetFilePointerEx(file,position,nullptr,FILE_BEGIN)!=0,"Seek disk image");
        os(ReadFile(file,output,DWORD(size),&count,nullptr)!=0&&count==size,"Read disk image");
    }
    template<typename T>T header(uint64_t offset) {T value{};read(offset,&value,sizeof(value));return value;}
    static bool reread(void* context,uint32_t rva,void* output,size_t size) {
        auto& image=*static_cast<DiskImage*>(context);
        for(const auto& s:image.sections)if(rva>=s.rva&&rva-s.rva<s.raw_size&&size<=s.raw_size-(rva-s.rva)) {
            try {image.read(uint64_t(s.raw)+rva-s.rva,output,size);return true;}catch(...){return false;}
        }
        return false;
    }
};
bool required_disk_rule(const std::string& name) {
    const char* names[]={"local_player","character_data_stack","skin_id_encoding","skin_id_decoding_with_subtract","character_data_stack_update",
                         "material_registry_getter","swap_chain"};
    return std::any_of(std::begin(names),std::end(names),[&](const char* value){return name==value;});
}
int scan_disk_image(const wchar_t* path) {
    std::string error,digest;std::vector<skinlab::profile::Result> results;
    try {
        HANDLE input=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        os(input!=INVALID_HANDLE_VALUE,"Open disk image");
        struct Owner {HANDLE file;~Owner(){CloseHandle(file);}} owner{input};
        digest=sha256(input);DiskImage image(input);
        results=skinlab::profile::analyze(image.snapshot,skinlab::profile::recovered_rules(),DiskImage::reread,&image);
    }catch(const std::exception& e){error=e.what();}
    size_t unique=0;
    for(const auto& r:results)if(required_disk_rule(r.name)&&r.name!="skin_id_encoding"&&r.name!="skin_id_decoding_with_subtract"&&r.has_candidate)++unique;
    const auto* codec=skinlab::profile::skin_codec(results);
    std::cout<<"{\"schema_version\":1,\"scope\":\"disk\",\"disk_sha256\":"<<std::quoted(digest)
        <<",\"required_rules_unique\":"<<(unique==5&&codec?"true":"false")
        <<",\"runtime_compatible\":null,\"error\":"<<std::quoted(error)<<",\"results\":[";
    for(size_t i=0;i<results.size();++i) {
        const auto& r=results[i];if(i)std::cout<<',';
        std::cout<<"{\"name\":"<<std::quoted(r.name)<<",\"required\":"<<(required_disk_rule(r.name)?"true":"false")
            <<",\"state\":"<<std::quoted(r.has_candidate?"unique_disk_reread_equal":skinlab::profile::state_name(r.state))
            <<",\"match_rvas\":[";
        for(size_t j=0;j<r.matches.size();++j){if(j)std::cout<<',';std::cout<<r.matches[j];}
        std::cout<<"],\"candidate\":";
        if(r.has_candidate)std::cout<<"{\"rva\":"<<r.candidate.rva<<",\"field_offset\":"<<r.candidate.field_offset
            <<",\"xor_key\":"<<r.candidate.xor_key<<",\"rotate_first\":"<<unsigned(r.candidate.rotate_first)
            <<",\"rotate_second\":"<<unsigned(r.candidate.rotate_second)<<",\"codec_kind\":"<<r.candidate.codec_kind
            <<",\"add_key\":"<<r.candidate.add_key<<",\"referenced_stack_offset\":"<<r.candidate.referenced_stack_offset<<'}';
        else std::cout<<"null";
        std::cout<<'}';
    }
    std::cout<<"]}\n";return error.empty()?0:1;
}
