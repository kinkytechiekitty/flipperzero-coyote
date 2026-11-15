// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "coyote_app.h"

#if COYOTE_WAVEFORM_COUNT == 21
static uint32_t fnv1a(uint32_t hash, const uint8_t* bytes, size_t count) {
    for(size_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

// Pins the v2 wire output: every pattern frame of the committed table and every
// power level on each channel. Taken before v3 support went in; if this moves,
// v2 output changed.
static void test_v2_pin(void) {
    uint32_t hash = 2166136261u;
    uint8_t out[3];
    for(size_t w = 0; w < COYOTE_WAVEFORM_COUNT; ++w) {
        for(size_t f = 0; f < coyote_waveforms[w].count; ++f) {
            const CoyoteFrame* frame = &coyote_waveforms[w].frames[f];
            coyote_encode_pattern(frame->x, frame->y, frame->z, out);
            hash = fnv1a(hash, out, sizeof(out));
        }
    }
    for(uint16_t level = 0; level <= COYOTE_POWER_MAX; ++level) {
        coyote_encode_power(level, 0, out);
        hash = fnv1a(hash, out, sizeof(out));
    }
    for(uint16_t level = 0; level <= COYOTE_POWER_MAX; ++level) {
        coyote_encode_power(0, level, out);
        hash = fnv1a(hash, out, sizeof(out));
    }
    assert(hash == 0x05905A97u);
}
#endif

static uint8_t hex_nibble(char c) {
    return (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
}

// One of DG-Lab's published B0 examples, byte for byte.
static void check_b0(
    const char* hex,
    uint8_t seq,
    uint8_t mode_a,
    uint8_t mode_b,
    uint8_t strength_a,
    uint8_t strength_b,
    const uint8_t freq_a[4],
    const uint8_t int_a[4],
    const uint8_t freq_b[4],
    const uint8_t int_b[4]) {
    uint8_t expected[COYOTE3_B0_LEN], out[COYOTE3_B0_LEN];
    assert(strlen(hex) == 2u * COYOTE3_B0_LEN);
    for(size_t i = 0; i < COYOTE3_B0_LEN; ++i)
        expected[i] = (uint8_t)((hex_nibble(hex[2 * i]) << 4) | hex_nibble(hex[2 * i + 1]));
    coyote3_encode_b0(
        seq, mode_a, mode_b, strength_a, strength_b, freq_a, int_a, freq_b, int_b, out);
    assert(memcmp(expected, out, sizeof(out)) == 0);
}

static void test_v3(void) {
    static const uint8_t f10[4] = {10, 10, 10, 10};
    static const uint8_t f15[4] = {15, 15, 15, 15};
    static const uint8_t f30[4] = {30, 30, 30, 30};
    static const uint8_t fmix[4] = {40, 60, 80, 100};
    static const uint8_t i1[4] = {0, 10, 20, 30};
    static const uint8_t i2[4] = {40, 50, 60, 70};
    static const uint8_t i3[4] = {80, 90, 100, 100};
    static const uint8_t i4[4] = {100, 90, 90, 90};
    static const uint8_t zero[4] = {0, 0, 0, 0};
    static const uint8_t bad[4] = {0, 0, 0, 101}; // the doc's "channel B ignored"
    static const uint8_t i10[4] = {10, 10, 10, 10};

    check_b0("B00000000A0A0A0A000A141E0000000000000065", 0, 0, 0, 0, 0, f10, i1, zero, bad);
    check_b0("B00000000F0F0F0F28323C460000000000000065", 0, 0, 0, 0, 0, f15, i2, zero, bad);
    check_b0("B00000001E1E1E1E505A64640000000000000065", 0, 0, 0, 0, 0, f30, i3, zero, bad);
    check_b0("B0000000283C5064645A5A5A0000000000000065", 0, 0, 0, 0, 0, fmix, i4, zero, bad);
    check_b0("B00405000A0A0A0A000A141E0000000000000065", 0, 1, 0, 5, 0, f10, i1, zero, bad);
    check_b0("B0140A00283C5064645A5A5A0000000000000065", 1, 1, 0, 10, 0, fmix, i4, zero, bad);
    check_b0("B00000000A0A0A0A000A141E0A0A0A0A00000000", 0, 0, 0, 0, 0, f10, i1, f10, zero);
    check_b0("B00000000F0F0F0F28323C460A0A0A0A0A0A0A0A", 0, 0, 0, 0, 0, f15, i2, f10, i10);

    assert(coyote3_freq_from_period(10) == 10);
    assert(coyote3_freq_from_period(100) == 100);
    assert(coyote3_freq_from_period(110) == 102);
    assert(coyote3_freq_from_period(600) == 200);
    assert(coyote3_freq_from_period(1000) == 240);
    assert(coyote3_freq_from_period(6) == 10);
    assert(coyote3_freq_from_period(1054) == 240);
    for(uint32_t period = 0; period <= 0xFFFFu; ++period) {
        const uint8_t freq = coyote3_freq_from_period((uint16_t)period);
        assert(freq >= 10 && freq <= 240);
    }

    uint8_t freq = 0, intensity = 0;
    CoyoteFrame frame = {1, 9, 20};
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(freq == 10 && intensity == 100);
    frame.z = 31;
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(intensity == 100);
    frame.z = 3;
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(intensity == 15);
    frame.x = 0;
    frame.z = 20;
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(intensity == 0 && freq == 10);
    frame.x = 1;
    frame.z = 0;
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(intensity == 0);
    frame = (CoyoteFrame){31, 1023, 31};
    coyote3_convert_frame(&frame, &freq, &intensity);
    assert(freq == 240 && intensity == 100);

    assert(coyote3_strength(0) == 0 && coyote3_strength(85) == 85);
    assert(coyote3_strength(200) == 200 && coyote3_strength(201) == 200);
    assert(coyote3_strength(292) == 200 && coyote3_strength(0xFFFF) == 200);

    CoyoteSettings settings;
    uint8_t bf[COYOTE3_BF_LEN];
    static const uint8_t bf_default[COYOTE3_BF_LEN] = {0xBF, 0x55, 0x55, 0xA0, 0xA0, 0x00, 0x00};
    coyote_settings_default(&settings);
    coyote3_limits_packet(&settings, bf);
    assert(memcmp(bf, bf_default, sizeof(bf)) == 0);
    settings.caps[0] = 292;
    settings.caps[1] = 5;
    coyote3_limits_packet(&settings, bf);
    assert(bf[1] == 0xC8 && bf[2] == 5);

    // The app's own packet: always absolute, never above 200, and a channel at
    // zero carries no intensity whatever frame it is on.
    static const uint16_t levels[] = {0, 1, 5, 85, 199, 200, 201, 292, 0xFFFF};
    const CoyoteFrame loud = {1, 9, 20};
    uint8_t b0[COYOTE3_B0_LEN];
    for(size_t a = 0; a < COUNT_OF(levels); ++a) {
        for(size_t b = 0; b < COUNT_OF(levels); ++b) {
            coyote3_output_packet(levels[a], levels[b], &loud, &loud, b0);
            assert(b0[0] == 0xB0 && b0[1] == 0x0F);
            assert(b0[2] <= 200 && b0[3] <= 200);
            assert(b0[2] == (levels[a] > 200 ? 200 : levels[a]));
            assert(b0[3] == (levels[b] > 200 ? 200 : levels[b]));
            for(size_t slice = 0; slice < 4; ++slice) {
                assert(b0[4 + slice] == 10 && b0[12 + slice] == 10);
                assert(b0[8 + slice] == (levels[a] ? 100 : 0));
                assert(b0[16 + slice] == (levels[b] ? 100 : 0));
            }
        }
    }
    // The zero packet, as sent on connect, on Back and at exit.
    static const uint8_t off[COYOTE3_B0_LEN] = {0xB0, 0x0F, 0,  0,  10, 10, 10, 10, 0, 0,
                                                0,    0,    10, 10, 10, 10, 0,  0,  0, 0};
    coyote3_output_packet(0, 0, NULL, NULL, b0);
    assert(memcmp(b0, off, sizeof(b0)) == 0);
    coyote3_output_packet(0, 0, &loud, &loud, b0);
    assert(memcmp(b0, off, sizeof(b0)) == 0);

    // Nothing in the table can produce a value the device would discard.
    for(size_t w = 0; w < COYOTE_WAVEFORM_COUNT; ++w) {
        for(size_t f = 0; f < coyote_waveforms[w].count; ++f) {
            coyote3_convert_frame(&coyote_waveforms[w].frames[f], &freq, &intensity);
            assert(freq >= 10 && freq <= 240);
            assert(intensity <= 100);
            coyote3_output_packet(200, 200, &coyote_waveforms[w].frames[f], NULL, b0);
            for(size_t slice = 0; slice < 4; ++slice) {
                assert(b0[4 + slice] == freq && b0[8 + slice] == intensity);
                assert(b0[12 + slice] == 10 && b0[16 + slice] == 0);
            }
        }
    }
}

int main(void) {
    CoyoteSettings settings;
    CoyoteRuntime runtime;

#if COYOTE_WAVEFORM_COUNT == 21
    test_v2_pin();
#endif
    test_v3();

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
