# AGENTS.md

Guidance for coding agents (Claude Code, Codex and friends) working in this
repository. Humans want [README.md](./README.md); the original design and device
behaviour are in [SPEC.md](./SPEC.md).

## What this is

A Flipper Zero external app (`.fap`) that drives a DG-Lab Coyote v2 electrostim
unit over BLE as a central. It only builds and runs against
[Flipper Blue++](https://github.com/andydixon/flipper-bluepp) firmware, which
swaps in ST's full BLE stack and exports the GAP/GATT command API to apps.

**This is adult hardware-control software.** Anything touching power levels,
caps, the Fire-mode output path or the disconnect/shutdown path is safety code.
Do not loosen a clamp, widen a range or shorten a reset path without saying so
explicitly; prefer failing closed (output zero).

## Layout

| Path | What |
| --- | --- |
| `app/application.fam` | FAP manifest (appid `coyote`, category Bluetooth) |
| `app/coyote_fap.c` | Everything UI: pages, input handling, drawing, the 20 ms worker and 100 ms output tick, settings file I/O |
| `app/coyote_app.c/.h` | Pure state: settings defaults and validation, waveform masks, level stepping, the power packet. No Flipper APIs — this is what the host tests exercise |
| `app/coyote_proto.h` | Coyote v2 wire encoding, header-only |
| `app/waveforms.c`, `app/waveforms_generated.h` | **Generated.** Do not hand-edit; run `tools/gen_waveforms.py` |
| — | `ble_central.c/.h` are **not** in this repo. Blue++'s build stages its own `libble/` copies into the app; don't vendor them back in |
| `tests/test_core.c` | Host test for the state/packet helpers; silent + exit 0 on pass |
| `tools/gen_waveforms.py` | Generates the waveform table from Feeder's `coyote-sequences.js` |
| `coyote.fap` | Local build artifact, gitignored. Releases carry the published one |

## Commands

CI (`.github/workflows/ci.yml`) runs the host tests on every push and PR, and
fails the build if the waveform count is not 21 or any `*_DG` name appears in
`app/waveforms.c` — the same guard as the pre-commit hook, for contributors who
have not enabled it.

```sh
# Host tests — no hardware, no firmware checkout needed. Run these.
cc -std=c11 -Wall -Wextra -Werror -pedantic -Iapp \
  tests/test_core.c app/coyote_app.c app/waveforms.c -o /tmp/coyote-core-test
/tmp/coyote-core-test

# Regenerate the committed 21-waveform table
python3 tools/gen_waveforms.py /path/to/feeder/server/lib/coyote-sequences.js

# Build (needs a Blue++ checkout that has had one full ./build.sh).
# BLUEPP defaults to ~/flipper-bluepp; SEQUENCES overrides the generator source.
tools/build.sh                # -> coyote.fap          (21 waveforms)
tools/build.sh --with-dglab   # -> coyote-with-dg.fap  (45, local only)
```

`tools/build.sh` regenerates the table, stages the app plus Blue++'s `libble/`
into `applications_user/`, builds that one fap with `./fbt`, copies it to the
repo root, and **regenerates the 21-waveform table again on the way out** — so
whatever was built, the working tree ends up matching what is committed. The DG
variant builds under appid `coyote_dg` so both can be installed on the Flipper
at once, and reads `settings-dg.bin` rather than the normal build's
`settings.bin` (the masks are incompatible; see below).

Blue++'s own `BLE_API=hci,gap,gatt ./build.sh fap` still works if you add
`coyote` to its `APPS` array, and builds every app.

## Things that will bite you

- **The DG-Lab presets must never be committed.** A local
  `gen_waveforms.py --with-dglab` build rewrites `app/waveforms.c` and
  `app/waveforms_generated.h` — the same tracked files — taking the count from
  21 to 45. Their licence cannot be reconciled with this repo's GPL-3.0 (README
  "Licensing"). `.githooks/pre-commit` refuses such a commit; enable it with
  `git config core.hooksPath .githooks`. If you find the working tree carrying
  45 waveforms, that is the owner's local build, not a change to commit —
  `tools/build.sh` normally leaves it on 21, so a 45 tree means a bare
  `gen_waveforms.py --with-dglab` ran.
- **Only `coyote.fap` is publishable.** `coyote-with-dg.fap` carries the presets
  and is local-only; both are gitignored. Check the release asset with
  `strings -a coyote.fap | grep -c ' DG'` — it must be 0.
- **The two builds' settings files are not interchangeable.** A 45-waveform
  build's masks set bits above 21, which `coyote_settings_validate()` rejects,
  so the DG build uses `settings-dg.bin` (`#ifdef COYOTE_WITH_DGLAB`, emitted
  into `waveforms_generated.h` by the generator). Don't collapse them.
- **Waveform masks are `uint64_t`** precisely so a 45-waveform local build
  works; `1u << 45` is undefined behaviour, so never narrow them back.
- **The settings file is versioned** (`SETTINGS_SIZE`, magic + version byte at
  `bytes[4]`, currently v6, with a v5 migration path). Any field change needs a
  new version and a migration, or users' files are silently reset.
- **Power units are Feeder's, not the device's.** The v2's raw 11-bit field is
  `7 ×` the displayed level; 292 is the ceiling (`COYOTE_POWER_MAX`).
- **`coyote_proto.h` is a port**, verbatim from working code (rezreal/coyote via
  Feeder). Don't tidy it without the device in hand. The same goes for Blue++'s
  `ble_central.c`, which is upstream's file in upstream's repo — fix it there.
- **Output safety invariants**, all in `coyote_fap.c`: levels start at 0 at
  launch, on return to the menu and on reconnect; Back zeroes before leaving a
  mode; Fire output starts on `InputTypePress` and stops on `InputTypeRelease`;
  each pattern write covers only 100 ms, so a dropped link stops output. These
  are confirmed on hardware, out-of-range disconnect included — a change that
  weakens any of them is a regression, not a refactor.
- **Not built yet, though specified:** the device picker and "Forget Coyote".
  The app connects to the first device advertising as `D-LAB`
  (`coyote_fap.c:1153`). SPEC.md "Choosing a Coyote" is the design.
- **SPEC.md has drifted** in places — it describes the 45-waveform set, a
  pre-64-bit-mask settings format and a power step range of 1–20 (the code is
  1–10), and predates the per-mode options pages, auto-advance, shuffle and the
  channel-change buzz. Trust the code; fix the spec when you touch an area.
