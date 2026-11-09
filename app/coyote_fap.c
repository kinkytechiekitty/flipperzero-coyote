// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KinkyTechieKitty
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_light.h>
#include <furi_hal_vibro.h>
#include <gui/canvas.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <storage/storage.h>
#include <ble/core/ble_defs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_central.h"
#include "coyote_app.h"

#define TAG "Coyote"
#define VIEW_MAIN 0u
// A --with-dglab build stores its settings separately: its waveform masks set
// bits the 21-waveform build rejects, so one shared file would reset whichever
// build opened it second. See README "Adding the DG-Lab presets yourself".
#ifdef COYOTE_WITH_DGLAB
#define SETTINGS_PATH EXT_PATH("apps_data/coyote/settings-dg.bin")
#else
#define SETTINGS_PATH EXT_PATH("apps_data/coyote/settings.bin")
#endif
// v6: magic(4) ver(1) step(1) vibro(1) buzz(1) normal(8) fire(8) caps(4)
//     auto_advance(2) shuffle(2) = 32. The per-BLE-address device records of
//     v3-v5 are gone; v5 files are migrated by taking slot 0's caps.
#define SETTINGS_SIZE 32u
#define SETTINGS_SIZE_V5 50u
#define WORKER_PERIOD_MS 20u
#define OUTPUT_PERIOD_MS 100u

typedef enum {
    PageMain,
    PageNormal,
    PageFire,
    PageSettings,
    PageNormalWaveforms,
    PageFireWaveforms,
    PageModeOptions,
    PageChannels,
    PageDevice,
} CoyotePage;
#define MODE_OPTION_ROWS 3u

// Every menu page uses the same row geometry: 12px apart, highlight box from
// baseline-9 for 12px. Text occupies baseline-7..baseline+2, so that leaves 2px
// of padding above the glyphs instead of the box starting flush with them.
#define MENU_SPACING 12u
#define MENU_BOX_TOP 9u
#define MENU_BOX_HEIGHT 12u

typedef struct {
    CoyoteSettings settings;
    CoyoteRuntime runtime;
    CoyotePage page;
    CoyotePage waveform_parent; // PageSettings or PageModeOptions
    CoyotePage options_parent; // PageNormal or PageFire: whose options these are
    CoyotePage options_return; // where Back goes: the mode, or back to Settings
    uint8_t option_selection;
    uint32_t dwell_ms[COYOTE_MODE_COUNT][COYOTE_CHANNEL_COUNT];
    bool advance_pending[COYOTE_MODE_COUNT][COYOTE_CHANNEL_COUNT];
    // Always 0: the runtime arrays keep a device dimension as the only
    // remaining multi-Coyote scaffolding, but nothing sets this yet.
    uint8_t active_device_index;
    uint8_t menu_selection;
    uint8_t wave_selection;
    uint8_t wave_scroll;
    size_t waveform[2][COYOTE_CHANNEL_COUNT];
    size_t frame[2][COYOTE_CHANNEL_COUNT];
    bool pattern_restart;
    bool fire_press_pending;
    bool fire_minimum_tick;
    bool back_mode_returned;
    bool unmute_pending[COYOTE_CHANNEL_COUNT];
    uint16_t unmute_elapsed_ms[COYOTE_CHANNEL_COUNT];
    int8_t held_power_direction;
    uint16_t ramp_elapsed_ms;
    bool fire_pressed;
    bool connected;
    uint8_t battery;
    bool battery_valid;
    char device_name[BC_NAME_MAX + 1];
    char status[48];
    uint32_t last_waveform_change_ms;
    uint8_t buzz_pulses;
    uint16_t buzz_phase_ms;
    bool settings_warning;
} CoyoteModel;

typedef struct {
    ViewDispatcher* dispatcher;
    View* view;
    Storage* storage;
    BleCentral* ble;
    FuriThread* worker;
    volatile bool stopping;
    volatile uint16_t battery_handle;
    uint8_t last_power[3];
    bool last_power_valid;
    bool was_advertising;
    bool last_fire_held; // worker-only; drives the redraw-on-change below
} CoyoteApp;

static CoyoteModel* model_lock(CoyoteApp* app) {
    return view_get_model(app->view);
}

static void model_unlock(CoyoteApp* app, bool update) {
    view_commit_model(app->view, update);
}

