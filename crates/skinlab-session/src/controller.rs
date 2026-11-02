// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Auto-connect state machine: find the single running game, wait for the
//! champion, connect once, apply the latest selection, restore on exit.
//!
//! All backend calls happen on the controller thread, one at a time. Requests
//! that arrive while a call is in flight are coalesced, so rapid selections
//! finish on the most recent choice.

use crate::backend::{Backend, ConnectError, Session};
use serde::Serialize;
use skinlab_core::live::ActiveHero;
use skinlab_core::{Catalog, Champion, Command, HostFailureKind, HostReply, LuxForm, Rarity};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender, channel};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Phase {
    NoGame,
    MultipleGames,
    WaitingHero,
    Connecting,
    Ready,
    Applying,
    /// Game build has no verified adaptation.
    Unsupported,
    /// This game process cannot be connected again (failure or stopped session).
    Blocked,
    Closing,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct HeroView {
    pub model: String,
    pub name: String,
    pub key: String,
    pub in_catalog: bool,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct SkinView {
    pub id: u32,
    pub name: String,
    pub rarity: Rarity,
    pub rarity_label: &'static str,
    /// Needs form/stack handling the shared update path does not provide yet.
    pub special: bool,
    pub chromas: bool,
    /// CN website art identifier (`key * 1000 + id`), when known.
    pub art_id: Option<u64>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Snapshot {
    pub phase: Phase,
    pub message: String,
    pub hero: Option<HeroView>,
    pub skins: Vec<SkinView>,
    pub current_skin: Option<u32>,
    pub original_skin: Option<u32>,
    pub desired_skin: Option<u32>,
    pub current_form: Option<LuxForm>,
    pub desired_form: Option<LuxForm>,
    pub forms: Vec<FormView>,
    pub game_version: Option<String>,
    pub catalog_version: String,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct FormView {
    pub id: LuxForm,
    pub name: &'static str,
}

#[derive(Debug, Clone, Copy)]
pub struct Timing {
    pub tick: Duration,
    /// How long to wait for the Live Client API before connecting without it.
    pub hero_grace: Duration,
    pub retry_after: Duration,
}

impl Default for Timing {
    fn default() -> Self {
        Timing {
            tick: Duration::from_millis(1500),
            hero_grace: Duration::from_secs(8),
            retry_after: Duration::from_secs(5),
        }
    }
}

pub enum Request {
    Select(u32),
    SelectForm(LuxForm),
    Restore,
    /// Rescan now instead of waiting for the next tick.
    Refresh,
    /// While paused the controller never connects (e.g. before the user has
    /// acknowledged the usage notice). An existing session is left alone.
    SetPaused(bool),
    /// Restore, stop and exit the controller thread; replies when done.
    Shutdown(Sender<()>),
}

type Notify = Box<dyn FnMut(&Snapshot) + Send>;

pub struct Controller {
    backend: Box<dyn Backend>,
    catalog: Catalog,
    timing: Timing,
    session: Option<Box<dyn Session>>,
    target: Option<u32>,
    seen_at: Instant,
    retry_at: Option<Instant>,
    blocked: Option<u32>,
    restore_requested: bool,
    paused: bool,
    snapshot: Snapshot,
    shared: Arc<Mutex<Snapshot>>,
    notify: Notify,
}

#[derive(Clone)]
pub struct ControllerHandle {
    sender: Sender<Request>,
    shared: Arc<Mutex<Snapshot>>,
}

impl ControllerHandle {
    pub fn snapshot(&self) -> Snapshot {
        self.shared.lock().expect("snapshot lock").clone()
    }

    pub fn select(&self, skin: u32) {
        let _ = self.sender.send(Request::Select(skin));
    }

    pub fn select_form(&self, form: LuxForm) {
        let _ = self.sender.send(Request::SelectForm(form));
    }

    pub fn restore(&self) {
        let _ = self.sender.send(Request::Restore);
    }

    pub fn refresh(&self) {
        let _ = self.sender.send(Request::Refresh);
    }

    pub fn set_paused(&self, paused: bool) {
        let _ = self.sender.send(Request::SetPaused(paused));
    }

    /// Restores the original skin and stops the session. Returns once the
    /// controller has finished, or after `timeout`.
    pub fn shutdown(&self, timeout: Duration) -> bool {
        let (done, wait) = channel();
        self.sender.send(Request::Shutdown(done)).is_ok() && wait.recv_timeout(timeout).is_ok()
    }
}

impl Controller {
    pub fn new(backend: Box<dyn Backend>, catalog: Catalog, timing: Timing) -> Self {
        let snapshot = Snapshot {
            phase: Phase::NoGame,
            message: "请先打开游戏".into(),
            hero: None,
            skins: Vec::new(),
            current_skin: None,
            original_skin: None,
            desired_skin: None,
            current_form: None,
            desired_form: None,
            forms: Vec::new(),
            game_version: None,
            catalog_version: catalog.version.clone(),
        };
        Controller {
            backend,
            catalog,
            timing,
            session: None,
            target: None,
            seen_at: Instant::now(),
            retry_at: None,
            blocked: None,
            restore_requested: false,
            paused: false,
            shared: Arc::new(Mutex::new(snapshot.clone())),
            snapshot,
            notify: Box::new(|_| {}),
        }
    }

    pub fn on_change(mut self, notify: impl FnMut(&Snapshot) + Send + 'static) -> Self {
        self.notify = Box::new(notify);
        self
    }

    /// Starts without connecting until `set_paused(false)`.
    pub fn paused(mut self, paused: bool) -> Self {
        self.paused = paused;
        self
    }

    pub fn snapshot(&self) -> &Snapshot {
        &self.snapshot
    }

    pub fn spawn(self) -> ControllerHandle {
        let (sender, receiver) = channel();
        let handle = ControllerHandle { sender, shared: self.shared.clone() };
        std::thread::Builder::new()
            .name("skinlab-controller".into())
            .spawn(move || self.run(receiver))
            .expect("spawn controller thread");
        handle
    }

    fn run(mut self, requests: Receiver<Request>) {
        self.tick();
        let mut next_tick = Instant::now() + self.timing.tick;
        loop {
            let wait = next_tick.saturating_duration_since(Instant::now());
            let first = match requests.recv_timeout(wait) {
                Ok(request) => Some(request),
                Err(RecvTimeoutError::Timeout) => None,
                Err(RecvTimeoutError::Disconnected) => {
                    self.end_session("SkinLab 已退出", None);
                    return;
                }
            };
            let mut tick = first.is_none();
            for request in first.into_iter().chain(requests.try_iter()) {
                match request {
                    Request::Select(skin) => self.select(skin),
                    Request::SelectForm(form) => self.select_form(form),
                    Request::Restore => self.restore_requested = true,
                    Request::Refresh => tick = true,
                    Request::SetPaused(paused) => {
                        self.paused = paused;
                        tick = true;
                    }
                    Request::Shutdown(done) => {
                        self.shutdown();
                        let _ = done.send(());
                        return;
                    }
                }
            }
            if tick {
                self.tick();
                next_tick = Instant::now() + self.timing.tick;
            }
            self.drive();
        }
    }

    fn publish(&mut self) {
        *self.shared.lock().expect("snapshot lock") = self.snapshot.clone();
        (self.notify)(&self.snapshot);
    }

    fn set(&mut self, phase: Phase, message: impl Into<String>) {
        self.snapshot.phase = phase;
        self.snapshot.message = message.into();
        self.publish();
    }

    fn show_hero(&mut self, model: &str, fallback_name: &str) {
        if self.snapshot.hero.as_ref().is_some_and(|h| h.model.eq_ignore_ascii_case(model)) {
            return;
        }
        let champion = self.catalog.champion(model).cloned();
        self.snapshot.skins = champion.as_ref().map(skin_views).unwrap_or_default();
        self.snapshot.hero = Some(match champion {
            Some(c) => HeroView { model: c.id, name: c.name, key: c.key, in_catalog: true },
            None => HeroView {
                model: model.into(),
                name: if fallback_name.is_empty() { model.into() } else { fallback_name.into() },
                key: String::new(),
                in_catalog: false,
            },
        });
    }

    fn clear_game(&mut self) {
        self.target = None;
        self.blocked = None;
        self.retry_at = None;
        self.snapshot.hero = None;
        self.snapshot.skins.clear();
        self.snapshot.game_version = None;
        self.snapshot.current_skin = None;
        self.snapshot.original_skin = None;
        self.snapshot.desired_skin = None;
        self.snapshot.current_form = None;
        self.snapshot.desired_form = None;
        self.snapshot.forms.clear();
    }

    /// One discovery pass. Public for tests; the controller thread calls it on a timer.
    pub fn tick(&mut self) {
        if self.paused && self.session.is_none() {
            return;
        }
        let pids = self.backend.game_pids();
        let single = (pids.len() == 1).then(|| pids[0]);

        if self.session.is_some() {
            if single.is_none() || single != self.target {
                self.end_session("对局已结束 · 等待下一局", None);
                self.clear_game();
                self.set(Phase::NoGame, "对局已结束 · 等待下一局");
            } else if self.session.as_ref().and_then(|s| s.last()).is_some_and(|r| r.state.is_final()) {
                self.end_session("本局会话已结束 · 等待下一局", self.target);
            }
            return;
        }

        let Some(pid) = single else {
            if self.target.is_some() || self.snapshot.phase != Phase::NoGame || pids.len() > 1 {
                self.clear_game();
                if pids.is_empty() {
                    self.set(Phase::NoGame, "请先打开游戏");
                } else {
                    self.set(Phase::MultipleGames, "检测到多个对局进程，请只保留一个");
                }
            }
            return;
        };

        if self.target != Some(pid) {
            self.clear_game();
            self.target = Some(pid);
            self.seen_at = Instant::now();
            self.snapshot.game_version = self.backend.game_version(pid);
        }
        let hero: Option<ActiveHero> = self.backend.active_hero();
        if let Some(hero) = &hero {
            self.show_hero(&hero.model, &hero.display_name);
        }
        if self.blocked == Some(pid) || self.retry_at.is_some_and(|at| Instant::now() < at) {
            self.publish();
            return;
        }
        if hero.is_none() && self.seen_at.elapsed() < self.timing.hero_grace {
            self.set(Phase::WaitingHero, "等待英雄载入…");
            return;
        }
        self.connect(pid);
    }

    fn connect(&mut self, pid: u32) {
        self.set(Phase::Connecting, "正在载入…");
        match self.backend.connect(pid) {
            Ok(session) => {
                let reply = session.last().cloned();
                self.session = Some(session);
                self.retry_at = None;
                if let Some(reply) = reply {
                    self.show_hero(&reply.model, "");
                    self.snapshot.current_skin = Some(reply.current_skin);
                    self.snapshot.original_skin = Some(reply.current_skin);
                    self.show_forms(&reply);
                }
                let message = if self.snapshot.hero.as_ref().is_some_and(|h| !h.in_catalog) {
                    "目录中没有该英雄，请更新 SkinLab"
                } else {
                    "选择皮肤即生效"
                };
                self.set(Phase::Ready, message);
            }
            Err(error) if error.retryable() => {
                self.retry_at = Some(Instant::now() + self.timing.retry_after);
                self.set(Phase::WaitingHero, error.to_string());
            }
            Err(error @ ConnectError::UnsupportedBuild { .. }) => {
                self.blocked = Some(pid);
                self.set(Phase::Unsupported, error.to_string());
            }
            Err(error) => {
                tracing::warn!(%error, "connect failed");
                self.blocked = Some(pid);
                self.set(Phase::Blocked, error.to_string());
            }
        }
    }

    /// Records a selection; applied by the next `drive`. Public for tests.
    pub fn select(&mut self, skin: u32) {
        if self.session.is_none() || !self.snapshot.skins.iter().any(|s| s.id == skin) {
            return;
        }
        self.snapshot.desired_skin = Some(skin);
        self.snapshot.desired_form = None;
        self.publish();
    }

    pub fn select_form(&mut self, form: LuxForm) {
        if self.session.is_none()
            || self.snapshot.current_skin != Some(7)
            || self.snapshot.desired_skin.is_some_and(|skin| skin != 7)
            || !self.snapshot.forms.iter().any(|available| available.id == form)
        {
            return;
        }
        self.snapshot.desired_form = (Some(form) != self.snapshot.current_form).then_some(form);
        self.publish();
    }

    fn show_forms(&mut self, reply: &HostReply) {
        self.snapshot.current_form = reply.current_form;
        self.snapshot.forms = if reply.model == "Lux" {
            reply.available_forms.iter().map(|&id| FormView { id, name: id.label() }).collect()
        } else {
            Vec::new()
        };
    }

    pub fn request_restore(&mut self) {
        self.restore_requested = true;
    }

    /// Sends at most one engine command toward the desired state. Public for tests.
    pub fn drive(&mut self) {
        if self.session.is_none() {
            self.restore_requested = false;
            self.snapshot.desired_form = None;
            return;
        }
        let command = if std::mem::take(&mut self.restore_requested) {
            self.snapshot.desired_skin = None;
            self.snapshot.desired_form = None;
            Command::Restore
        } else {
            match self.snapshot.desired_skin {
                Some(wanted) if Some(wanted) != self.snapshot.current_skin => Command::Apply(wanted),
                _ if self.snapshot.desired_form.is_some_and(|form| Some(form) != self.snapshot.current_form) => {
                    Command::Form(self.snapshot.desired_form.expect("checked above"))
                }
                _ => return,
            }
        };
        self.set(Phase::Applying, if command == Command::Restore { "正在恢复…" } else { "正在切换…" });
        let session = self.session.as_mut().expect("checked above");
        match session.command(command) {
            Ok(reply) => {
                self.snapshot.current_skin = Some(reply.current_skin);
                self.show_forms(&reply);
                self.snapshot.desired_form = None;
                if self.snapshot.desired_skin == Some(reply.current_skin) || command == Command::Restore {
                    self.snapshot.desired_skin = None;
                }
                let done = if command == Command::Restore { "已恢复原皮" } else { "已生效" };
                self.set(Phase::Ready, done);
            }
            Err(error) => {
                self.snapshot.desired_skin = None;
                self.snapshot.desired_form = None;
                if let Some(last) = &error.last {
                    self.snapshot.current_skin = Some(last.current_skin);
                    self.show_forms(last);
                }
                let ended = error.last.as_ref().is_some_and(|r| r.state.is_final())
                    || matches!(error.failure.kind, HostFailureKind::TargetExited | HostFailureKind::TargetChanged);
                if ended {
                    self.end_session(&error.failure.message, self.target);
                } else {
                    self.set(Phase::Ready, error.failure.message);
                }
            }
        }
    }

    /// Closes any session. `block` keeps the given process from being reconnected.
    fn end_session(&mut self, message: &str, block: Option<u32>) {
        if let Some(mut session) = self.session.take()
            && let Err(error) = session.close()
        {
            tracing::warn!(%error, "session close failed");
        }
        self.snapshot.current_skin = None;
        self.snapshot.original_skin = None;
        self.snapshot.desired_skin = None;
        self.snapshot.current_form = None;
        self.snapshot.desired_form = None;
        self.snapshot.forms.clear();
        if block.is_some() {
            self.blocked = block;
            self.set(Phase::Blocked, message);
        }
    }

    pub fn shutdown(&mut self) {
        if self.session.is_some() {
            self.set(Phase::Closing, "正在恢复原皮并退出…");
            self.end_session("已退出", None);
        }
    }
}

fn skin_views(champion: &Champion) -> Vec<SkinView> {
    champion
        .skins
        .iter()
        .map(|skin| SkinView {
            id: skin.skin_id,
            name: skin.name.clone(),
            rarity: skin.rarity(),
            rarity_label: skin.rarity().label(),
            special: skin.needs_special_handling(),
            chromas: skin.has_chroma_variants,
            art_id: champion.art_id(skin.skin_id),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock::{MockBackend, MockGame};

    fn timing() -> Timing {
        Timing { tick: Duration::from_millis(1), hero_grace: Duration::ZERO, retry_after: Duration::ZERO }
    }

    fn controller(game: &Arc<Mutex<MockGame>>) -> Controller {
        Controller::new(Box::new(MockBackend::new(game.clone())), Catalog::embedded(), timing())
    }

    #[test]
    fn no_game_prompts_to_open() {
        let game = MockGame::shared(None);
        let mut c = controller(&game);
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::NoGame);
        assert!(c.snapshot().hero.is_none());
    }

    #[test]
    fn connects_and_applies_latest_selection() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game);
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::Ready);
        assert_eq!(c.snapshot().hero.as_ref().unwrap().model, "Ahri");
        assert_eq!(c.snapshot().current_skin, Some(0));

        let ids: Vec<u32> = c.snapshot().skins.iter().map(|s| s.id).collect();
        c.select(ids[1]);
        c.select(ids[2]);
        c.drive();
        assert_eq!(c.snapshot().current_skin, Some(ids[2]));
        assert_eq!(c.snapshot().desired_skin, None);
        assert_eq!(game.lock().unwrap().applied, vec![ids[2]]);
    }

    #[test]
    fn unknown_skin_is_ignored() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game);
        c.tick();
        c.select(99_999);
        c.drive();
        assert!(game.lock().unwrap().applied.is_empty());
    }

    #[test]
    fn manual_forms_follow_latest_choice_and_restore_with_skin() {
        let game = MockGame::shared(Some("Lux"));
        let mut c = controller(&game);
        c.tick();
        assert_eq!(c.snapshot().forms.len(), 10);
        c.select_form(LuxForm::Air);
        c.drive();
        assert!(game.lock().unwrap().applied_forms.is_empty());

        c.select(7);
        c.drive();
        c.select_form(LuxForm::Air);
        c.select_form(LuxForm::Fire);
        c.drive();
        assert_eq!(c.snapshot().current_form, Some(LuxForm::Fire));
        assert_eq!(game.lock().unwrap().applied_forms, vec![LuxForm::Fire]);

        c.select_form(LuxForm::Air);
        c.select(1);
        c.drive();
        assert_eq!(c.snapshot().current_skin, Some(1));
        assert_eq!(c.snapshot().current_form, None);
        assert_eq!(c.snapshot().desired_form, None);
        assert_eq!(game.lock().unwrap().applied_forms, vec![LuxForm::Fire]);

        c.select(7);
        c.drive();
        c.select_form(LuxForm::Air);
        c.drive();
        c.request_restore();
        c.drive();
        assert_eq!(c.snapshot().current_skin, Some(0));
        assert_eq!(c.snapshot().current_form, None);
        c.shutdown();
        assert!(c.snapshot().forms.is_empty());
        assert!(game.lock().unwrap().closed);
    }

    #[test]
    fn restore_returns_to_original() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game);
        c.tick();
        let id = c.snapshot().skins[1].id;
        c.select(id);
        c.drive();
        c.request_restore();
        c.drive();
        assert_eq!(c.snapshot().current_skin, Some(0));
    }

    #[test]
    fn game_exit_ends_session_and_resets() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game);
        c.tick();
        game.lock().unwrap().running = false;
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::NoGame);
        assert!(game.lock().unwrap().closed);
    }

    #[test]
    fn unsupported_build_blocks_until_next_game() {
        let game = MockGame::shared(Some("Ahri"));
        game.lock().unwrap().unsupported = true;
        let mut c = controller(&game);
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::Unsupported);
        c.tick();
        assert_eq!(game.lock().unwrap().connects, 1);
    }

    #[test]
    fn not_ready_is_retried() {
        let game = MockGame::shared(Some("Ahri"));
        game.lock().unwrap().not_ready = 1;
        let mut c = controller(&game);
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::WaitingHero);
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::Ready);
    }

    #[test]
    fn paused_controller_does_not_connect() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game).paused(true);
        c.tick();
        assert_eq!((c.snapshot().phase, game.lock().unwrap().connects), (Phase::NoGame, 0));
        c.paused = false;
        c.tick();
        assert_eq!(c.snapshot().phase, Phase::Ready);
    }

    #[test]
    fn shutdown_closes_session() {
        let game = MockGame::shared(Some("Ahri"));
        let mut c = controller(&game);
        c.tick();
        c.shutdown();
        assert!(game.lock().unwrap().closed);
    }

    #[test]
    fn threaded_handle_round_trip() {
        let game = MockGame::shared(Some("Ahri"));
        let handle = controller(&game).spawn();
        let deadline = Instant::now() + Duration::from_secs(2);
        while handle.snapshot().phase != Phase::Ready && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(5));
        }
        let id = handle.snapshot().skins[1].id;
        handle.select(id);
        while handle.snapshot().current_skin != Some(id) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(5));
        }
        assert_eq!(handle.snapshot().current_skin, Some(id));
        assert!(handle.shutdown(Duration::from_secs(1)));
        assert!(game.lock().unwrap().closed);
    }
}
