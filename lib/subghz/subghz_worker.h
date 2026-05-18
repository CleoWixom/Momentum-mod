#pragma once

#include <furi_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file subghz_worker.h
 * @brief SubGHz signal worker — converts raw CC1101 GPIO pulses into
 *        level-duration pairs fed to the protocol decoders.
 *
 * ## Threading model
 * SubGhzWorker runs its processing loop in a dedicated FreeRTOS task at
 * FuriThreadPriorityHigh.  The rx_callback (subghz_worker_rx_callback) is
 * called from an **ISR context** by the CC1101 GPIO interrupt handler and may
 * only perform lock-free stream-buffer writes.  All other public functions
 * must be called from a **single owner thread** — concurrent calls from
 * multiple threads are not supported and will produce undefined behaviour.
 *
 * ## Ownership
 * The caller that allocates the worker owns it and is responsible for calling
 * subghz_worker_free().  Callbacks and their context pointers are borrowed
 * references; the worker does not free them.
 *
 * ## Lifecycle
 * ```
 * alloc → set_*_callback → set_context → start → [rx_callback …] → stop → free
 * ```
 * start/stop may be called multiple times on the same instance.
 */
typedef struct SubGhzWorker SubGhzWorker;

/**
 * Called from the worker thread when the internal stream buffer overflows.
 * The implementation must be short and non-blocking.
 */
typedef void (*SubGhzWorkerOverrunCallback)(void* context);

/**
 * Called from the worker thread for each level-duration pair that passes the
 * short-pulse filter and the rate-limit gate.
 * @param context   User-supplied context pointer (see subghz_worker_set_context).
 * @param level     GPIO level: true = high, false = low.
 * @param duration  Pulse duration in microseconds.
 */
typedef void (*SubGhzWorkerPairCallback)(void* context, bool level, uint32_t duration);

/**
 * ISR-safe RX callback.  Must be registered with the CC1101 driver as the
 * GPIO edge interrupt handler.  Writes one LevelDuration sample to the
 * lock-free stream buffer; the worker thread drains the buffer and fires
 * SubGhzWorkerPairCallback.
 *
 * @note Called from interrupt context — must not block or call any Furi
 *       mutex/semaphore primitives.
 */
void subghz_worker_rx_callback(bool level, uint32_t duration, void* context);

/**
 * Allocate and initialise a SubGhzWorker.
 *
 * Default settings after alloc:
 * - Short-pulse filter: 30 µs
 * - Thread priority: FuriThreadPriorityHigh
 * - Rate-limit cooldown: 30 ms
 * - All callbacks: NULL
 *
 * @return Newly allocated SubGhzWorker.  Never returns NULL (asserts on OOM).
 */
SubGhzWorker* subghz_worker_alloc(void);

/**
 * Free a SubGhzWorker.  The worker must be stopped before freeing.
 *
 * @param instance  Worker to free.  Must not be NULL.
 */
void subghz_worker_free(SubGhzWorker* instance);

/**
 * Register an overrun callback.
 * Called from the worker thread when the stream buffer is full and a new
 * sample arrives (i.e. the decoder is too slow for the incoming RF traffic).
 *
 * @param instance  Worker instance.
 * @param callback  Callback function or NULL to clear.
 */
void subghz_worker_set_overrun_callback(
    SubGhzWorker* instance,
    SubGhzWorkerOverrunCallback callback);

/**
 * Register a pair callback.
 * Called for every level-duration pair that survives the short-pulse filter
 * and rate-limit gate.  Typically forwards to SubGhzReceiver.
 *
 * @param instance  Worker instance.
 * @param callback  Callback function or NULL to clear.
 */
void subghz_worker_set_pair_callback(SubGhzWorker* instance, SubGhzWorkerPairCallback callback);

/**
 * Set the context pointer passed to all callbacks.
 *
 * @param instance  Worker instance.
 * @param context   Arbitrary user pointer.  Ownership is not transferred.
 */
void subghz_worker_set_context(SubGhzWorker* instance, void* context);

/**
 * Start the worker thread and begin processing samples from the stream buffer.
 * Must only be called when the worker is not already running.
 *
 * @param instance  Worker instance.
 */
void subghz_worker_start(SubGhzWorker* instance);

/**
 * Stop the worker thread.  Blocks until the thread has exited.
 * Safe to call if the worker is not running (no-op).
 *
 * @param instance  Worker instance.
 */
void subghz_worker_stop(SubGhzWorker* instance);

/**
 * Query whether the worker thread is currently running.
 *
 * @param instance  Worker instance.
 * @return true if running.
 */
bool subghz_worker_is_running(SubGhzWorker* instance);

/**
 * Configure the short-pulse filter.
 *
 * Consecutive samples whose duration is below @p timeout are merged into a
 * single sample.  This removes glitches caused by RF noise or CC1101 clock
 * jitter without affecting real signal edges.
 *
 * Default: 30 µs.  Set to 0 to disable filtering entirely.
 *
 * @param instance  Worker instance.
 * @param timeout   Minimum pulse duration in microseconds.
 */
void subghz_worker_set_filter(SubGhzWorker* instance, uint16_t timeout);

#ifdef __cplusplus
}
#endif
