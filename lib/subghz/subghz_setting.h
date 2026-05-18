
#pragma once

#include <math.h>
#include <furi.h>
#include <m-list.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_SETTING_FILE_TYPE    "Flipper SubGhz Setting File"
#define SUBGHZ_SETTING_FILE_VERSION 1

#define SUBGHZ_SETTING_DEFAULT_PRESET_COUNT 4

LIST_DEF(FrequencyList, uint32_t)

#define M_OPL_FrequencyList_t() LIST_OPLIST(FrequencyList)

typedef struct SubGhzSetting SubGhzSetting;

/**
 * Centralised frequency validator (BUG-05).
 *
 * Wraps furi_hal_subghz_is_frequency_valid() and provides a single call site
 * for all code paths: UI, RPC, file loading, frequency hopper.  Callers that
 * previously used furi_hal_subghz_is_frequency_valid() directly should be
 * migrated here so that any future policy change (extended-range unlock, band
 * exclusions) only needs to be applied in one place.
 *
 * @param frequency  Frequency in Hz to validate.
 * @return true if the frequency is within a hardware-supported and
 *         policy-allowed range, false otherwise.
 */
bool subghz_setting_frequency_valid(uint32_t frequency);

SubGhzSetting* subghz_setting_alloc(void);

void subghz_setting_free(SubGhzSetting* instance);

void subghz_setting_load(SubGhzSetting* instance, const char* file_path);

size_t subghz_setting_get_frequency_count(SubGhzSetting* instance);

size_t subghz_setting_get_hopper_frequency_count(SubGhzSetting* instance);

size_t subghz_setting_get_preset_count(SubGhzSetting* instance);

const char* subghz_setting_get_preset_name(SubGhzSetting* instance, size_t idx);

int subghz_setting_get_inx_preset_by_name(SubGhzSetting* instance, const char* preset_name);

uint8_t* subghz_setting_get_preset_data(SubGhzSetting* instance, size_t idx);

size_t subghz_setting_get_preset_data_size(SubGhzSetting* instance, size_t idx);

uint8_t* subghz_setting_get_preset_data_by_name(SubGhzSetting* instance, const char* preset_name);

bool subghz_setting_load_custom_preset(
    SubGhzSetting* instance,
    const char* preset_name,
    FlipperFormat* fff_data_file);

bool subghz_setting_delete_custom_preset(SubGhzSetting* instance, const char* preset_name);

/**
 * @brief Save a custom preset to the user settings file and add it to the
 *        in-memory preset list.
 *
 * If a preset with the same name already exists in memory this function
 * returns false without modifying the file, so callers should check first
 * with subghz_setting_get_inx_preset_by_name().
 *
 * The preset is appended to EXT_PATH("subghz/assets/setting_user") using
 * FlipperFormat's append mode, so all previously saved custom presets are
 * preserved.  The file is created with a valid header if it does not exist.
 *
 * @param instance        SubGhzSetting instance.
 * @param preset_name     Null-terminated name string (max 64 chars).
 * @param preset_data     Raw CC1101 register bytes (register-address/value
 *                        pairs followed by PA table, same layout as
 *                        Custom_preset_data in the setting file).
 * @param preset_data_size  Number of bytes in preset_data.
 * @return true on success, false if the name already exists or I/O failed.
 */
bool subghz_setting_save_custom_preset(
    SubGhzSetting* instance,
    const char* preset_name,
    const uint8_t* preset_data,
    size_t preset_data_size);

uint32_t subghz_setting_get_frequency(SubGhzSetting* instance, size_t idx);

uint32_t subghz_setting_get_hopper_frequency(SubGhzSetting* instance, size_t idx);

uint32_t subghz_setting_get_frequency_default_index(SubGhzSetting* instance);

uint32_t subghz_setting_get_default_frequency(SubGhzSetting* instance);

void subghz_setting_set_default_frequency(SubGhzSetting* instance, uint32_t frequency_to_setup);

uint8_t subghz_setting_customs_presets_to_log(SubGhzSetting* instance);

#ifdef __cplusplus
}
#endif