static uint64_t read_u64_le(const uint8_t* p) {
    uint64_t v = 0;
    for(size_t i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static void write_u64_le(uint8_t* p, uint64_t value) {
    for(size_t i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void write_u16_le(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void load_settings(CoyoteApp* app, CoyoteModel* model) {
    coyote_settings_default(&model->settings);
    storage_simply_mkdir(app->storage, EXT_PATH("apps_data/coyote"));
    if(!storage_file_exists(app->storage, SETTINGS_PATH)) return;

    File* file = storage_file_alloc(app->storage);
    uint8_t bytes[SETTINGS_SIZE_V5] = {0};
    bool ok = storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t size = 0;
    if(ok) {
        size = (size_t)storage_file_size(file);
        ok = (size == SETTINGS_SIZE || size == SETTINGS_SIZE_V5) &&
             (size_t)storage_file_read(file, bytes, size) == size;
    }
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);

    const bool v5 = ok && size == SETTINGS_SIZE_V5 && bytes[4] == 5u;
    const bool v6 = ok && size == SETTINGS_SIZE && bytes[4] == 6u;
    if((v5 || v6) && memcmp(bytes, "CYT2", 4) == 0 && bytes[6] <= 1u && bytes[7] <= 1u) {
        CoyoteSettings loaded;
        coyote_settings_default(&loaded);
        loaded.power_step = bytes[5];
        loaded.vibration_on = bytes[6] != 0u;
        loaded.normal_waveforms_mask = read_u64_le(&bytes[8]);
        loaded.fire_waveforms_mask = read_u64_le(&bytes[16]);
        if(v6) {
            loaded.channel_buzz_on = bytes[7] != 0u;
            loaded.caps[0] = read_u16_le(&bytes[24]);
            loaded.caps[1] = read_u16_le(&bytes[26]);
            for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
                loaded.auto_advance_s[mode] = bytes[28 + mode];
                loaded.shuffle_on[mode] = bytes[30 + mode] != 0u;
            }
        } else {
            // v5 layout: vibro[6], masks at 7/15, device records from 23 (so
            // slot 0's caps at 30/32), buzz[45], dwell[46], shuffle[48].
            loaded.vibration_on = bytes[6] != 0u;
            loaded.channel_buzz_on = bytes[45] != 0u;
            loaded.normal_waveforms_mask = read_u64_le(&bytes[7]);
            loaded.fire_waveforms_mask = read_u64_le(&bytes[15]);
            loaded.caps[0] = read_u16_le(&bytes[30]);
            loaded.caps[1] = read_u16_le(&bytes[32]);
            for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
                loaded.auto_advance_s[mode] = bytes[46 + mode];
                loaded.shuffle_on[mode] = bytes[48 + mode] != 0u;
            }
        }
        if(coyote_settings_validate(&loaded)) {
            model->settings = loaded;
            return;
        }
    }

    model->settings_warning = true;
    FURI_LOG_E(TAG, "Invalid settings file; using defaults");
}

static bool save_settings(CoyoteApp* app, const CoyoteSettings* settings) {
    if(!coyote_settings_validate(settings)) {
        FURI_LOG_E(TAG, "Refusing to save invalid settings");
        return false;
    }

    storage_simply_mkdir(app->storage, EXT_PATH("apps_data/coyote"));
    uint8_t bytes[SETTINGS_SIZE] = {0};
    memcpy(bytes, "CYT2", 4);
    bytes[4] = 6;
    bytes[5] = settings->power_step;
    bytes[6] = settings->vibration_on ? 1u : 0u;
    bytes[7] = settings->channel_buzz_on ? 1u : 0u;
    write_u64_le(&bytes[8], settings->normal_waveforms_mask);
    write_u64_le(&bytes[16], settings->fire_waveforms_mask);
    write_u16_le(&bytes[24], settings->caps[0]);
    write_u16_le(&bytes[26], settings->caps[1]);
    for(size_t mode = 0; mode < COYOTE_MODE_COUNT; ++mode) {
        bytes[28 + mode] = settings->auto_advance_s[mode];
        bytes[30 + mode] = settings->shuffle_on[mode] ? 1u : 0u;
    }

    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, bytes, sizeof(bytes)) == sizeof(bytes);
    if(storage_file_is_open(file)) {
        if(!storage_file_sync(file)) ok = false;
        storage_file_close(file);
    }
    storage_file_free(file);
    if(!ok) FURI_LOG_E(TAG, "Could not save settings");
    return ok;
}

// Vertical layout uses explicit baselines, never AlignCenter: that only centres
// the ascent (it does y += ascent/2), so descenders hang below it and anything
// near the bottom edge gets clipped — which is what pushed the waveform name off
// the screen. With a baseline at y, glyphs occupy y-ascent .. y+descender;
// treating ascent as the font's full height over-reserves slightly and is always
// safe. Metrics from canvas_font_params: Primary h8/desc2, Secondary h7/desc2,
// BigNumbers h15/desc0. The screen is rows 0..63.
#define WAVE_ROWS 5u

// Normal and Fire both have pages hanging off them (options, and the waveform
// list under that). These answer "which mode do we belong to" and "should the
// resting output still be flowing" for all of them, so adjusting a waveform or
// a dwell never interrupts play.
static size_t active_mode(const CoyoteModel* model) {
    switch(model->page) {
    case PageFire:
    case PageFireWaveforms:
        return 1u;
    case PageModeOptions:
        return model->options_parent == PageFire ? 1u : 0u;
    default:
        return 0u;
    }
}

static bool normal_output_live(const CoyoteModel* model) {
    switch(model->page) {
    case PageNormal:
        return true;
    case PageModeOptions:
        return model->options_return == PageNormal;
    case PageNormalWaveforms:
        return model->waveform_parent == PageModeOptions &&
               model->options_return == PageNormal;
    default:
        return false;
    }
}

// The waveform page is a list of virtual rows: up to two action rows ("Enable
// all" / "Disable all", each hidden when it would do nothing) followed by the
// waveforms. Exactly one action row shows when the set is empty or full, two
// otherwise, so the count is always 1 or 2 and the list is never actionless.
static uint8_t wave_action_count(const CoyoteModel* model, bool fire) {
    const size_t enabled = coyote_waveform_count_enabled(&model->settings, fire);
    uint8_t count = 0;
    if(enabled < COYOTE_WAVEFORM_COUNT) count++; // Enable all
    if(enabled > 0u) count++; // Disable all
    return count;
}

static uint8_t wave_row_count(const CoyoteModel* model, bool fire) {
    return (uint8_t)(wave_action_count(model, fire) + COYOTE_WAVEFORM_COUNT);
}

// True when the action row at `row` is "Enable all"; with one row showing it is
// whichever action is possible.
static bool wave_action_is_enable(const CoyoteModel* model, bool fire, uint8_t row) {
    if(wave_action_count(model, fire) == 2u) return row == 0u;
    return coyote_waveform_count_enabled(&model->settings, fire) == 0u;
}

static void wave_clamp_scroll(CoyoteModel* model, bool fire) {
    const uint8_t rows = wave_row_count(model, fire);
    if(model->wave_selection >= rows) model->wave_selection = (uint8_t)(rows - 1u);
    if(model->wave_selection < model->wave_scroll) model->wave_scroll = model->wave_selection;
    if(model->wave_selection >= model->wave_scroll + WAVE_ROWS)
        model->wave_scroll = (uint8_t)(model->wave_selection - (WAVE_ROWS - 1u));
    const uint8_t max_scroll = rows > WAVE_ROWS ? (uint8_t)(rows - WAVE_ROWS) : 0u;
    if(model->wave_scroll > max_scroll) model->wave_scroll = max_scroll;
}

// A tick drawn from two lines: the fonts have no checkmark glyph, and using a
// letter would make the column width depend on the state, shifting the name.
static void draw_check(Canvas* canvas, uint8_t baseline) {
    canvas_draw_line(canvas, 7, (uint8_t)(baseline - 4), 9, (uint8_t)(baseline - 2));
    canvas_draw_line(canvas, 9, (uint8_t)(baseline - 2), 13, (uint8_t)(baseline - 8));
    canvas_draw_line(canvas, 7, (uint8_t)(baseline - 3), 9, (uint8_t)(baseline - 1));
    canvas_draw_line(canvas, 9, (uint8_t)(baseline - 1), 13, (uint8_t)(baseline - 7));
}

static void draw_centered(Canvas* canvas, uint8_t baseline, const char* text) {
    // AlignBottom leaves y untouched, so y is exactly the baseline.
    canvas_draw_str_aligned(canvas, 64, baseline, AlignCenter, AlignBottom, text);
}

static void draw_menu(Canvas* canvas, const CoyoteModel* model) {
    const char* rows[7];
    char advance_text[28], shuffle_text[24], waves_text[28];
    size_t count = 0;
    uint8_t first_baseline = 0;

    switch(model->page) {
    case PageMain:
        rows[0] = "Normal";
        rows[1] = "Fire";
        rows[2] = "Settings";
        count = 3;
        first_baseline = 32;
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 9, model->connected ? model->device_name : "Not connected");
        canvas_set_font(canvas, FontSecondary);
        if(model->settings_warning) {
            // Shown here rather than on the Settings page, whose six rows fill
            // the screen with nowhere left to put it.
            draw_centered(canvas, 20, "Settings were reset");
        } else if(model->connected) {
            char battery[24];
            if(model->battery_valid)
                snprintf(battery, sizeof(battery), "Battery %u%%", model->battery);
            else
                snprintf(battery, sizeof(battery), "Battery --%%");
            draw_centered(canvas, 20, battery);
        } else {
            draw_centered(canvas, 20, model->status);
        }
        break;
    case PageSettings:
        rows[0] = "Normal mode...";
        rows[1] = "Fire mode...";
        rows[2] = "Channels...";
        rows[3] = "Device...";
        count = 4;
        first_baseline = 22;
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 10, "SETTINGS");
        break;
    case PageChannels:
        snprintf(
            waves_text, sizeof(waves_text), "Max Power A: %u",
            model->settings.caps[0]);
        snprintf(
            advance_text, sizeof(advance_text), "Max Power B: %u",
            model->settings.caps[1]);
        snprintf(shuffle_text, sizeof(shuffle_text), "Power step: %u", model->settings.power_step);
        rows[0] = waves_text;
        rows[1] = advance_text;
        rows[2] = shuffle_text;
        count = 3;
        first_baseline = 26;
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 10, "CHANNELS");
        break;
    case PageDevice:
        rows[0] = model->settings.vibration_on ? "Vibration: On" : "Vibration: Off";
        rows[1] = model->settings.channel_buzz_on ? "Channel buzz: On" : "Channel buzz: Off";
        count = 2;
        first_baseline = 26;
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 10, "DEVICE");
        break;
    case PageModeOptions: {
        const bool fire = model->options_parent == PageFire;
        const uint8_t advance = model->settings.auto_advance_s[fire ? 1u : 0u];
        if(advance == 0u)
            snprintf(advance_text, sizeof(advance_text), "Auto-advance: Off");
        else
            snprintf(advance_text, sizeof(advance_text), "Auto-advance: %us", advance);
        snprintf(
            shuffle_text, sizeof(shuffle_text), "Shuffle: %s",
            model->settings.shuffle_on[fire ? 1u : 0u] ? "On" : "Off");
        snprintf(
            waves_text, sizeof(waves_text), "Waveforms: %u/%u",
            (unsigned)coyote_waveform_count_enabled(&model->settings, fire),
            (unsigned)COYOTE_WAVEFORM_COUNT);
        rows[0] = waves_text;
        rows[1] = advance_text;
        rows[2] = shuffle_text;
        count = MODE_OPTION_ROWS;
        first_baseline = 26;
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 10, fire ? "FIRE OPTIONS" : "NORMAL OPTIONS");
        break;
    }
    case PageNormalWaveforms:
    case PageFireWaveforms: {
        const bool fire = model->page == PageFireWaveforms;
        const uint8_t actions = wave_action_count(model, fire);
        const uint8_t rows_total = wave_row_count(model, fire);
        char header[32];
        snprintf(
            header, sizeof(header), "%s %u/%u", fire ? "FIRE" : "NORMAL",
            (unsigned)coyote_waveform_count_enabled(&model->settings, fire),
            (unsigned)COYOTE_WAVEFORM_COUNT);
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 8, header);
        canvas_set_font(canvas, FontSecondary);

        for(size_t row = 0; row < WAVE_ROWS; ++row) {
            const uint8_t virt = (uint8_t)(model->wave_scroll + row);
            if(virt >= rows_total) break;
            const uint8_t baseline = (uint8_t)(19 + row * 9); // 19..55
            const bool picked = virt == model->wave_selection;
            // The highlight stops short of the scrollbar lane at x >= 123.
            if(picked) canvas_draw_box(canvas, 2, (uint8_t)(baseline - 8), 119, 9);
            canvas_set_color(canvas, picked ? ColorWhite : ColorBlack);
            if(virt < actions) {
                canvas_draw_str(
                    canvas, 16, baseline,
                    wave_action_is_enable(model, fire, virt) ? "Enable all" : "Disable all");
            } else {
                const size_t index = virt - actions;
                // Name always starts at the same x, so toggling never shifts it.
                canvas_draw_str(canvas, 16, baseline, coyote_waveforms[index].name);
                if(coyote_waveform_enabled(&model->settings, fire, index))
                    draw_check(canvas, baseline);
            }
            canvas_set_color(canvas, ColorBlack);
        }

        // Scrollbar: track 123..127, thumb proportional to the window.
        canvas_draw_frame(canvas, 123, 12, 5, 50);
        if(rows_total > WAVE_ROWS) {
            const uint8_t track = 48;
            uint8_t thumb = (uint8_t)((track * WAVE_ROWS) / rows_total);
            if(thumb < 4u) thumb = 4u;
            const uint8_t span = (uint8_t)(rows_total - WAVE_ROWS);
            const uint8_t top =
                (uint8_t)(13u + ((track - thumb) * model->wave_scroll) / span);
            canvas_draw_box(canvas, 124, top, 3, thumb);
        } else {
            canvas_draw_box(canvas, 124, 13, 3, 48);
        }
        return;
    }
    default:
        return;
    }

    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < count; ++i) {
        const uint8_t baseline = (uint8_t)(first_baseline + i * MENU_SPACING);
        char text[32];
        snprintf(text, sizeof(text), "%s", rows[i]);

        const bool picked = model->page == PageModeOptions ? i == model->option_selection :
                                                             i == model->menu_selection;
        if(picked) canvas_draw_box(canvas, 2, (uint8_t)(baseline - MENU_BOX_TOP), 124, MENU_BOX_HEIGHT);
        canvas_set_color(canvas, picked ? ColorWhite : ColorBlack);
        canvas_draw_str(canvas, 8, baseline, text);
        canvas_set_color(canvas, ColorBlack);
    }
}

