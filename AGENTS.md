# Memory

## Project Overview
This is a norns fork for a custom Fates shield with an SSD1325 OLED. The panel is
**128x64**, 4-bit grayscale (2 pixels per byte) — not the 128x128 the earlier revision of
this file claimed; `ssd1325.h` is the source of truth.

The screen uses direct SPI from userspace via a custom driver
(`matron/src/hardware/screen/ssd1325.cc`) instead of the old fbtft kernel driver. No
device tree overlay — all SPI control is GPIO (D/C), spidev (data), and libgpiod.

Both `main` and `upgrade/3.0.2` are at norns **v3.0.2** + the Fates fixes — `main` was
fast-forwarded to `upgrade/3.0.2` (`cc0354d9`) after a verified clean cold boot, and the
device runs this. The old 2.9.x state lives **only** in the `pre-upstream-3.0.2` tag
(`1e56b315`), pushed to origin. Working on `main` directly is fine.

## Architecture Notes
- **SSD1325 protocol**: all bytes in a command sequence (command byte + parameter bytes)
  must be sent with D/C=LOW. Pixel data is sent separately with D/C=HIGH. See
  `ssd1325_write_command` — the SPI transfer combines command + params into a single
  `spidev_buf` write with a single `gpiod_line_set_value(gpio_dc, 0)`.
- **Pixel data packing**: remap 0x56 sets vertical address increment. Data is sent
  column-by-column (x outer loop, y inner loop), packing two 4-bit pixels per byte.
- **Init values**: from the fbtft driver (proven working with this exact panel) — remap
  0x56, oscillator 0xF1, etc. These must not be changed.
- **GPIO**: Fates differs from stock norns — DC = GPIO17, RESET = GPIO4. Set in both
  `ssd1325.h` and `ssd1322.h`.
- **libmonome**: `monome_led_ring_intensity` doesn't exist in released versions. Use
  `monome_led_intensity` instead.
- **4th encoder**: this shield has 4 knobs, stock norns has 3. GPIO arrays are sized `[4]`
  in `matron/src/hardware/input/gpio.cc`; `lua/core/encoders.lua` has 4-element tables;
  `matronrc.lua` binds `knob4-event`.

## Build System
waf: `./waf clean && ./waf configure --release && ./waf build --release`

Since 3.0.2 the build lives in **`norns/wscript`**, not `matron/wscript` (deleted
upstream). All matron sources are `.cc` (C++). The source list there is explicit — a new
driver must be added to it or it is silently dropped from the link.

## Device Prerequisites (Raspbian 10 buster)
The box is too old for the packaged deps. Both were installed by hand and are **not**
recoverable from git — a reimage needs them again:

- **nng 1.11** from source. buster has no `libnng-dev`, and 3.0.2 replaced nanomsg with
  nng. Upstream's Dockerfile does the same (`Dockerfile:44`, `142-148`):
  `git clone --depth=1 --recursive --branch v1.11 https://github.com/nanomsg/nng.git`,
  then `cmake .. && make && sudo make install && sudo ldconfig`. Uses `make`, not ninja.
  Installs as static `/usr/local/lib/libnng.a` only.
- **`/usr/lib/arm-linux-gnueabihf/pkgconfig/readline.pc`** written by hand. buster's
  `libreadline-dev` ships the lib and header but no pkg-config file. Without it
  `maiden-repl/wscript:12` fails configure.
- **`~/matronrc.lua`** copied from the repo, or the 4th encoder never binds.

## Fates Hardware Configuration
The only annotated copy of the original Fates image config lives in the **untracked**
`temp/tue_may_26_2026_updating_norns_without_losing_custom_drivers.md` — consult it
before touching boot config. Recorded here so the repo carries it too:

