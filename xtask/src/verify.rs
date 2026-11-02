// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! End-to-end checks against SkinLab's own hidden WARP process. Never touches the game.

use anyhow::Result;
use std::path::Path;

#[cfg(not(windows))]
pub fn run(_root: &Path) -> Result<()> {
    anyhow::bail!("verify drives the native host and needs Windows x64")
}

#[cfg(windows)]
pub fn run(root: &Path) -> Result<()> {
    use anyhow::{Context, ensure};
    use skinlab_core::{Catalog, HostReply, SessionState};
    use skinlab_session::game::FixtureBackend;
    use skinlab_session::{Controller, NativeBundle, Phase, Timing};
    use std::time::{Duration, Instant};

    let bundle = NativeBundle::from_repo(root).context("run `cargo xtask native` first")?;
    let host = bundle.verify()?;

    println!("host self-test");
    let output = std::process::Command::new(&host).current_dir(bundle.dir()).output()?;
    let report: serde_json::Value = serde_json::from_slice(&output.stdout).context("host self-test output")?;
    let failed: Vec<&str> = report["checks"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|c| c["passed"] != true)
        .filter_map(|c| c["name"].as_str())
        .collect();
    ensure!(report["status"] == "passed" && failed.is_empty(), "host self-test failed: {} {failed:?}", report["error"]);

    // An impossible PID fails before installing a hook. Startup failures must
    // use the same JSON envelope as normal replies, not the observation report.
    println!("session startup failure protocol");
    let output = std::process::Command::new(&host)
        .args(["--observe-game", "4294967295", &"0".repeat(64), "--accept-module-until-exit", "--session"])
        .current_dir(bundle.dir())
        .output()?;
    let reply = HostReply::parse(std::str::from_utf8(&output.stdout)?)
        .context("session startup failure must be a session reply")?;
    ensure!(
        !output.status.success()
            && reply.is_session()
            && reply.state == SessionState::Invalid
            && !reply.error.is_empty(),
        "startup failure diagnostic was lost"
    );

    println!("controller against fixture");
    let mut catalog = Catalog::embedded();
    catalog.champions.push(FixtureBackend::champion());
    let backend = FixtureBackend::new(bundle, None);
    let mut controller =
        Controller::new(Box::new(backend), catalog, Timing { hero_grace: Duration::ZERO, ..Timing::default() });
    let mut checks = 0;
    let mut check = |name: &str, ok: bool| -> Result<()> {
        ensure!(ok, "check failed: {name}");
        checks += 1;
        println!("  ok {name}");
        Ok(())
    };

    let deadline = Instant::now() + Duration::from_secs(30);
    while controller.snapshot().phase != Phase::Ready && Instant::now() < deadline {
        controller.tick();
    }
    let s = controller.snapshot();
    check(
        "auto_connects_owned_fixture",
        s.phase == Phase::Ready && s.hero.as_ref().is_some_and(|h| h.model == "Fixture"),
    )?;
    check("starts_on_original_skin", s.current_skin == Some(0) && s.original_skin == Some(0))?;

    controller.select(1);
    controller.drive();
    check("selection_applies", controller.snapshot().current_skin == Some(1))?;

    controller.select(0);
    controller.select(1);
    controller.select(0);
    controller.drive();
    check("rapid_selection_finishes_on_latest", controller.snapshot().current_skin == Some(0))?;

    controller.select(1);
    controller.drive();
    controller.request_restore();
    controller.drive();
    check("restore_returns_to_original", controller.snapshot().current_skin == Some(0))?;

    controller.select(1);
    controller.drive();
    controller.shutdown();
    check("shutdown_stops_session", controller.snapshot().current_skin.is_none())?;

    println!("verify passed ({checks} controller checks)");
    Ok(())
}