// canvas_draw_triangle rasterises its sloped edges asymmetrically, so its up
// and down arrows are not quite mirror images. Drawing the rows explicitly makes
// them exact: 7px base narrowing to 1px over 4 rows, away from base_y.
static void draw_arrow(Canvas* canvas, uint8_t centre, uint8_t base_y, bool up) {
    for(uint8_t i = 0; i < 4u; ++i) {
        const uint8_t half = (uint8_t)(3u - i);
        const uint8_t y = up ? (uint8_t)(base_y - i) : (uint8_t)(base_y + i);
        canvas_draw_line(
            canvas, (uint8_t)(centre - half), y, (uint8_t)(centre + half), y);
    }
}

// A "no waveforms" sign where the level would be: ringed circle with a slash.
static void draw_no_waveform(Canvas* canvas, uint8_t centre) {
    canvas_draw_circle(canvas, centre, 30, 8);
    canvas_draw_circle(canvas, centre, 30, 7);
    canvas_draw_line(canvas, (uint8_t)(centre - 5), 35, (uint8_t)(centre + 5), 25);
    canvas_draw_line(canvas, (uint8_t)(centre - 5), 34, (uint8_t)(centre + 5), 24);
}

// Mode screen, per column (channel A left, B right):
//   header     Primary    baseline 8   ->  0..10
//   up arrow              base 16, apex 12
//   frame                 19..57  (content 20..56)
//     letter   Secondary  baseline 37  -> 30..39, at the frame's left edge
//     level    BigNumbers baseline 37  -> 22..37, centred
//     name     Secondary  baseline 54  -> 47..56
//   down arrow            base 59, apex 63
// The frame around the selected channel replaces the old "< A >" text line: it
// is what makes the selection readable at a glance in a dark room.
static void draw_mode(Canvas* canvas, const CoyoteModel* model, bool fire) {
    const bool has_waveforms = coyote_waveform_count_enabled(&model->settings, fire) > 0u;
    const bool options_card = model->runtime.selection == COYOTE_CHANNEL_OPTIONS;
    canvas_set_font(canvas, FontPrimary);
    if(!model->connected) {
        draw_centered(canvas, 8, "DISCONNECTED");
    } else if(fire && model->fire_pressed) {
        draw_centered(canvas, 8, "ZAP");
    } else {
        draw_centered(canvas, 8, fire ? "FIRE" : "NORMAL");
    }

    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        const size_t device = model->active_device_index;
        const uint8_t centre = channel == 0 ? 32 : 96;
        const uint8_t frame_x = channel == 0 ? 1 : 65;
        const bool selected = model->runtime.selection == COYOTE_CHANNEL_BOTH ||
                              (size_t)model->runtime.selection == channel;
        const bool muted = !fire && model->runtime.muted[device][channel];

        if(selected) canvas_draw_rframe(canvas, frame_x, 19, 62, 39, 3);
        if(options_card) continue; // drawn once, full width, after the loop

        // Arrows sit outside the frame, and vanish at the cap or at zero so a
        // limit is visible without reading the number.
        if(has_waveforms && selected &&
           model->runtime.levels[device][channel] <
               model->settings.caps[channel])
            draw_arrow(canvas, centre, 16, true); // 13..16, 2px above the frame
        if(has_waveforms && selected && model->runtime.levels[device][channel] > 0)
            draw_arrow(canvas, centre, 60, false); // 60..63, 2px below the frame

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, (uint8_t)(frame_x + 4), 37, channel == 0 ? "A" : "B");

        if(!has_waveforms) {
            draw_no_waveform(canvas, centre);
        } else if(muted) {
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(canvas, centre, 33, AlignCenter, AlignBottom, "MUTE");
            canvas_set_font(canvas, FontSecondary);
            char saved[12];
            snprintf(saved, sizeof(saved), "(%u)", model->runtime.mute_levels[device][channel]);
            canvas_draw_str_aligned(canvas, centre, 44, AlignCenter, AlignBottom, saved);
        } else {
            char level[8];
            snprintf(level, sizeof(level), "%u", model->runtime.levels[device][channel]);
            canvas_set_font(canvas, FontBigNumbers);
            canvas_draw_str_aligned(canvas, centre, 37, AlignCenter, AlignBottom, level);
        }

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, centre, 54, AlignCenter, AlignBottom,
            has_waveforms ? coyote_waveforms[model->waveform[fire ? 1u : 0u][channel]].name :
                            "No waveforms");
    }

    if(options_card) {
        // The fourth stop in the left/right cycle: one card across the whole
        // width, with the levels still shown so moving onto it does not hide
        // what the channels are doing.
        const size_t device = model->active_device_index;
        char levels[24];
        snprintf(
            levels, sizeof(levels), "A %u    B %u",
            model->runtime.levels[device][0], model->runtime.levels[device][1]);
        canvas_draw_rframe(canvas, 1, 19, 126, 39, 3);
        canvas_set_font(canvas, FontPrimary);
        draw_centered(canvas, 34, "Options");
        canvas_set_font(canvas, FontSecondary);
        draw_centered(canvas, 45, "OK to open");
        draw_centered(canvas, 55, levels);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    const CoyoteModel* model = context;
    canvas_clear(canvas);
    if(model->page == PageNormal || model->page == PageFire) {
        draw_mode(canvas, model, model->page == PageFire);
    } else {
        draw_menu(canvas, model);
    }
}

