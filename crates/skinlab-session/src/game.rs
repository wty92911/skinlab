// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Game discovery and the two native backends: the real game and SkinLab's
//! own WARP fixture. The install location is read from the running process,
//! so no path is configured anywhere.

use crate::backend::{Backend, ConnectError, Session};
use crate::bundle::{NativeBundle, hex};
use crate::host::HostSession;
use crate::live::LiveClient;
use sha2::{Digest, Sha256};
use skinlab_core::builds::DiskBuildCheck;
use skinlab_core::live::ActiveHero;
use skinlab_core::{Champion, Skin, SupportedBuilds};
use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

pub const GAME_EXE: &str = "League of Legends.exe";
pub const FIXTURE_MODEL: &str = "Fixture";

pub fn sha256_file(path: &Path) -> std::io::Result<String> {
    let mut file = std::fs::File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0u8; 1 << 16];
    loop {
        let count = file.read(&mut buffer)?;
        if count == 0 {
            return Ok(hex(&hasher.finalize()));
        }
        hasher.update(&buffer[..count]);
    }
}

/// Runs the host's data-only PE scan. Does not attach to a process or load a DLL.
pub fn check_build(host: &Path, exe: &Path) -> std::io::Result<DiskBuildCheck> {
    let mut command = std::process::Command::new(host);
    command.arg("--scan-image").arg(exe);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let output = command.output()?;
    let check: DiskBuildCheck = serde_json::from_slice(&output.stdout)?;
    if check.schema_version != 1
        || check.scope != "disk"
        || check.runtime_compatible.is_some()
        || (output.status.success() != check.error.is_empty())
    {
        return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "无效的文件检查报告"));
    }
    Ok(check)
}

/// Hashing the game executable takes a moment; reuse it while size and mtime hold.
#[derive(Default)]
pub struct HashCache {
    entries: HashMap<PathBuf, (u64, SystemTime, String)>,
}

impl HashCache {
    pub fn sha256(&mut self, path: &Path) -> std::io::Result<String> {
        let meta = std::fs::metadata(path)?;
        let key = (meta.len(), meta.modified()?);
        if let Some((len, modified, sha)) = self.entries.get(path)
            && (*len, *modified) == key
        {
            return Ok(sha.clone());
        }
        let sha = sha256_file(path)?;
        self.entries.insert(path.to_path_buf(), (key.0, key.1, sha.clone()));
        Ok(sha)
    }
}

