// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! In-memory backend for controller tests and UI development on any OS.
//! It never starts a process.

use crate::backend::{Backend, ConnectError, Session, SessionError};
use skinlab_core::live::ActiveHero;
use skinlab_core::{Command, HostFailureKind, HostReply, SessionState, form_set};
use std::sync::{Arc, Mutex};
use std::time::Duration;

pub const MOCK_PID: u32 = 4242;

#[derive(Debug, Default)]
pub struct MockGame {
    pub running: bool,
    pub model: Option<String>,
    /// Connect attempts that fail with a retryable "not ready" error first.
    pub not_ready: u32,
    pub unsupported: bool,
    /// Simulated latency for connect and commands (UI demo only).
    pub latency: Duration,
    pub current: u32,
    pub applied: Vec<u32>,
    /// Whether the session announces the forms in `skinlab_core::forms`.
    pub forms: bool,
    pub current_form: Option<&'static str>,
    pub applied_forms: Vec<&'static str>,
    pub connects: u32,
    pub closed: bool,
}

impl MockGame {
    /// Forms the game supports for the applied skin, if announced.
    fn form_set(&self) -> Option<&'static skinlab_core::FormSet> {
        if !self.forms {
            return None;
        }
        form_set(self.model.as_deref()?, self.current)
    }

    pub fn shared(model: Option<&str>) -> Arc<Mutex<MockGame>> {
        Arc::new(Mutex::new(MockGame {
            running: model.is_some(),
            model: model.map(str::to_string),
            forms: true,
            ..Default::default()
        }))
    }
}

pub struct MockBackend {
    game: Arc<Mutex<MockGame>>,
}

impl MockBackend {
    pub fn new(game: Arc<Mutex<MockGame>>) -> Self {
        MockBackend { game }
    }

    /// A game with a champion already loaded and human-scale latency.
    pub fn demo(model: &str) -> Self {
        let game = MockGame::shared(Some(model));
        game.lock().unwrap().latency = Duration::from_millis(450);
        MockBackend::new(game)
    }
}

fn reply(game: &MockGame, state: SessionState) -> HostReply {
    HostReply {
        event: "session".into(),
        state,
        result: 0,
        model: game.model.clone().unwrap_or_default(),
        current_skin: game.current,
        current_form: game.current_form.map(str::to_string),
        // Like the host: announced for the champion regardless of the applied skin.
        available_forms: skinlab_core::forms::FORM_SETS
            .iter()
            .filter(|set| game.forms && game.model.as_deref().is_some_and(|m| set.champion.eq_ignore_ascii_case(m)))
            .flat_map(|set| set.forms.iter().map(|f| f.id.to_string()))
            .collect(),
        pending: false,
        render_tid: 1,
        target_pid: MOCK_PID,
        allowed_skins: Vec::new(),
        error: String::new(),
        stack_probe: None,
    }
}

impl Backend for MockBackend {
    fn game_pids(&mut self) -> Vec<u32> {
        if self.game.lock().unwrap().running { vec![MOCK_PID] } else { Vec::new() }
    }

    fn active_hero(&mut self) -> Option<ActiveHero> {
        let game = self.game.lock().unwrap();
        let model = game.model.clone()?;
        Some(ActiveHero { model, display_name: String::new(), skin_id: Some(game.current) })
    }

    fn game_version(&mut self, _pid: u32) -> Option<String> {
        Some("16.21.832.5350".into())
    }

    fn connect(&mut self, _pid: u32) -> Result<Box<dyn Session>, ConnectError> {
        let latency = self.game.lock().unwrap().latency;
        std::thread::sleep(latency * 2);
        let mut game = self.game.lock().unwrap();
        game.connects += 1;
        if game.unsupported {
            return Err(ConnectError::UnsupportedBuild { version: "99.1.0.0".into(), sha256: "0".repeat(64) });
        }
        if game.not_ready > 0 {
            game.not_ready -= 1;
            return Err(SessionError::new(HostFailureKind::NotReady, "等待英雄载入…", None).into());
        }
        let last = Some(reply(&game, SessionState::Ready));
        drop(game);
        Ok(Box::new(MockSession { game: self.game.clone(), original: 0, last }))
    }
}

struct MockSession {
    game: Arc<Mutex<MockGame>>,
    original: u32,
    last: Option<HostReply>,
}

impl Session for MockSession {
    fn command(&mut self, command: Command) -> Result<HostReply, SessionError> {
        let latency = self.game.lock().unwrap().latency;
        std::thread::sleep(latency);
        let mut game = self.game.lock().unwrap();
        if !game.running {
            return Err(SessionError::new(HostFailureKind::TargetExited, "对局进程已退出", self.last.clone()));
        }
        let state = match command {
            Command::Apply(skin) => {
                game.current = skin;
                game.current_form = game.form_set().map(|set| set.base().id);
                game.applied.push(skin);
                SessionState::Active
            }
            Command::Restore => {
                game.current = self.original;
                game.current_form = None;
                SessionState::Ready
            }
            Command::Stop => {
                game.current = self.original;
                game.current_form = None;
                SessionState::Stopped
            }
            Command::Status | Command::Inspect => self.last.as_ref().map_or(SessionState::Ready, |r| r.state),
            Command::Form(form) => {
                if game.form_set().and_then(|set| set.form(form)).is_none() {
                    return Err(SessionError::new(HostFailureKind::Other, "当前皮肤不支持该形态", self.last.clone()));
                }
                game.current_form = Some(form);
                game.applied_forms.push(form);
                SessionState::Active
            }
        };
        let reply = reply(&game, state);
        self.last = Some(reply.clone());
        Ok(reply)
    }

    fn close(&mut self) -> Result<Option<HostReply>, SessionError> {
        if self.game.lock().unwrap().running {
            self.command(Command::Stop)?;
        }
        self.game.lock().unwrap().closed = true;
        Ok(self.last.clone())
    }

    fn last(&self) -> Option<&HostReply> {
        self.last.as_ref()
    }
}
