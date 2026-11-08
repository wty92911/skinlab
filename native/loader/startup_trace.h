// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Debug-bundle-only startup evidence independent of the shared IPC mapping.
// Called only for our registered request, never from DllMain or each frame.
void startup_trace(const char* step,DWORD error=0,uint64_t a=0,uint64_t b=0) noexcept {
#ifdef SKINLAB_STARTUP_TRACE
    const DWORD saved=GetLastError();
    static volatile LONG records=0;
    if(InterlockedIncrement(&records)<=96) {
        HMODULE module=nullptr;wchar_t path[32768]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&startup_trace),&module)) {
            const auto length=GetModuleFileNameW(module,path,32768);
            if(length&&length<32600) {
                wchar_t* leaf=path;
                for(wchar_t* p=path;*p;++p)if(*p==L'\\'||*p==L'/')leaf=p+1;
                std::swprintf(leaf,160,L"startup-hook-%lu.log",static_cast<unsigned long>(GetCurrentProcessId()));
                HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                    nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(file!=INVALID_HANDLE_VALUE) {
                    char line[384]{};
                    const int count=std::snprintf(line,sizeof(line),"tick=%llu pid=%lu tid=%lu step=%s error=%lu a=%llu b=%llu\r\n",
                        static_cast<unsigned long long>(GetTickCount64()),static_cast<unsigned long>(GetCurrentProcessId()),
                        static_cast<unsigned long>(GetCurrentThreadId()),step,static_cast<unsigned long>(error),
                        static_cast<unsigned long long>(a),static_cast<unsigned long long>(b));
                    DWORD written=0;
                    if(count>0&&size_t(count)<sizeof(line))WriteFile(file,line,DWORD(count),&written,nullptr);
                    CloseHandle(file);
                } else {
                    // An independent fallback for a read-only bundle directory.
                    OutputDebugStringA("SkinLab startup trace file could not be opened");
                }
            }
        }
    }
    SetLastError(saved);
#else
    (void)step;(void)error;(void)a;(void)b;
#endif
}
