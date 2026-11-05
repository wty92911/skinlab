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
    use skinlab_core::{Catalog, Command, HostReply, SessionState};
    use skinlab_session::game::{FIXTURE_PID, FixtureBackend};
    use skinlab_session::{Backend, Controller, NativeBundle, Phase, Session, Timing};
    use std::time::{Duration, Instant};

    let bundle = NativeBundle::from_repo(root).context("run `cargo xtask native` first")?;
    let host = bundle.verify()?;
    verify_disk_scan(&host, bundle.dir())?;

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

    println!("read-only frame probe against fixture");
    let mut probe_backend = FixtureBackend::new(bundle.clone(), None);
    let mut session = probe_backend.connect(FIXTURE_PID)?;
    let before = session.last().context("fixture startup reply")?.clone();
    let inspected = session.command(Command::Inspect)?;
    let probe = inspected.stack_probe.context("inspect must return a frame snapshot")?;
    ensure!(
        probe.schema_version == 1
            && probe.read_status == 1
            && probe.sequence > 0
            && probe.render_tid == before.render_tid
            && probe.render_tid != 0,
        "probe did not run on the fixture's render thread"
    );
    ensure!(
        probe.stack_prefix_hex.len() == 176 * 2
            && probe.end - probe.begin == 288
            && probe.layers_head_hex.len() == 256 * 2
            && probe.layers_head_hex.starts_with("42")
            && probe.layers_tail_hex.len() == 256 * 2
            && probe.layers_tail_hex.ends_with("7d")
            && inspected.current_skin == before.current_skin
            && before.available_forms.is_empty()
            && before.current_form.is_none()
            && probe.refresh_calls == 0,
        "probe changed fixture state or did not respect bounded opaque spans"
    );
    session.command(Command::Apply(1))?;
    let active = session.command(Command::Inspect)?;
    let active_probe = active.stack_probe.context("active frame snapshot")?;
    ensure!(
        active.current_skin == 1
            && active_probe.refresh_calls == 1
            && &active_probe.stack_prefix_hex[0x38 * 2..0x3c * 2] == "01000000"
            && active_probe.layers_head_hex == probe.layers_head_hex
            && active_probe.layers_tail_hex == probe.layers_tail_hex,
        "inspect must observe the selected skin without refreshing or changing layers"
    );
    session.command(Command::Restore)?;
    let restored = session.command(Command::Inspect)?.stack_probe.context("restored frame snapshot")?;
    ensure!(
        restored.stack_prefix_hex == probe.stack_prefix_hex && restored.refresh_calls == 2,
        "probe must preserve the original fixture layout across restore"
    );
    session.close()?;

    println!("codec v2 apply and restoration against fixture");
    let mut codec_session = skinlab_session::host::HostSession::spawn(&host, &["--fixture-session-v2".into()], None)?;
    let before = codec_session.last().context("codec fixture startup")?.clone();
    ensure!(before.current_skin == 0 && before.current_encoded_skin == 0x3e17_9126, "codec fixture original disagrees");
    let applied = codec_session.command(Command::Apply(1))?;
    ensure!(
        applied.current_skin == 1 && applied.current_encoded_skin == 0x3e17_91a6,
        "codec v2 encoding disagrees with golden"
    );
    let restored = codec_session.command(Command::Restore)?;
    ensure!(
        restored.current_skin == 0 && restored.current_encoded_skin == before.current_encoded_skin,
        "codec v2 failed to restore bytes"
    );
    codec_session.close()?;

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

#[cfg(windows)]
fn verify_disk_scan(host: &Path, directory: &Path) -> Result<()> {
    use anyhow::ensure;
    use skinlab_session::game::check_build;
    println!("data-only disk scan: RVA mapping, uniqueness and malformed PE rejection");
    // A synthetic PE carried as data only. No process is launched from this file.
    let mut bytes = vec![0u8; 0x600];
    let put16 = |bytes: &mut [u8], at: usize, value: u16| bytes[at..at + 2].copy_from_slice(&value.to_le_bytes());
    let put32 = |bytes: &mut [u8], at: usize, value: u32| bytes[at..at + 4].copy_from_slice(&value.to_le_bytes());
    put16(&mut bytes, 0, 0x5a4d);
    put32(&mut bytes, 0x3c, 0x80);
    put32(&mut bytes, 0x80, 0x4550);
    put16(&mut bytes, 0x84, 0x8664);
    put16(&mut bytes, 0x86, 1);
    put16(&mut bytes, 0x94, 240);
    put16(&mut bytes, 0x98, 0x20b);
    put32(&mut bytes, 0xd0, 0x2000); // SizeOfImage
    put32(&mut bytes, 0xd4, 0x200); // SizeOfHeaders
    put32(&mut bytes, 0x190, 0x400); // VirtualSize
    put32(&mut bytes, 0x194, 0x1000); // RVA
    put32(&mut bytes, 0x198, 0x400); // SizeOfRawData
    put32(&mut bytes, 0x19c, 0x200); // PointerToRawData
    put32(&mut bytes, 0x1ac, 0x6000_0020);
    let pattern = [0x48, 0x8b, 0x3d, 0xf9, 1, 0, 0, 0x48, 0x85, 0xff, 0x74, 0x15, 0x48, 0x81, 0xc7];
    bytes[0x200..0x200 + pattern.len()].copy_from_slice(&pattern);
    let path = directory.join("disk-scan-fixture.pe");
    let result = (|| -> Result<()> {
        std::fs::write(&path, &bytes)?;
        let check = check_build(host, &path)?;
        let rule = check.results.iter().find(|r| r.name == "local_player").unwrap();
        ensure!(
            check.error.is_empty()
                && check.runtime_compatible.is_none()
                && !check.required_rules_unique
                && rule.state == "unique_disk_reread_equal"
                && rule.match_rvas == [0x1000]
                && rule.candidate.as_ref().is_some_and(|c| c["rva"] == 0x1200),
            "disk scan confused raw offsets, RVAs or runtime compatibility"
        );
        bytes[0x240..0x240 + pattern.len()].copy_from_slice(&pattern);
        std::fs::write(&path, &bytes)?;
        let check = check_build(host, &path)?;
        let rule = check.results.iter().find(|r| r.name == "local_player").unwrap();
        ensure!(rule.state == "ambiguous" && rule.candidate.is_none(), "ambiguous disk rule published a candidate");
        put32(&mut bytes, 0x198, 0x1000);
        std::fs::write(&path, &bytes)?;
        let check = check_build(host, &path)?;
        ensure!(!check.error.is_empty() && check.results.is_empty(), "malformed disk extent was accepted");
        Ok(())
    })();
    std::fs::remove_file(&path)?;
    result
}
