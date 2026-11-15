# TODO

Things that are specified or known to be missing, in rough priority order.

## Device selection / multi-device support

- [ ] **Device picker / choosing between several Coyotes.** Today the app takes
      the strongest signal among `D-LAB…` and `47L121000`, which is wrong
      anywhere someone else's Coyote may be nearer. SPEC.md "Choosing a Coyote"
      is the design.
- [ ] **"Forget Coyote"** in Settings, once there is something to remember.
- [ ] Blue++ keeps a device in its scan table for 60 s after it was last seen,
      so a unit that was just switched off can still be chosen (and fail to
      connect) for up to a minute. Prefer recently seen candidates.

## Docs

- [ ] **SPEC.md has drifted**: it describes the 45-waveform set, a
      pre-64-bit-mask settings format, a power step range of 1–20 (the code is
      1–10) and a v2-only world, and predates the per-mode options pages,
      auto-advance, shuffle and the channel-change buzz. Fix an area when you
      touch it; AGENTS.md lists the known differences.
