#pragma once

/**
 * @file subghz_txrx.h
 * @brief SubGHz TX/RX helper — radio device + protocol state machine.
 *
 * SubGhzTxRx is the central coordinator for the SubGHz application.  It owns:
 *   - the active radio device (internal CC1101 or external CC1101-ext)
 *   - the SubGhzWorker that delivers decoded RF pulse trains to a receiver
 *   - the SubGhzReceiver / SubGhzTransmitter for protocol decoding/encoding
 *   - the current modulation preset and frequency
 *
 * ## Thread safety
 * None of the functions in this module are thread-safe.  All calls MUST be
 * made from the SubGHz application thread (the FuriThread that created the
 * instance).  Callbacks installed via subghz_txrx_set_rx_callback() and
 * subghz_txrx_set_need_save_callback() are invoked synchronously on the
 * SubGhzWorker thread — they must be short and must NOT call back into
 * SubGhzTxRx to avoid re-entrancy.
 *
 * ## Ownership
 * - subghz_txrx_alloc() creates and returns an opaque, heap-allocated
 *   SubGhzTxRx instance.  The caller owns it and MUST call
 *   subghz_txrx_free() exactly once.
 * - Pointers returned by subghz_txrx_get_fff_data() and
 *   subghz_txrx_get_setting() are borrowed references valid until the next
 *   call that modifies the instance; callers must NOT free them.
 *
 * ## Valid state transitions
 * @code
 *   alloc → [Idle] → rx_start → [RX] → stop → [Idle]
 *                  → tx_start → [TX] → stop → [Idle]
 *                                            → sleep → [Sleep]
 * @endcode
 * Calling tx_start while in [RX] state (or vice-versa) is not supported;
 * call subghz_txrx_stop() first.
 */

#include "subghz_types.h"

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/devices/devices.h>

typedef struct SubGhzTxRx SubGhzTxRx;

/** Callback invoked when a received signal is ready to be saved to SD.
 *  Called from the SubGhzWorker thread — keep it short. */
typedef void (*SubGhzTxRxNeedSaveCallback)(void* context);

/** Return codes for subghz_txrx_tx_start(). */
typedef enum {
    /** TX started successfully. */
    SubGhzTxRxStartTxStateOk,
    /** This radio device / region is RX-only; TX is blocked by policy. */
    SubGhzTxRxStartTxStateErrorOnlyRx,
    /** The loaded protocol file could not be parsed or is incomplete. */
    SubGhzTxRxStartTxStateErrorParserOthers,
} SubGhzTxRxStartTxState;

/**
 * Allocate and initialise a SubGhzTxRx instance.
 *
 * Loads the protocol database, applies the default modulation preset, and
 * powers up the radio device.  Must be paired with subghz_txrx_free().
 *
 * @return Newly allocated SubGhzTxRx.  Never returns NULL (aborts on OOM).
 */
SubGhzTxRx* subghz_txrx_alloc(void);

/**
 * Stop any active TX/RX operation, power down the radio, and release all
 * resources owned by the instance.
 *
 * @param instance  Pointer to a SubGhzTxRx.  Must not be NULL.  Becomes
 *                  invalid after this call — do not use it further.
 */
void subghz_txrx_free(SubGhzTxRx* instance);

/**
 * Check whether the protocol decoder database was successfully loaded from SD.
 *
 * A missing or corrupt database means no protocols can be decoded; the
 * receiver will still capture RAW signals.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          true if the database is loaded and valid.
 */
bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance);

/**
 * Configure the active modulation preset.
 *
 * Must be called before subghz_txrx_tx_start() or subghz_txrx_rx_start() if
 * a non-default preset is required.  Safe to call while idle.
 *
 * @param instance         Pointer to a SubGhzTxRx.
 * @param preset_name      Name string for the preset (e.g. "AM650").  Copied
 *                         internally; the caller may free the string after.
 * @param frequency        Carrier frequency in Hz.
 * @param latitude         GPS latitude of capture location (0.0 if unknown).
 * @param longitude        GPS longitude of capture location (0.0 if unknown).
 * @param preset_data      CC1101 register + PA-table byte array, or NULL to
 *                         look up a built-in preset by name.
 * @param preset_data_size Length of preset_data in bytes.
 */