static size_t next_enabled_waveform(const CoyoteSettings* settings, bool fire, size_t current, int dir) {
    for(size_t attempt = 0; attempt < COYOTE_WAVEFORM_COUNT; ++attempt) {
        current = dir > 0 ? (current + 1) % COYOTE_WAVEFORM_COUNT :
                            (current + COYOTE_WAVEFORM_COUNT - 1) % COYOTE_WAVEFORM_COUNT;
        if(coyote_waveform_enabled(settings, fire, current)) return current;
    }
    return current;
}

static size_t first_enabled_waveform(const CoyoteSettings* settings, bool fire) {
    for(size_t i = 0; i < COYOTE_WAVEFORM_COUNT; ++i) {
        if(coyote_waveform_enabled(settings, fire, i)) return i;
    }
    return 0;
}

// Disabling the waveform that is currently playing must take effect, so the
// channel is moved onto an enabled one. With nothing enabled there is nothing
// to play: callers output zero and the screen says so.
static bool reconcile_waveforms(CoyoteModel* model, bool fire) {
    const size_t mode = fire ? 1u : 0u;
    if(coyote_waveform_count_enabled(&model->settings, fire) == 0u) return false;
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(!coyote_waveform_enabled(&model->settings, fire, model->waveform[mode][channel])) {
            model->waveform[mode][channel] = first_enabled_waveform(&model->settings, fire);
            model->frame[mode][channel] = 0;
        }
    }
    return true;
}

// Shuffle picks uniformly from the enabled set (excluding the one playing when
// there is a choice, so a "change" is always audible); otherwise it steps to the
// next enabled waveform in order.
static size_t pick_next_waveform(const CoyoteModel* model, bool fire, size_t channel) {
    const size_t mode = fire ? 1u : 0u;
    const size_t current = model->waveform[mode][channel];
    const size_t enabled = coyote_waveform_count_enabled(&model->settings, fire);
    if(enabled == 0u) return current;
    if(!model->settings.shuffle_on[mode] || enabled == 1u)
        return next_enabled_waveform(&model->settings, fire, current, 1);

    const size_t others = coyote_waveform_enabled(&model->settings, fire, current) ?
                              enabled - 1u :
                              enabled;
    size_t wanted = (size_t)(furi_hal_random_get() % (uint32_t)others);
    for(size_t i = 0; i < COYOTE_WAVEFORM_COUNT; ++i) {
        if(i == current || !coyote_waveform_enabled(&model->settings, fire, i)) continue;
        if(wanted-- == 0u) return i;
    }
    return current;
}

static void queue_buzz(CoyoteModel* model, uint8_t pulses) {
    if(model->settings.vibration_on) {
        model->buzz_pulses = pulses;
        model->buzz_phase_ms = 0;
    }
}

static void step_selected_channels(CoyoteModel* model, int direction) {
    const size_t device = model->active_device_index;
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(model->runtime.selection != COYOTE_CHANNEL_BOTH &&
           (size_t)model->runtime.selection != channel)
            continue;

        if(model->unmute_pending[channel]) model->unmute_pending[channel] = false;
        uint16_t* level = model->runtime.muted[device][channel] ?
                              &model->runtime.mute_levels[device][channel] :
                              &model->runtime.levels[device][channel];
        int next = (int)*level + direction * (int)model->settings.power_step;
        if(next < 0) next = 0;
        if(next > (int)model->settings.caps[channel])
            next = model->settings.caps[channel];
        *level = (uint16_t)next;
    }
}

static void set_selected_mute(CoyoteModel* model) {
    const size_t device = model->active_device_index;
    bool should_mute = false;
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(model->runtime.selection != COYOTE_CHANNEL_BOTH &&
           (size_t)model->runtime.selection != channel)
            continue;
        if(!model->runtime.muted[device][channel]) should_mute = true;
    }
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(model->runtime.selection != COYOTE_CHANNEL_BOTH &&
           (size_t)model->runtime.selection != channel)
            continue;
        if(should_mute) {
            if(!model->runtime.muted[device][channel])
                model->runtime.mute_levels[device][channel] =
                    model->runtime.levels[device][channel];
            model->runtime.muted[device][channel] = true;
            model->runtime.levels[device][channel] = 0;
            model->unmute_pending[channel] = false;
        } else {
            model->runtime.muted[device][channel] = false;
            model->runtime.levels[device][channel] = 0;
            model->unmute_pending[channel] = true;
            model->unmute_elapsed_ms[channel] = 0;
        }
    }
}

static void select_waveform(CoyoteModel* model, int direction) {
    const size_t mode = model->page == PageFire ? 1u : 0u;
    const bool fire = mode == 1u;
    const size_t current =
        model->runtime.selection == COYOTE_CHANNEL_B ? model->waveform[mode][1] :
                                                       model->waveform[mode][0];
    const size_t next = next_enabled_waveform(&model->settings, fire, current, direction);
    if(model->runtime.selection == COYOTE_CHANNEL_A ||
       model->runtime.selection == COYOTE_CHANNEL_BOTH)
        model->waveform[mode][0] = next;
    if(model->runtime.selection == COYOTE_CHANNEL_B ||
       model->runtime.selection == COYOTE_CHANNEL_BOTH)
        model->waveform[mode][1] = next;
    if(model->runtime.selection != COYOTE_CHANNEL_B) model->frame[mode][0] = 0;
    if(model->runtime.selection != COYOTE_CHANNEL_A) model->frame[mode][1] = 0;
    model->pattern_restart = true;
}

static void leave_mode(CoyoteModel* model) {
    coyote_set_channels_off(&model->runtime);
    model->fire_pressed = false;
    model->fire_press_pending = false;
    model->fire_minimum_tick = false;
    model->held_power_direction = 0;
    model->ramp_elapsed_ms = 0;
    model->unmute_pending[0] = false;
    model->unmute_pending[1] = false;
    model->page = PageMain;
}

static void open_waveforms(CoyoteModel* model, bool fire) {
    model->waveform_parent = model->page;
    model->page = fire ? PageFireWaveforms : PageNormalWaveforms;
    model->wave_selection = 0;
    model->wave_scroll = 0;
}

