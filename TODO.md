# TODO

Things that are specified or known to be missing, in rough priority order.

## Coyote v3: confirm on hardware

v3 support was written from DG-Lab's published BLE document and the official
app, and is covered by host tests. Basic use has since been tried on a v3 and
looked good; the owner did not tick individual items below, so treat each as
open until it is. Known: turning the device wheel does not change the power
shown in the app (expected, since the app never acts on B1).

1. [ ] v2 regression: connects, Normal and Fire behave as before.
2. [ ] v3 alone: connects, shows "v3" and the battery level.
3. [ ] Max Power 20: turn the device wheel up; it must stop at 20.
4. [ ] Level 5 on a test load or meter: output present; Back stops it.
5. [ ] Fire at level 5: starts on press, stops on release; note any lag.
6. [ ] Link loss at level 5: power the Flipper off, then repeat by walking out
       of range; time how long output continues.
7. [ ] Turn the wheel during Normal: does the app pull the strength back?
8. [ ] Exit the app, power-cycle the v3, turn the wheel up with nothing
       connected: it should still stop at your Max Power.
9. [ ] v2 and v3 both on: the nearer one is taken; swap and repeat.
10. [ ] Same number on v2 and v3 by feel, starting at 5.

Protocol questions behind those, still open (a no-output BLE probe from the
development Mac was attempted and could not run: macOS denied Bluetooth to the
tool's process, so nothing was sent to the device):

- [ ] GATT layout matches `app/coyote3_proto.h`, and Blue++ reports the UUIDs
      as 2-byte little-endian (the helper also accepts the 16-byte form).
- [ ] B0 with absolute strength is reflected in B1.
- [ ] BF is applied (it has no response): B0 absolute above the limit is held
      to the limit.
- [ ] Repeating the same absolute B0 does not ratchet.
- [ ] What happens when B0 packets stop: assumed output ends within 100 ms and
      the strength stays set on the device.
- [ ] Whether an off-cadence B0 (sent early when a strength drops, or on a
      Fire press) replaces the previous packet or queues behind it.
- [ ] Absolute strength every 100 ms overrides the device's wheels.

## Device selection

- [ ] **Device picker / choosing between several Coyotes.** Today the app takes
      the strongest signal among `D-LAB…` and `47L121000`, which is wrong
      anywhere someone else's Coyote may be nearer. SPEC.md "Choosing a Coyote"
      is the design.
- [ ] **"Forget Coyote"** in Settings, once there is something to remember.
- [ ] Blue++ keeps a device in its scan table for 60 s after it was last seen,
      so a unit that was just switched off can still be chosen (and fail to
      connect) for up to a minute. Prefer recently seen candidates.

## Coyote v3: later

- [ ] **Read the soft limit back.** No documented way exists (the BE command is
      deprecated); find a probe, so the app can confirm BF took instead of
      trusting it.
- [ ] On a v3, pressing Up past 200 can show a number above 200 for one redraw
      before the worker clamps it. Clamp in the step itself.
- [ ] Burst emulation for the pulse-train waveforms (Custom B, C, D, I, J feel
      weaker on a v3). Deliberately not done.

## Docs

- [ ] **SPEC.md has drifted**: it describes the 45-waveform set, a
      pre-64-bit-mask settings format, a power step range of 1–20 (the code is
      1–10) and a v2-only world, and predates the per-mode options pages,
      auto-advance, shuffle and the channel-change buzz. Fix an area when you
      touch it; AGENTS.md lists the known differences.
