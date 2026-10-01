# jackd wedge and systemd traps

## Wedge variants

jackd stays `active` but stops serving. Log signature for both variants:

```
jack_client_open() failed; status = 17
JackShmReadWritePtr::~JackShmReadWritePtr - Init not done for -1
JACK server is not running or cannot be started
```

1. **Classic** — a norns restart orphans jackd's shared-memory registry.
   jackd survives, new clients fail, SYSTEM>RESTART cannot recover because it
   only restarts `norns-main`.
2. **Deleted-registry variant** (observed 2026-10-01 after rapid restart
   cycles) — `/dev/shm` lists no jack files (they show as `(deleted)` in
   jackd's fds), yet the realtime graph keeps running via open fds. Sneaky:
   **the user still hears audio** — engine, monitoring, transport all work —
   while every new client is refused and the *next* norns restart would die.

Detection (one line each):

```
timeout 5 jack_lsp | grep -c ":"        # 0 = new clients refused
ls /dev/shm | grep -c jack              # 0 while jackd runs = deleted-registry variant
```

Because of variant 2, "sound is working" does **not** prove jack is healthy.
Always check both lines (or run `scripts/fates-health.sh`).

### Catching the onset (root cause still unknown)

Repro attempts on 2026-10-01 (script load, engine connect, script/engine
switch) did **not** trigger it; both real onsets hit ~60–90 s after services
came up, right when the boot auto-resume loaded the last script and its
engine connected. To catch the next one, leave a watcher running:

```
nohup sh -c 'while true; do echo "$(date +%T) shm=$(ls /dev/shm 2>/dev/null |
grep -c jack) ports=$(timeout 2 jack_lsp 2>/dev/null | grep -c :)"; sleep 3;
done' >/tmp/shmwatch.log 2>&1 &
```

When it flips to `shm=0`, grab `journalctl -u norns-jack --since "-5m"` — the
correlation is the missing evidence. Remote script loading for repro:
`printf 'norns.script.load("...")\n' | TERM=xterm script -qec "timeout 12
~/norns/build/maiden-repl/maiden-repl" /dev/null` (maiden-repl needs a pty).

## Recovery

`scripts/fates-recover-jack.sh` runs this sequence (audio stops ~30 s):

```
sudo systemctl stop norns.target
sudo systemctl stop norns-jack
sudo systemctl restart norns-jack
sudo systemctl reset-failed norns-main
sudo systemctl start norns.target
```

`reset-failed` is required — without it systemd's start-limit latch refuses
the unit even once jack is healthy. A plain reboot also clears everything.
After recovery: services active, `NRestarts=0`, `jack_lsp` serves 24 ports,
and codec values are re-applied by norns at script load.

## Why the coupling exists (do not remove)

`norns-jack.service` carries `PartOf=norns-main.service` and
`norns-main.service` carries `BindsTo=norns-jack.service`, so the two cycle
together on restart — this is what made plain restarts safe. Removing it
brings back the classic wedge on every restart.

## systemd traps (each has bitten once)

- **Enable, don't just start.** `systemctl start norns.target` runs the stack
  once; only `systemctl enable norns.target` registers it at boot. Skipping
  enable leaves the box healthy until the next reboot, then blank. Verify
  after any service change: `systemctl is-enabled norns.target`.
- **Never blindly copy upstream's unit files** from
  `image/config/etc/systemd/system/`. Upstream ships `hw:sndrpimonome` in
  `norns-jack.service`; the Fates codec is `hw:sndrpiproto`. Wrong card →
  jackd dies → `norns-sclang` and `norns-main` (both `Requires=` jack) die →
  blank screen.
- `norns-watcher.service` exists upstream but is **not** in `norns.target`'s
  `Requires` list, so it never runs.
- Old-layout units (`norns-matron`/`norns-crone`) were moved to
  `/root/norns-*.service.disabled`; originals also exist under
  `/home/we/fates/install/norns/files/`.

## Benign journal noise — do not chase

- `error loading keyboard layout, using old value: us`
- `hook: read passthrough state failed ... mod.lua` (dust script warning)
- `### SCRIPT ERROR: NO SCRIPT` after boot (no script selected)
- One `SUPERCOLLIDER FAIL` right at startup: sclang race, self-heals. Verify
  `scynth`/engine actually comes up before treating it as a fault.
- Occasional `JackEngine::XRun ... Process error` lines: load-related xruns,
  not the wedge.
