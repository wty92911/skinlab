// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Everything between the UI and the native host: finding the game, checking
//! its build, driving `skinlab-observe-host.exe`, and the auto-connect loop.

pub mod backend;
pub mod bundle;
pub mod controller;
pub mod game;
pub mod host;
pub mod live;
pub mod mock;

pub use backend::{Backend, ConnectError, Session, SessionError};
pub use bundle::NativeBundle;
pub use controller::{Controller, ControllerHandle, Phase, Request, Snapshot, Timing};
