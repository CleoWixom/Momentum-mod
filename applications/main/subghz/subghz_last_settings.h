#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>
#include <lib/subghz/types.h>
#include <furi/core/event_loop_timer.h>

#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER        (-93.0f)
// 1 = "AM650"
// "AM270", "AM650", "FM238", "FM12K", "FM476",
#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET                    1
#define SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY                 433920000
#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL 2

typedef struct {
    uint32_t frequency;
    uint32_t preset_index; // AKA Modulation
    uint32_t frequency_analyzer_feedback_level;
    float frequency_analyzer_trigger;
    bool protocol_file_names;
    bool enable_hopping;
    uint32_t ignore_filter;
    uint32_t filter;
    float rssi;
    bool delete_old_signals;

    uint32_t gps_baudrate;
    bool remove_duplicates;
    uint32_t repeater_state;
    bool enable_sound;
    bool autosave;
    float hopping_threshold;
    uint8_t tx_power;

    /*
     * SETTINGS-01: async-save state.
     * save_timer is a one-shot FuriEventLoopTimer (1500 ms).  Any call to
     * subghz_last_settings_mark_dirty() sets dirty=true and (re)starts the
     * timer, debouncing rapid successive changes into a single file write.
     * The timer runs on the ViewDispatcher event loop, so the storage call
     * in the callback executes on the UI thread — same as the old synchronous
     * saves, but deferred so the UI is not stalled for ~10 ms per keypress.
     */
    FuriEventLoopTimer* save_timer;
    bool dirty;
} SubGhzLastSettings;

SubGhzLastSettings* subghz_last_settings_alloc(void);

void subghz_last_settings_free(SubGhzLastSettings* instance);

void subghz_last_settings_load(SubGhzLastSettings* instance, size_t preset_count);

bool subghz_last_settings_save(SubGhzLastSettings* instance);

/**
 * @brief Attach an async-save timer to the given event loop.
 *
 * Must be called once after alloc(), passing the ViewDispatcher's event loop.
 * The timer is one-shot and fires SUBGHZ_LAST_SETTINGS_SAVE_DELAY_MS after
 * the last call to subghz_last_settings_mark_dirty().
 *
 * @param instance  SubGhzLastSettings instance.
 * @param event_loop  FuriEventLoop to attach the timer to (must outlive instance).
 */
void subghz_last_settings_init_save_timer(
    SubGhzLastSettings* instance,
    FuriEventLoop* event_loop);

/**
 * @brief Schedule a deferred file write.
 *
 * Sets the dirty flag and (re)starts the 1500 ms one-shot save timer.
 * Multiple rapid calls coalesce into a single write.  Safe to call from the
 * ViewDispatcher thread at any time after subghz_last_settings_init_save_timer().
 *
 * @param instance  SubGhzLastSettings instance.
 */
void subghz_last_settings_mark_dirty(SubGhzLastSettings* instance);

/**
 * @brief Write to disk immediately if dirty, then clear the pending timer.
 *
 * Call this before the app exits or the ViewDispatcher event loop stops, so
 * that unsaved changes are not lost if the timer has not fired yet.
 *
 * @param instance  SubGhzLastSettings instance.
 */
void subghz_last_settings_flush_save(SubGhzLastSettings* instance);
