// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coyote_proto.h"
#include "coyote3_proto.h"
#include "waveforms.h"

#define COYOTE_CHANNEL_COUNT 2u
#define COYOTE_DEVICE_COUNT 2u
#define COYOTE_MIN_CAP 5u
#define COYOTE_MAX_CAP 292u
#define COYOTE_DEFAULT_CAP 85u
#define COYOTE_MIN_STEP 1u
#define COYOTE_MAX_STEP 10u
#define COYOTE_DEFAULT_STEP 5u
// Caps adjust by a fixed amount, not by power_step: tying the two together
// meant 207 presses to walk a cap from 85 to 292 with a step of 1.
#define COYOTE_CAP_STEP 5u
#define COYOTE_MODE_COUNT 2u // [0] = normal, [1] = fire

typedef enum {
    COYOTE_CHANNEL_A = 0,
    COYOTE_CHANNEL_B = 1,
    COYOTE_CHANNEL_BOTH = 2,
    // A fourth stop in the left/right cycle rather than a channel: it selects
    // nothing, so every "is this channel selected" test below excludes it, and
    // OK on it opens the mode's options instead of muting or firing.
    COYOTE_CHANNEL_OPTIONS = 3,
} CoyoteChannel;
#define COYOTE_SELECTION_COUNT 4u

// Which wire protocol the connected unit speaks. Decided by GATT discovery,
// never by the advertised name.
typedef enum { COYOTE_KIND_V2 = 0, COYOTE_KIND_V3 = 1 } CoyoteDeviceKind;

typedef struct {
    // One set of caps, not one per device: pad placement does not survive
    // between sessions, so a limit remembered against a BLE address is
    // remembering the wrong thing. See SPEC.md "Two Coyotes later".
    uint16_t caps[COYOTE_CHANNEL_COUNT];
    uint8_t power_step;
    bool vibration_on;
    bool channel_buzz_on; // the buzz when switching channel; off by default
    uint64_t normal_waveforms_mask;
    uint64_t fire_waveforms_mask;
    // Per mode. Dwell is in seconds, 0 = off; the switch itself waits for the
    // current waveform to finish its cycle. Shuffle picks at random from the
    // enabled set instead of advancing in order, and does nothing when the
    // dwell is off.
    uint8_t auto_advance_s[COYOTE_MODE_COUNT];
    bool shuffle_on[COYOTE_MODE_COUNT];
} CoyoteSettings;

typedef struct {
    uint16_t levels[COYOTE_DEVICE_COUNT][COYOTE_CHANNEL_COUNT];
    uint16_t mute_levels[COYOTE_DEVICE_COUNT][COYOTE_CHANNEL_COUNT];
    bool muted[COYOTE_DEVICE_COUNT][COYOTE_CHANNEL_COUNT];
    CoyoteChannel selection;
} CoyoteRuntime;

void coyote_settings_default(CoyoteSettings* settings);
bool coyote_settings_validate(const CoyoteSettings* settings);

bool coyote_waveform_enabled(const CoyoteSettings* settings, bool fire_mode, size_t index);
void coyote_waveform_set_enabled(CoyoteSettings* settings, bool fire_mode, size_t index, bool enabled);
// Enable or disable every waveform in the mode.
void coyote_waveform_set_all(CoyoteSettings* settings, bool fire_mode, bool enabled);
// Zero is a legal state: nothing plays and the mode screen says so.
size_t coyote_waveform_count_enabled(const CoyoteSettings* settings, bool fire_mode);

void coyote_runtime_reset(CoyoteRuntime* runtime);
void coyote_set_channel_levels(CoyoteRuntime* runtime, size_t device, CoyoteChannel selection, uint16_t level_a, uint16_t level_b, const CoyoteSettings* settings);
void coyote_step_channels(CoyoteRuntime* runtime, size_t device, CoyoteChannel selection, int step, const CoyoteSettings* settings);
void coyote_set_channels_off(CoyoteRuntime* runtime);
void coyote_power_packet(const uint16_t levels[COYOTE_CHANNEL_COUNT], uint8_t out[3]);

// Coyote v3. Levels are 1:1 with the v3's strength (the number the official
// app shows), so the only conversion is the ceiling of 200.
uint8_t coyote3_strength(uint16_t level);
// One v2 frame as a v3 frequency/intensity pair, by the official app's rule:
// period = x + y ms; intensity = z * 5 capped at 100, and 0 when the frame has
// no pulses (x == 0) or no width (z == 0). Always yields a legal pair.
void coyote3_convert_frame(const CoyoteFrame* frame, uint8_t* freq, uint8_t* intensity);
// The 100 ms B0 packet. Always absolute strength for both channels, sequence 0;
// each frame fills all four 25 ms slices. A channel at strength 0, or with a
// NULL frame, carries zero intensity on a legal frequency.
void coyote3_output_packet(
    uint16_t level_a,
    uint16_t level_b,
    const CoyoteFrame* frame_a,
    const CoyoteFrame* frame_b,
    uint8_t out[COYOTE3_B0_LEN]);
// BF: soft limits = the caps (ceiling 200), balances at the official 160/160/0/0.
void coyote3_limits_packet(const CoyoteSettings* settings, uint8_t out[COYOTE3_BF_LEN]);