#[cfg(windows)]
mod win {
    use std::ffi::OsString;
    use std::os::windows::ffi::{OsStrExt, OsStringExt};
    use std::path::{Path, PathBuf};
    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::Storage::FileSystem::{
        GetFileVersionInfoSizeW, GetFileVersionInfoW, VS_FIXEDFILEINFO, VerQueryValueW,
    };
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, PROCESSENTRY32W, Process32FirstW, Process32NextW, TH32CS_SNAPPROCESS,
    };
    use windows_sys::Win32::System::Threading::{
        OpenProcess, PROCESS_NAME_WIN32, PROCESS_QUERY_LIMITED_INFORMATION, QueryFullProcessImageNameW,
    };

    struct Handle(HANDLE);
    impl Drop for Handle {
        fn drop(&mut self) {
            // SAFETY: the handle was returned open by a successful Win32 call.
            unsafe { CloseHandle(self.0) };
        }
    }

    fn wide(text: &str) -> Vec<u16> {
        text.encode_utf16().chain(Some(0)).collect()
    }

    pub fn process_ids(name: &str) -> Vec<u32> {
        // SAFETY: plain snapshot call; result checked before use.
        let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if snapshot == INVALID_HANDLE_VALUE {
            return Vec::new();
        }
        let snapshot = Handle(snapshot);
        // SAFETY: PROCESSENTRY32W is plain data; zero is a valid bit pattern.
        let mut entry: PROCESSENTRY32W = unsafe { std::mem::zeroed() };
        entry.dwSize = size_of::<PROCESSENTRY32W>() as u32;
        let mut found = Vec::new();
        // SAFETY: entry is a properly sized PROCESSENTRY32W owned by this frame.
        let mut more = unsafe { Process32FirstW(snapshot.0, &mut entry) } != 0;
        while more {
            let len = entry.szExeFile.iter().position(|&c| c == 0).unwrap_or(entry.szExeFile.len());
            let exe = OsString::from_wide(&entry.szExeFile[..len]);
            if exe.to_string_lossy().eq_ignore_ascii_case(name) {
                found.push(entry.th32ProcessID);
            }
            // SAFETY: as above.
            more = unsafe { Process32NextW(snapshot.0, &mut entry) } != 0;
        }
        found
    }

    pub fn image_path(pid: u32) -> std::io::Result<PathBuf> {
        // SAFETY: query-only access; result checked.
        let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
        if process.is_null() {
            return Err(std::io::Error::last_os_error());
        }
        let process = Handle(process);
        let mut buffer = vec![0u16; 32768];
        let mut len = buffer.len() as u32;
        // SAFETY: buffer holds `len` u16 units.
        let ok = unsafe { QueryFullProcessImageNameW(process.0, PROCESS_NAME_WIN32, buffer.as_mut_ptr(), &mut len) };
        if ok == 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(PathBuf::from(OsString::from_wide(&buffer[..len as usize])))
    }

    pub fn file_version(path: &Path) -> Option<String> {
        let name: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        let mut ignored = 0u32;
        // SAFETY: name is NUL-terminated.
        let size = unsafe { GetFileVersionInfoSizeW(name.as_ptr(), &mut ignored) };
        if size == 0 {
            return None;
        }
        let mut data = vec![0u8; size as usize];
        // SAFETY: data holds `size` bytes.
        if unsafe { GetFileVersionInfoW(name.as_ptr(), 0, size, data.as_mut_ptr().cast()) } == 0 {
            return None;
        }
        let mut info: *mut core::ffi::c_void = std::ptr::null_mut();
        let mut len = 0u32;
        let root = wide("\\");
        // SAFETY: data is the version block filled above; VerQueryValueW points into it.
        if unsafe { VerQueryValueW(data.as_ptr().cast(), root.as_ptr(), &mut info, &mut len) } == 0
            || info.is_null()
            || (len as usize) < size_of::<VS_FIXEDFILEINFO>()
        {
            return None;
        }
        // SAFETY: checked non-null and large enough; points inside `data`.
        let info = unsafe { &*(info as *const VS_FIXEDFILEINFO) };
        Some(format!(
            "{}.{}.{}.{}",
            info.dwFileVersionMS >> 16,
            info.dwFileVersionMS & 0xffff,
            info.dwFileVersionLS >> 16,
            info.dwFileVersionLS & 0xffff
        ))
    }
}

pub fn game_pids() -> Vec<u32> {
    #[cfg(windows)]
    return win::process_ids(GAME_EXE);
    #[cfg(not(windows))]
    Vec::new()
}

pub fn image_path(pid: u32) -> std::io::Result<PathBuf> {
    #[cfg(windows)]
    return win::image_path(pid);
    #[cfg(not(windows))]
    {
        let _ = pid;
        Err(std::io::Error::new(std::io::ErrorKind::Unsupported, "Windows only"))
    }
}

pub fn file_version(path: &Path) -> Option<String> {
    #[cfg(windows)]
    return win::file_version(path);
    #[cfg(not(windows))]
    {
        let _ = path;
        None
    }
}

/// The real game.
pub struct GameBackend {
    builds: SupportedBuilds,
    bundle: NativeBundle,
    live: LiveClient,
    hashes: HashCache,
    log_dir: Option<PathBuf>,
}