void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    float latitude,
    float longitude,
    uint8_t* preset_data,
    size_t preset_data_size);

/**
 * Patch the PA-table TX power entry inside a raw preset byte array.
 *
 * The function operates on a caller-supplied buffer and returns a pointer to
 * the PA-table section within it; it does NOT take ownership of the buffer.
 *
 * @param preset_data       Raw CC1101 preset data array (register/value pairs
 *                          followed by a 0x00 sentinel and 8 PA bytes).
 * @param preset_data_size  Total size of preset_data in bytes.
 * @param tx_power          Index into the TX-power menu (0 = maximum).
 * @return                  Pointer to the updated PA-table section inside
 *                          preset_data, or NULL on error.
 */
uint8_t* subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power);

/**
 * Map a raw preset name string to a canonical display name.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param preset    Raw preset name as stored in a .sub file.
 * @return          Human-readable name, or the original string if no mapping
 *                  exists.  The returned pointer is valid as long as
 *                  @p instance is alive.
 */
const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset);

/**
 * Return a copy of the currently active radio preset.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          SubGhzRadioPreset struct (copied by value).
 */
SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance);

/**
 * Fill @p frequency and @p modulation with human-readable strings for the
 * current preset (e.g. "433.92" and "AM650").
 *
 * @param instance    Pointer to a SubGhzTxRx.
 * @param frequency   Output FuriString for the frequency.  Caller owns it.
 * @param modulation  Output FuriString for the modulation name.  Caller owns.
 * @param long_name   If true, use the full preset name; otherwise use a short
 *                    abbreviation suitable for a narrow UI element.
 */
void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name);

/**
 * Return the GPS latitude stored in the active preset.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Latitude in decimal degrees, or 0.0 if not set.
 */
float subghz_txrx_get_latitude(SubGhzTxRx* instance);

/**
 * Return the GPS longitude stored in the active preset.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Longitude in decimal degrees, or 0.0 if not set.
 */
float subghz_txrx_get_longitude(SubGhzTxRx* instance);

/**
 * Decode @p flipper_format, configure the transmitter, and start TX.
 *
 * The function parses the protocol, frequency, and modulation from the
 * FlipperFormat data, applies check_tx() (TXRX-01), and hands control to the
 * CC1101 DMA transmit engine.  Returns immediately; the actual RF transmission
 * runs in the background driven by DMA interrupts.
 *
 * @pre  The instance must be in Idle state (not already transmitting or
 *       receiving).  Call subghz_txrx_stop() first if needed.
 * @pre  subghz_txrx_set_preset() must have been called with valid data.
 *
 * @param instance       Pointer to a SubGhzTxRx.
 * @param flipper_format Parsed .sub file data.  Ownership is NOT transferred;
 *                       the caller must keep it alive until TX completes.
 * @return               SubGhzTxRxStartTxStateOk on success, or an error code
 *                       describing why TX could not start.
 */
SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format);

/**
 * Configure the radio for reception and start the SubGhzWorker thread.
 *
 * Signals are delivered to the callback registered via
 * subghz_txrx_set_rx_callback().  Call subghz_txrx_stop() to return to idle.
 *
 * @pre  The instance must be in Idle or Sleep state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_rx_start(SubGhzTxRx* instance);

/**
 * Abort any active TX or RX operation and return the radio to idle.
 *
 * Stops the SubGhzWorker, disables DMA, and transitions the CC1101 to its
 * idle calibration state.  Safe to call from any non-Sleep state; calling it
 * while already idle is a no-op.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_stop(SubGhzTxRx* instance);

/**
 * Put the radio into the CC1101 SLEEP state (lowest power).
 *
 * The CC1101 loses its register configuration in SLEEP; the driver will
 * re-apply the preset when the next TX or RX operation starts.
 * Call subghz_txrx_stop() before this function.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_sleep(SubGhzTxRx* instance);

/**
 * Step the frequency hopper to the next channel if RSSI is below the
 * stay threshold.
 *
 * Called periodically by the application timer while in hopper mode.
 * Must be called from the application thread (not interrupt context).
 *
 * @param instance        Pointer to a SubGhzTxRx.
 * @param stay_threshold  RSSI level (dBm) above which the hopper stays on the
 *                        current channel (signal present).
 */