static bool input_callback(InputEvent* event, void* context) {
    CoyoteApp* app = context;
    CoyoteSettings settings_to_save;
    bool should_save = false;
    bool should_stop = false;
    CoyoteModel* model = model_lock(app);

    if(event->key == InputKeyBack && event->type == InputTypePress) {
        if(model->page == PageNormal || model->page == PageFire) {
            leave_mode(model);
            model->back_mode_returned = true;
            model_unlock(app, true);
            return true;
        }
        if(model->page == PageMain) {
            coyote_set_channels_off(&model->runtime);
            model_unlock(app, true);
            view_dispatcher_stop(app->dispatcher);
            return true;
        }
    }

    // Release always follows a press, Short only when it was not a long press.
    // Clearing the flag here stops a long Back press in a mode (which emits no
    // Short) from leaving it set and swallowing the next Back elsewhere.
    if(event->key == InputKeyBack && event->type == InputTypeRelease) {
        model->back_mode_returned = false;
        model_unlock(app, true);
        return true;
    }

    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        if(model->back_mode_returned) {
            model->back_mode_returned = false;
            model_unlock(app, true);
            return true;
        }
        switch(model->page) {
        case PageNormal:
        case PageFire:
            leave_mode(model);
            break;
        case PageSettings:
            settings_to_save = model->settings;
            should_save = true;
            model->page = PageMain;
            break;
        case PageNormalWaveforms:
        case PageFireWaveforms:
            settings_to_save = model->settings;
            should_save = true;
            model->page = model->waveform_parent;
            break;
        case PageModeOptions:
            settings_to_save = model->settings;
            should_save = true;
            model->page = model->options_return; // the mode (output intact), or Settings
            break;
        case PageChannels:
        case PageDevice:
            settings_to_save = model->settings;
            should_save = true;
            // Put the cursor back on the row that opened this page; read the
            // page before overwriting it.
            model->menu_selection = model->page == PageChannels ? 2u : 3u;
            model->page = PageSettings;
            break;
        case PageMain: should_stop = true; break;
        }
        model_unlock(app, true);
        if(should_save) (void)save_settings(app, &settings_to_save);
        if(should_stop) view_dispatcher_stop(app->dispatcher);
        return true;
    }

    if(model->page == PageNormal || model->page == PageFire) {
        const bool fire = model->page == PageFire;
        if(event->key == InputKeyOk && model->runtime.selection == COYOTE_CHANNEL_OPTIONS) {
            // Takes priority over both mute and fire, so the gesture is the same
            // in either mode and Back still means "leave the mode".
            if(event->type == InputTypeShort) {
                model->options_parent = model->page;
                model->options_return = model->page;
                model->page = PageModeOptions;
                model->option_selection = 0;
            }
        } else if(event->key == InputKeyOk && fire) {
            if(event->type == InputTypePress) {
                model->fire_pressed = true;
                model->fire_press_pending = true;
                if(model->runtime.selection != COYOTE_CHANNEL_B)
                    model->frame[1][0] = 0;
                if(model->runtime.selection != COYOTE_CHANNEL_A)
                    model->frame[1][1] = 0;
                model->pattern_restart = true;
            } else if(event->type == InputTypeRelease) {
                model->fire_pressed = false;
            }
        } else if(event->key == InputKeyOk && !fire && event->type == InputTypeShort) {
            set_selected_mute(model);
            queue_buzz(model, 1);
        } else if(event->key == InputKeyOk && !fire && event->type == InputTypeLong) {
            open_waveforms(model, false);
        } else if((event->key == InputKeyLeft || event->key == InputKeyRight) &&
                  event->type == InputTypeShort) {
            const int direction = event->key == InputKeyRight ? 1 : -1;
            const int selection =
                ((int)model->runtime.selection + direction + (int)COYOTE_SELECTION_COUNT) %
                (int)COYOTE_SELECTION_COUNT;
            model->runtime.selection = (CoyoteChannel)selection;
            if(model->settings.channel_buzz_on && selection != (int)COYOTE_CHANNEL_OPTIONS)
                queue_buzz(model, (uint8_t)(selection + 1));
        } else if((event->key == InputKeyLeft || event->key == InputKeyRight) &&
                  (event->type == InputTypeLong || event->type == InputTypeRepeat)) {
            // InputTypeRepeat arrives every ~150 ms, which flicked through the
            // waveforms far too fast to feel. The first change (Long) is always
            // applied; held repeats are rate-limited to one per 450 ms.
            const uint32_t now = furi_get_tick();
            if(model->runtime.selection != COYOTE_CHANNEL_OPTIONS &&
               (event->type == InputTypeLong ||
                now - model->last_waveform_change_ms >= 450u)) {
                model->last_waveform_change_ms = now;
                select_waveform(model, event->key == InputKeyRight ? 1 : -1);
            }
        } else if((event->key == InputKeyUp || event->key == InputKeyDown) &&
                  event->type == InputTypeShort) {
            step_selected_channels(model, event->key == InputKeyUp ? 1 : -1);
            queue_buzz(model, 1);
        } else if((event->key == InputKeyUp || event->key == InputKeyDown) &&
                  event->type == InputTypeLong) {
            model->held_power_direction = event->key == InputKeyUp ? 1 : -1;
            model->ramp_elapsed_ms = 0;
        } else if((event->key == InputKeyUp || event->key == InputKeyDown) &&
                  event->type == InputTypeRelease) {
            model->held_power_direction = 0;
            model->ramp_elapsed_ms = 0;
        }
    } else if(event->type == InputTypeShort) {
        if(model->page == PageMain) {
            if(event->key == InputKeyUp)
                model->menu_selection = (uint8_t)((model->menu_selection + 2u) % 3u);
            else if(event->key == InputKeyDown)
                model->menu_selection = (uint8_t)((model->menu_selection + 1u) % 3u);
            else if(event->key == InputKeyOk) {
                if(model->menu_selection == 0) model->page = PageNormal;
                else if(model->menu_selection == 1) model->page = PageFire;
                else {
                    model->page = PageSettings;
                    model->menu_selection = 0;
                    model->settings_warning = false; // acknowledged by opening Settings
                }
            }
        } else if(model->page == PageSettings) {
            if(event->key == InputKeyUp)
                model->menu_selection = (uint8_t)((model->menu_selection + 3u) % 4u);
            else if(event->key == InputKeyDown)
                model->menu_selection = (uint8_t)((model->menu_selection + 1u) % 4u);
            else if(event->key == InputKeyOk) {
                if(model->menu_selection < 2) {
                    // The page the modes open too; Back returns here rather than
                    // dropping into the mode.
                    model->options_parent = model->menu_selection == 1 ? PageFire : PageNormal;
                    model->options_return = PageSettings;
                    model->page = PageModeOptions;
                    model->option_selection = 0;
                } else {
                    model->page = model->menu_selection == 2 ? PageChannels : PageDevice;
                    model->menu_selection = 0;
                }
            }
        } else if(model->page == PageChannels) {
            if(event->key == InputKeyUp)
                model->menu_selection = (uint8_t)((model->menu_selection + 2u) % 3u);
            else if(event->key == InputKeyDown)
                model->menu_selection = (uint8_t)((model->menu_selection + 1u) % 3u);
            else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                const int delta = event->key == InputKeyRight ? 1 : -1;
                if(model->menu_selection < 2) {
                    uint16_t* cap = &model->settings.caps[model->menu_selection];
                    int value = (int)*cap + delta * (int)COYOTE_CAP_STEP;
                    if(value < (int)COYOTE_MIN_CAP) value = COYOTE_MIN_CAP;
                    if(value > (int)COYOTE_MAX_CAP) value = COYOTE_MAX_CAP;
                    *cap = (uint16_t)value;
                } else {
                    int value = (int)model->settings.power_step + delta;
                    if(value < (int)COYOTE_MIN_STEP) value = COYOTE_MIN_STEP;
                    if(value > (int)COYOTE_MAX_STEP) value = COYOTE_MAX_STEP;
                    model->settings.power_step = (uint8_t)value;
                }
            }
        } else if(model->page == PageDevice) {
            if(event->key == InputKeyUp || event->key == InputKeyDown)
                model->menu_selection = model->menu_selection == 0u ? 1u : 0u;
            else if(event->key == InputKeyOk) {
                if(model->menu_selection == 0)
                    model->settings.vibration_on = !model->settings.vibration_on;
                else
                    model->settings.channel_buzz_on = !model->settings.channel_buzz_on;
            }
        } else if(model->page == PageModeOptions) {
            const bool fire = model->options_parent == PageFire;
            const size_t mode = fire ? 1u : 0u;
            if(event->key == InputKeyUp)
                model->option_selection =
                    (uint8_t)((model->option_selection + MODE_OPTION_ROWS - 1u) % MODE_OPTION_ROWS);
            else if(event->key == InputKeyDown)
                model->option_selection =
                    (uint8_t)((model->option_selection + 1u) % MODE_OPTION_ROWS);
            else if(event->key == InputKeyOk && model->option_selection == 0) {
                open_waveforms(model, fire);
                model->waveform_parent = PageModeOptions;
            }
            else if(event->key == InputKeyOk && model->option_selection == 2)
                model->settings.shuffle_on[mode] = !model->settings.shuffle_on[mode];
            else if(
                (event->key == InputKeyLeft || event->key == InputKeyRight) &&
                model->option_selection == 1) {
                // Off, 5, 10, 30, 60 seconds.
                static const uint8_t steps[] = {0u, 5u, 10u, 30u, 60u};
                size_t at = 0;
                for(size_t i = 0; i < COUNT_OF(steps); ++i)
                    if(steps[i] == model->settings.auto_advance_s[mode]) at = i;
                if(event->key == InputKeyRight)
                    at = (at + 1u) % COUNT_OF(steps);
                else
                    at = (at + COUNT_OF(steps) - 1u) % COUNT_OF(steps);
                model->settings.auto_advance_s[mode] = steps[at];
                for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
                    model->dwell_ms[mode][channel] = 0;
                    model->advance_pending[mode][channel] = false;
                }
            }
        } else if(model->page == PageNormalWaveforms || model->page == PageFireWaveforms) {
            const bool fire = model->page == PageFireWaveforms;
            const uint8_t rows_total = wave_row_count(model, fire);
            const uint8_t actions = wave_action_count(model, fire);
            if(event->key == InputKeyUp) {
                model->wave_selection =
                    (uint8_t)((model->wave_selection + rows_total - 1u) % rows_total);
            } else if(event->key == InputKeyDown) {
                model->wave_selection = (uint8_t)((model->wave_selection + 1u) % rows_total);
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                // Real paging: the new row lands at the TOP of the page, not
                // merely five rows along with the window nudged just enough to
                // show it. The last page is the final WAVE_ROWS rows, so paging
                // past either end wraps to the other end's page.
                const uint8_t max_scroll =
                    rows_total > WAVE_ROWS ? (uint8_t)(rows_total - WAVE_ROWS) : 0u;
                uint8_t next;
                if(event->key == InputKeyRight) {
                    if(model->wave_selection >= max_scroll)
                        next = 0u;
                    else
                        next = (uint8_t)(model->wave_selection + WAVE_ROWS);
                    if(next > max_scroll) next = max_scroll;
                } else {
                    if(model->wave_selection == 0u)
                        next = max_scroll;
                    else if(model->wave_selection >= WAVE_ROWS)
                        next = (uint8_t)(model->wave_selection - WAVE_ROWS);
                    else
                        next = 0u;
                }
                model->wave_selection = next;
                model->wave_scroll = next;
            } else if(event->key == InputKeyOk) {
                if(model->wave_selection < actions) {
                    const bool enable = wave_action_is_enable(model, fire, model->wave_selection);
                    coyote_waveform_set_all(&model->settings, fire, enable);
                    // One of the action rows has just disappeared, so the row
                    // indices below it move. Park on the row that remains
                    // rather than leaving the cursor on a different item.
                    model->wave_selection = 0;
                    model->wave_scroll = 0;
                } else {
                    const size_t index = model->wave_selection - actions;
                    const bool was_enabled = coyote_waveform_enabled(&model->settings, fire, index);
                    coyote_waveform_set_enabled(&model->settings, fire, index, !was_enabled);
                    // Toggling the first or last one adds or removes an action
                    // row; shift the cursor so it stays on the same waveform.
                    const uint8_t actions_now = wave_action_count(model, fire);
                    model->wave_selection =
                        (uint8_t)(model->wave_selection + actions_now - actions);
                }
            }
            wave_clamp_scroll(model, fire);
        }
    }

    model_unlock(app, true);
    return true;
}

