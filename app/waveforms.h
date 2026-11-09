// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#pragma once

#if defined(__has_include)
#if __has_include(<furi.h>)
#include <furi.h>
#endif
#endif

#include <stddef.h>
#include <stdint.h>

#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#endif

// One Coyote v2 pattern frame, played for one 100 ms tick:
// x = pulses (0-31), y = gap (0-1023), z = pulse width (0-31, > 20 stings more).
typedef struct {
    uint8_t x;
    uint16_t y;
    uint8_t z;
} CoyoteFrame;

typedef struct {
    const char* name;
    const CoyoteFrame* frames;
    size_t count;
} CoyoteWaveform;

#include "waveforms_generated.h" // defines COYOTE_WAVEFORM_COUNT

// Masks are 64-bit: a --with-dglab build has 45 waveforms, which do not fit in
// 32 bits, and (1u << 45) is undefined behaviour rather than merely truncating.
// Fire-mode defaults: Stutter, Low buzz, Fast taps, Hit / pulse (see SPEC.md).
// The DG-Lab presets default to off in Fire mode until judged by feel.
#define COYOTE_FIRE_DEFAULT_MASK \
    ((1ULL << 4) | (1ULL << 13) | (1ULL << 15) | (1ULL << 18))
#define COYOTE_ALL_WAVEFORMS_MASK ((1ULL << COYOTE_WAVEFORM_COUNT) - 1ULL)

extern const CoyoteWaveform coyote_waveforms[COYOTE_WAVEFORM_COUNT];