void subghz_txrx_hopper_update(SubGhzTxRx* instance, float stay_threshold);

/**
 * Return the current frequency-hopper state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          One of SubGhzHopperStateOff / Running / Pause.
 */
SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance);

/**
 * Set the frequency-hopper state directly.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param state     New hopper state.
 */
void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state);

/**
 * Resume a previously paused frequency hopper.
 *
 * Equivalent to subghz_txrx_hopper_set_state(instance, SubGhzHopperStateRunning).
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_hopper_unpause(SubGhzTxRx* instance);

/**
 * Pause the frequency hopper without stopping it.
 *
 * The hopper stops advancing through channels but can be resumed with
 * subghz_txrx_hopper_unpause() without re-initialisation.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_hopper_pause(SubGhzTxRx* instance);

/**
 * Enable the speaker, routing demodulated audio through the buzzer.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_speaker_on(SubGhzTxRx* instance);

/**
 * Disable the speaker entirely (state = Off).
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_speaker_off(SubGhzTxRx* instance);

/**
 * Temporarily mute the speaker without changing the On/Off state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_speaker_mute(SubGhzTxRx* instance);

/**
 * Un-mute the speaker, restoring the previous audio output.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_speaker_unmute(SubGhzTxRx* instance);

/**
 * Directly set the speaker state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param state     New speaker state (Off / On / Mute).
 */
void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state);

/**
 * Return the current speaker state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Current SubGhzSpeakerState.
 */
SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance);

/**
 * Attach the protocol decoder identified by @p name_protocol to the receiver.
 *
 * Used when replaying a saved signal: the correct decoder must be active before
 * the encoder starts emitting pulses so that loop-back verification works.
 *
 * @param instance       Pointer to a SubGhzTxRx.
 * @param name_protocol  Protocol name string as stored in the .sub file
 *                       "Protocol" field.
 * @return               true if the decoder was found and loaded.
 */
bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol);

/**
 * Return the currently active protocol decoder.
 *
 * The returned pointer is a borrowed reference owned by the receiver.  It
 * becomes invalid after subghz_txrx_stop() or the next call to
 * subghz_txrx_load_decoder_by_name_protocol().
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Active SubGhzProtocolDecoderBase, or NULL if none loaded.
 */
SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance);

/**
 * Register a callback to be called when a newly decoded signal should be saved.
 *
 * The callback is invoked on the SubGhzWorker thread.  It must be fast and
 * must NOT call back into SubGhzTxRx.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param callback  Callback function, or NULL to clear.
 * @param context   Opaque pointer passed to the callback.
 */
void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context);

/**
 * Return a borrowed pointer to the FlipperFormat buffer holding the last
 * loaded/captured signal data.
 *
 * The pointer remains valid until the next file load or subghz_txrx_free().
 * Callers must NOT free it.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Borrowed FlipperFormat pointer.
 */
FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance);

/**
 * Return a borrowed pointer to the SubGhzSetting (frequency list + presets).
 *
 * Valid until subghz_txrx_free().  Callers must NOT free it.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Borrowed SubGhzSetting pointer.
 */
SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance);

/**
 * Check whether the currently decoded protocol supports serialisation to SD.
 *
 * A protocol is serialisable if it implements the encode/decode .sub file
 * interface.  RAW captures are always serialisable; some exotic dynamic-code
 * protocols are not.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          true if the current protocol can be saved to a .sub file.
 */
bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance);

/**
 * Check whether the currently decoded protocol can be retransmitted.
 *
 * @param instance    Pointer to a SubGhzTxRx.
 * @param check_type  If true, also verify that the protocol type (Static vs
 *                    Dynamic) allows replay on this device/region.
 * @return            true if retransmission is currently possible.
 */
bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type);

/**
 * Set the protocol filter applied to incoming decoded signals.
 *
 * Only decoders whose flags intersect @p filter will be active.  Use
 * SubGhzProtocolFlag_Decodable for all decodable protocols, or combine
 * flags to restrict to a subset (e.g. weather stations only).
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param filter    Bitmask of SubGhzProtocolFlag values to allow.
 */
void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter);

/**
 * Set the protocol ignore-filter (protocols to suppress even if matched).
 *
 * @param instance       Pointer to a SubGhzTxRx.
 * @param ignore_filter  Bitmask of SubGhzProtocolFlag values to suppress.
 */
void subghz_txrx_receiver_set_ignore_filter(
    SubGhzTxRx* instance,
    SubGhzProtocolFilter ignore_filter);

/**
 * Register the callback invoked each time a signal is decoded.
 *
 * Called from the SubGhzWorker thread for every successfully decoded frame.
 * Must be fast; long processing should be deferred via a message queue.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param callback  SubGhzReceiverCallback, or NULL to clear.
 * @param context   Opaque pointer forwarded to the callback.
 */
void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context);

/**
 * Register the callback invoked when the RAW encoder worker finishes.
 *
 * Used by the Read RAW scene to know when DMA has consumed all pulse data.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param callback  End-of-stream callback, or NULL to clear.
 * @param context   Opaque pointer forwarded to the callback.
 */
void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context);

/**
 * Check whether the external radio device identified by @p name is connected.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param name      Device name string (e.g. "cc1101_ext").
 * @return          true if the named external device responds on the SPI bus.
 */
bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name);

/**
 * Switch to the given radio device type and return the type that was actually
 * activated (may differ if the requested device is not present).
 *
 * @param instance          Pointer to a SubGhzTxRx.
 * @param radio_device_type Desired device type.
 * @return                  The radio device type now in use.
 */
SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type);

/**
 * Return the currently active radio device type.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Active SubGhzRadioDeviceType.
 */
SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance);

/**
 * Read the RSSI (received signal strength) from the active radio device.
 *
 * Only meaningful while in RX state; returns a stale or undefined value
 * in other states.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          RSSI in dBm (negative value, e.g. -85.0).
 */
float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance);

/**
 * Return the display name of the active radio device.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Null-terminated name string.  Borrowed — do not free.
 */
const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance);

/**
 * Check whether @p frequency is within the active radio device's supported
 * hardware range (ignoring TX policy).
 *
 * @param instance   Pointer to a SubGhzTxRx.
 * @param frequency  Frequency in Hz.
 * @return           true if the hardware can tune to this frequency.
 */
bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency);

/**
 * Check whether TX is permitted on @p frequency for the active radio device.
 *
 * This is the same gate enforced inside subghz_txrx_tx_start() (TXRX-01).
 * Call it before constructing a TX UI to give early feedback.
 *
 * @param instance   Pointer to a SubGhzTxRx.
 * @param frequency  Frequency in Hz to evaluate.
 * @return           SubGhzTxAllowed if TX is permitted; one of the
 *                   SubGhzTxBlocked* or SubGhzTxUnsupported values otherwise.
 */
SubGhzTx subghz_txrx_radio_device_check_tx(SubGhzTxRx* instance, uint32_t frequency);

/**
 * Enable or disable the debug GPIO mirror pin.
 *
 * When enabled the radio device mirrors the raw OOK data line to a GPIO pin
 * that can be captured by a logic analyser.  Has no effect on the decoded
 * output seen by the application.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param state     true = enable, false = disable.
 */
void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state);

/**
 * Return whether the debug GPIO mirror pin is currently enabled.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          true if the debug pin is active.
 */
bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance);

/**
 * Reset the rolling-code sequence counter and any custom button bindings.
 *
 * Call before replaying a captured dynamic-code signal to ensure the
 * expected counter value is presented to the target device.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance);

/**
 * Return the underlying SubGhzReceiver.
 *
 * @deprecated  Use only from the DecodeRaw scene; do not introduce new callers.
 *              Direct access to the receiver bypasses SubGhzTxRx state tracking.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return          Borrowed SubGhzReceiver pointer.
 */
SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance);

/**
 * Set the active preset to AM650 at @p frequency (or 433.92 MHz if 0).
 *
 * Convenience wrapper around subghz_txrx_set_preset() for the common case
 * where no custom register data is needed.
 *
 * @param instance   Pointer to a SubGhzTxRx.
 * @param frequency  Carrier frequency in Hz, or 0 for the default 433.92 MHz.
 */
void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency);

/**
 * Apply preset index @p index from SubGhzSetting at @p frequency with TX power
 * @p tx_power, and return the preset's canonical name.
 *
 * @param instance   Pointer to a SubGhzTxRx.
 * @param frequency  Carrier frequency in Hz.
 * @param index      Preset index within the SubGhzSetting list.
 * @param tx_power   TX power menu index (0 = maximum).
 * @return           Borrowed name string of the selected preset.
 */
const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power);

/**
 * Allocate SubGhzTxRx
 * 
 * @return SubGhzTxRx* pointer to SubGhzTxRx
 */
SubGhzTxRx* subghz_txrx_alloc(void);

/**
 * Free SubGhzTxRx
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_free(SubGhzTxRx* instance);

/**
 * Check if the database is loaded
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return bool True if the database is loaded
 */
bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance);

/**
 * Set preset 
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param preset_name Name of preset
 * @param frequency Frequency in Hz
 * @param latitude Latitude in float
 * @param longitude Longitude in float
 * @param preset_data Data of preset
 * @param preset_data_size Size of preset data
 */
void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    float latitude,
    float longitude,
    uint8_t* preset_data,
    size_t preset_data_size);

/**
 * Set TX Power
 * 
 * @param preset_data Data of preset
 * @param preset_data_size Size of preset data
 * @param tx_power Menu Index of TX Power Setting. (Saves iterating in Config enter)
 */
uint8_t* subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power);

/**
 * Get name of preset
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param preset String of preset 
 * @return const char*  Name of preset
 */
const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset);

/**
 * Get of preset
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzRadioPreset Preset
 */
SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance);

/**
 * Get string frequency and modulation
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param frequency Pointer to a string frequency
 * @param modulation Pointer to a string modulation
 */
void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name);

/**
 * Get latitude value
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return latitude
*/
float subghz_txrx_get_latitude(SubGhzTxRx* instance);

/**
 * Get longitude value
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return longitude
*/
float subghz_txrx_get_longitude(SubGhzTxRx* instance);

/**
 * Start TX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param flipper_format Pointer to a FlipperFormat
 * @return SubGhzTxRxStartTxState 
 */
SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format);

/**
 * Start RX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_rx_start(SubGhzTxRx* instance);

/**
 * Stop TX/RX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_stop(SubGhzTxRx* instance);

/**
 * Set sleep mode CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_sleep(SubGhzTxRx* instance);

/**
 * Update frequency CC1101 in automatic mode (hopper)
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param stay_threshold RSSI theshold over which to stay before hopping
 */
void subghz_txrx_hopper_update(SubGhzTxRx* instance, float stay_threshold);

/**
 * Get state hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzHopperState 
 */
SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance);

/**
 * Set state hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param state State hopper
 */
void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state);

/**
 * Unpause hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_hopper_unpause(SubGhzTxRx* instance);

/**
 * Set pause hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_hopper_pause(SubGhzTxRx* instance);

/**
 * Speaker on
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_on(SubGhzTxRx* instance);

/**
 * Speaker off
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_off(SubGhzTxRx* instance);

/**
 * Speaker mute
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_mute(SubGhzTxRx* instance);

/**
 * Speaker unmute
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_unmute(SubGhzTxRx* instance);

/**
 * Set state speaker
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @param state State speaker
 */
void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state);

/**
 * Get state speaker
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return SubGhzSpeakerState 
 */
SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance);

/**
 * load decoder by name protocol
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param name_protocol Name protocol
 * @return bool True if the decoder is loaded 
 */
bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol);

/**
 * Get decoder
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzProtocolDecoderBase* Pointer to a SubGhzProtocolDecoderBase
 */
SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance);

/**
 * Set callback for save data
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for save data
 * @param context Context for callback
 */
void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context);

/**
 * Get pointer to a load data key
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return FlipperFormat* 
 */
FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance);

/**
 * Get pointer to a SugGhzSetting
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzSetting* 
 */
SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance);

/**
 * Is it possible to save this protocol
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return bool True if it is possible to save this protocol
 */
bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance);

/**
 * Is it possible to send this protocol
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return bool True if it is possible to send this protocol
 */
bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type);

/**
 * Set filter, what types of decoder to use 
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param filter Filter
 */
void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter);

/**
 * Set ignore filter, what types of decoder to skip 
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param ignore_filter Ignore filter
 */
void subghz_txrx_receiver_set_ignore_filter(
    SubGhzTxRx* instance,
    SubGhzProtocolFilter ignore_filter);

/**
 * Set callback for receive data
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for receive data
 * @param context Context for callback
 */
void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context);

/**
 * Set callback for Raw decoder, end of data transfer  
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for Raw decoder, end of data transfer 
 * @param context Context for callback
 */
void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context);

/* Checking if an external radio device is connected
 *
 * @param instance Pointer to a SubGhzTxRx
 * @param name Name of external radio device
 * @return bool True if is connected to the external radio device
 */
bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name);

/* Set the selected radio device to use
 *
 * @param instance Pointer to a SubGhzTxRx
 * @param radio_device_type Radio device type
 * @return SubGhzRadioDeviceType Type of installed radio device
 */
SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type);

/* Get the selected radio device to use
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzRadioDeviceType Type of installed radio device
 */
SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance);

/* Get RSSI the selected radio device to use
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return float RSSI
 */
float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance);

/* Get name the selected radio device to use
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return const char* Name of installed radio device
 */
const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance);

/* Get intelligence whether frequency the selected radio device to use
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return bool True if the frequency is valid
 */
bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency);

/**
 * Check whether TX is permitted on the given frequency for the active radio device.
 *
 * Wraps subghz_devices_check_tx() to provide a single call site; this is the
 * same gate enforced inside the low-level subghz_txrx_tx() helper (TXRX-01).
 *
 * @param instance   Pointer to a SubGhzTxRx.
 * @param frequency  Frequency in Hz to check.
 * @return SubGhzTx  SubGhzTxAllowed if transmitting is permitted, or one of
 *                   the SubGhzTxBlocked* / SubGhzTxUnsupported values
 *                   describing why it is not.
 */
SubGhzTx subghz_txrx_radio_device_check_tx(SubGhzTxRx* instance, uint32_t frequency);

/**
 * Enable or disable the debug output pin.
 * When enabled the radio device mirrors its internal data line to the debug
 * GPIO, which allows logic-analyser capture of raw OOK pulses.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @param state     true = enable, false = disable.
 */
void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state);

/**
 * Query the current debug-pin state.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 * @return true if the debug pin is currently enabled.
 */
bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance);

/**
 * Reset the rolling-code sequence counter and any custom button bindings.
 * Typically called before replaying a previously captured dynamic-code signal
 * to ensure the counter starts at the expected value.
 *
 * @param instance  Pointer to a SubGhzTxRx.
 */
void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance);

SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance); // TODO use only in DecodeRaw

/**
 * @brief Set current preset AM650 without additional params
 * 
 * @param instance - instance Pointer to a SubGhzTxRx
 * @param frequency - frequency of preset, if pass 0 then taking default frequency 433.92MHz
 */
void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency);

/**
 * @brief Set current preset by index
 * 
 * @param instance  - instance Pointer to a SubGhzTxRx
 * @param frequency - frequency of new preset
 * @param index - index of preset taken from SubGhzSetting
 * @param tx_power - index of TX Power menu index option to use.
 * @return const char* -  name of preset
 */
const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power);
