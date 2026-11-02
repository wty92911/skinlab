//! In-memory backend for controller tests and UI development on any OS.
//! It never starts a process.

use crate::backend::{Backend, ConnectError, Session, SessionError};
use skinlab_core::live::ActiveHero;
use skinlab_core::{Command, HostFailureKind, HostReply, SessionState};
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
    pub connects: u32,
    pub closed: bool,
}

impl MockGame {
    pub fn shared(model: Option<&str>) -> Arc<Mutex<MockGame>> {
        Arc::new(Mutex::new(MockGame {
            running: model.is_some(),
            model: model.map(str::to_string),
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
        pending: false,
        render_tid: 1,
        target_pid: MOCK_PID,
        allowed_skins: Vec::new(),
        error: String::new(),
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
                game.applied.push(skin);
                SessionState::Active
            }
            Command::Restore => {
                game.current = self.original;
                SessionState::Ready
            }
            Command::Stop => {
                game.current = self.original;
                SessionState::Stopped
            }
            Command::Status => self.last.as_ref().map_or(SessionState::Ready, |r| r.state),
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
