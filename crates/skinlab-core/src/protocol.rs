// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Line protocol spoken by `skinlab-observe-host.exe --session`: one command
//! per stdin line, one JSON reply per stdout line.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Command {
    Apply(u32),
    Restore,
    Stop,
    Status,
}

impl Command {
    pub fn line(self) -> String {
        match self {
            Command::Apply(skin) => format!("apply {skin}\n"),
            Command::Restore => "restore\n".into(),
            Command::Stop => "stop\n".into(),
            Command::Status => "status\n".into(),
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SessionState {
    Starting,
    Ready,
    Active,
    Stopped,
    Recovery,
    TargetChanged,
    TargetExited,
    Invalid,
}

impl SessionState {
    /// The host will not accept further engine commands in this state.
    pub fn is_final(self) -> bool {
        matches!(self, SessionState::Stopped | SessionState::TargetExited)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct HostReply {
    pub event: String,
    pub state: SessionState,
    #[serde(default)]
    pub result: u32,
    #[serde(default)]
    pub model: String,
    #[serde(default)]
    pub current_skin: u32,
    #[serde(default)]
    pub pending: bool,
    #[serde(default)]
    pub render_tid: u32,
    #[serde(default)]
    pub target_pid: u32,
    #[serde(default)]
    pub allowed_skins: Vec<u32>,
    #[serde(default)]
    pub error: String,
}

impl HostReply {
    pub fn parse(line: &str) -> Result<HostReply, serde_json::Error> {
        serde_json::from_str(line)
    }

    pub fn is_session(&self) -> bool {
        self.event == "session"
    }

    pub fn succeeded(&self) -> bool {
        self.is_session() && self.error.is_empty() && self.result == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_host_reply() {
        let reply = HostReply::parse(
            r#"{"event":"session","state":"active","result":0,"model":"Lux","current_skin":7,"pending":false,"module_resident":true,"frame_hook_retained":true,"render_tid":10,"target_pid":4,"allowed_skins":[0,7],"error":""}"#,
        )
        .unwrap();
        assert_eq!(reply.state, SessionState::Active);
        assert_eq!(reply.current_skin, 7);
        assert!(reply.succeeded());
    }

    #[test]
    fn exited_target_is_final() {
        let reply =
            HostReply::parse(r#"{"event":"session","state":"target_exited","error":"Target process has exited"}"#)
                .unwrap();
        assert!(reply.state.is_final());
        assert!(!reply.succeeded());
    }

    #[test]
    fn command_lines() {
        assert_eq!(Command::Apply(12).line(), "apply 12\n");
        assert_eq!(Command::Stop.line(), "stop\n");
    }
}
