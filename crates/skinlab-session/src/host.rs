// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Child-process transport for `skinlab-observe-host.exe --session`.

use crate::backend::{Session, SessionError};
use skinlab_core::{Command, HostFailureKind, HostReply, error::classify};
use std::fs::File;
use std::io::{BufRead, BufReader, Write};
use std::path::Path;
use std::process::{Child, ChildStdin, Stdio};
use std::sync::mpsc::{Receiver, RecvTimeoutError, channel};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// Native startup allows 60 seconds for background/slow game message and frame
// delivery. Leave time for identity hashing and host cleanup around that wait.
const CONNECT_TIMEOUT: Duration = Duration::from_secs(90);
const COMMAND_TIMEOUT: Duration = Duration::from_secs(15);
const EXIT_TIMEOUT: Duration = Duration::from_secs(3);

enum Line {
    Reply(Box<HostReply>),
    Invalid(String),
}

pub struct HostSession {
    child: Child,
    stdin: Option<ChildStdin>,
    lines: Receiver<Line>,
    last: Option<HostReply>,
    log: Option<File>,
}

impl HostSession {
    /// Starts the host and waits for its first session reply.
    pub fn spawn(host: &Path, args: &[String], log_dir: Option<&Path>) -> Result<HostSession, SessionError> {
        let mut command = std::process::Command::new(host);
        command.args(args).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
        if let Some(dir) = host.parent() {
            command.current_dir(dir);
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            command.creation_flags(CREATE_NO_WINDOW);
        }
        let mut child = command
            .spawn()
            .map_err(|e| SessionError::new(HostFailureKind::Other, format!("无法启动原生控制器：{e}"), None))?;
        let stdout = child.stdout.take().expect("piped stdout");
        let (sender, lines) = channel();
        std::thread::Builder::new()
            .name("skinlab-host-reader".into())
            .spawn(move || {
                for line in BufReader::new(stdout).lines() {
                    let Ok(line) = line else { break };
                    let parsed = match HostReply::parse(&line) {
                        Ok(reply) => Line::Reply(Box::new(reply)),
                        Err(_) => Line::Invalid(line),
                    };
                    if sender.send(parsed).is_err() {
                        break;
                    }
                }
            })
            .expect("spawn host reader thread");
        let log = log_dir.and_then(open_log);
        let mut session = HostSession { stdin: child.stdin.take(), child, lines, last: None, log };
        match session.receive(CONNECT_TIMEOUT) {
            Ok(_) => Ok(session),
            Err(error) => {
                session.stdin = None;
                if session.wait_exit(Duration::from_secs(2)).is_none() {
                    let _ = session.child.kill();
                    let _ = session.child.wait();
                }
                Err(error)
            }
        }
    }

    fn receive(&mut self, timeout: Duration) -> Result<HostReply, SessionError> {
        let reply = match self.lines.recv_timeout(timeout) {
            Ok(Line::Reply(reply)) => *reply,
            Ok(Line::Invalid(line)) => {
                tracing::warn!(%line, "invalid host line");
                return Err(self.error(HostFailureKind::Other, "原生控制器返回无效协议数据"));
            }
            Err(RecvTimeoutError::Timeout) => {
                return Err(self.error(HostFailureKind::NoFrameAck, "尚未收到帧线程确认；命令可能在后续帧执行"));
            }
            Err(RecvTimeoutError::Disconnected) => {
                return Err(self.error(HostFailureKind::TargetExited, "原生控制器已退出"));
            }
        };
        if let Some(log) = &mut self.log {
            let at = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis()).unwrap_or(0);
            let _ = writeln!(log, "{}", serde_json::json!({"at_ms": at, "reply": &reply}));
        }
        tracing::debug!(?reply.state, reply.result, reply.current_skin, %reply.error, "host reply");
        self.last = Some(reply.clone());
        if !reply.succeeded() {
            if reply.startup.is_some() {
                tracing::warn!(startup = ?reply.startup, diagnostic = %reply.error, "native startup failed");
            }
            let failure = classify(&reply.error, reply.result);
            return Err(SessionError { failure, last: self.last.clone().map(Box::new) });
        }
        Ok(reply)
    }

    fn error(&self, kind: HostFailureKind, message: &str) -> SessionError {
        SessionError::new(kind, message, self.last.clone())
    }

    fn running(&mut self) -> bool {
        matches!(self.child.try_wait(), Ok(None))
    }

    fn wait_exit(&mut self, timeout: Duration) -> Option<std::process::ExitStatus> {
        let deadline = Instant::now() + timeout;
        loop {
            if let Ok(Some(status)) = self.child.try_wait() {
                return Some(status);
            }
            if Instant::now() >= deadline {
                return None;
            }
            std::thread::sleep(Duration::from_millis(30));
        }
    }
}

impl Session for HostSession {
    fn command(&mut self, command: Command) -> Result<HostReply, SessionError> {
        if !self.running() {
            return Err(self.error(HostFailureKind::TargetExited, "会话未连接或已退出"));
        }
        let Some(stdin) = &mut self.stdin else {
            return Err(self.error(HostFailureKind::TargetExited, "会话输入已关闭"));
        };
        if stdin.write_all(command.line().as_bytes()).and_then(|_| stdin.flush()).is_err() {
            return Err(self.error(HostFailureKind::TargetExited, "原生控制器已退出"));
        }
        self.receive(COMMAND_TIMEOUT)
    }

    fn close(&mut self) -> Result<Option<HostReply>, SessionError> {
        let stopped = self.last.as_ref().is_some_and(|r| r.state.is_final());
        if self.running() && !stopped {
            if let Err(error) = self.command(Command::Stop) {
                let exited = error.last.as_ref().is_some_and(|r| r.state.is_final());
                if !exited && error.failure.kind != HostFailureKind::TargetExited {
                    return Err(error);
                }
            }
        }
        self.stdin = None;
        if self.wait_exit(EXIT_TIMEOUT).is_none() {
            return Err(self.error(HostFailureKind::Other, "停止已请求，但控制器尚未退出"));
        }
        Ok(self.last.clone())
    }

    fn last(&self) -> Option<&HostReply> {
        self.last.as_ref()
    }
}

impl Drop for HostSession {
    fn drop(&mut self) {
        // Closing stdin makes the host send `stop` itself; never kill it while a
        // command may be in flight on the game's frame thread.
        self.stdin = None;
        let _ = self.wait_exit(Duration::from_secs(2));
    }
}

fn open_log(dir: &Path) -> Option<File> {
    std::fs::create_dir_all(dir).ok()?;
    let at = SystemTime::now().duration_since(UNIX_EPOCH).ok()?.as_millis();
    File::options().create_new(true).append(true).open(dir.join(format!("session-{at}.jsonl"))).ok()
}
