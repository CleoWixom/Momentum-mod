#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"
#include <gui/modules/validators.h>
#include <lib/subghz/subghz_setting.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

#define TAG "SubGhzScenePresetSave"

/* -------------------------------------------------------------------------
 * SETTINGS-03: "Save Preset As…" and "Load Preset from file"
 *
 * Save flow:
 *   ReceiverConfig → [Save Preset As…] → PresetSave (TextInput)
 *     → on confirm: subghz_setting_save_custom_preset()
 *     → popup "Saved!" or "Error" → back to ReceiverConfig
 *
 * Load flow:
 *   ReceiverConfig → [Load Preset] → PresetSave state=Load
 *     → DialogsApp file browser (*.sgp in /ext/subghz/presets/)
 *     → on select: read Custom_preset_data → add to memory
 *     → back to ReceiverConfig (Modulation list now contains new preset)
 * -------------------------------------------------------------------------
 */

#define SUBGHZ_PRESET_FOLDER       EXT_PATH("subghz/presets")
#define SUBGHZ_PRESET_FILE_EXT     ".sgp"
#define SUBGHZ_PRESET_FILE_TYPE    "Flipper SubGhz Preset"
#define SUBGHZ_PRESET_FILE_VERSION 1
#define SUBGHZ_PRESET_MAX_NAME     SUBGHZ_MAX_LEN_NAME

/** State flag stored in scene state to distinguish save vs load entry. */
#define SUBGHZ_PRESET_SCENE_SAVE 0u
#define SUBGHZ_PRESET_SCENE_LOAD 1u

/* ---- helpers ---- */

static void subghz_scene_preset_save_ensure_dir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, SUBGHZ_PRESET_FOLDER);
    furi_record_close(RECORD_STORAGE);
}

/**
 * @brief Write the current preset to a standalone .sgp file so it can be
 *        shared and loaded on another device.
 *
 * The file lives at SUBGHZ_PRESET_FOLDER/<name>.sgp and contains the
 * standard FlipperFormat header plus the preset name and hex register data.
 *
 * @return true on success.
 */
static bool subghz_scene_preset_save_write_sgp(
    const char* preset_name,
    const uint8_t* data,
    size_t data_size) {
    subghz_scene_preset_save_ensure_dir();

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);

    FuriString* path = furi_string_alloc_printf(
        "%s/%s%s", SUBGHZ_PRESET_FOLDER, preset_name, SUBGHZ_PRESET_FILE_EXT);

    bool ok = false;
    do {
        if(!flipper_format_file_open_new(fff, furi_string_get_cstr(path))) {
            FURI_LOG_E(TAG, "Cannot create %s", furi_string_get_cstr(path));
            break;
        }
        if(!flipper_format_write_header_cstr(
               fff, SUBGHZ_PRESET_FILE_TYPE, SUBGHZ_PRESET_FILE_VERSION))
            break;
        if(!flipper_format_write_string_cstr(fff, "Custom_preset_name", preset_name)) break;
        if(!flipper_format_write_hex(fff, "Custom_preset_data", data, data_size)) break;
        ok = true;
    } while(false);

    furi_string_free(path);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* ---- TextInput callback (Save path) ---- */

static void subghz_scene_preset_save_text_input_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventScenePresetNameEntered);
}

/* ---- on_enter ---- */

