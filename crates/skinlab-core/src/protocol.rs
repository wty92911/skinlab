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
    /// Read-only adaptation snapshot; not exposed by the player UI.
    Inspect,
    /// Model alias from `forms::FORM_SETS`.
    Form(&'static str),
}

impl Command {
    pub fn line(self) -> String {
        match self {
            Command::Apply(skin) => format!("apply {skin}\n"),
            Command::Restore => "restore\n".into(),
            Command::Stop => "stop\n".into(),
            Command::Status => "status\n".into(),
            Command::Inspect => "inspect\n".into(),
            Command::Form(form) => format!("form {form}\n"),
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SessionState {
    Starting,
    Ready,
    Active,
    /// This host is finished; a new host can reconnect after a clean restore.
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

/// Opaque on-frame observations. These do not establish a stack-entry layout
/// or a verified gear/model-changing implementation.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct StackProbe {
    pub schema_version: u32,
    pub sequence: u32,
    pub render_tid: u32,
    pub refresh_calls: u32,
    /// 1 = reread equal, 2 = unreadable span, 3 = invalid vector bounds,
    /// 4 = target changed, 5 = unstable snapshot, 6 = core state unavailable.
    pub read_status: u32,
    /// Total structural stack references; at most six code contexts are sampled.
    #[serde(default)]
    pub stack_reference_count: u32,
    pub begin: u64,
    pub end: u64,
    pub capacity: u64,
    pub stack_prefix_hex: String,
    pub layers_head_hex: String,
    pub layers_tail_hex: String,
    pub code: Vec<CodeProbe>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CodeProbe {
    /// Native shared protocol v9 / core ABI v2 retain stack_reference_0..5,
    /// material_registry_getter and swap_chain_context to the five original roles.
    /// Every role is diagnostic only; its bytes do not enable an engine binding.
    pub role: String,
    pub rva: u32,
    pub bytes_hex: String,
}

/// Best-effort startup progress, including when the completion wait expires.
/// This is diagnostic data, never proof of a usable or unloaded session.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct StartupProbe {
    pub stage: String,
    pub phase: u32,
    pub hook_tid: u32,
    pub worker_tid: u32,
    pub render_tid: u32,
    pub hook_remove: u32,
    pub module_pinned: bool,
    pub collection_ms: u64,
    pub result_count: u32,
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
    pub current_encoded_skin: u32,
    #[serde(default)]
    pub current_form: Option<String>,
    #[serde(default)]
    pub available_forms: Vec<String>,
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
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub stack_probe: Option<StackProbe>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub startup: Option<StartupProbe>,
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
            r#"{"event":"session","state":"active","result":0,"model":"Lux","current_skin":7,"current_form":"LuxAir","available_forms":["Lux","LuxAir"],"pending":false,"module_resident":true,"frame_hook_retained":true,"render_tid":10,"target_pid":4,"allowed_skins":[0,7],"error":""}"#,
        )
        .unwrap();
        assert_eq!(reply.state, SessionState::Active);
        assert_eq!(reply.current_skin, 7);
        assert_eq!(reply.current_form.as_deref(), Some("LuxAir"));
        assert_eq!(reply.available_forms, ["Lux", "LuxAir"]);
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
    fn startup_failure_keeps_retryable_diagnostic() {
        let reply = HostReply::parse(
            r#"{"event":"session","state":"invalid","target_pid":42,"error":"Target observation failed: Cannot read consistent current player skin data"}"#,
        )
        .unwrap();
        assert!(!reply.succeeded());
        assert_eq!(reply.state, SessionState::Invalid);
        let failure = crate::error::classify(&reply.error, reply.result);
        assert_eq!(failure.kind, crate::HostFailureKind::NotReady);
        assert!(failure.kind.retryable());
    }

    #[test]
    fn startup_timeout_keeps_partial_progress() {
        let reply = HostReply::parse(
            r#"{"event":"session","state":"invalid","error":"Observation startup timeout: stage=frame","startup":{"stage":"frame","phase":2,"hook_tid":11,"worker_tid":12,"render_tid":0,"hook_remove":0,"module_pinned":true,"collection_ms":25,"result_count":17}}"#,
        )
        .unwrap();
        assert!(!reply.succeeded());
        let failure = crate::error::classify(&reply.error, reply.result);
        assert_eq!(failure.kind, crate::HostFailureKind::StartupTimeout);
        assert!(!failure.kind.retryable());
        let probe = reply.startup.unwrap();
        assert_eq!(probe.stage, "frame");
        assert!(probe.module_pinned);
        assert_eq!(probe.render_tid, 0);
    }

    #[test]
    fn command_lines() {
        assert_eq!(Command::Apply(12).line(), "apply 12\n");
        assert_eq!(Command::Stop.line(), "stop\n");
        assert_eq!(Command::Inspect.line(), "inspect\n");
        assert_eq!(Command::Form("LuxAir").line(), "form LuxAir\n");
    }
}
