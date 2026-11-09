# Flipper Coyote — spec

A Flipper Zero app (`.fap`) that drives a DG-Lab Coyote v2 directly over BLE: no phone, computer or server. Runs on the Blue++ firmware build in `~/flipper-bluepp` (official 1.4.3 + ST's full BLE stack, built with `EXPORT_FLASH=0 BLE_API=hci,gap,gatt`), which lets apps act as a BLE central.

Built for a dark play space: state readable at a glance, feedback you can feel without looking.

## Scope

- One Coyote v2 now. Data structures and settings are laid out for two (see **Two Coyotes later**).
- Coyote v3 is out of scope (different protocol; `lib/coyote3.js` in Feeder).

## Screens

```
Launch → "Searching for Coyote…" → Main menu
Main menu:  Normal / Fire / Settings
            status line: Coyote name + battery %, or "Not connected"
            Back → exit the app (all output zeroed first)
Normal, Fire:  Back → all channels to 0, return to the main menu
Settings:      Back → save, return to the main menu
```

## Choosing a Coyote

**Not yet built — current behaviour is to grab the first "D-LAB" device that advertises.** That is wrong anywhere another Coyote might be in range, which is exactly the play-party case: the app could connect to someone else's device.

Scanning starts at launch and is automatic, not a Settings action. What changes is which device it picks:

- **No Coyote remembered** (first run, or after "Forget"): the main menu is replaced by a **picker** listing every "D-LAB" device in range, strongest signal first, each row showing its name and RSSI as bars. The one in your hand is almost always strongest. OK connects and remembers that address; Back exits the app. The list refreshes every 300 ms and devices unseen for 60 s drop off (`bc_snapshot` already does both).
- **A Coyote remembered:** it connects to that address only and ignores every other device, so a stranger's Coyote in range is never touched. The picker never appears. This is the normal case at the party: launch, connect, go.
- **Remembered Coyote not found:** the status line reads "Looking for <name>…" and it keeps scanning. OK opens the picker so a different device can be chosen deliberately; the remembered one is not replaced until a new one is picked.
- **Reconnect after a drop** always targets the remembered address, and comes back at level 0 as now.

Settings gains **Forget Coyote**, which clears the remembered address (and that slot's caps) and returns to the picker. This also fixes the current dead end where both address-keyed slots fill permanently and a third device is refused with no way to clear them.

The settings record already carries `addr`/`addr_type` per slot, so remembering costs no format change; a `remembered` flag (or slot index) is enough. The picker is a second `View` on the existing `ViewDispatcher`.

## Buttons

Flipper input: 5-way pad (Up, Down, Left, Right, OK) + Back. Per button the firmware emits `Press` and `Release` (immediate), `Short` (on release, only if no `Long`), `Long` (~300 ms held, while still held) and `Repeat` (~every 150 ms after `Long`). So tap and hold on the same button never both fire.

### Normal mode

| Button | Tap | Hold |
|---|---|---|
| ◀ ▶ | channel A → B → Both → A | previous / next enabled waveform |
| ▲ | +step | ramp up, one step every ~0.4 s |
| ▼ | −step | ramp down, one step every ~0.2 s |
| OK | mute / unmute the selected channel(s) | Normal waveform list (same as Settings → Normal waveforms) |
| Back | all channels off, back to the menu | |

- Mute fires on release of OK (the firmware only knows it was a tap once the button comes up; < 0.3 s).
- Unmute ramps back to the pre-mute level over ~1 s instead of jumping.
- With Both selected, ▲/▼ move both channels by the same step, each clamped to its own cap; ◀ ▶ hold sets both to the same waveform.

### Fire mode

Same layout as Normal. The number is the shock power; output is 0 unless OK is held.

| Button | Tap | Hold |
|---|---|---|
| ◀ ▶ | channel A → B → Both → A | previous / next enabled waveform |
| ▲ ▼ | shock power ±step | ramp up / ramp down (same rates as Normal) |
| OK | — | shock the selected channel(s) for as long as it is held; no time limit |
| Back | all channels off, back to the menu | |

- Output starts on `Press` and stops on `Release` (≤ 100 ms latency: the next tick).
- Each press restarts the waveform from its first frame, so a quick tap always gets the strong opening frames rather than landing in a gap.

## Settings

Saved to the SD card (`/ext/apps_data/coyote/`). Power levels are never saved: every launch starts at 0.

| Setting | Default | Range |
|---|---|---|
| Cap, channel A | 85 | 5–292 |
| Cap, channel B | 85 | 5–292 |
| Power step | 5 | 1–20 |
| Vibration | On | On / Off |
| Normal waveforms | all enabled | checkbox list, at least one stays enabled |
| Fire waveforms | the punishing set below | checkbox list, at least one stays enabled |

Power units are Feeder's: `lib/coyote.js` writes `7 × level` into the v2's 11-bit power field, so 85 here = `maxPower` 85 in `lib/coyotes.js`, and 292 (2047 / 7) is the hardware ceiling.

## Waveforms

The 45 sequences in Feeder's `server/lib/coyote-sequences.js`, ported as-is (`[x, y, z]` v2 pattern frames, one per 100 ms tick): Feeder's 21 hand-made ones, then the 24 built-in presets of the official DG-Lab app 4.2.6 (`*_DG`, in the app's order, under the app's English names; "Heartbeat" becomes "Heartbeat DG" to stay distinct). How they were extracted and checked is in `feeder-2.0/dglab-official-waveforms.md`. DG-Lab grant no licence and forbid commercial use without permission, so keep the app non-commercial and credit them. Display names must fit one ~12-character line.

45 waveforms no longer fit the `uint32_t` enable masks (`normal_waveforms_mask` / `fire_waveforms_mask`, `COYOTE_ALL_WAVEFORMS_MASK`); they need 64-bit masks, and the settings file format changes with them. `tools/gen_waveforms.py` needs the 24 new names added to `NAMES`.

| Sequence | Display name | Normal default | Fire default |
|---|---|---|---|
| HEARTBEAT | Heartbeat | on | |
| BREATH | Breath | on | |
| KNOCK | Knock | on | |
| WAVE | Wave | on | |
| STUTTER | Stutter | on | **on** |
| RAMP_UP_DROP | Ramp & drop | on | |
| THRUM | Thrum | on | |
| BURST_TRAIN | Burst train | on | |
| SLOW_BURN | Slow burn | on | |
| CHAOS | Chaos | on | |
| CUSTOM_A | Tap squeeze | on | |
| CUSTOM_B | Light taps | on | |
| CUSTOM_C | 3 light taps | on | |
| CUSTOM_D | Low buzz | on | **on** |
| CUSTOM_E | Squeeze | on | |
| CUSTOM_F | Fast taps | on | **on** |
| CUSTOM_G | Tap to buzz | on | |
| CUSTOM_H | Tap to buzz 2 | on | |
| CUSTOM_I | Hit / pulse | on | **on** |
| CUSTOM_J | Slow pulse | on | |
| CUSTOM_K | Ramp 2 | on | |
| EXTRUSION_DG | Extrusion | on |  |
| BUBBLE_DG | Bubble | on |  |
| RHYTHM_DG | Rhythm | on |  |
| AIR_WAVES_DG | Air Waves | on | **on** |
| DANCE_DG | Dance | on |  |
| CLIMB_DG | Climb | on |  |
| SHADE_DG | Shade | on | **on** |
| PULSE_DG | Pulse | on | **on** |
| BREATHING_DG | Breathing | on |  |
| TIDE_DG | Tide | on |  |
| PULSATING_DG | Pulsating | on | **on** |
| QUICK_RUB_DG | Quick Rub | on |  |
| GRADUAL_RUB_DG | Gradual Rub | on |  |
| HEARTBEAT_DG | Heartbeat DG | on | **on** |
| COMPRESS_DG | Compress | on | **on** |
| RHYTHMIC_DG | Rhythmic | on |  |
| GRAINY_DG | Grainy | on | **on** |
| BOUNCY_DG | Bouncy | on |  |
| RIPPLE_DG | Ripple | on |  |
| RAINFALL_DG | Rainfall | on |  |
| TEMPO_TAP_DG | Tempo Tap | on | **on** |
| SIGNAL_DG | Signal | on | **on** |
| TEASE_1_DG | Tease 1 | on |  |
| TEASE_2_DG | Tease 2 | on |  |

Fire defaults were picked from the frame data, not by feel: high pulse width (`z`, > 20 is stingier), fast rate, and no silent frames near the start, so even a short press is felt. The official presets never go above `z` = 20, so their Fire picks are the ones at full `z` from the first frame. Needs confirming on the device.

## Layout (128×64)

Two columns, one per channel, each a large number (`FontBigNumbers`). ▲ / ▼ are drawn only around the selected channel(s), so the selection is visible without inversion; ▲ disappears at the cap and ▼ at 0, so a limit shows without reading the number.

```
NORMAL                Coyote 82%
     ▲
    42             MUTE         ← big digits
     ▼              (18)
  Breath          Pulse 3
```

- Muted channel: `MUTE` with the pre-mute level small underneath.
- Fire mode: header `FIRE`; while OK is held the selected column(s) invert and the header reads `ZAP`.
- Disconnected: header shows it; levels are 0 after reconnecting.

## Feedback

- **Backlight:** follows the Flipper's own timeout.
- **Vibration** (Settings → Vibration turns all of it off):
  - channel change: 1 buzz = A, 2 = B, 3 = Both
  - a light tick per power step
  - Fire mode: buzzes continuously while a shock is held
  - nothing on STOP / Back, nothing while disconnected, nothing during Normal output
- **LED:**
  - off: no output
  - dim green: Normal output on
  - red: shock firing
  - blue blink: disconnected

## Safety

- Per-channel cap enforced on every change; nothing can exceed it.
- Every launch, every return to the menu and every reconnect starts at 0.
- Back always zeroes all channels before leaving a mode.
- If the app crashes or the link drops, the v2 stops on its own because each pattern write covers only 100 ms of output. **Confirmed on hardware**, by walking out of Bluetooth range mid-output.

## Two Coyotes later

**Low priority.** One Coyote covers what almost anyone needs; this is a someday item, not a planned feature.

Nothing is keyed to a BLE address any more. Max power used to be saved per device, on the reasoning that two Coyotes on different placements need different limits — but pad placement does not survive between sessions anyway, so an address-keyed limit remembers the wrong thing, and it cost a permanently-filling slot table, a failure path on connect and a "Forget Coyote" command to dig out of it. Caps, step and the per-mode waveform sets are now stored once, flat.

What would still be needed:

- **Telling the two apart while setting max power.** The danger is adjusting a limit while looking at the wrong device. Ideas, in order of how likely they are to work:
  - *Identify pulse* — briefly drive the device at a low fixed level, the way Feeder's calibration does (`IDENTIFY_POWER` in `lib/coyotes.js`, "where do you feel this pulse?"). Proven against this hardware, needs no new protocol, and identifies the device by feel rather than by sight, which suits a dark room.
  - *Blink its LEDs* — nicer when the devices are visible, but **no LED characteristic is known in the v2 protocol**: `lib/coyote.js` only touches config, power, pattern and battery. Would need the GATT database walked (BT Inspector can do this) to find out whether one exists at all. Treat as unverified.
  - Showing each device's battery percentage next to its name is a weak tiebreak and free.
- **A device picker** at startup, since the app currently grabs the first "D-LAB" device that advertises. Needed before any room with more than one Coyote in it, independently of supporting two at once.
- The runtime arrays already carry a device dimension (`levels[device][channel]`, `active_device_index`), which is the only scaffolding left. Blue++ allows two BLE links.
- Channels would join the left/right cycle as A1 → B1 → A2 → B2 → All → Options, with four narrower columns.

## Open questions

1. Fire defaults (Stutter, Low buzz, Fast taps, Hit / pulse, and the nine official presets marked above): confirm or change after trying them on the device.
