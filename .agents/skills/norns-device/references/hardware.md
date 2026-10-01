# Fates hardware reference

The only annotated copy of the original Fates image config is the **untracked**
`temp/tue_may_26_2026_updating_norns_without_losing_custom_drivers.md` — read
it before touching boot config. Repo `AGENTS.md` carries the same facts in
tracked form.

## Boot overlays (`/boot/config.txt`) — verified set

```
enable_uart=1
dtoverlay=i2s-mmap
dtoverlay=miniuart-bt            # BT moved to mini-UART, freeing PL011 (ttyAMA0)
dtoverlay=uart0
dtoverlay=midi-uart0             # PL011 pinned to MIDI baud for DIN MIDI
dtoverlay=rpi-proto              # WM8731 codec on I2S
dtoverlay=fates-buttons-4encoders
dtoverlay=fates-ssd1325          # legacy kernel overlay; userspace driver replaced it
```

`hciuart` must stay disabled (`update/update.sh` does this — stock shield
code, do not remove).

## Display

SSD1325 OLED, **128x64**, 4-bit grayscale (2 pixels/byte), driven by the
userspace SPI driver `matron/src/hardware/screen/ssd1325.cc` (GPIO DC=17,
RESET=4, set in both `ssd1325.h` and `ssd1322.h`). Init values come from the
fbtft driver and are empirically proven — never change them.

## UART-MIDI

Chain: `midi-uart0` overlay (PL011 on GPIO14/15) → **ttymidi** daemon
([okyeron/ttymidi](https://github.com/okyeron/ttymidi)) → ALSA sequencer →
norns sees it like any USB MIDI interface.

- `ttymidi.service` (enabled) runs `/usr/bin/ttymidi -s /dev/ttyAMA0 -b
  38400 -n ttymidi` — **38400, not 31250**.
- ttymidi's ALSA client is bridged through a Virtual RawMIDI client so
  portmidi can use it.
- Verify: `aconnect -l` shows the ttymidi client connected.

## WM8731 codec (card `sndrpiproto`)

`/etc/asound.conf` maps the default device to this card.

- **One** DAC volume pair (`Master Playback Volume`, numid=1, 0–127,
  values=2) feeds the line outs (LOUT/ROUT) *and* the headphone driver
  (LHPOUT/RHPOUT). Separate line/headphone levels are **not possible with
  this codec** — a level difference between them is a wiring property.
- Control map (verified): `Master Playback Volume` (1), `Master Playback ZC
  Switch` (2), `Capture Volume` (3, 0–31), `Line Capture Switch` (4),
  `Mic Boost Volume` (5, 0–1), `Mic Capture Switch` (6), `Sidetone Playback
  Volume` (7, 0–3), `ADC High Pass Filter Switch` (8), `Store DC Offset
  Switch` (9), `Playback Deemphasis Switch` (10), output mixer switches
  (11/12/13), **`Input Mux` (14)** — enum: 0 = `Line In`, 1 = `Mic`.
- **Control path**: `matron/src/hardware/alsa_ctl.cc` writes via the kernel
  ALSA ctl interface (kernel-driver-coherent; `ALSA` is already in matron's
  uselib). Bindings: `_norns.gain_hp` (param 0–63 → master 0–127; falls back
  to the TPA6130A2 `i2c_hp` on factory norns) and `_norns.input_mux` (0/1).
  Lua: `Audio.headphone_gain`, `Audio.input_mux`; LEVELS params
  `headphone gain` + `input mux`; persisted in `norns.state.mix.*`.
- **`Audio.apply_state()`** runs from `Script.clear` after params rebuild.
  Param actions only fire when touched — without apply_state the saved
  LEVELS values never reached crone/codec at boot (this was why the monitor
  mode switch appeared dead: crone's monitor matrix defaults to silent).
- `/etc/rc.local` still sets `i2cset -f -y 1 0x1a 0x05 0x70` (LOUT1V=112) at
  boot. Superseded by apply_state/`gain_hp` (same value via
  `state.mix.headphone_gain` = 56). Raw i2c writes bypass the kernel driver's
  register cache — do not add more of them.
- udev `alsactl` restores `/var/lib/alsa/asound.state` at card probe; norns
  then overrides with its own state at script load.

## Read-back verification

Trust hardware state, not code claims:

```
amixer --device hw:sndrpiproto cget name="Master Playback Volume"
amixer --device hw:sndrpiproto cget name="Input Mux"
aconnect -l                       # uart-midi bridge
ls /dev/shm | grep -c jack        # jackd registry health
```
