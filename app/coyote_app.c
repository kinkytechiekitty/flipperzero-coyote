// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#include "coyote_app.h"

#include <string.h>

static inline uint64_t coyote_mask_for_index(size_t index) {
    return index < 64u ? (1ULL << index) : 0ULL;
}

void coyote_settings_default(CoyoteSettings* settings) {
    if(settings == NULL) {
        return;
    }

    memset(settings, 0, sizeof(*settings));
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel)
        settings->caps[channel] = COYOTE_DEFAULT_CAP;
    settings->power_step = COYOTE_DEFAULT_STEP;
    settings->vibration_on = true;
    settings->channel_buzz_on = false; // deliberately off: it fires on every switch
    for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
        settings->auto_advance_s[mode] = 0u; // off
        settings->shuffle_on[mode] = true; // only takes effect once a dwell is set
    }
    settings->normal_waveforms_mask = COYOTE_ALL_WAVEFORMS_MASK;
    settings->fire_waveforms_mask = COYOTE_FIRE_DEFAULT_MASK;
}

bool coyote_settings_validate(const CoyoteSettings* settings) {
    if(settings == NULL) {
        return false;
    }

    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(settings->caps[channel] < COYOTE_MIN_CAP ||
           settings->caps[channel] > COYOTE_MAX_CAP)
            return false;
    }

    if(settings->power_step < COYOTE_MIN_STEP || settings->power_step > COYOTE_MAX_STEP) {
        return false;
    }

    for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
        const uint8_t dwell = settings->auto_advance_s[mode];
        if(dwell != 0u && dwell != 5u && dwell != 10u && dwell != 30u && dwell != 60u) {
            return false;
        }
    }

    if((settings->normal_waveforms_mask & ~COYOTE_ALL_WAVEFORMS_MASK) != 0ULL ||
       (settings->fire_waveforms_mask & ~COYOTE_ALL_WAVEFORMS_MASK) != 0ULL) {
        return false;
    }

    return true;
}

bool coyote_waveform_enabled(const CoyoteSettings* settings, bool fire_mode, size_t index) {
    if(settings == NULL || index >= COYOTE_WAVEFORM_COUNT) {
        return false;
    }

    const uint64_t mask = coyote_mask_for_index(index);
    const uint64_t waveforms = fire_mode ? settings->fire_waveforms_mask : settings->normal_waveforms_mask;
    return (waveforms & mask) != 0ULL;
}

void coyote_waveform_set_enabled(CoyoteSettings* settings, bool fire_mode, size_t index, bool enabled) {
    if(settings == NULL || index >= COYOTE_WAVEFORM_COUNT) {
        return;
    }

    uint64_t* target = fire_mode ? &settings->fire_waveforms_mask : &settings->normal_waveforms_mask;
    const uint64_t mask = coyote_mask_for_index(index);
    if(enabled) {
        *target |= mask;
    } else {
        *target &= ~mask;
    }
}

void coyote_waveform_set_all(CoyoteSettings* settings, bool fire_mode, bool enabled) {
    if(settings == NULL) {
        return;
    }

    uint64_t* target = fire_mode ? &settings->fire_waveforms_mask : &settings->normal_waveforms_mask;
    *target = enabled ? COYOTE_ALL_WAVEFORMS_MASK : 0ULL;
}

size_t coyote_waveform_count_enabled(const CoyoteSettings* settings, bool fire_mode) {
    if(settings == NULL) {
        return 0u;
    }

    const uint64_t waveforms =
        (fire_mode ? settings->fire_waveforms_mask : settings->normal_waveforms_mask) &
        COYOTE_ALL_WAVEFORMS_MASK;
    size_t count = 0;
    for(size_t i = 0; i < COYOTE_WAVEFORM_COUNT; ++i) {
        if(waveforms & coyote_mask_for_index(i)) count++;
    }
    return count;
}

void coyote_runtime_reset(CoyoteRuntime* runtime) {
    if(runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->selection = COYOTE_CHANNEL_A;
}

static inline uint16_t coyote_clamp_level(uint16_t value, uint16_t cap) {
    if(value > cap) {
        return cap;
    }
    return value;
}

void coyote_set_channel_levels(
    CoyoteRuntime* runtime,
    size_t device,
    CoyoteChannel selection,
    uint16_t level_a,
    uint16_t level_b,
    const CoyoteSettings* settings) {
    if(runtime == NULL || settings == NULL || device >= COYOTE_DEVICE_COUNT) {
        return;
    }

    const uint16_t cap_a = settings->caps[0];
    const uint16_t cap_b = settings->caps[1];
    const uint16_t clamped_a = coyote_clamp_level(level_a, cap_a);
    const uint16_t clamped_b = coyote_clamp_level(level_b, cap_b);

    switch(selection) {
    case COYOTE_CHANNEL_A:
        runtime->levels[device][0] = clamped_a;
        runtime->levels[device][1] =
            coyote_clamp_level(runtime->levels[device][1], cap_b);
        break;
    case COYOTE_CHANNEL_B:
        runtime->levels[device][0] =
            coyote_clamp_level(runtime->levels[device][0], cap_a);
        runtime->levels[device][1] = clamped_b;
        break;
    case COYOTE_CHANNEL_BOTH:
        runtime->levels[device][0] = clamped_a;
        runtime->levels[device][1] = clamped_b;
        break;
    default:
        break;
    }
}

void coyote_step_channels(
    CoyoteRuntime* runtime,
    size_t device,
    CoyoteChannel selection,
    int step,
    const CoyoteSettings* settings) {
    if(runtime == NULL || settings == NULL || device >= COYOTE_DEVICE_COUNT) {
        return;
    }

    const int delta = step * (int)settings->power_step;
    if(selection == COYOTE_CHANNEL_A || selection == COYOTE_CHANNEL_BOTH) {
        int next = (int)runtime->levels[device][0] + delta;
        if(next < 0) {
            next = 0;
        }
        if(next > (int)settings->caps[0]) {
            next = settings->caps[0];
        }
        runtime->levels[device][0] = (uint16_t)next;
    }
    if(selection == COYOTE_CHANNEL_B || selection == COYOTE_CHANNEL_BOTH) {
        int next = (int)runtime->levels[device][1] + delta;
        if(next < 0) {
            next = 0;
        }
        if(next > (int)settings->caps[1]) {
            next = settings->caps[1];
        }
        runtime->levels[device][1] = (uint16_t)next;
    }
}

void coyote_set_channels_off(CoyoteRuntime* runtime) {
    if(runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->selection = COYOTE_CHANNEL_A;
}

void coyote_power_packet(const uint16_t levels[COYOTE_CHANNEL_COUNT], uint8_t out[3]) {
    if(out == NULL || levels == NULL) {
        return;
    }

    coyote_encode_power(levels[0], levels[1], out);
}
