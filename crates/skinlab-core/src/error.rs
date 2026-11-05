// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Classifies native host failures into kinds the controller can act on, and
//! the Chinese message shown to the player. The host speaks English
//! diagnostics; this table is the only place that knows their wording.

use crate::builds::DiskBuildCheck;
use serde::Serialize;

/// Wording for an unknown build; disk matches never grant runtime support.
pub fn build_check_message(check: Option<&DiskBuildCheck>) -> String {
    match check {
        Some(check) if check.error.is_empty() && check.required_rules_unique => {
            "文件特征检查通过，仍需确认运行时结构与调用兼容性".into()
        }
        Some(check) if check.error.is_empty() => "无法从游戏文件确认兼容性，请等待 SkinLab 适配".into(),
        _ => "自动检查未完成，请等待适配或重新构建 SkinLab".into(),
    }
}

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum HostFailureKind {
    /// Champion not yet loaded; retry shortly.
    NotReady,
    /// Game runs at a higher integrity level than SkinLab.
    Integrity,
    UnsupportedSkin,
    RecipeFailed,
    AlreadyActive,
    WrongImage,
    BuildMismatch,
    TargetExited,
    TargetChanged,
    NoFrameAck,
    RecoveryRequired,
    Other,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct HostFailure {
    pub kind: HostFailureKind,
    pub message: String,
}

impl HostFailureKind {
    /// Whether connecting to the same game process again may succeed.
    pub fn retryable(self) -> bool {
        matches!(self, HostFailureKind::NotReady | HostFailureKind::NoFrameAck)
    }
}

const FRAGMENTS: &[(&str, HostFailureKind, &str)] = &[
    ("consistent current player", HostFailureKind::NotReady, "等待英雄载入…"),
    ("Expected exactly one visible unowned window", HostFailureKind::NotReady, "等待游戏窗口就绪…"),
    (
        "Target integrity exceeds observer integrity",
        HostFailureKind::Integrity,
        "游戏权限高于 SkinLab，请以管理员身份重新打开",
    ),
    ("Current model/skin has no verified", HostFailureKind::UnsupportedSkin, "当前英雄或皮肤暂时无法应用"),
    ("Required session recipe", HostFailureKind::RecipeFailed, "关键版本特征未通过检查，未应用皮肤"),
    (
        "An observation worker is already running",
        HostFailureKind::AlreadyActive,
        "本局已有会话或已停止，请在下一局接入",
    ),
    ("A session is already active", HostFailureKind::AlreadyActive, "本局已有会话，请在下一局接入"),
    ("Session has stopped", HostFailureKind::AlreadyActive, "本局会话已停止，请在下一局接入"),
    ("Requested PID does not have", HostFailureKind::WrongImage, "该进程不是英雄联盟对局进程"),
    ("Target disk SHA256", HostFailureKind::BuildMismatch, "游戏文件与已验证版本不一致"),
    ("Target process has exited", HostFailureKind::TargetExited, "对局进程已退出"),
    ("Target exited", HostFailureKind::TargetExited, "对局进程已退出"),
    ("No frame acknowledgement", HostFailureKind::NoFrameAck, "尚未收到帧线程确认，请稍后重试"),
    ("A prior command is pending", HostFailureKind::NoFrameAck, "上一条命令仍在处理，请稍候"),
];

/// Classifies a host reply's `error` text and `result` code.
pub fn classify(error: &str, result: u32) -> HostFailure {
    if let Some((_, kind, message)) = FRAGMENTS.iter().find(|(f, _, _)| error.contains(f)) {
        return HostFailure { kind: *kind, message: (*message).into() };
    }
    let (kind, message) = match result {
        5 => (HostFailureKind::UnsupportedSkin, "该皮肤编号不在当前英雄的目录中".to_string()),
        6 => (HostFailureKind::TargetChanged, "当前角色已变化，请等待重新接入".to_string()),
        9 => (HostFailureKind::RecoveryRequired, "恢复尚未完成，请重试恢复".to_string()),
        _ if !error.is_empty() => (HostFailureKind::Other, error.to_string()),
        code => (HostFailureKind::Other, format!("操作未完成，状态码 {code}")),
    };
    HostFailure { kind, message }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_fragment_is_translated() {
        let failure = classify("Target integrity exceeds observer integrity; no hook installed", 0);
        assert_eq!(failure.kind, HostFailureKind::Integrity);
        assert!(!failure.kind.retryable());
    }

    #[test]
    fn not_ready_is_retryable() {
        assert!(classify("No consistent current player yet", 0).kind.retryable());
    }

    #[test]
    fn result_codes_without_text() {
        assert_eq!(classify("", 6).kind, HostFailureKind::TargetChanged);
        assert_eq!(classify("", 42).message, "操作未完成，状态码 42");
    }

    #[test]
    fn unknown_text_is_kept() {
        let failure = classify("Something new", 0);
        assert_eq!((failure.kind, failure.message.as_str()), (HostFailureKind::Other, "Something new"));
    }
}
