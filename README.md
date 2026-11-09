# Flipper Coyote

A Flipper Zero app that drives a **DG-Lab Coyote v2** electrostim unit directly
over Bluetooth LE — no phone, no computer, no server. Two channels, 21
waveforms, a Normal mode and a hold-to-fire mode.

> **Status: working, tested on hardware** — one Flipper Zero and one Coyote v2,
> by me. That includes the case that matters most: walking out of Bluetooth
> range mid-output stops the device. This is adult hardware-control software, so
> still **start at low power and try it on yourself first**; read **Safety**
> below.

## What you need

- A **Flipper Zero** running [Flipper Blue++](https://github.com/andydixon/flipper-bluepp)
  firmware. Stock firmware cannot act as a BLE central, so the app cannot work
  on it — this is the one unavoidable hurdle.
- A **DG-Lab Coyote v2**. The v3 speaks a different protocol and is not
  supported.

## Install

1. **Flash Blue++ firmware.** Clone it and run `./build.sh flash` (Flipper
   connected over USB, qFlipper closed), or `./build.sh` and install the
   resulting `dist/f7-C/f7-update-*/` package from the Flipper's file browser.
   This replaces the radio stack as well as the firmware; reinstalling official
   firmware restores the stock one.
2. **Copy `coyote.fap`** (from this repo's
   [Releases](../../releases), or build it yourself below) to the SD card under
   `apps/Bluetooth/`. Launch it from Apps → Bluetooth → Flipper Coyote.
3. Switch the Coyote on. The app scans at launch and connects on its own.

Notes:

- The release `.fap` is built against Blue++ on firmware 1.4.3 with
  `BLE_API=all` (**API 87.2**). If the Flipper refuses to load it ("API
  mismatch"), your firmware exports a different symbol set — rebuild from
  source against your own Blue++ checkout.
- Settings live at `/ext/apps_data/coyote/settings.bin`. Delete that file to
  reset. Power levels are never saved: every launch starts at zero.
- While the app runs, the Flipper's own Bluetooth advertising stops (so any
  phone link drops) and is restored on exit.
- **It connects to the first device advertising as "D-LAB".** If somebody
  else's Coyote is in range, it may grab theirs. A device picker is specified
  but not built — see SPEC.md "Choosing a Coyote". Don't use this in a room
  with more than one Coyote in it.

## Using it

Main menu: **Normal** / **Fire** / **Settings**, with the Coyote's name and
battery on the status line. Back from the menu exits (output zeroed first).

Both modes show one big number per channel, the selected channel's waveform
underneath, and arrows only where a change is possible.

| Button | Tap | Hold |
|---|---|---|
| ◀ ▶ | select A → B → Both → Options | previous / next enabled waveform |
| ▲ ▼ | power ± step | ramp up / down |
| OK | Normal: mute / unmute. Fire: — | Normal: waveform list. Fire: **output while held** |
| Back | all channels to 0, back to the menu | |

In Fire mode the number is the shock power and output is zero unless OK is
held; each press restarts the waveform from its first frame. Selecting
**Options** (the fourth stop in the ◀ ▶ cycle) and pressing OK opens that
mode's waveform set, auto-advance dwell and shuffle.

Settings → **Normal mode…** / **Fire mode…** (per-mode waveforms, auto-advance,
shuffle), **Channels…** (Max Power A/B, 5–292, default 85; power step 1–10),
**Device…** (vibration, channel-change buzz). Power units are the host
library's: the device's raw 11-bit power is `7 ×` the number shown, so 292 is
the hardware ceiling.

## Safety

- Set **Max Power** before you play, not during. The cap is enforced on every
  change and nothing can exceed it.
- **Never place pads so current crosses the chest or the heart.** Below the
  waist only.
- Levels reset to 0 at launch, on every return to the menu and on reconnect;
  Back always zeroes output before leaving a mode.
- Each pattern write covers only 100 ms of output, so a dropped link leaves the
  device silent within a tick. **Tested:** walking out of range mid-output stops
  it. Worth confirming once on your own pair anyway, at a low level, before you
  trust it with anyone else.

## Build from source

The app needs Blue++'s full BLE central stack and its exported ST GAP/GATT
symbols; it will not build or load against stock firmware.

1. Clone [Flipper Blue++](https://github.com/andydixon/flipper-bluepp) and run
   `./build.sh` once (first build takes 5–15 minutes and fetches its own
   toolchain).
2. Copy this project's `app/` directory into the Blue++ checkout as `coyote/`,
   and add `coyote` to the `APPS` array in Blue++'s `build.sh`.
3. `BLE_API=hci,gap,gatt ./build.sh fap`

The result is `flipperzero-firmware/build/f7-firmware-C/.extapps/coyote.fap`,
and the full `./build.sh` also puts it inside the firmware update package.

`ble_central.c` is **not** in this repo: Blue++'s staging step copies
`libble/*.c` into any app that includes `ble_central.h`, so the BLE layer comes
from your own checkout. To skip Blue++'s `build.sh` and iterate with `./fbt`
directly, stage the app by hand and bring that layer with it:

```sh
cp -r app "$BLUEPP"/flipperzero-firmware/applications_user/coyote
cp "$BLUEPP"/libble/*.c "$BLUEPP"/libble/*.h \
   "$BLUEPP"/flipperzero-firmware/applications_user/coyote/
cd "$BLUEPP"/flipperzero-firmware && ./fbt -j4 COMPACT=1 DEBUG=0 \
  COPRO_STACK_TYPE=ble_full \
  COPRO_STACK_BIN=stm32wb5x_BLE_Stack_full_fw.bin fap_coyote
```

### Host checks

The state and packet helpers build and run without a Flipper:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Iapp \
  tests/test_core.c app/coyote_app.c app/waveforms.c -o /tmp/coyote-core-test
/tmp/coyote-core-test
```

No output and exit 0 means every assertion passed.

## Waveforms

The app ships **21 waveforms**, the hand-made set from Feeder's
`server/lib/coyote-sequences.js` (a separate, unpublished project of mine, and
where the v2 protocol port came from). `app/waveforms.c` and
`app/waveforms_generated.h` are generated from it and committed:

```sh
python3 tools/gen_waveforms.py [path/to/coyote-sequences.js]
```

The generator range-checks every frame (`x` 0–31, `y` 0–1023, `z` 0–31) and
writes the waveform count beside the table, so a build with a different set
stays consistent. Frames are `[x, y, z]` v2 pattern frames, one per 100 ms
tick: `x` pulses, `y` gap, `z` pulse width (above 20 stings noticeably more).

### Adding the DG-Lab presets yourself

The official DG-Lab app has 24 built-in Coyote presets. They are **not in this
repo** for licensing reasons (below), but nothing stops you adding them to your
own build:

```sh
python3 tools/gen_waveforms.py --with-dglab   # needs them in the source file
```

That expects the 24 sequences under the names in `DGLAB_NAMES`
(`EXTRUSION_DG`, `BUBBLE_DG`, …) in whatever `coyote-sequences.js` you point it
at. Getting the frame data is the part you have to do yourself:

1. **The easy 16.** DG-Lab publish 16 of the 24 themselves, with expected
   bytes, in
   [`socket/DG_WAVES_V2_V3_simple.js`](https://github.com/DG-LAB-OPENSOURCE/DG-LAB-OPENSOURCE/blob/master/socket/DG_WAVES_V2_V3_simple.js).
   Their share strings there carry some stale rest times compared with the
   current app.
2. **All 24.** They live in the app's UI web bundle (`res/3E.zip` inside the
   APK), in a `static/js/async/*.js` chunk: a table of share strings
   `Dungeonlab+pulse:<rest>,<speed>,<balance>=<sections>`, plus the parser and
   the v2/v3 frame generators. Reading the bundle and porting the generator is
   enough; the app's code does not need to be run.

The share-string format, the frequency and duration tables and the exact v2
frame maths (`x = max(1, floor(sqrt(ms/1000) * balance))`, `y = ms - x`,
`z = floor(intensity / 5)`) are the whole of what you need; the line above is
the summary of a longer write-up that lives in that unpublished repo. None of
the 24 use `z` above
20, so they are all gentler than the harsher custom waveforms at the same power
level.

## Licensing

**GPL-3.0-or-later.** The built app links Blue++'s `ble_central.c`
(GPL-3.0-or-later, Copyright © 2026 Andy Dixon) and the GPL-3.0 Flipper Zero
firmware's exported API, so the binary is a combined work and the whole thing
must be GPL-3.0-or-later. See [LICENSE](./LICENSE). None of Blue++'s source is
redistributed here — it comes from your own checkout at build time.

That is also why the DG-Lab presets are not committed here. DG-Lab grant no
licence for them and ask that their content not be used commercially without
authorisation; GPL-3.0 section 7 forbids adding restrictions of that kind, so
the two cannot be reconciled in one distributed work.

A `pre-commit` hook guards against adding them by accident, since a local
`--with-dglab` build rewrites the same tracked files. Enable it with:

```sh
git config core.hooksPath .githooks
```
