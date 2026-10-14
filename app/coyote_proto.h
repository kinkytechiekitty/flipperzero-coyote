// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// Coyote v2 wire encoding, ported from Feeder's server/lib/coyote.js (itself
// verbatim from rezreal/coyote). Fiddly: don't "clean up" without a device to
// test against.

// Feeder power levels are 1/7 of the device's 11-bit raw power.
#define COYOTE_POWER_SCALE 7
#define COYOTE_POWER_MAX 292 // 2047 / 7

// 3 bytes: zero(2) | powerA(11) | powerB(11), then first and third byte swapped.
static inline void coyote_encode_power(uint16_t level_a, uint16_t level_b, uint8_t out[3]) {
    uint16_t a = level_a * COYOTE_POWER_SCALE;
    uint16_t b = level_b * COYOTE_POWER_SCALE;
    uint8_t b0 = (a >> 5) & 0x3F;
    uint8_t b1 = ((a & 0x1F) << 3) | ((b & 0x7FF) >> 8);
    uint8_t b2 = b & 0xFF;
    out[0] = b2;
    out[1] = b1;
    out[2] = b0;
}

// 3 bytes: zero(4) | z(5) | y(10) | x(5), then first and third byte swapped.
static inline void coyote_encode_pattern(uint8_t x, uint16_t y, uint8_t z, uint8_t out[3]) {
    uint8_t b0 = (z & 0x1E) >> 1;
    uint16_t v = ((uint16_t)(z & 0x01) << 15) | ((y & 0x3FF) << 5) | (x & 0x1F);
    uint8_t b1 = v >> 8;
    uint8_t b2 = v & 0xFF;
    out[0] = b2;
    out[1] = b1;
    out[2] = b0;
}
