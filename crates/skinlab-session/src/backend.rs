// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! The seam between the controller and the platform. The real backend talks to
//! the game; the fixture backend to SkinLab's own WARP window; the mock backend
//! to nothing at all (UI development and tests).

use skinlab_core::live::ActiveHero;
use skinlab_core::{Command, HostFailure, HostFailureKind, HostReply};
use thiserror::Error;

#[derive(Debug, Error)]
#[error("{}", failure.message)]
pub struct SessionError {
    pub failure: HostFailure,
    /// Last reply the host sent, if any; it still describes the live state.
    pub last: Option<Box<HostReply>>,
}

impl SessionError {
    pub fn new(kind: HostFailureKind, message: impl Into<String>, last: Option<HostReply>) -> Self {
        SessionError { failure: HostFailure { kind, message: message.into() }, last: last.map(Box::new) }
    }
}

#[derive(Debug, Error)]
pub enum ConnectError {
    #[error("游戏版本 {version} 暂时无法接入：{diagnostic}")]
    UnsupportedBuild { version: String, sha256: String, diagnostic: String },
    #[error("{0}")]
    Session(#[from] SessionError),
    #[error("{0}")]
    Setup(String),
}

impl ConnectError {
    pub fn retryable(&self) -> bool {
        matches!(self, ConnectError::Session(e) if e.failure.kind.retryable())
    }
}

/// A connected native session for one game process.
pub trait Session: Send {
    fn command(&mut self, command: Command) -> Result<HostReply, SessionError>;
    /// Restores the original skin, stops the session and waits for the host to exit.
    fn close(&mut self) -> Result<Option<HostReply>, SessionError>;
    fn last(&self) -> Option<&HostReply>;
}

pub trait Backend: Send {
    /// Process IDs of running game clients.
    fn game_pids(&mut self) -> Vec<u32>;
    /// The local player's champion from the Live Client API, if loaded.
    fn active_hero(&mut self) -> Option<ActiveHero>;
    /// Game build string for display, if known.
    fn game_version(&mut self, pid: u32) -> Option<String>;
    fn connect(&mut self, pid: u32) -> Result<Box<dyn Session>, ConnectError>;
}
