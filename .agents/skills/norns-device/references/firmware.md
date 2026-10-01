# Firmware and update procedures

## Rule zero: never run SYSTEM>UPDATE on this box

The Fates update script replaces `/home/we/norns` with the packaged copy,
destroying local changes and branch state. The `hw:sndrpiproto` fixes inside
`update/update.sh` (amixer card, `ssd1325-spi` overlay) are for **building
Fates disk images**, not for in-place upgrades on this device.

"Updating the device" therefore means one of two things:

1. Shipping new code → the deploy workflow in SKILL.md (push → pull → build →
   restart → cold boot). This is the everyday path.
2. Merging a new upstream norns version → procedure below.

## Device build prerequisites (Raspbian 10 buster)

Installed by hand, **not recoverable from git** — a reimage needs them again:

- **nng 1.11** from source (buster has no `libnng-dev`; 3.0.2 replaced
  nanomsg): `git clone --depth=1 --recursive --branch v1.11
  https://github.com/nanomsg/nng.git`, `cmake .. && make && sudo make install
  && sudo ldconfig`. Installs static `/usr/local/lib/libnng.a` only.
- **`/usr/lib/arm-linux-gnueabihf/pkgconfig/readline.pc`** handwritten
  (buster's libreadline-dev ships no pkg-config file; without it
  `maiden-repl/wscript` fails configure).
- **`~/matronrc.lua`** copied from the repo — without it the 4th encoder
  never binds.
- `lua5.3` interpreter is absent (package 404s on the stale buster archive);
  `lua` (5.1) exists and is fine for syntax checks.

## Upstream version upgrade (merge procedure)

1. Tag the current good state: `git tag pre-upstream-<ver>` and push the tag
   (rollback points that exist on only one machine do not exist).
2. Branch: `git checkout -b upgrade/<ver>`.
3. `git merge upstream/main` — **never `--squash`**: the rename detection is
   what carries fork edits from `screen.c` onto upstream's `screen.cc`.
4. Conflict map:
   - `.github/workflows/*` — take upstream or drop (token pushes that add or
     modify workflow files need the `workflow` scope; else drop).
   - `norns/wscript` — keep ours, then re-add any new upstream sources and
     keep `ssd1325.cc` in place of `ssd1322.cc`.
   - `lua/core/menu/update.lua` and `update/update.sh` — keep upstream's new
     structure and reapply the Fates values (both gained helper functions
     upstream; a plain "ours" throws away new logic).
5. Port `ssd1325.c` → `.cc` if upstream renames/moves it; C-isms become hard
   errors (`open_spi()` arg mismatch, `void*` casts).
6. Device: submodules first, build, restart, **cold boot**.

## Old toolchain constraints (these fixes are load-bearing)

- `jack_client.cpp` — `std::atomic_fetch_add/exchange` with `uint32_t{...}`
  operand casts: GCC 8 template deduction fails otherwise.
- `alsa_ctl.cc` — buster alsa-lib 1.1.8 uses `snd_ctl_elem_value_set_integer`
  and `snd_ctl_elem_value_set_enumerated(elem, idx, val)`; the newer
  `set_value` / `set_enumerated_item` names do not exist.
- `version.mk` stays `0.0.0` — upstream never populates it. Only
  `VERSION_HASH` is real.
- Rollback to 2.9.x = the `pre-upstream-3.0.2` tag on origin, **not** `main`
  (main is 3.0.2-era now). See AGENTS.md for the full device-side rollback
  sequence including restoring the archived unit files.
