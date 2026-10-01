---
name: norns-device
description: Remote-manage the Fates norns music computer over SSH (host alias `fates`) — health checks, deploying code to the device, device builds, jackd/audio recovery, firmware and upstream update procedures, WM8731 codec and uart-midi hardware. Use whenever the user mentions the device, fates, norns deploy/build/update, jackd, SUPERCOLLIDER FAIL, blank screen, SYSTEM>UPDATE, firmware, wm8731, uart-midi, SSD1325, or sound problems on the box — even if they don't say "device".
---

# norns device management (Fates fork)

The device is a Raspberry Pi running this repo's norns fork. Work happens on
this machine; deployment and verification happen over SSH on real hardware.

## Ground rules

- SSH is key-based: `ssh fates` (the SSH config alias pins `we`@192.168.8.163
  with `id_ed25519`). Batch mode works. Never fall back to passwords or
  SSH_ASKPASS scripts.
- `AGENTS.md` in the repo root is the deep source of truth — device
  prerequisites, systemd layout, rollback. Read it before anything unusual.
- The device is slow. Start long builds in the background on the device and
  poll the log; do not hold one SSH session open for the duration.
- The Windows checkout shows CRLF churn: `git status` lists many modified
  files with 0 insertions / 0 deletions. That is autocrlf noise — never commit
  it, never "fix" it by rewriting line endings.
- Old-toolchain fixes exist in the code on purpose (GCC 8 atomic casts in
  `jack_client.cpp`, buster alsa-lib API names in `alsa_ctl.cc`). Do not
  "modernize" them back; they are what makes the device build possible.

## Health check

Run `scripts/fates-health.sh` for the full report (services, jack, codec,
midi, throttling, journal errors). It ends with `HEALTH: OK` or
`HEALTH: PROBLEMS` and exits non-zero on problems. Run it before and after any
device change; interpret as follows:

- `JACK WEDGED (new clients refused)` or `(deleted shm registry)` →
  references/recovery.md, then `scripts/fates-recover-jack.sh`.
- Codec values wrong → check whether `Audio.apply_state()` ran at script load
  (journal: `# script load`); see references/hardware.md.
- `throttled` bit 0/16 set = undervoltage (marginal 5 V supply, recurring
  hardware issue); bit 3/19 = soft temperature cap (benign, box runs warm).

## Deploying code to the device

1. Commit and push to `origin main` from this machine. Main is the working
   branch (fast-forwarded past 3.0.2); 2.9.x rollback lives only in the
   `pre-upstream-3.0.2` tag.
2. On the device: `git pull` and `git submodule update --init --recursive`
   (submodules first — they matter after any upstream merge).
3. Build in the background: `nohup sh -c "./waf build --release >
   /tmp/build.log 2>&1" &`, then poll `/tmp/build.log`. `--release` on both
   configure and build; it targets cortex-a53.
4. Restart with a plain `sudo systemctl restart norns-main` — safe: jackd
   cycles with it via the PartOf/BindsTo coupling.
5. Verify: services active, `NRestarts=0`, no new journal errors, then run
   the health check. Read back actual hardware state (e.g. `amixer ... cget`)
   rather than trusting the deploy.
6. **Cold boot is a separate verification.** `sudo systemctl reboot` after any
   systemd or boot-path change — a service start working does not prove the
   boot path works. This rule exists because two different boot failures each
   looked like success until reboot.

Lua-only changes need no rebuild — but still require a restart for matron to
reload them. Syntax-check changed Lua on the device first with
`lua -e "assert(loadfile('/home/we/norns/<file>'))"` (lua5.3 binary is absent;
`lua` is 5.1 and good enough for syntax).

## Audio or jackd problems

Read references/recovery.md — wedge signatures and variants, the recovery
sequence, systemd traps (enable vs start, upstream unit files), and which
journal noise is benign.

## Firmware / updates

Read references/firmware.md before any update work. Headline rule: **never
run SYSTEM>UPDATE on this box** — the update script replaces `~/norns`
wholesale and destroys branch state.

## Hardware (WM8731 codec, uart-midi, display)

Read references/hardware.md for the control map, the alsa_ctl path, the
uart-midi chain, and what must never be "cleaned up".