void subghz_scene_preset_save_on_enter(void* context) {
    SubGhz* subghz = context;
    uint32_t mode = scene_manager_get_scene_state(subghz->scene_manager, SubGhzScenePresetSave);

    if(mode == SUBGHZ_PRESET_SCENE_LOAD) {
        /* ---- Load path: open file browser ---- */
        subghz_scene_preset_save_ensure_dir();

        FuriString* preset_path = furi_string_alloc_set(SUBGHZ_PRESET_FOLDER);
        DialogsFileBrowserOptions browser_opts;
        dialog_file_browser_set_basic_options(&browser_opts, SUBGHZ_PRESET_FILE_EXT, NULL);
        browser_opts.base_path = SUBGHZ_PRESET_FOLDER;

        bool selected =
            dialog_file_browser_show(subghz->dialogs, preset_path, preset_path, &browser_opts);

        if(selected) {
            /* Read the preset data from the .sgp file */
            Storage* storage = furi_record_open(RECORD_STORAGE);
            FlipperFormat* fff = flipper_format_file_alloc(storage);
            SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
            bool loaded = false;

            do {
                if(!flipper_format_file_open_existing(fff, furi_string_get_cstr(preset_path)))
                    break;

                FuriString* name_str = furi_string_alloc();
                /* Read name — fall back to filename if key is absent */
                if(!flipper_format_read_string(fff, "Custom_preset_name", name_str)) {
                    /* Use filename without extension as the preset name */
                    path_extract_filename(preset_path, name_str, true);
                }
                flipper_format_rewind(fff);

                const char* preset_name = furi_string_get_cstr(name_str);

                /* Check for duplicate */
                if(subghz_setting_get_inx_preset_by_name(setting, preset_name) >= 0) {
                    FURI_LOG_W(TAG, "Preset '%s' already in list", preset_name);
                    furi_string_set(subghz->error_str, "Already exists:\n");
                    furi_string_cat(subghz->error_str, preset_name);
                    furi_string_free(name_str);
                    break;
                }

                loaded = subghz_setting_load_custom_preset(setting, preset_name, fff);
                if(!loaded) {
                    furi_string_set(subghz->error_str, "Bad preset file");
                }
                furi_string_free(name_str);
            } while(false);

            flipper_format_free(fff);
            furi_record_close(RECORD_STORAGE);

            if(loaded) {
                /* Switch to the freshly loaded preset */
                size_t new_idx = (size_t)subghz_setting_get_preset_count(
                                     subghz_txrx_get_setting(subghz->txrx)) -
                                 1;
                const char* new_name =
                    subghz_setting_get_preset_name(subghz_txrx_get_setting(subghz->txrx), new_idx);
                uint8_t* new_data =
                    subghz_setting_get_preset_data(subghz_txrx_get_setting(subghz->txrx), new_idx);
                size_t new_size = subghz_setting_get_preset_data_size(
                    subghz_txrx_get_setting(subghz->txrx), new_idx);
                SubGhzRadioPreset cur = subghz_txrx_get_preset(subghz->txrx);
                subghz_txrx_set_preset(
                    subghz->txrx, new_name, cur.frequency, NAN, NAN, new_data, new_size);
                subghz->last_settings->preset_index = (uint32_t)new_idx;
                subghz_last_settings_mark_dirty(subghz->last_settings);

                /* Show success popup */
                furi_string_set(subghz->error_str, "Loaded!");
            }

            furi_string_free(preset_path);

            /* Show result in error scene (reused for messages) */
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
        } else {
            furi_string_free(preset_path);
            scene_manager_previous_scene(subghz->scene_manager);
        }
        return;
    }

    /* ---- Save path: show TextInput ---- */
    TextInput* text_input = subghz->text_input;

    /* Pre-fill with the current preset name */
    SubGhzRadioPreset cur = subghz_txrx_get_preset(subghz->txrx);
    strlcpy(subghz->file_name_tmp, furi_string_get_cstr(cur.name), SUBGHZ_PRESET_MAX_NAME);

    text_input_set_header_text(text_input, "Preset name");
    text_input_set_result_callback(
        text_input,
        subghz_scene_preset_save_text_input_callback,
        subghz,
        subghz->file_name_tmp,
        SUBGHZ_PRESET_MAX_NAME,
        true /* select all on enter */);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
}

/* ---- on_event ---- */

bool subghz_scene_preset_save_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == SubGhzCustomEventScenePresetNameEntered) {
        const char* name = subghz->file_name_tmp;

        if(name[0] == '\0') {
            furi_string_set(subghz->error_str, "Name is empty");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
            consumed = true;
        } else {
            SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
            SubGhzRadioPreset cur = subghz_txrx_get_preset(subghz->txrx);

            if(subghz_setting_get_inx_preset_by_name(setting, name) >= 0) {
                furi_string_set(subghz->error_str, "Name already taken");
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
            } else if(
                subghz_setting_save_custom_preset(setting, name, cur.data, cur.data_size) &&
                subghz_scene_preset_save_write_sgp(name, cur.data, cur.data_size)) {
                /* Switch active modulation to the newly saved preset */
                size_t new_idx = (size_t)subghz_setting_get_preset_count(setting) - 1;
                const char* new_name = subghz_setting_get_preset_name(setting, new_idx);
                uint8_t* new_data = subghz_setting_get_preset_data(setting, new_idx);
                size_t new_size = subghz_setting_get_preset_data_size(setting, new_idx);
                subghz_txrx_set_preset(
                    subghz->txrx, new_name, cur.frequency, NAN, NAN, new_data, new_size);
                subghz->last_settings->preset_index = (uint32_t)new_idx;
                subghz_last_settings_mark_dirty(subghz->last_settings);

                furi_string_printf(subghz->error_str, "Saved as:\n%s", name);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
            } else {
                furi_string_set(subghz->error_str, "Save failed");
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(subghz->scene_manager);
        consumed = true;
    }

    return consumed;
}

/* ---- on_exit ---- */

void subghz_scene_preset_save_on_exit(void* context) {
    SubGhz* subghz = context;
    text_input_reset(subghz->text_input);
}
