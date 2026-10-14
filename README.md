# Flipper Coyote

Flipper Zero external app for controlling one DG-Lab Coyote v2 over BLE. The
implementation and device behavior are described in [SPEC.md](./SPEC.md).

## Build

The app uses Blue++ firmware's full BLE central stack and exported ST GAP/GATT
APIs. It is not expected to work on stock firmware.

1. Build or prepare the Blue++ checkout described in `SPEC.md`.
2. Copy this project's `app/` directory into the Blue++ checkout as `coyote/`.
3. Add `coyote` to the `APPS` array in Blue++'s `build.sh`.
4. Build the FAP with `BLE_API=hci,gap,gatt ./build.sh fap` after that firmware
   checkout has had its initial full build.

The resulting app is `flipperzero-firmware/build/f7-firmware-C/.extapps/coyote.fap`.
The full Blue++ build script may also place it in the firmware update package.

The runtime and settings reserve two address-keyed Coyote slots with two
channels each. Only one device is scanned and connected by the current UI;
power levels are always reset to zero and are never saved.

## Waveforms

The app ships **21 waveforms**, the hand-made set from Feeder's
`server/lib/coyote-sequences.js`. `app/waveforms.c` and
`app/waveforms_generated.h` are generated from it:

```sh
python3 tools/gen_waveforms.py [path/to/coyote-sequences.js]
```

The generator checks every frame is in range (`x` 0-31, `y` 0-1023, `z` 0-31)
and writes the waveform count beside the table, so builds with different
waveform sets stay consistent.

### Adding the DG-Lab presets yourself

The official DG-Lab app has 24 built-in Coyote presets. They are **not in this
repo** for licensing reasons (below), but nothing stops you adding them to your
own build:

```sh
python3 tools/gen_waveforms.py --with-dglab   # needs them present in the source file
```

That expects the 24 sequences under the names in `DGLAB_NAMES`
(`EXTRUSION_DG`, `BUBBLE_DG`, …) in whatever `coyote-sequences.js` you point it
at. Getting the frame data is the part you have to do yourself. Two routes:

1. **The easy 16.** DG-Lab publish 16 of the 24 themselves, with expected
   bytes, in
   [`socket/DG_WAVES_V2_V3_simple.js`](https://github.com/DG-LAB-OPENSOURCE/DG-LAB-OPENSOURCE/blob/master/socket/DG_WAVES_V2_V3_simple.js)
   in their open-source repo. Note their share strings there carry some stale
   rest times compared with the current app.
2. **All 24.** They live in the app's UI web bundle (`res/3E.zip` inside the
   APK), in a `static/js/async/*.js` chunk: a table of share strings of the form
   `Dungeonlab+pulse:<rest>,<speed>,<balance>=<sections>`, plus the parser and
   the v2/v3 frame generators. Reading the bundle and porting the generator is
   enough; the app's code does not need to be run.

The share-string format, the frequency and duration tables, and the exact v2
frame maths (`x = max(1, floor(sqrt(ms/1000) * balance))`, `y = ms - x`,
`z = floor(intensity / 5)`) are written up in `dglab-official-waveforms.md` in
the Feeder repo, which is where this app's copy came from.

A practical note from that work: none of the 24 use `z` above 20, so they are
all gentler than Feeder's harsher custom waveforms at the same power level.

## Licensing

**GPL-3.0-or-later.** The app links `ble_central.c` from
[Flipper Blue++](https://github.com/andydixon/flipper-bluepp) (GPL-3.0-or-later,
Copyright (C) 2026 Andy Dixon), itself derived from the GPL-3.0 Flipper Zero
firmware, so the whole work must be GPL-3.0-or-later. See `LICENSE`.

That is also why the DG-Lab presets are not committed here. DG-Lab grant no
licence for them and ask that their content not be used commercially without
authorisation; GPL-3.0 section 7 forbids adding restrictions of that kind, so
the two cannot be reconciled in one distributed work. Keeping them out is the
only honest option until DG-Lab say otherwise.

A `pre-commit` hook guards against adding them by accident, since a local
`--with-dglab` build rewrites the same tracked files. Enable it with:

```sh
git config core.hooksPath .githooks
```

## Host checks

The state and packet helpers can be tested without Flipper hardware:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Iapp \
  tests/test_core.c app/coyote_app.c app/waveforms.c -o /tmp/coyote-core-test
/tmp/coyote-core-test
```

To regenerate the pinned 21-waveform table from Feeder:

```sh
python3 tools/gen_waveforms.py /path/to/feeder/server/lib/coyote-sequences.js
```

The generator intentionally ignores additional sequences in newer Feeder
versions; adding presets requires updating the app's names, mask, defaults, and
display behavior together.

## Bench verification

The BLE protocol and output safety behavior still need bench verification with
the target Coyote v2 and Blue++ Flipper before use. In particular, verify that
output stops on disconnect and confirm the Fire defaults by feel, as called out
in [SPEC.md](./SPEC.md).
