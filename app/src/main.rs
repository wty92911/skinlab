// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod settings;

use settings::Settings;
use skinlab_core::{Catalog, SupportedBuilds};
use skinlab_session::game::{FixtureBackend, GameBackend};
use skinlab_session::mock::MockBackend;
use skinlab_session::{Backend, Controller, ControllerHandle, NativeBundle, Snapshot, Timing};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;
use tauri::{AppHandle, Emitter, Manager, WindowEvent};
use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut, ShortcutState};

const SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(20);
/// Bump when the usage notice text changes so users see it again.
const NOTICE_VERSION: u32 = 1;
const REPOSITORY: &str = env!("CARGO_PKG_REPOSITORY");
const LICENSE: &str = "PolyForm Noncommercial 1.0.0";
const KEEP_SESSION_LOGS: usize = 30;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
enum Mode {
    /// The real game.
    Game,
    /// SkinLab's own WARP window through the real native host.
    Fixture,
    /// In-memory backend for UI work; never starts a process.
    Demo,
}

impl Mode {
    fn from_env() -> Mode {
        let arg = std::env::args().skip(1).find(|a| a.starts_with("--"));
        let env = std::env::var("SKINLAB_MODE").ok();
        match arg.as_deref().or(env.as_deref()) {
            Some("--fixture" | "fixture") => Mode::Fixture,
            Some("--demo" | "demo") => Mode::Demo,
            _ if cfg!(windows) => Mode::Game,
            _ => Mode::Demo,
        }
    }
}

struct AppState {
    controller: ControllerHandle,
    settings: Mutex<Settings>,
    settings_path: PathBuf,
    log_dir: PathBuf,
    mode: Mode,
    hotkey: bool,
    closing: AtomicBool,
    _log_guard: tracing_appender::non_blocking::WorkerGuard,
}

#[derive(serde::Serialize)]
struct AppInfo {
    version: &'static str,
    commit: &'static str,
    repository: &'static str,
    license: &'static str,
    notice_version: u32,
    mode: Mode,
    hotkey: bool,
    catalog_version: String,
    champions: usize,
    skins: usize,
    supported_builds: Vec<String>,
}

#[tauri::command]
fn get_snapshot(state: tauri::State<'_, AppState>) -> Snapshot {
    state.controller.snapshot()
}

#[tauri::command]
fn select_skin(state: tauri::State<'_, AppState>, skin: u32) {
    state.controller.select(skin);
}

#[tauri::command]
fn restore_skin(state: tauri::State<'_, AppState>) {
    state.controller.restore();
}

#[tauri::command]
fn refresh(state: tauri::State<'_, AppState>) {
    state.controller.refresh();
}

#[tauri::command]
fn app_info(state: tauri::State<'_, AppState>) -> AppInfo {
    let catalog = Catalog::embedded();
    AppInfo {
        version: env!("CARGO_PKG_VERSION"),
        commit: env!("SKINLAB_COMMIT"),
        repository: REPOSITORY,
        license: LICENSE,
        notice_version: NOTICE_VERSION,
        mode: state.mode,
        hotkey: state.hotkey,
        champions: catalog.champions.len(),
        skins: catalog.skin_count(),
        catalog_version: catalog.version,
        supported_builds: SupportedBuilds::embedded().builds.into_iter().map(|b| b.version).collect(),
    }
}

#[tauri::command]
fn get_settings(state: tauri::State<'_, AppState>) -> Settings {
    state.settings.lock().expect("settings lock").clone()
}

#[tauri::command]
fn set_settings(app: AppHandle, state: tauri::State<'_, AppState>, mut settings: Settings) -> Result<(), String> {
    // Only `accept_notice` records acknowledgement; a stale UI copy cannot undo it.
    settings.notice_accepted = state.settings.lock().expect("settings lock").notice_accepted;
    apply_window_settings(&app, &settings);
    settings.save(&state.settings_path).map_err(|e| format!("无法保存设置：{e}"))?;
    *state.settings.lock().expect("settings lock") = settings;
    Ok(())
}

/// Records that the usage notice was read, then lets the controller connect.
#[tauri::command]
fn accept_notice(state: tauri::State<'_, AppState>) -> Result<(), String> {
    let mut settings = state.settings.lock().expect("settings lock");
    settings.notice_accepted = NOTICE_VERSION;
    settings.save(&state.settings_path).map_err(|e| format!("无法保存设置：{e}"))?;
    state.controller.set_paused(false);
    Ok(())
}

/// Same path as closing the window: restore, stop, exit.
#[tauri::command]
fn quit_app(app: AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.close();
    }
}

#[tauri::command]
fn open_repository() -> Result<(), String> {
    let opener = if cfg!(windows) { "explorer" } else { "open" };
    std::process::Command::new(opener).arg(REPOSITORY).spawn().map(drop).map_err(|e| e.to_string())
}

#[tauri::command]
fn open_logs(state: tauri::State<'_, AppState>) -> Result<(), String> {
    let opener = if cfg!(windows) { "explorer" } else { "open" };
    std::process::Command::new(opener).arg(&state.log_dir).spawn().map(drop).map_err(|e| e.to_string())
}

fn apply_window_settings(app: &AppHandle, settings: &Settings) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.set_always_on_top(settings.always_on_top);
    }
}

fn toggle_main_window(app: &AppHandle) {
    let Some(window) = app.get_webview_window("main") else { return };
    let visible = window.is_visible().unwrap_or(false) && !window.is_minimized().unwrap_or(false);
    if visible {
        let _ = window.hide();
    } else {
        let _ = window.unminimize();
        let _ = window.show();
        let _ = window.set_focus();
    }
}