static void update_status(CoyoteApp* app, bool connected, const char* text, uint8_t battery) {
    CoyoteModel* model = model_lock(app);
    model->connected = connected;
    model->battery = battery;
    model->battery_valid = connected && battery <= 100u;
    snprintf(model->status, sizeof(model->status), "%s", text);
    if(!connected) model->device_name[0] = '\0';
    model_unlock(app, true);
}

static bool uuid_equal(const BcService* service, const uint8_t uuid[16]) {
    return service->uuid_len == 16u && memcmp(service->uuid, uuid, 16) == 0;
}

static bool char_uuid_equal(const BcChar* chr, const uint8_t uuid[16]) {
    return chr->uuid_len == 16u && memcmp(chr->uuid, uuid, 16) == 0;
}

static const uint8_t uuid_coyote_service[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x0B, 0x18, 0x5A, 0x95};
static const uint8_t uuid_battery_service[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x0A, 0x18, 0x5A, 0x95};
static const uint8_t uuid_power[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x04, 0x15, 0x5A, 0x95};
static const uint8_t uuid_pattern_b[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x05, 0x15, 0x5A, 0x95};
static const uint8_t uuid_pattern_a[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x06, 0x15, 0x5A, 0x95};
static const uint8_t uuid_battery[16] = {
    0xAD, 0xE8, 0xF3, 0xD4, 0xB8, 0x84, 0x94, 0xA0,
    0xAA, 0xF5, 0xE2, 0x0F, 0x00, 0x15, 0x5A, 0x95};

static void battery_notification(
    uint16_t value_handle,
    const uint8_t* data,
    uint8_t length,
    void* context) {
    CoyoteApp* app = context;
    if(value_handle != app->battery_handle || length == 0u || data[0] > 100u) return;
    CoyoteModel* model = model_lock(app);
    model->battery = data[0];
    model->battery_valid = true;
    model_unlock(app, true);
}

typedef struct {
    uint16_t power;
    uint16_t pattern_a;
    uint16_t pattern_b;
    uint16_t battery;
} CoyoteHandles;

static bool discover_device(CoyoteApp* app, CoyoteHandles* handles, uint8_t* battery) {
    BcService* services = malloc(sizeof(*services) * BC_MAX_SERVICES);
    BcChar* chars = malloc(sizeof(*chars) * BC_MAX_CHARS);
    bool found = false;
    *battery = 0xFF;
    if(!services || !chars) {
        FURI_LOG_E(TAG, "Not enough memory for GATT discovery");
        free(services);
        free(chars);
        return false;
    }

    const int service_count = bc_discover_services(app->ble, services, BC_MAX_SERVICES);
    if(service_count < 0) goto done;
    const BcService* coyote = NULL;
    const BcService* battery_service = NULL;
    for(int i = 0; i < service_count; ++i) {
        if(uuid_equal(&services[i], uuid_coyote_service)) coyote = &services[i];
        if(uuid_equal(&services[i], uuid_battery_service)) battery_service = &services[i];
    }
    if(!coyote) goto done;

    const int char_count = bc_discover_chars(app->ble, coyote, chars, BC_MAX_CHARS);
    if(char_count < 0) goto done;
    memset(handles, 0, sizeof(*handles));
    for(int i = 0; i < char_count; ++i) {
        if(char_uuid_equal(&chars[i], uuid_power)) handles->power = chars[i].value_handle;
        else if(char_uuid_equal(&chars[i], uuid_pattern_a))
            handles->pattern_a = chars[i].value_handle;
        else if(char_uuid_equal(&chars[i], uuid_pattern_b))
            handles->pattern_b = chars[i].value_handle;
    }
    if(!handles->power || !handles->pattern_a || !handles->pattern_b) goto done;

    if(battery_service) {
        const int battery_char_count =
            bc_discover_chars(app->ble, battery_service, chars, BC_MAX_CHARS);
        BcChar battery_characteristic = {0};
        if(battery_char_count > 0) {
            for(int i = 0; i < battery_char_count; ++i) {
                if(char_uuid_equal(&chars[i], uuid_battery)) {
                    battery_characteristic = chars[i];
                    handles->battery = battery_characteristic.value_handle;
                    break;
                }
            }
        }
        if(handles->battery) {
            uint8_t value[4];
            const int length = bc_read(app->ble, handles->battery, value, sizeof(value));
            if(length > 0 && value[0] <= 100u) *battery = value[0];
            else FURI_LOG_W(TAG, "Could not read battery level");
            if((battery_characteristic.props & CHAR_PROP_NOTIFY) != 0u) {
                uint16_t end_handle = battery_service->end;
                for(int i = 0; i < battery_char_count; ++i) {
                    if(chars[i].decl_handle > battery_characteristic.decl_handle &&
                       chars[i].decl_handle - 1u < end_handle)
                        end_handle = chars[i].decl_handle - 1u;
                }
                app->battery_handle = handles->battery;
                if(!bc_set_notify(
                       app->ble, &battery_characteristic, end_handle, true))
                    FURI_LOG_W(TAG, "Battery notifications unavailable");
            }
        }
    }
    found = true;

done:
    free(chars);
    free(services);
    return found;
}

