// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#pragma once
#include <stdint.h>

// Coyote v3 wire encoding, from DG-Lab's published v3 BLE document and checked
// against the official app. Raw encoders: nothing here clamps except the
// frequency conversion, so the callers in coyote_app.c own the ranges.
//
// Service 0x180C: 0x150A (write without response, <= 20 bytes), 0x150B (notify).
// Service 0x180A: 0x1500 battery, one byte 0-100.
//
// The device discards a channel's four slices if ANY of its frequency bytes is
// outside 10-240 or any intensity byte is above 100. Never emit one on purpose.

#define COYOTE3_STRENGTH_MAX 200
#define COYOTE3_B0_LEN 20
#define COYOTE3_BF_LEN 7

// Strength interpretation, two bits per channel in B0 byte 1.
#define COYOTE3_MODE_NO_CHANGE 0
#define COYOTE3_MODE_INCREASE 1
#define COYOTE3_MODE_DECREASE 2
#define COYOTE3_MODE_ABSOLUTE 3

// Frequency byte (10-240) from a pulse period in ms, clamped to 10-1000 first.
static inline uint8_t coyote3_freq_from_period(uint16_t period_ms) {
    if(period_ms < 10u) period_ms = 10u;
    if(period_ms > 1000u) period_ms = 1000u;
    if(period_ms <= 100u) return (uint8_t)period_ms;
    if(period_ms <= 600u) return (uint8_t)((period_ms - 100u) / 5u + 100u);
    return (uint8_t)((period_ms - 600u) / 10u + 200u);
}

// B0, 20 bytes, one per 100 ms; each of the four slices is 25 ms.
// [0]=0xB0 [1]=(seq<<4)|(mode_a<<2)|mode_b [2]=strength A [3]=strength B
// [4..7]=A frequency [8..11]=A intensity [12..15]=B frequency [16..19]=B intensity
static inline void coyote3_encode_b0(
    uint8_t seq,
    uint8_t mode_a,
    uint8_t mode_b,
    uint8_t strength_a,
    uint8_t strength_b,
    const uint8_t freq_a[4],
    const uint8_t int_a[4],
    const uint8_t freq_b[4],
    const uint8_t int_b[4],
    uint8_t out[COYOTE3_B0_LEN]) {
    out[0] = 0xB0;
    out[1] = (uint8_t)(((seq & 0x0Fu) << 4) | ((mode_a & 0x03u) << 2) | (mode_b & 0x03u));
    out[2] = strength_a;
    out[3] = strength_b;
    for(uint8_t i = 0; i < 4u; ++i) {
        out[4u + i] = freq_a[i];
        out[8u + i] = int_a[i];
        out[12u + i] = freq_b[i];
        out[16u + i] = int_b[i];
    }
}

// BF, 7 bytes, no response: soft limits (0-200) and balance parameters (0-255).
// The limits persist on the device across power cycles.
static inline void coyote3_encode_bf(
    uint8_t limit_a,
    uint8_t limit_b,
    uint8_t fbal_a,
    uint8_t fbal_b,
    uint8_t sbal_a,
    uint8_t sbal_b,
    uint8_t out[COYOTE3_BF_LEN]) {
    out[0] = 0xBF;
    out[1] = limit_a;
    out[2] = limit_b;
    out[3] = fbal_a;
    out[4] = fbal_b;
    out[5] = sbal_a;
    out[6] = sbal_b;
}
