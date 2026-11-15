# Flipper Coyote

[![CI](https://github.com/kinkytechiekitty/flipperzero-coyote/actions/workflows/ci.yml/badge.svg)](https://github.com/kinkytechiekitty/flipperzero-coyote/actions/workflows/ci.yml)

A Flipper Zero app that drives a **DG-Lab Coyote v2 or v3** electrostim unit directly
over Bluetooth LE — no phone, no computer, no server. Includes a Normal mode and
a hold-to-fire mode and 21 waveforms that I made semi-randomly.

> **Status: working, tested on hardware** — one Flipper Zero and one Coyote v2.
> **Coyote v3 support is new**: basic use has been tried on a v3 and works, but
> the checklist in `TODO.md` (link loss in particular) is not finished, so read
> the v3 notes under **Safety** before using it.
> This is adult hardware-control software, so **start at low power and try it on yourself first**;
> read **Safety** below.

## What you need

- A **Flipper Zero** running [Flipper Blue++](https://github.com/andydixon/flipper-bluepp)
  firmware. Stock firmware cannot act as a BLE central, so the app cannot work
  on it.
- A **DG-Lab Coyote v2** (advertises as `D-LAB…`) or **Coyote v3** (advertises
  as `47L121000`). The two speak different protocols; the app works out which
  one it has after connecting and shows `v2` or `v3` beside the device name.
  v3 support has had only light hardware testing so far.

## Install

1. **Flash Blue++ firmware.** Clone it and run `./build.sh flash` (Flipper
   connected over USB, qFlipper closed), or `./build.sh` and install the
   resulting `dist/f7-C/f7-update-*/` package from the Flipper's file browser.
   This replaces the radio stack as well as the firmware; reinstalling official
   firmware restores the stock one.
2. **Copy `coyote.fap`** (from this repo's
   [Releases](../../releases), or build it yourself below) to the SD card under
   `apps/Bluetooth/`.
3. Turn on your DG-Lab Coyote.
4. Launch the Coyote app from Apps → Bluetooth → Flipper Coyote.  The app should
   automatically scan for and then connect to the Coyote.

Notes:

- The release `.fap` is built against Blue++ on firmware 1.4.3 with
  `BLE_API=all` (**API 87.2**). If the Flipper refuses to load it ("API
  mismatch"), your firmware exports a different symbol set — rebuild from
  source against your own Blue++ checkout.
- Settings live at `/ext/apps_data/coyote/settings.bin`. Delete that file to
  reset.
- While the app runs, the Flipper's own Bluetooth advertising stops (so any
  phone link drops) and is restored on exit.
- **Safety Note: The app connects to the Coyote with the strongest signal.**
  It looks for v2 (`D-LAB…`) and v3 (`47L121000`) units, waits a second after
  the first one shows up, then connects to whichever is loudest — normally the
  nearest. There currently isn't a way to force it to connect to a specific
  Coyote, so with several in range and not already paired with a controlling
  device (phone app, etc) it can still pick one that isn't yours. A way to
  choose is planned (see [TODO.md](./TODO.md)) but doesn't exist yet.

## Using it

Main menu: **Normal** / **Fire** / **Settings**, with the Coyote's name and
battery on the status line. Back button from the menu exits and disconnects
the Coyote.

Both modes show one big number per channel, the selected channel's waveform
underneath, and arrows only where a change is possible.

| Button | Tap | Hold/Long press |
|---|---|---|
| ◀ ▶ | Select Channel: A → B → Both → Options | Select Waveform: previous / next enabled waveform |
| ▲ ▼ | power ± step | ramp up / down |
| OK | Normal mode: mute / unmute. Fire mode: **shock while held** | Normal: waveform list. Fire: **shock while held** |
| Back | Resets all channels to 0, returns to the main menu | |

In Fire mode the number is the shock power and output is zero unless OK is
held; each press restarts the waveform from its first frame. Selecting
**Options** (the fourth stop in the ◀ ▶ cycle) and pressing OK opens Fire
mode's waveform set, auto-advance dwell, and shuffle.

Settings →
   **Normal mode…** / **Fire mode…** (per-mode waveforms, auto-advance,
shuffle),
   **Channels…** (Max Power A/B, 5–292, default 85; power step 1–10),
   **Device…** (vibration, channel-change buzz).

Power numbers are the same ones the official DG-Lab apps show. On a v2 the
device's raw 11-bit power is `7 ×` the number shown, so 292 is the hardware
ceiling. A v3 takes the number as its strength directly and tops out at **200**:
on a v3 a Max Power above 200 acts as 200, and levels stop there. Whether the
same number *feels* the same on a v2 and a v3 has not been checked — start low.

## Safety

- **Max Power** sets the upper limit of how high channels can go.
- Levels reset to 0 at launch, on every return to the menu and on reconnect;
  Back always zeroes output before leaving a mode.
- If the Coyote goes out of range from the FlipperZero and loses connection,
  all output stops immediately.  I think this is how the Coyote app works.
  **This is confirmed on a v2 only** — see below for the v3.

### Coyote v3 notes

- **Soft limit.** On connecting to a v3 the app sets the device's own strength
  limit to your Max Power (per channel, 200 at most) before any output, so the
  wheels on the unit cannot take it higher. The v3 stores that limit across
  power cycles and the app **leaves it there on exit**. The official app sets
  its own limit the next time it connects; other tools may not, and will then
  inherit the lower one.
- **Link loss is not yet confirmed.** Each packet carries 100 ms of output, so
  output should stop when the Flipper goes away, but how a v3 actually behaves
  when the link drops has not been measured. Do not rely on it.
- **Some waveforms feel weaker.** A v3 has no direct equivalent of the v2's
  pulse trains, so five waveforms built on long ones — Custom B, C, D, I and J —
  come out weaker on a v3.
- **Pulse width clips.** Widths above `z` 20 (the "stings more" range on a v2)
  all become the v3's maximum intensity, so those waveforms lose that edge.

### Standard estim safety warnings apply!
- **Keep estim contact points below the waist**
- **Don't play with this if your bottom/subject has a pacemaker or any other electrical health related device**
- Kink is generally an inherently dangerous thing to do, estim even more so.
  Do your research, learn what you're doing.  You can't have fun if you're dead or you burn your nerves off.

## Build from source

The app needs Blue++'s full BLE central stack and its exported ST GAP/GATT
symbols; it will not build or load against stock firmware.

1. Clone [Flipper Blue++](https://github.com/andydixon/flipper-bluepp) and run
   `./build.sh` once (5–15 minutes; it fetches its own ARM toolchain).
2. `tools/build.sh` → `coyote.fap` in the repo root.

Set `BLUEPP` if the checkout is not at `~/flipper-bluepp`, and `SEQUENCES` if
Feeder's `coyote-sequences.js` is not where `tools/gen_waveforms.py` expects it.

`ble_central.c` is **not** in this repo — the BLE layer is copied out of your
Blue++ checkout at build time, which is what the script does for you. To build
through Blue++'s own `build.sh` instead, copy `app/` into the checkout as
`coyote/`, add `coyote` to its `APPS` array, and run
`BLE_API=hci,gap,gatt ./build.sh fap`; the `.fap` lands in
`flipperzero-firmware/build/f7-firmware-C/.extapps/` and inside the firmware
update package.

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
tools/build.sh --with-dglab   # -> coyote-with-dg.fap
```

That build is for your own Flipper and must never be released (see
**Licensing**). It installs as a separate app, "Coyote +DG", so it can sit
alongside the normal one, and it keeps its own `settings-dg.bin` — its waveform
masks set bits the 21-waveform build rejects, so a shared settings file would
reset whichever build opened it second. The script regenerates the committed
21-waveform table on its way out, so the working tree is never left carrying
preset data.

It expects the 24 sequences under the names in `DGLAB_NAMES` (`EXTRUSION_DG`,
`BUBBLE_DG`, …) in whatever `coyote-sequences.js` the generator reads. Getting
the frame data is the part you have to do yourself:

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