static bool is_coyote(const BcDevice* device) {
    return strncmp(device->name, "D-LAB", 5) == 0;
}

static void set_outputs_off(CoyoteApp* app, const CoyoteHandles* handles) {
    uint8_t power[3];
    uint8_t zero[3] = {0, 0, 0};
    coyote_encode_power(0, 0, power);
    if(bc_write(app->ble, handles->power, power, sizeof(power), false)) {
        memcpy(app->last_power, power, sizeof(power));
        app->last_power_valid = true;
    } else {
        app->last_power_valid = false;
        FURI_LOG_W(TAG, "Could not zero Coyote power: 0x%02X", bc_last_error(app->ble));
    }
    if(!bc_write(app->ble, handles->pattern_a, zero, sizeof(zero), false))
        FURI_LOG_W(TAG, "Could not clear channel A pattern");
    if(!bc_write(app->ble, handles->pattern_b, zero, sizeof(zero), false))
        FURI_LOG_W(TAG, "Could not clear channel B pattern");
}

static void set_light(bool connected, bool normal_on, bool fire_on, uint32_t tick) {
    if(!connected) {
        // furi_get_tick() is milliseconds, so this toggles every 400 ms (~1.2 Hz).
        // Dividing by a small number blinks far too fast to read as a blink.
        const uint8_t value = ((tick / 400u) & 1u) ? 0x20 : 0;
        furi_hal_light_set(LightBlue, value);
        furi_hal_light_set(LightGreen, 0);
        furi_hal_light_set(LightRed, 0);
    } else if(fire_on) {
        furi_hal_light_set(LightRed, 0xFF);
        furi_hal_light_set(LightGreen, 0);
        furi_hal_light_set(LightBlue, 0);
    } else if(normal_on) {
        furi_hal_light_set(LightGreen, 0x18);
        furi_hal_light_set(LightRed, 0);
        furi_hal_light_set(LightBlue, 0);
    } else {
        furi_hal_light_set(LightRed, 0);
        furi_hal_light_set(LightGreen, 0);
        furi_hal_light_set(LightBlue, 0);
    }
}

static void fill_frame(const CoyoteModel* model, size_t channel, uint8_t out[3]) {
    const size_t mode = active_mode(model);
    const CoyoteWaveform* waveform = &coyote_waveforms[model->waveform[mode][channel]];
    const CoyoteFrame* frame = &waveform->frames[model->frame[mode][channel] % waveform->count];
    coyote_encode_pattern(frame->x, frame->y, frame->z, out);
}

static bool transmit_tick(CoyoteApp* app, const CoyoteHandles* handles, uint32_t* elapsed) {
    uint8_t power_packet[3], pattern_a[3], pattern_b[3];
    uint16_t output_a, output_b;
    bool normal_on, fire_on, fire_held, connected, patterns_due;
    bool vibrate;
    CoyoteModel* model = model_lock(app);

    if(model->held_power_direction != 0) {
        model->ramp_elapsed_ms += WORKER_PERIOD_MS;
        const uint16_t interval = model->held_power_direction > 0 ? 400u : 200u;
        while(model->ramp_elapsed_ms >= interval) {
            model->ramp_elapsed_ms -= interval;
            step_selected_channels(model, model->held_power_direction);
            queue_buzz(model, 1);
        }
    }

    *elapsed += WORKER_PERIOD_MS;
    patterns_due = model->pattern_restart || *elapsed >= OUTPUT_PERIOD_MS;
    if(*elapsed >= OUTPUT_PERIOD_MS) *elapsed %= OUTPUT_PERIOD_MS;
    model->pattern_restart = false;

    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        if(model->unmute_pending[channel]) {
            const uint16_t next_elapsed =
                model->unmute_elapsed_ms[channel] + WORKER_PERIOD_MS;
            model->unmute_elapsed_ms[channel] = next_elapsed > 1000u ? 1000u : next_elapsed;
            const uint32_t level =
                (uint32_t)model->runtime.mute_levels[model->active_device_index][channel] *
                model->unmute_elapsed_ms[channel] / 1000u;
            model->runtime.levels[model->active_device_index][channel] = (uint16_t)level;
            if(model->unmute_elapsed_ms[channel] >= 1000u) {
                model->runtime.levels[model->active_device_index][channel] =
                    model->runtime.mute_levels[model->active_device_index][channel];
                model->unmute_pending[channel] = false;
            }
        }
    }

    connected = model->connected;
    const size_t mode = active_mode(model);
    const bool has_waveforms = reconcile_waveforms(model, mode == 1u);
    const bool resting_live = normal_output_live(model);
    const size_t device = model->active_device_index;
    if(model->fire_press_pending) {
        model->fire_press_pending = false;
        model->fire_minimum_tick = true;
    }
    fire_held = model->page == PageFire &&
                (model->fire_pressed || model->fire_minimum_tick);
    // Fire only drives the selected channel(s) while held; otherwise the resting
    // levels flow whenever normal output is live, which includes its options and
    // waveform pages so that adjusting them does not interrupt play.
    output_a = (fire_held && model->runtime.selection != COYOTE_CHANNEL_B) || resting_live ?
                   model->runtime.levels[device][0] :
                   0;
    output_b = (fire_held && model->runtime.selection != COYOTE_CHANNEL_A) || resting_live ?
                   model->runtime.levels[device][1] :
                   0;
    if(!has_waveforms) {
        output_a = 0;
        output_b = 0;
    }
    coyote_encode_power(output_a, output_b, power_packet);
    fill_frame(model, 0, pattern_a);
    fill_frame(model, 1, pattern_b);
    // Auto-advance: the dwell runs on wall-clock time, but the switch waits for
    // the waveform to finish its cycle, so it never cuts off mid-pattern.
    const uint8_t dwell_s = model->settings.auto_advance_s[mode];
    if(dwell_s > 0u && has_waveforms) {
        for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
            model->dwell_ms[mode][channel] += WORKER_PERIOD_MS;
            if(model->dwell_ms[mode][channel] >= (uint32_t)dwell_s * 1000u)
                model->advance_pending[mode][channel] = true;
        }
    }
    if(patterns_due) {
        for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
            const CoyoteWaveform* waveform = &coyote_waveforms[model->waveform[mode][channel]];
            const size_t next = model->frame[mode][channel] + 1u;
            if(next < waveform->count) {
                model->frame[mode][channel] = next;
                continue;
            }
            model->frame[mode][channel] = 0; // cycle boundary
            if(!model->advance_pending[mode][channel]) continue;
            model->advance_pending[mode][channel] = false;
            model->dwell_ms[mode][channel] = 0;
            model->waveform[mode][channel] = pick_next_waveform(model, mode == 1u, channel);
        }
    }
    normal_on = resting_live && (output_a || output_b);
    fire_on = fire_held && (output_a || output_b);
    if(model->fire_minimum_tick && !model->fire_pressed) model->fire_minimum_tick = false;

    vibrate = false;
    if(connected && model->settings.vibration_on) {
        if(fire_on) {
            vibrate = true;
        } else if(model->buzz_pulses) {
            vibrate = model->buzz_phase_ms < 55u;
            model->buzz_phase_ms += WORKER_PERIOD_MS;
            if(model->buzz_phase_ms >= 120u) {
                model->buzz_phase_ms = 0;
                model->buzz_pulses--;
            }
        }
    } else {
        model->buzz_pulses = 0;
        model->buzz_phase_ms = 0;
    }
    // The worker ticks every 20 ms; asking the GUI to redraw that often is 50
    // fps of wasted CPU. Redraw on the 100 ms pattern boundary (10 fps, enough
    // for the ramp and the level digits) and immediately when ZAP starts or
    // stops, which has to feel instant.
    const bool redraw = patterns_due || fire_held != app->last_fire_held;
    app->last_fire_held = fire_held;
    model_unlock(app, redraw);

    set_light(connected, normal_on, fire_on, furi_get_tick());
    furi_hal_vibro_on(vibrate);
    if(!connected) return true;

    if(patterns_due &&
       (!bc_write(app->ble, handles->pattern_a, pattern_a, sizeof(pattern_a), false) ||
        !bc_write(app->ble, handles->pattern_b, pattern_b, sizeof(pattern_b), false))) {
        FURI_LOG_E(TAG, "Pattern write failed: 0x%02X", bc_last_error(app->ble));
        return false;
    }
    if(!app->last_power_valid || memcmp(power_packet, app->last_power, sizeof(power_packet)) != 0) {
        if(!bc_write(app->ble, handles->power, power_packet, sizeof(power_packet), false)) {
            FURI_LOG_E(TAG, "Power write failed: 0x%02X", bc_last_error(app->ble));
            return false;
        }
        memcpy(app->last_power, power_packet, sizeof(power_packet));
        app->last_power_valid = true;
    }
    if(!bc_is_connected(app->ble)) {
        FURI_LOG_E(TAG, "Output write failed: 0x%02X", bc_last_error(app->ble));
        return false;
    }
    return true;
}

