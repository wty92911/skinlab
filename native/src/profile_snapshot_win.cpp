#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <skinlab/profile_scan.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace skinlab { namespace profile {
namespace {
constexpr uint32_t max_image=128u*1024u*1024u;
bool read_address(uintptr_t address,void* out,size_t size) {
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&copied)&&copied==size;
}
bool readable(DWORD protection) {
    if(protection&(PAGE_GUARD|PAGE_NOACCESS))return false;
    switch(protection&0xff) {
    case PAGE_READONLY:case PAGE_READWRITE:case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:case PAGE_EXECUTE_READWRITE:case PAGE_EXECUTE_WRITECOPY:return true;
    default:return false;
    }
}
bool executable(DWORD protection) {
    if(!readable(protection))return false;
    return (protection&0xff)==PAGE_EXECUTE_READ||(protection&0xff)==PAGE_EXECUTE_READWRITE
        ||(protection&0xff)==PAGE_EXECUTE_WRITECOPY;
}
struct Reference {HMODULE value=nullptr;~Reference(){if(value)FreeLibrary(value);}};
template<typename T>T read_header(uintptr_t base,uint32_t offset) {
    T out{};
    if(!read_address(base+offset,&out,sizeof(out)))throw std::runtime_error("Cannot read current-module PE header");
    return out;
}
void append_range(Snapshot& s,uint32_t rva,uint32_t size,bool rd,bool ex) {
    if(!s.ranges.empty()) {
        auto& last=s.ranges.back();
        if(uint64_t(last.rva)+last.size==rva&&last.readable==rd&&last.executable==ex) {last.size+=size;return;}
    }
    s.ranges.push_back({rva,size,rd,ex});
}
void append_bytes(Snapshot& s,uint32_t rva,const uint8_t* data,size_t size) {
    if(s.regions.empty()||uint64_t(s.regions.back().rva)+s.regions.back().bytes.size()!=rva)
        s.regions.push_back({rva,{}});
    auto& bytes=s.regions.back().bytes;bytes.insert(bytes.end(),data,data+size);s.copied_bytes+=size;
}
}
bool read_current_rva(void* context,uint32_t rva,void* out,size_t size) {
    if(!context||!out)return false;
    const auto& s=*static_cast<Snapshot*>(context);
    if(!s.module_base||rva>s.image_size||size>s.image_size-rva
       ||s.module_base>UINTPTR_MAX-s.image_size)return false;
    // The kernel copy reports failures instead of dereferencing a potentially
    // unmapped/guarded page. No remote handle, privilege or protection changes.
    return read_address(s.module_base+rva,out,size);
}
Snapshot collect_current_module(void* module,uint32_t budget) {
    Snapshot s;const ULONGLONG started=GetTickCount64();
    try {
        if(!module||!budget||budget>10000)throw std::runtime_error("Invalid module or collection budget");
        Reference reference;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&reference.value)
           ||reference.value!=module)throw std::runtime_error("Address is not a loaded current-process module base");
        const uintptr_t base=reinterpret_cast<uintptr_t>(module);s.module_base=base;
        MEMORY_BASIC_INFORMATION initial{};
        if(!VirtualQuery(module,&initial,sizeof(initial))||initial.Type!=MEM_IMAGE||initial.AllocationBase!=module)
            throw std::runtime_error("Module does not have MEM_IMAGE provenance");
        const auto dos=read_header<IMAGE_DOS_HEADER>(base,0);
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<LONG(sizeof(dos))||dos.e_lfanew>1024*1024)
            throw std::runtime_error("Invalid DOS header or bounded PE offset");
        const uint32_t nt_offset=uint32_t(dos.e_lfanew);
        const auto nt=read_header<IMAGE_NT_HEADERS64>(base,nt_offset);
        if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC
           ||nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64)
           ||!nt.FileHeader.NumberOfSections||nt.FileHeader.NumberOfSections>96)
            throw std::runtime_error("Unsupported PE machine, header or section count");
        s.image_size=nt.OptionalHeader.SizeOfImage;
        const uint64_t table=uint64_t(nt_offset)+4+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        if(!s.image_size||s.image_size>max_image||base>UINTPTR_MAX-s.image_size
           ||nt.OptionalHeader.SizeOfHeaders>s.image_size
           ||table+uint64_t(nt.FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>nt.OptionalHeader.SizeOfHeaders)
            throw std::runtime_error("PE image or section table exceeds bounds");
        struct Section {uint32_t rva,size;bool code;};std::vector<Section> sections;
        for(uint32_t i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto section=read_header<IMAGE_SECTION_HEADER>(base,uint32_t(table+i*sizeof(IMAGE_SECTION_HEADER)));
            const uint32_t size=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
            if(!size)continue;
            if(section.VirtualAddress<nt.OptionalHeader.SizeOfHeaders||section.VirtualAddress>s.image_size
               ||size>s.image_size-section.VirtualAddress)throw std::runtime_error("Section extent is outside the PE image");
            sections.push_back({section.VirtualAddress,size,(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0});
        }
        std::sort(sections.begin(),sections.end(),[](const Section& a,const Section& b){return a.rva<b.rva;});
        uint64_t previous=0;
        for(const auto& section:sections) {
            if(section.rva<previous)throw std::runtime_error("Overlapping PE sections");
            previous=uint64_t(section.rva)+section.size;
            if(section.code)s.expected_bytes+=section.size;
        }
        if(!s.expected_bytes)throw std::runtime_error("No executable PE sections");
        bool timed_out=false;
        std::vector<uint8_t> chunk(65536);
        for(const auto& section:sections) {
            const uint32_t end=section.rva+section.size;
            uint32_t cursor=section.rva;
            while(cursor<end) {
                if(GetTickCount64()-started>=budget) {timed_out=true;break;}
                MEMORY_BASIC_INFORMATION memory{};
                const uintptr_t address=base+cursor;
                if(!VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory))
                   ||reinterpret_cast<uintptr_t>(memory.BaseAddress)>address||!memory.RegionSize
                   ||reinterpret_cast<uintptr_t>(memory.BaseAddress)>UINTPTR_MAX-memory.RegionSize
                   ||reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize<=address)
                    throw std::runtime_error("VirtualQuery failed or returned an invalid extent");
                const uint64_t remaining=reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize-address;
                const uint32_t size=uint32_t(std::min<uint64_t>(end-cursor,remaining));
                const bool same=memory.Type==MEM_IMAGE&&memory.AllocationBase==module&&memory.State==MEM_COMMIT;
                const bool rd=same&&readable(memory.Protect),ex=rd&&executable(memory.Protect);
                append_range(s,cursor,size,rd,ex);
                if(section.code) {
                    if(!ex)s.gaps.push_back({cursor,size,ERROR_NOACCESS});
                    else for(uint32_t copied=0;copied<size;) {
                        if(GetTickCount64()-started>=budget) {timed_out=true;break;}
                        const uint32_t count=std::min<uint32_t>(65536,size-copied);
                        if(read_address(address+copied,chunk.data(),count))append_bytes(s,cursor+copied,chunk.data(),count);
                        else s.gaps.push_back({cursor+copied,count,GetLastError()});
                        copied+=count;
                    }
                }
                if(timed_out)break;
                cursor+=size;
            }
            if(timed_out)break;
        }
        if(timed_out)s.error="Collection deadline exceeded";
        s.complete=!timed_out&&s.gaps.empty()&&s.copied_bytes==s.expected_bytes;
    } catch(const std::exception& error) {s.error=error.what();s.complete=false;}
    s.elapsed_ms=GetTickCount64()-started;
    return s;
}
} }
