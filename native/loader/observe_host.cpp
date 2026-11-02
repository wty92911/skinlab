// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "observe_protocol.h"
#include "observe_hash.h"
#include "skin_catalog.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <climits>

namespace {
// Any install location; identity comes from the disk SHA256 the controller expects.
constexpr wchar_t game_image_name[]=L"League of Legends.exe";
struct Check {std::string name;bool passed;};std::vector<Check> checks;
SlObserveShared observation{};bool have_observation=false,game_mode=false,hook_removed=false;

bool session_requested=false;
std::string target_sha;uint32_t selected_pid=0;
DWORD observer_integrity=0,target_integrity=0;
void os(bool ok,const char* action){if(!ok)throw std::runtime_error(std::string(action)+": Win32 "+std::to_string(GetLastError()));}
DWORD integrity(HANDLE process) {
    HANDLE token=nullptr;os(OpenProcessToken(process,TOKEN_QUERY,&token)!=0,"Query message integrity token");
    struct Token {HANDLE value;~Token(){CloseHandle(value);}} owner{token};
    DWORD size=0;GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);
    if(!size||size>65536)throw std::runtime_error("Invalid integrity token size");
    std::vector<unsigned char> buffer(size);
    os(GetTokenInformation(token,TokenIntegrityLevel,buffer.data(),size,&size)!=0,"Read message integrity level");
    const auto* label=reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    os(IsValidSid(label->Label.Sid)!=0,"Validate integrity SID");
    const UCHAR count=*GetSidSubAuthorityCount(label->Label.Sid);
    if(!count)throw std::runtime_error("Integrity SID has no subauthority");
    return *GetSidSubAuthority(label->Label.Sid,count-1);
}
void check(const char* name,bool ok){checks.push_back({name,ok});if(!ok)throw std::runtime_error(name);}
std::string text(const char (&value)[192]) {return std::string(value,std::find(value,value+192,0));}
std::string sha256(HANDLE file) {
    SlObserveHasher hash;DWORD count=0;std::vector<UCHAR> buffer(65536);
    LARGE_INTEGER size{};os(GetFileSizeEx(file,&size)!=0,"Read target file size");
    if(size.QuadPart<=0||size.QuadPart>512ll*1024*1024)throw std::runtime_error("Target file size is outside the 512MiB bound");
    LARGE_INTEGER start{};os(SetFilePointerEx(file,start,nullptr,FILE_BEGIN)!=0,"Seek target image");
    uint64_t read=0;
    do {
        os(ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr)!=0,"Hash target image");
        if(count)hash.add(buffer.data(),count);
        read+=count;
    } while(count);
    if(read!=uint64_t(size.QuadPart))throw std::runtime_error("Target file size changed during hashing");
    const auto digest=hash.finish();return sl_observe_hex(digest.data(),digest.size());
}
struct Host {
    HANDLE mapping=nullptr,done=nullptr,ready=nullptr,job=nullptr,image_file=INVALID_HANDLE_VALUE;
    HANDLE process=nullptr;PROCESS_INFORMATION child{};
    SlObserveShared* shared=nullptr;HMODULE local_module=nullptr;HHOOK hook=nullptr;HWND window=nullptr;
    uint32_t pid=0,tid=0;uint64_t nonce=0,creation=0;std::wstring directory,name;
    explicit Host(DWORD game_pid,const std::string& expected_sha) {try {
        wchar_t exe[32768];const DWORD units=GetModuleFileNameW(nullptr,exe,32768);
        os(units&&units<32768,"Locate observer host");directory.assign(exe,units);directory.erase(directory.find_last_of(L"\\/")+1);
        os(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Generate observation nonce");
        if(!nonce)throw std::runtime_error("Invalid zero observation nonce");
        name=sl_observe_name(GetCurrentProcessId(),nonce);
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SlObserveShared),name.c_str());
        os(mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"Create unique observation mapping");
        shared=static_cast<SlObserveShared*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(SlObserveShared)));
        os(shared!=nullptr,"Map observation IPC");std::memset(shared,0,sizeof(*shared));
        done=CreateEventW(nullptr,TRUE,FALSE,(name+L"-done").c_str());os(done&&GetLastError()!=ERROR_ALREADY_EXISTS,"Create unique completion event");
        ready=CreateEventW(nullptr,FALSE,FALSE,(name+L"-ready").c_str());os(ready&&GetLastError()!=ERROR_ALREADY_EXISTS,"Create unique fixture event");
        if(game_pid) {
            pid=game_pid;
            process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);os(process!=nullptr,"Open requested game for identity/wait only");
            validate_process(std::wstring(),expected_sha);
        } else {
            const auto target=directory+L"skinlab-observe-fixture.exe";
            job=CreateJobObjectW(nullptr,nullptr);os(job!=nullptr,"Create owned-child cleanup job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            os(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))!=0,"Configure owned-child cleanup");
            auto command=L"\""+target+L"\" \""+name+L"\"";
            STARTUPINFOW startup{};startup.cb=sizeof(startup);
            os(CreateProcessW(target.c_str(),&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,directory.c_str(),&startup,&child)!=0,"Create owned observation fixture");
            process=child.hProcess;pid=child.dwProcessId;
            os(AssignProcessToJobObject(job,process)!=0,"Assign fixture cleanup job");
            validate_process(target,std::string());
        }
        selected_pid=pid;creation=sl_creation_time(process);os(creation!=0,"Read target creation identity");
        shared->magic=SL_OBSERVE_MAGIC;shared->version=SL_OBSERVE_VERSION;shared->struct_size=sizeof(*shared);
        shared->host_pid=GetCurrentProcessId();shared->target_pid=pid;shared->nonce=nonce;shared->creation_time=creation;
        shared->request_session=session_requested?1:0;
        if(!game_pid) {
            os(ResumeThread(child.hThread)!=DWORD(-1),"Start observation fixture");
            os(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,"Wait for owned fixture window");MemoryBarrier();
            window=reinterpret_cast<HWND>(shared->window);
            check("fixture_starts_without_observer_dll",!shared->fixture_module_present);
        } else {
            struct Windows {DWORD pid;std::vector<HWND> found;static BOOL CALLBACK visit(HWND window,LPARAM value){
                auto& self=*reinterpret_cast<Windows*>(value);DWORD owner=0;GetWindowThreadProcessId(window,&owner);
                if(owner==self.pid&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER))self.found.push_back(window);
                return TRUE;
            }} candidates{pid,{}};
            os(EnumWindows(Windows::visit,reinterpret_cast<LPARAM>(&candidates))!=0,"Enumerate requested game's top-level windows");
            if(candidates.found.size()!=1)throw std::runtime_error("Expected exactly one visible unowned window in the requested game process; found "+std::to_string(candidates.found.size()));
            window=candidates.found[0];shared->window=reinterpret_cast<uintptr_t>(window);
        }
        DWORD window_pid=0;tid=GetWindowThreadProcessId(window,&window_pid);
        os(tid&&window_pid==pid&&WaitForSingleObject(process,0)==WAIT_TIMEOUT,"Validate target window/process lifetime");
        if(!game_pid)check("fixture_window_matches_created_process",shared->fixture_tid==tid&&window_pid==child.dwProcessId&&pid!=GetCurrentProcessId());
    } catch(...){cleanup();throw;} }
    void validate_process(const std::wstring& expected,const std::string& expected_sha) {
        wchar_t path[32768];DWORD units=32768;os(QueryFullProcessImageNameW(process,0,path,&units)!=0,"Read requested process image path");
        const std::wstring image(path,units);
        // Game: the running image under its own name, wherever installed. Fixture: the exact owned path.
        const auto slash=image.find_last_of(L"\\/");
        const std::wstring leaf=slash==std::wstring::npos?image:image.substr(slash+1);
        if(expected.empty()?_wcsicmp(leaf.c_str(),game_image_name)!=0:_wcsicmp(image.c_str(),expected.c_str())!=0)
            throw std::runtime_error("Requested PID does not have the exact allowed image path");
        if(expected.empty()&&expected_sha.empty())throw std::runtime_error("Game observation requires an expected disk SHA256");
        BOOL wow64=FALSE;os(IsWow64Process(process,&wow64)!=0&&!wow64,"Require an x64 target");
        image_file=CreateFileW(image.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        os(image_file!=INVALID_HANDLE_VALUE,"Open allowed target image for stable hash");
        target_sha=sha256(image_file);
        if(!expected_sha.empty()&&target_sha!=expected_sha)throw std::runtime_error("Target disk SHA256 does not match the explicitly expected build");
        if(!game_mode)check("fixture_exact_path_and_disk_identity_read",target_sha.size()==64);
    }
    void install() {
        observer_integrity=integrity(GetCurrentProcess());target_integrity=integrity(process);
        if(observer_integrity<target_integrity)
            throw std::runtime_error("Target integrity exceeds observer integrity; no hook installed");
        if(!game_mode)check("integrity_preflight_passed_before_hook",observer_integrity>=target_integrity&&observer_integrity!=0);
        local_module=LoadLibraryExW((directory+L"skinlab-observe.dll").c_str(),nullptr,0x100|0x800);os(local_module!=nullptr,"Load local observation DLL");
        const auto proc=reinterpret_cast<HOOKPROC>(reinterpret_cast<void(*)()>(GetProcAddress(local_module,"skinlab_observe_hook")));os(proc!=nullptr,"Resolve observation hook export");
        DWORD owner=0;os(GetWindowThreadProcessId(window,&owner)==tid&&owner==pid&&WaitForSingleObject(process,0)==WAIT_TIMEOUT,"Revalidate target before hook installation");
        hook=SetWindowsHookExW(WH_GETMESSAGE,proc,local_module,tid);os(hook!=nullptr,"Install requested-thread observation hook");hook_removed=false;
    }
    void trigger() {
        const UINT command=RegisterWindowMessageW(SL_OBSERVE_MESSAGE);os(command!=0,"Register private observation message");
        os(PostMessageW(window,command,GetCurrentProcessId(),LPARAM(nonce))!=0,"Post private observation request");
    }
    void query_fixture() {
        ResetEvent(ready);os(PostMessageW(window,SL_O_FIXTURE_QUERY,0,0)!=0,"Query owned fixture state");
        os(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"Wait for owned fixture query");MemoryBarrier();
    }
    void wait_report() {
        HANDLE waits[]={done,process};const auto wait=WaitForMultipleObjects(2,waits,FALSE,20000);
        if(wait==WAIT_OBJECT_0+1)throw std::runtime_error("Target exited before observation completed");
        if(wait!=WAIT_OBJECT_0)throw std::runtime_error("Observation timed out or wait failed; target module residency is unknown until target exits");
        MemoryBarrier();std::memcpy(&observation,shared,sizeof(observation));have_observation=true;
        if(observation.magic!=SL_OBSERVE_MAGIC||observation.version!=SL_OBSERVE_VERSION||observation.struct_size!=sizeof(observation)
           ||observation.nonce!=nonce||observation.target_pid!=pid||observation.creation_time!=creation
           ||observation.window!=reinterpret_cast<uintptr_t>(window)||observation.host_pid!=GetCurrentProcessId()
           ||observation.result_count>SL_OBSERVE_RULES)throw std::runtime_error("Observation response identity/schema changed");
        for(size_t i=0;i<observation.result_count;++i) {
            const auto& r=observation.results[i];
            if(r.state>uint32_t(skinlab::profile::State::RereadEqual)||r.match_count>2
               ||r.has_candidate>1||(r.has_candidate&&r.state!=uint32_t(skinlab::profile::State::RereadEqual)))
                throw std::runtime_error("Observation response contains invalid result fields");
        }
        if(observation.phase!=SL_O_COMPLETE)throw std::runtime_error("Target observation failed: "+text(observation.error));
    }
    void unhook() {
        if(hook){os(UnhookWindowsHookEx(hook)!=0,"Remove observation message hook");hook=nullptr;hook_removed=true;}
        if(local_module){os(FreeLibrary(local_module)!=0,"Release local observation DLL");local_module=nullptr;}
    }
    void command(uint32_t value,uint32_t skin=0) {
        if(shared->session_state==3) {
            if(value==3)return;
            throw std::runtime_error("Session has stopped; end the target process before reconnecting");
        }
        if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT)throw std::runtime_error("Target process has exited");
        const auto started=GetTickCount64();
        while(InterlockedCompareExchange(&shared->command_lock,1,0)!=0) {
            if(GetTickCount64()-started>500)throw std::runtime_error("Command publication is busy");
            Sleep(1);
        }
        const auto sequence=InterlockedCompareExchange(&shared->command_sequence,0,0);
        if(sequence>=LONG_MAX|| (sequence!=InterlockedCompareExchange(&shared->completed_sequence,0,0)&&value!=3)) {
            InterlockedExchange(&shared->command_lock,0);throw std::runtime_error("A prior command is pending; wait or stop");
        }
        shared->session_command=value;shared->requested_skin=skin;
        InterlockedExchange(&shared->command_sequence,sequence+1);InterlockedExchange(&shared->command_lock,0);
        while(InterlockedCompareExchange(&shared->completed_sequence,0,0)!=sequence+1) {
            if(WaitForSingleObject(process,10)==WAIT_OBJECT_0)throw std::runtime_error("Target exited while processing command");
            if(GetTickCount64()-started>10000)throw std::runtime_error("No frame acknowledgement; command may still execute on a later frame");
        }
        MemoryBarrier();
    }
    void session_reply(const std::string& error="") {
        const char* names[]={"starting","ready","active","stopped","recovery","target_changed"};
        const auto state=shared->session_state;
        const bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT;
        std::cout<<"{\"event\":\"session\",\"state\":"<<std::quoted(!alive?"target_exited":(state<6?names[state]:"invalid"))<<",\"result\":"<<shared->session_result
            <<",\"model\":"<<std::quoted(std::string(shared->model_name,std::find(shared->model_name,shared->model_name+96,0)))
            <<",\"current_skin\":"<<shared->session_skin<<",\"pending\":"<<(shared->command_sequence!=shared->completed_sequence?"true":"false")
            <<",\"module_resident\":true,\"frame_hook_retained\":true,\"render_tid\":"<<shared->render_tid
            <<",\"target_pid\":"<<pid<<",\"allowed_skins\":[";
        const auto* entry=sl_catalog_model(shared->model_name);
        if(!std::strcmp(shared->model_name,"Fixture"))std::cout<<"0,1";
        else if(entry)for(uint32_t i=0;i<entry->count;++i){if(i)std::cout<<',';std::cout<<sl_catalog_numbers[entry->first+i];}
        std::cout<<"],\"error\":"<<std::quoted(error)<<"}\n"<<std::flush;
    }
    void cleanup() {
        if(process&&shared&&session_requested&&shared->phase==SL_O_COMPLETE&&shared->session_state!=3) {
            try{command(3);}catch(...){/* Retained target context handles controller exit on its next frame. */}
        }
        if(hook){if(UnhookWindowsHookEx(hook))hook_removed=true;hook=nullptr;}
        if(local_module){FreeLibrary(local_module);local_module=nullptr;}
        // Never close, kill, restart or post fixture control commands to a game.
        if(child.hProcess) {
            DWORD owner=0;if(window&&GetWindowThreadProcessId(window,&owner)&&owner==child.dwProcessId)PostMessageW(window,WM_CLOSE,0,0);
            if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,3);WaitForSingleObject(child.hProcess,3000);}
            CloseHandle(child.hThread);
        }
        if(process)CloseHandle(process);
        if(image_file!=INVALID_HANDLE_VALUE)CloseHandle(image_file);
        if(job)CloseHandle(job);
        if(shared)UnmapViewOfFile(shared);
        if(mapping)CloseHandle(mapping);
        if(done)CloseHandle(done);
        if(ready)CloseHandle(ready);
    }
    ~Host(){cleanup();}
};
void session_loop(DWORD pid,const std::string& expected) {
    Host host(pid,expected);host.install();InterlockedExchange(&host.shared->phase,SL_O_ARMED);host.trigger();host.wait_report();host.unhook();
    host.session_reply();std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream input(line);std::string command,number,extra;input>>command;
        try {
            if(command=="apply") {
                input>>number;
                if(number.empty()||number.size()>6||!std::all_of(number.begin(),number.end(),[](char c){return c>='0'&&c<='9';})||(input>>extra))
                    throw std::runtime_error("Use apply followed by a nonnegative skin number");
                host.command(1,uint32_t(std::stoul(number)));
            } else if(command=="restore")host.command(2);
            else if(command=="stop")host.command(3);
            else if(command!="status")throw std::runtime_error("Unknown session command");
            host.session_reply();
            if(command=="stop"&&host.shared->session_state==3)return;
        }catch(const std::exception& error){host.session_reply(error.what());}
    }
    host.command(3);host.session_reply();
}
void owned_test() {
    SlObserveHasher hash;hash.add("abc",3);const auto golden=hash.finish();
    check("sha256_matches_independent_abc_golden",sl_observe_hex(golden.data(),golden.size())=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    skinlab::profile::Snapshot framing;framing.image_size=0x4000;
    framing.regions={{0x1000,{0xde,0xad}},{0x2000,{0xbe,0xef,1}}};
    const auto framed=sl_observe_snapshot_hash(framing);
    check("snapshot_framing_matches_independent_python_golden",sl_observe_hex(framed.data(),framed.size())=="27e4d90614a6046221b1a9af2ed1e3efcfac38deb9e87add2a1fea229bb0c1c0");
    Host host(0,"");host.install();
    host.shared->version=SL_OBSERVE_VERSION+1;InterlockedExchange(&host.shared->phase,SL_O_ARMED);
    host.trigger();host.query_fixture();
    check("invalid_request_schema_does_not_scan",host.shared->phase==SL_O_ARMED&&WaitForSingleObject(host.done,0)==WAIT_TIMEOUT&&!host.shared->worker_tid);
    check("windows_loaded_observer_in_distinct_target",host.shared->fixture_module_present&&host.shared->target_pid==host.child.dwProcessId);
    host.shared->version=SL_OBSERVE_VERSION;host.trigger();host.wait_report();
    check("observation_has_valid_identity_and_all_rules",have_observation&&observation.result_count==16&&observation.target_pid==host.pid&&observation.nonce==host.nonce);
    check("worker_runs_outside_target_window_thread",observation.hook_tid==host.tid&&observation.worker_tid&&observation.worker_tid!=host.tid);
    check("target_current_image_code_snapshot_complete",observation.complete&&observation.expected_bytes>0&&observation.expected_bytes==observation.copied_bytes&&!observation.gap_count);
    check("target_code_snapshot_fingerprint_published",observation.snapshot_hash_present==1);
    check("diagnostic_module_explicitly_pinned",observation.module_pinned==1);
    bool missing=true;for(size_t i=0;i<observation.result_count;++i)missing=missing&&observation.results[i].state==uint32_t(skinlab::profile::State::Missing)&&!observation.results[i].has_candidate;
    check("nongame_fixture_returns_sixteen_missing_rules",missing);
    host.unhook();host.query_fixture();
    check("unhook_does_not_claim_target_dll_unloaded",hook_removed&&host.shared->fixture_module_present);
    const auto previous=host.shared->fixture_queries;host.query_fixture();
    check("target_window_still_processes_messages_after_unhook",host.shared->fixture_queries==previous+1);
    os(PostMessageW(host.window,WM_CLOSE,0,0)!=0,"Close owned observation fixture");
    os(WaitForSingleObject(host.process,5000)==WAIT_OBJECT_0,"Wait for owned fixture exit");
    DWORD exit=1;os(GetExitCodeProcess(host.process,&exit)!=0,"Read owned fixture exit");
    check("owned_fixture_exits_normally_with_resident_module",exit==0&&host.shared->fixture_exited);
    host.window=nullptr;
}
bool sha_argument(const std::wstring& value) {return value.size()==64&&std::all_of(value.begin(),value.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
void output(const std::string& error) {
    std::cout<<"{\"schema_version\":1,\"mode\":"<<std::quoted(game_mode?"game_observation":"owned_fixture_test")<<",\"status\":"<<std::quoted(error.empty()?(game_mode?"observed":"passed"):"failed")
        <<",\"target_pid\":"<<selected_pid<<",\"disk_sha256\":"<<std::quoted(target_sha)
        <<",\"rule_source_dll_sha256\":"<<std::quoted(skinlab::profile::recovered_dll_sha256())<<",\"runtime_image_disk_identity_proven\":false,\"session_requested\":"<<(session_requested?"true":"false")<<",\"engine_calls\":"<<(have_observation?observation.refresh_calls:0)<<",\"game_field_writes_possible\":"<<(session_requested?"true":"false")<<",\"hook_removed\":"<<(hook_removed?"true":"false")
        <<",\"target_module_lifetime\":\"pinned_until_target_exit_once_started; unknown_on_timeout\",\"error\":"<<std::quoted(error)<<",\"checks\":[";
    for(size_t i=0;i<checks.size();++i){if(i)std::cout<<',';std::cout<<"{\"name\":"<<std::quoted(checks[i].name)<<",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';}
    std::cout<<"],\"render_tid\":"<<observation.render_tid<<",\"refresh_calls\":"<<observation.refresh_calls<<",\"observer_integrity_rid\":"<<observer_integrity<<",\"target_integrity_rid\":"<<target_integrity<<",\"observation\":";
    if(!have_observation)std::cout<<"null";
    else {
        const std::string model(observation.model_name,std::find(observation.model_name,observation.model_name+96,0));
        std::cout<<"{\"player_read_status\":"<<observation.player_read_status<<",\"player_read_error\":"<<observation.player_read_error<<",\"player_stable\":"<<observation.player_stable<<",\"skin_fields_agree\":"<<observation.skin_fields_agree<<",\"encoded_skin\":"<<observation.encoded_skin<<",\"decoded_skin\":"<<observation.decoded_skin<<",\"stack_skin\":"<<observation.stack_skin<<",\"model_name\":"<<std::quoted(model)<<",\"phase\":"<<observation.phase<<",\"hook_tid\":"<<observation.hook_tid<<",\"worker_tid\":"<<observation.worker_tid<<",\"module_pinned\":"<<(observation.module_pinned?"true":"false")
            <<",\"win32_error\":"<<observation.win32_error<<",\"image_size\":"<<observation.image_size<<",\"collection_complete\":"<<(observation.complete?"true":"false")<<",\"code_snapshot_sha256\":";
        if(observation.snapshot_hash_present)std::cout<<std::quoted(sl_observe_hex(observation.snapshot_hash,32));
        else std::cout<<"null";
        std::cout<<",\"expected_bytes\":"<<observation.expected_bytes<<",\"copied_bytes\":"<<observation.copied_bytes<<",\"gap_count\":"<<observation.gap_count
            <<",\"collection_ms\":"<<observation.collection_ms<<",\"error\":"<<std::quoted(text(observation.error))<<",\"results\":[";
        const auto rules=skinlab::profile::recovered_rules();
        for(size_t i=0;i<std::min<uint32_t>(observation.result_count,SL_OBSERVE_RULES);++i) {
            if(i)std::cout<<',';
            const auto& r=observation.results[i];std::cout<<"{\"name\":"<<std::quoted(rules[i].name)<<",\"state\":"<<std::quoted(skinlab::profile::state_name(skinlab::profile::State(r.state)))
                <<",\"reason\":"<<std::quoted(text(r.reason))<<",\"match_count_capped\":"<<(r.match_count_capped?"true":"false")<<",\"match_rvas\":[";
            for(size_t m=0;m<std::min<uint32_t>(r.match_count,2);++m){if(m)std::cout<<',';std::cout<<r.match_rvas[m];}
            std::cout<<"],\"candidate\":";
            if(r.has_candidate)std::cout<<"{\"rva\":"<<r.rva<<",\"field_offset\":"<<r.field_offset<<",\"xor_key\":"<<r.xor_key<<",\"rotate_first\":"<<r.rotate_first<<",\"rotate_second\":"<<r.rotate_second<<'}';
            else std::cout<<"null";
            std::cout<<'}';
        }
        std::cout<<"]}";
    }
    std::cout<<"}\n";
}
}
int wmain(int argc,wchar_t** argv) {
    DWORD pid=0;std::string expected;
    const bool fixture_session=argc==2&&std::wstring(argv[1])==L"--fixture-session";
    session_requested=fixture_session;
    if((argc==5||(argc==6&&std::wstring(argv[5])==L"--session"))&&std::wstring(argv[1])==L"--observe-game"&&std::wstring(argv[4])==L"--accept-module-until-exit"&&sha_argument(argv[3])) {
        const std::wstring number=argv[2];
        if(!number.empty()&&number.size()<=10&&std::all_of(number.begin(),number.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})) {
            const auto wide=std::stoull(number);if(wide&&wide<=UINT32_MAX){pid=DWORD(wide);game_mode=true;session_requested=argc==6;expected.assign(argv[3],argv[3]+64);}
        }
    }
    if(argc!=1&&!game_mode&&!fixture_session) {
        std::cerr<<"Usage: skinlab-observe-host.exe [--observe-game PID EXPECTED_DISK_SHA256 --accept-module-until-exit]\n";return 2;
    }
    std::string error;
    try {
        if(session_requested){session_loop(pid,expected);return 0;}
        if(game_mode){Host host(pid,expected);host.install();InterlockedExchange(&host.shared->phase,SL_O_ARMED);host.trigger();host.wait_report();host.unhook();}
        else owned_test();
    }catch(const std::exception& e){error=e.what();}
    output(error);return error.empty()?0:1;
}