static int32_t worker_thread(void* context) {
    CoyoteApp* app = context;
    uint32_t elapsed = 0;
    CoyoteHandles handles = {0};
    app->ble = bc_alloc(battery_notification, app);
    if(!app->ble) {
        update_status(app, false, "BLE allocation failed", 0);
        return 0;
    }
    if(!bc_supported()) {
        update_status(app, false, "Full BLE stack required", 0);
        while(!app->stopping) furi_delay_ms(100);
        bc_free(app->ble);
        app->ble = NULL;
        return 0;
    }

    BcDevice* devices = malloc(sizeof(*devices) * BC_MAX_DEVICES);
    if(!devices) {
        update_status(app, false, "Not enough memory", 0);
        bc_free(app->ble);
        app->ble = NULL;
        return 0;
    }

    while(!app->stopping) {
        update_status(app, false, "Searching for Coyote...", 0);
        if(!bc_scan_start(app->ble)) {
            FURI_LOG_E(TAG, "BLE scan start failed: 0x%02X", bc_last_error(app->ble));
            furi_delay_ms(1000);
            continue;
        }
        BcDevice target = {0};
        bool found = false;
        while(!app->stopping && !found) {
            const size_t count = bc_snapshot(app->ble, devices, BC_MAX_DEVICES);
            for(size_t i = 0; i < count; ++i) {
                if(is_coyote(&devices[i])) {
                    target = devices[i];
                    found = true;
                    break;
                }
            }
            set_light(false, false, false, furi_get_tick());
            // 100 ms, not 250: the disconnect blink toggles every 400 ms, and
            // sampling it slower than that makes it stutter. Also finds the
            // Coyote sooner.
            furi_delay_ms(100);
        }
        if(app->stopping) break;
        update_status(app, false, "Connecting...", 0);
        if(!bc_connect(app->ble, &target, 15000)) {
            FURI_LOG_E(TAG, "BLE connect failed: 0x%02X", bc_last_error(app->ble));
            continue;
        }

        uint8_t battery = 0xFF;
        app->battery_handle = 0;
        if(!discover_device(app, &handles, &battery)) {
            FURI_LOG_E(TAG, "Coyote GATT service/characteristic discovery failed");
            bc_disconnect(app->ble);
            continue;
        }

        CoyoteModel* model = model_lock(app);
        coyote_runtime_reset(&model->runtime);
        model->fire_pressed = false;
        model->fire_press_pending = false;
        model->fire_minimum_tick = false;
        model->held_power_direction = 0;
        model->connected = true;
        model->battery = battery;
        model->battery_valid = battery <= 100u;
        snprintf(model->device_name, sizeof(model->device_name), "%s", target.name);
        snprintf(model->status, sizeof(model->status), "Connected");
        model_unlock(app, true);
        set_outputs_off(app, &handles);
        elapsed = 0;

        while(!app->stopping && bc_is_connected(app->ble)) {
            if(!transmit_tick(app, &handles, &elapsed)) break;
            furi_delay_ms(WORKER_PERIOD_MS);
        }

        set_outputs_off(app, &handles);
        if(bc_is_connected(app->ble)) bc_disconnect(app->ble);
        model = model_lock(app);
        coyote_runtime_reset(&model->runtime);
        model->connected = false;
        model->fire_pressed = false;
        model->fire_press_pending = false;
        model->fire_minimum_tick = false;
        model->held_power_direction = 0;
        model->unmute_pending[0] = false;
        model->unmute_pending[1] = false;
        snprintf(model->status, sizeof(model->status), "Disconnected; reconnecting");
        model->device_name[0] = '\0';
        model_unlock(app, true);
        furi_hal_vibro_on(false);
        furi_delay_ms(500);
    }

    if(bc_is_connected(app->ble)) {
        set_outputs_off(app, &handles);
        bc_disconnect(app->ble);
    }
    furi_hal_light_set(LightRed, 0);
    furi_hal_light_set(LightGreen, 0);
    furi_hal_light_set(LightBlue, 0);
    furi_hal_vibro_on(false);
    free(devices);
    bc_free(app->ble);
    app->ble = NULL;
    return 0;
}

static void init_view(CoyoteApp* app) {
    CoyoteModel* model;
    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(CoyoteModel));
    model = model_lock(app);
    memset(model, 0, sizeof(*model));
    coyote_settings_default(&model->settings);
    coyote_runtime_reset(&model->runtime);
    model->page = PageMain;
    snprintf(model->status, sizeof(model->status), "Searching for Coyote...");
    load_settings(app, model);
    for(size_t channel = 0; channel < COYOTE_CHANNEL_COUNT; ++channel) {
        model->waveform[0][channel] = first_enabled_waveform(&model->settings, false);
        model->waveform[1][channel] = first_enabled_waveform(&model->settings, true);
    }
    model_unlock(app, false);
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, draw_callback);
    view_set_input_callback(app->view, input_callback);
    view_dispatcher_add_view(app->dispatcher, VIEW_MAIN, app->view);
    view_dispatcher_switch_to_view(app->dispatcher, VIEW_MAIN);
}

int32_t coyote_app(void* p) {
    UNUSED(p);
    CoyoteApp app = {0};
    app.storage = furi_record_open(RECORD_STORAGE);
    app.dispatcher = view_dispatcher_alloc();
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app.dispatcher, gui, ViewDispatcherTypeFullscreen);

    // The radio can only run one role at a time: our own advertising (and any
    // phone link) has to stop before the central can scan or connect. Restored
    // on exit. Blue++ expects every BLE app to do this for itself.
    app.was_advertising = furi_hal_bt_is_active();
    furi_hal_bt_stop_advertising();
    furi_delay_ms(100); // let the resulting disconnect event drain

    init_view(&app);
    app.worker = furi_thread_alloc();
    furi_thread_set_name(app.worker, "CoyoteBLE");
    furi_thread_set_stack_size(app.worker, 4096);
    furi_thread_set_callback(app.worker, worker_thread);
    furi_thread_set_context(app.worker, &app);
    furi_thread_start(app.worker);

    view_dispatcher_run(app.dispatcher);
    app.stopping = true;
    furi_thread_join(app.worker);
    furi_thread_free(app.worker);
    if(app.was_advertising) furi_hal_bt_start_advertising();
    view_dispatcher_remove_view(app.dispatcher, VIEW_MAIN);
    view_free(app.view);
    view_dispatcher_free(app.dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    return 0;
}
