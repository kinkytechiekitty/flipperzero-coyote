// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#include <assert.h>
#include <stdint.h>

#include "coyote_app.h"

int main(void) {
    CoyoteSettings settings;
    CoyoteRuntime runtime;

    coyote_settings_default(&settings);
    assert(coyote_settings_validate(&settings));
    assert(settings.caps[0] == 85 && settings.caps[1] == 85);
    assert(settings.power_step == 5 && settings.vibration_on);
    assert(settings.normal_waveforms_mask == COYOTE_ALL_WAVEFORMS_MASK);
    assert(settings.fire_waveforms_mask == COYOTE_FIRE_DEFAULT_MASK);

    // An empty waveform set is now legal: the mode screen shows a disabled
    // symbol and outputs nothing, rather than one waveform being forced on.
    assert(coyote_waveform_count_enabled(&settings, false) == COYOTE_WAVEFORM_COUNT);
    coyote_waveform_set_enabled(&settings, false, 0, false);
    assert(!coyote_waveform_enabled(&settings, false, 0));
    assert(coyote_waveform_count_enabled(&settings, false) == COYOTE_WAVEFORM_COUNT - 1);
    for(size_t i = 1; i < COYOTE_WAVEFORM_COUNT; ++i)
        coyote_waveform_set_enabled(&settings, false, i, false);
    assert(coyote_waveform_count_enabled(&settings, false) == 0);
    assert(!coyote_waveform_enabled(&settings, false, COYOTE_WAVEFORM_COUNT - 1));
    assert(coyote_settings_validate(&settings));

    coyote_waveform_set_all(&settings, false, true);
    assert(coyote_waveform_count_enabled(&settings, false) == COYOTE_WAVEFORM_COUNT);
    coyote_waveform_set_all(&settings, false, false);
    assert(coyote_waveform_count_enabled(&settings, false) == 0);
    // The other mode is untouched by either call.
    assert(coyote_waveform_count_enabled(&settings, true) == 4);
    coyote_waveform_set_all(&settings, false, true);

    for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
        assert(settings.auto_advance_s[mode] == 0);
        assert(settings.shuffle_on[mode]);
    }

    coyote_runtime_reset(&runtime);
    assert(runtime.selection == COYOTE_CHANNEL_A);
    coyote_set_channel_levels(&runtime, 0, COYOTE_CHANNEL_A, 300, 80, &settings);
    assert(runtime.levels[0][0] == 85 && runtime.levels[0][1] == 0);
    coyote_set_channel_levels(&runtime, 0, COYOTE_CHANNEL_B, 10, 300, &settings);
    assert(runtime.levels[0][0] == 85 && runtime.levels[0][1] == 85);

    settings.caps[0] = 15;
    settings.caps[1] = 20;
    settings.power_step = 5;
    coyote_step_channels(&runtime, 0, COYOTE_CHANNEL_BOTH, 1, &settings);
    assert(runtime.levels[0][0] == 15 && runtime.levels[0][1] == 20);
    coyote_step_channels(&runtime, 0, COYOTE_CHANNEL_BOTH, -1, &settings);
    assert(runtime.levels[0][0] == 10 && runtime.levels[0][1] == 15);
    coyote_step_channels(&runtime, 0, COYOTE_CHANNEL_A, -3, &settings);
    assert(runtime.levels[0][0] == 0 && runtime.levels[0][1] == 15);
    // Caps are shared now, not per device, so the second device clamps to the
    // same 15/20 rather than keeping limits of its own.
    coyote_set_channel_levels(&runtime, 1, COYOTE_CHANNEL_BOTH, 40, 45, &settings);
    assert(runtime.levels[1][0] == 15 && runtime.levels[1][1] == 20);
    assert(runtime.levels[0][0] == 0 && runtime.levels[0][1] == 15);

    const uint16_t power[] = {1, 2};
    uint8_t encoded_power[3];
    coyote_power_packet(power, encoded_power);
    assert(encoded_power[0] == 14 && encoded_power[1] == 56 && encoded_power[2] == 0);

    coyote_set_channels_off(&runtime);
    for(size_t device = 0; device < COYOTE_DEVICE_COUNT; ++device) {
        assert(runtime.levels[device][0] == 0 && runtime.levels[device][1] == 0);
        assert(!runtime.muted[device][0] && !runtime.muted[device][1]);
    }

    return 0;
}
