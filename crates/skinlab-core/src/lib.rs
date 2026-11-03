// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Platform-independent SkinLab data. Nothing here touches a process, a file
//! outside the embedded data, or the network.

pub mod art;
pub mod builds;
pub mod catalog;
pub mod error;
pub mod forms;
pub mod live;
pub mod protocol;

pub use builds::{Build, SupportedBuilds};
pub use catalog::{Catalog, Champion, Chroma, Rarity, Skin};
pub use error::{HostFailure, HostFailureKind};
pub use forms::{Form, FormSet, form_set};
pub use protocol::{Command, HostReply, SessionState};