### Boot overlays (`/boot/config.txt`)
```
enable_uart=1
dtoverlay=i2s-mmap
dtoverlay=miniuart-bt            # BT moved to mini-UART, freeing PL011 (ttyAMA0)
dtoverlay=uart0
dtoverlay=midi-uart0             # PL011 pinned to MIDI baud (31250) for DIN MIDI
dtoverlay=rpi-proto              # WM8731 codec on I2S
dtoverlay=fates-buttons-4encoders
dtoverlay=fates-ssd1325          # legacy kernel overlay; userspace driver replaced it
```
`hciuart` must stay disabled — `update/update.sh:459` already does this (stock shield
code; don't remove).

### UART-MIDI
Chain: `midi-uart0` overlay (31250-baud PL011 on GPIO14/15) → **ttymidi** daemon
([okyeron/ttymidi](https://github.com/okyeron/ttymidi), run with
`-s /dev/ttyAMA0 -b 31250`) → ALSA sequencer ports → norns picks it up like any USB
MIDI interface, no norns-side config needed. Verify with `amidi -l` / `aconnect -l`.
ttymidi ships a `ttymidi.service`; the Fates image launched it from `/etc/rc.local`
(copied from `/home/we/fates/install/norns/files/rc.local`). Exact launch line on this
device still unverified (device offline when checked 2026-10-01).

### WM8731 audio (card `sndrpiproto`; `/etc/asound.conf` maps default to it)
- **One** DAC volume pair ("Master Playback Volume", LOUT1V/ROUT1V) feeds the line outs
  (LOUT/ROUT) *and* the headphone driver (LHPOUT/RHPOUT). Separate level adjustments per
  output are **not possible with this codec** — a line/headphone level difference is a
  wiring property, not a mixer property.
- Input mux is hardware-switchable: DAPM enum `Input Select` with options `Line In` /
  `Mic` (old kernels expose it as `Input Mux`):
  `amixer -c sndrpiproto cset name='Input Select' 'Mic'`
- Other controls: `Capture Volume` (0–31), `Mic Boost` (+20 dB), `Sidetone` (input→
  output bypass monitor), `Line`/`Mic` capture switches, `ADC High Pass Filter`,
  `Playback Deemphasis`.
- The 2.9.x image build applied a one-time fix using bare numids (`amixer cset numid=3
  29`, `numid=4/8/10/13 on`, then `alsactl store`) — see the temp note near line 2375.
  Map them with `amixer -c sndrpiproto contents` before reusing; prefer names over numids.
- **Mic/line Lua switch**: the old menu code is in *no* reachable git history (this fork,
  all branches and tags, and the fates-project fork tips) — most likely a device-local
  edit wiped by an update (the update.sh-overwrites-repo issue). Reimplementation is
  small: a SETTINGS/MIX menu entry calling `norns.system_cmd` with the amixer line
  above, re-applied via `hook.system_post_startup` (`lua/core/norns.lua:268`) on every
  start, persisted with `sudo alsactl store`. Confirm the exact control name on the
  device first.

## systemd Layout
Pre-3.0.2 the device ran two services (`norns-matron`, `norns-crone`) pointing at
`build/matron/matron` and `build/crone/crone`. 3.0.2 converged these into **one** binary
at `build/norns/norns`, and upstream's `image/config/etc/systemd/system/` provides
`norns-main.service`.

**Do not blindly copy upstream's units.** `norns-jack.service` ships hardcoded
`hw:sndrpimonome`; the Fates codec is a WM8731 on card `sndrpiproto`. Installing
upstream's unit as-is makes jackd fail with `control open "hw:sndrpimonome" (No such
device)`, which cascades — `norns-sclang` and `norns-main` both `Requires=` jack, so the
whole stack dies and the screen is blank on boot. The repo copy is already corrected;
diff every unit before touching the device.

`norns-watcher.service` exists upstream but is **not** in `norns.target`'s `Requires`
list, so it never runs.

**Enable `norns.target`, don't just start it.** `systemctl start norns.target` runs the
stack once; only `systemctl enable norns.target` registers it at boot (via
`multi-user.target.wants`). Skipping enable leaves the box working until the next reboot,
then blank. This bit once already — the target was disabled and `norns-main` never started
on cold boot. Verify after any service change with `systemctl is-enabled norns.target`.

The Fates image also keeps its own copies of the old unit files under
`/home/we/fates/install/norns/files/` — useful reference for rebuilding an image.

## jackd Wedge (fixed; recovery below is a fallback)
Restarting `norns-main` used to orphan jackd's shared-memory registry:

```
JACK semaphore error: semop (Invalid argument)
jack_shm_lock_registry fails...
jack_client_open() failed; status = 17
unable to connect to JACK server → child killed (signal 6) → start-limit-hit
```

**Fixed in `5ffbfe73`**: `norns-jack.service` carries `PartOf=norns-main.service` and
`norns-main.service` carries `BindsTo=norns-jack.service`, so the two cycle together.
Verified with three consecutive plain `systemctl restart norns-main` runs and a cold boot —
a bare restart, including SYSTEM>RESTART, is safe as long as the device's installed unit
files match the repo copies. If the wedge ever reappears (stale units on the device, or a
different cause), recovery:

```
sudo systemctl stop norns-main norns-sclang
sudo systemctl restart norns-jack
sudo systemctl reset-failed norns-main
sudo systemctl start norns.target
```

`reset-failed` is required — without it systemd's start-limit latch refuses the unit even
once jack is healthy. A plain reboot also clears it.

## Version Display
The main menu's top-right reads **`$HOME/version.txt`**, not `update/version.txt` inside
the repo (`lua/core/norns.lua:176` → `lua/core/menu/home.lua:79`). The repo copy is
`260819`; the device's `$HOME` copy can go stale — sync with
`cp ~/norns/update/version.txt ~/version.txt`.

Note `version.mk` is `0.0.0` and always has been — upstream never populates it. Only
`VERSION_HASH` (from `git rev-parse --short HEAD`) is real. Don't read a version off the
binary.

## Upstream Update Procedure
1. Tag the current good state first: `git tag pre-upstream-<ver>`
2. Branch off `main`: `git checkout -b upgrade/<ver>`
3. `git merge upstream/main` — **never `--squash`**, the rename detection is what carries
   your edits from `screen.c` onto upstream's `screen.cc`.
4. Resolve conflicts. Expect `.github/workflows/*` (take upstream or drop), `norns/wscript`
   (keep yours), `lua/core/menu/update.lua` and `update/update.sh` (keep upstream's new
   structure, reapply the Fates values — both gained helper functions upstream, so a
   plain "ours" throws away new logic).
5. Port `ssd1325.c` → `.cc` and add it to `norns/wscript` in place of `ssd1322.cc`.
6. Push. **GitHub blocks token pushes that add or modify `.github/workflows/` files**
   without the `workflow` scope. Either grant it or drop the workflow files.
7. On the device: submodules first (`git submodule update --init --recursive`), then build.

### Things that break on old toolchains
- **`jack_client.cpp`** — `std::atomic_fetch_add/exchange` on a `std::atomic<uint32_t>`
  with bare `int` operands fails template deduction on GCC 8. Upstream bug; their
  Dockerfile uses a newer compiler. Cast operands to `uint32_t`. **Worth reporting
  upstream.**
- C-isms in the driver: `open_spi()` was declared with no args but called with a path
  (silently fine in C, hard error in C++); `void*` from `calloc`/`realloc` needs casts.

## Development Workflow
1. Edit code on WSL2 machine
2. Commit and push to `https://github.com/schrab/norns` (branch `upgrade/3.0.2`)
3. On Fates RPi (192.168.8.163): `cd ~/norns && git pull && git submodule update --init
   --recursive && ./waf build --release`
4. A bare `systemctl restart norns-main` is safe since the PartOf/BindsTo coupling; use
   the jack-recovery sequence above only if the wedge reappears
5. Cold boot is a separate verification — a service start working does not prove the boot
   path works

## Rollback
`main` is now 3.0.2 (fast-forwarded), so 2.9.x lives only in the `pre-upstream-3.0.2` tag.
Branch off the tag when rolling back — don't `git checkout main` expecting 2.9.x. From the
device:

```
git checkout pre-upstream-3.0.2 && git submodule update --init --recursive && \
  ./waf configure --release && ./waf build --release && \
  sudo systemctl disable norns-main && sudo systemctl enable norns-matron norns-crone
```

The old unit files were moved to `/root/norns-{matron,crone}.service.disabled` (not
deleted) and originals also exist under `/home/we/fates/install/norns/files/`. To roll
back fully, restore them and re-enable:

```
sudo mv /root/norns-matron.service.disabled /etc/systemd/system/norns-matron.service
sudo mv /root/norns-crone.service.disabled   /etc/systemd/system/norns-crone.service
sudo systemctl enable norns-matron norns-crone && sudo reboot
```

The 2.9.x build (the tag) uses `libnanomsg-dev` (still installed) and doesn't need nng.

## Common Issues
- **update.sh overwrites repo**: the Fates update script replaces `/home/we/norns` with the
  packaged copy, losing local changes and branch state. Don't run SYSTEM>UPDATE on this
  box. The `hw:sndrpiproto` fixes in it (amixer card, `ssd1325-spi` overlay) are for
  building Fates images, not for this device's in-place upgrade.
- **Under-voltage detected**: the Pi's 5V supply is marginal. Causes SPI glitches, USB
  drops, crashes.
- **Missing libmonome**: `git clone https://github.com/monome/libmonome && cd libmonome &&
  ./waf configure --prefix=/usr && sudo ./waf install`
- **SSH**: the device needs a password (`we`@192.168.8.163). `sshpass` isn't installed
  here; use an `SSH_ASKPASS` script, and delete it afterwards. `fates` resolves to IPv6
  only — use the IPv4 address.