/// Installed: `<resources>/native`. Development: `SKINLAB_NATIVE_DIR`, then the
/// repository's `out/current.json`.
fn native_bundle(app: &AppHandle) -> NativeBundle {
    if let Ok(dir) = std::env::var("SKINLAB_NATIVE_DIR") {
        return NativeBundle::new(dir);
    }
    if cfg!(debug_assertions) {
        let repo = Path::new(env!("CARGO_MANIFEST_DIR")).parent().expect("app crate lives in the repository");
        if let Some(bundle) = NativeBundle::from_repo(repo) {
            return bundle;
        }
    }
    let installed = app.path().resource_dir().map(|d| d.join("native")).unwrap_or_default();
    NativeBundle::new(installed)
}

fn init_logging(dir: &Path) -> anyhow::Result<tracing_appender::non_blocking::WorkerGuard> {
    use tracing_subscriber::{EnvFilter, fmt, prelude::*};
    std::fs::create_dir_all(dir)?;
    let file = tracing_appender::rolling::Builder::new()
        .rotation(tracing_appender::rolling::Rotation::DAILY)
        .filename_prefix("skinlab")
        .filename_suffix("log")
        .max_log_files(7)
        .build(dir)?;
    let (writer, guard) = tracing_appender::non_blocking(file);
    let filter = EnvFilter::try_from_env("SKINLAB_LOG").unwrap_or_else(|_| EnvFilter::new("info"));
    tracing_subscriber::registry()
        .with(filter)
        .with(fmt::layer().with_writer(writer).with_ansi(false))
        .with(cfg!(debug_assertions).then(|| fmt::layer().with_writer(std::io::stderr)))
        .try_init()?;
    Ok(guard)
}

/// Keeps the newest session transcripts only.
fn prune_session_logs(dir: &Path) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    let mut logs: Vec<PathBuf> = entries
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().is_some_and(|x| x == "jsonl"))
        .collect();
    logs.sort();
    let excess = logs.len().saturating_sub(KEEP_SESSION_LOGS);
    for old in &logs[..excess] {
        let _ = std::fs::remove_file(old);
    }
}

/// `SKINLAB_DEMO_HERO=Lux` picks the demo champion; otherwise a different one each launch.
fn demo_hero(catalog: &Catalog) -> String {
    if let Ok(model) = std::env::var("SKINLAB_DEMO_HERO")
        && let Some(champion) = catalog.champion(&model)
    {
        return champion.id.clone();
    }
    let seed = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.subsec_nanos());
    catalog.champions[seed as usize % catalog.champions.len()].id.clone()
}

fn setup(app: &mut tauri::App) -> Result<(), Box<dyn std::error::Error>> {
    let handle = app.handle().clone();
    let log_dir = app.path().app_log_dir()?;
    let guard = init_logging(&log_dir)?;
    let session_logs = log_dir.join("sessions");
    prune_session_logs(&session_logs);

    let mode = Mode::from_env();
    tracing::info!(version = env!("CARGO_PKG_VERSION"), ?mode, "starting");

    let settings_path = app.path().app_config_dir()?.join("settings.json");
    let settings = Settings::load(&settings_path);
    apply_window_settings(&handle, &settings);

    let mut catalog = Catalog::embedded();
    let backend: Box<dyn Backend> = match mode {
        Mode::Game => {
            Box::new(GameBackend::new(SupportedBuilds::embedded(), native_bundle(&handle), Some(session_logs)))
        }
        Mode::Fixture => {
            catalog.champions.push(FixtureBackend::champion());
            Box::new(FixtureBackend::new(native_bundle(&handle), Some(session_logs)))
        }
        Mode::Demo => Box::new(MockBackend::demo(&demo_hero(&catalog))),
    };
    let emitter = handle.clone();
    let controller = Controller::new(backend, catalog, Timing::default())
        .paused(settings.notice_accepted < NOTICE_VERSION)
        .on_change(move |snapshot| {
            let _ = emitter.emit("snapshot", snapshot);
        })
        .spawn();

    let hotkey = match app.global_shortcut().register(Shortcut::new(None, Code::Insert)) {
        Ok(()) => true,
        Err(error) => {
            tracing::warn!(%error, "Insert hotkey unavailable");
            false
        }
    };

    app.manage(AppState {
        controller,
        settings: Mutex::new(settings),
        settings_path,
        log_dir,
        mode,
        hotkey,
        closing: AtomicBool::new(false),
        _log_guard: guard,
    });
    Ok(())
}

fn main() {
    let shortcuts = tauri_plugin_global_shortcut::Builder::new()
        .with_handler(|app, shortcut, event| {
            if event.state() == ShortcutState::Pressed && shortcut.key == Code::Insert {
                toggle_main_window(app);
            }
        })
        .build();

    tauri::Builder::default()
        .plugin(shortcuts)
        .setup(setup)
        .invoke_handler(tauri::generate_handler![
            get_snapshot,
            select_skin,
            restore_skin,
            refresh,
            app_info,
            get_settings,
            set_settings,
            accept_notice,
            quit_app,
            open_repository,
            open_logs
        ])
        .on_window_event(|window, event| {
            let WindowEvent::CloseRequested { api, .. } = event else { return };
            let app = window.app_handle().clone();
            let state = app.state::<AppState>();
            if state.closing.swap(true, Ordering::SeqCst) {
                return;
            }
            // Restore the original skin and stop the session before the process exits.
            api.prevent_close();
            let controller = state.controller.clone();
            std::thread::spawn(move || {
                if !controller.shutdown(SHUTDOWN_TIMEOUT) {
                    tracing::warn!("controller did not confirm shutdown in time");
                }
                app.exit(0);
            });
        })
        .run(tauri::generate_context!())
        .expect("error while running SkinLab");
}
