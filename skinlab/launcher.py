"""Small Windows-only launcher helpers. No game access on import."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import sys
import threading


def game_pids():
    k=c.WinDLL('kernel32',use_last_error=True)
    class Entry(c.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',c.c_size_t),('module',w.DWORD),
                  ('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('name',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Process32FirstW.argtypes=[w.HANDLE,c.POINTER(Entry)];k.Process32NextW.argtypes=[w.HANDLE,c.POINTER(Entry)]
    k.CloseHandle.argtypes=[w.HANDLE]
    handle=k.CreateToolhelp32Snapshot(2,0)
    if handle==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
    result=[]
    try:
        entry=Entry();entry.size=c.sizeof(entry)
        found=k.Process32FirstW(handle,c.byref(entry))
        while found:
            if entry.name.casefold()=='league of legends.exe':result.append(entry.pid)
            found=k.Process32NextW(handle,c.byref(entry))
    finally:k.CloseHandle(handle)
    return result


def is_admin():
    return bool(c.windll.shell32.IsUserAnAdmin())


def elevate_launcher():
    script=Path(__file__).resolve().parents[1]/'SkinLab.pyw'
    executable=Path(sys.executable)
    pythonw=executable.with_name('pythonw.exe')
    if pythonw.exists():executable=pythonw
    shell=c.WinDLL('shell32',use_last_error=True)
    shell.ShellExecuteW.argtypes=[w.HWND,w.LPCWSTR,w.LPCWSTR,w.LPCWSTR,w.LPCWSTR,c.c_int]
    shell.ShellExecuteW.restype=c.c_void_p
    result=shell.ShellExecuteW(None,'runas',str(executable),subprocess.list2cmdline([str(script)]),str(script.parent),1)
    if not result or result<=32:raise OSError('管理员启动已取消或失败')


class InsertHotkey:
    """Optional standard thread hotkey; no keyboard hook or game interaction."""
    def __init__(self,callback):
        self.callback=callback;self.tid=0;self.error=None;self.ready=threading.Event()
        self.thread=threading.Thread(target=self._run,daemon=True);self.thread.start()
        if not self.ready.wait(2):raise OSError('快捷键线程尚未就绪')
        if self.error:raise OSError(self.error)

    def _run(self):
        user=c.WinDLL('user32',use_last_error=True);kernel=c.WinDLL('kernel32')
        user.RegisterHotKey.argtypes=[w.HWND,c.c_int,w.UINT,w.UINT]
        user.GetMessageW.argtypes=[c.POINTER(w.MSG),w.HWND,w.UINT,w.UINT]
        user.PeekMessageW.argtypes=[c.POINTER(w.MSG),w.HWND,w.UINT,w.UINT,w.UINT]
        self.tid=kernel.GetCurrentThreadId();message=w.MSG()
        user.PeekMessageW(c.byref(message),None,0,0,0)
        if not user.RegisterHotKey(None,0x534c,0x4000,0x2d):
            self.error='Insert 快捷键被占用或无法注册';self.ready.set();return
        self.ready.set()
        try:
            while user.GetMessageW(c.byref(message),None,0,0)>0:
                if message.message==0x312:self.callback()
        finally:user.UnregisterHotKey(None,0x534c)

    def close(self):
        if self.tid:c.windll.user32.PostThreadMessageW(self.tid,0x12,0,0)
        if self.thread is not threading.current_thread():self.thread.join(timeout=1)