impl GameBackend {
    pub fn new(builds: SupportedBuilds, bundle: NativeBundle, log_dir: Option<PathBuf>) -> Self {
        GameBackend { builds, bundle, live: LiveClient::default(), hashes: HashCache::default(), log_dir }
    }
}

impl Backend for GameBackend {
    fn game_pids(&mut self) -> Vec<u32> {
        game_pids()
    }

    fn active_hero(&mut self) -> Option<ActiveHero> {
        self.live.active_hero()
    }

    fn game_version(&mut self, pid: u32) -> Option<String> {
        file_version(&image_path(pid).ok()?)
    }

    fn connect(&mut self, pid: u32) -> Result<Box<dyn Session>, ConnectError> {
        let path = image_path(pid).map_err(|e| ConnectError::Setup(format!("无法读取对局进程信息：{e}")))?;
        let is_game = path.file_name().is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case(GAME_EXE));
        if !is_game {
            return Err(ConnectError::Setup("该进程不是英雄联盟对局进程".into()));
        }
        let sha256 = self.hashes.sha256(&path).map_err(|e| ConnectError::Setup(format!("无法读取游戏文件：{e}")))?;
        let version = file_version(&path).unwrap_or_else(|| "未知".into());
        tracing::info!(%version, reference_build = self.builds.find(&sha256).is_some(), pid, "resolving current game code");
        let host = self.bundle.verify().map_err(|e| ConnectError::Setup(e.to_string()))?;
        let args = [
            "--observe-game".to_string(),
            pid.to_string(),
            sha256,
            "--accept-module-until-exit".into(),
            "--session".into(),
        ];
        Ok(Box::new(HostSession::spawn(&host, &args, self.log_dir.as_deref())?))
    }
}

/// SkinLab's own hidden WARP window; exercises the whole native path without a game.
pub struct FixtureBackend {
    bundle: NativeBundle,
    log_dir: Option<PathBuf>,
}

pub const FIXTURE_PID: u32 = u32::MAX;

impl FixtureBackend {
    pub fn new(bundle: NativeBundle, log_dir: Option<PathBuf>) -> Self {
        FixtureBackend { bundle, log_dir }
    }

    /// Catalog entry the controller needs to show the fixture's two skins.
    pub fn champion() -> Champion {
        let skin = |id, name: &str| Skin {
            skin_id: id,
            name: name.into(),
            art: String::new(),
            chromas: Vec::new(),
            requires_special_handling: None,
            rarity: if id == 0 { "base".into() } else { "Epic".into() },
        };
        Champion {
            id: FIXTURE_MODEL.into(),
            key: String::new(),
            name: "自建演示".into(),
            art: String::new(),
            skins: vec![skin(0, "原始皮肤"), skin(1, "测试皮肤 1")],
        }
    }
}

impl Backend for FixtureBackend {
    fn game_pids(&mut self) -> Vec<u32> {
        vec![FIXTURE_PID]
    }

    fn active_hero(&mut self) -> Option<ActiveHero> {
        Some(ActiveHero { model: FIXTURE_MODEL.into(), display_name: "自建演示".into(), skin_id: Some(0) })
    }

    fn game_version(&mut self, _pid: u32) -> Option<String> {
        Some("fixture".into())
    }

    fn connect(&mut self, _pid: u32) -> Result<Box<dyn Session>, ConnectError> {
        let host = self.bundle.verify().map_err(|e| ConnectError::Setup(e.to_string()))?;
        let args = ["--fixture-session".to_string()];
        Ok(Box::new(HostSession::spawn(&host, &args, self.log_dir.as_deref())?))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hash_cache_rehashes_after_change() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("game.exe");
        std::fs::write(&path, b"abc").unwrap();
        let mut cache = HashCache::default();
        assert_eq!(cache.sha256(&path).unwrap(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::fs::write(&path, b"abcd").unwrap();
        assert_ne!(cache.sha256(&path).unwrap(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
}
