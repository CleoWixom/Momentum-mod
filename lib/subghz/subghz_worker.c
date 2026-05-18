#include "subghz_worker.h"

#include <furi.h>

#define TAG "SubGhzWorker"

struct SubGhzWorker {
    FuriThread* thread;
    FuriStreamBuffer* stream;

    volatile bool running;
    volatile bool overrun;

    LevelDuration filter_level_duration;
    uint16_t filter_duration;

    SubGhzWorkerOverrunCallback overrun_callback;
    SubGhzWorkerPairCallback pair_callback;
    void* context;

    /*
     * WORKER-01: rate-limiting state.
     * Tracks the last time the pair_callback fired and the number of
     * samples dropped due to rate limiting.  When the RF environment is very
     * dense (e.g. busy parking lot with many remotes), the stream buffer can
     * saturate and the FreeRTOS scheduler stalls.  A short cooldown after each
     * callback prevents runaway CPU consumption without dropping unique signals.
     */
    uint32_t last_callback_tick;
    uint32_t dropped_count;
};

/** Minimum gap between consecutive pair_callback invocations, in ms.
 *  Signals arriving faster than this are assumed to be re-transmissions of
 *  the same burst and are silently discarded.  30 ms covers the typical
 *  inter-frame gap used by most rolling-code and static-code remotes. */
#define SUBGHZ_WORKER_RATE_LIMIT_MS 30u

/** Rx callback timer
 * 
 * @param level received signal level
 * @param duration received signal duration
 * @param context 
 */
void subghz_worker_rx_callback(bool level, uint32_t duration, void* context) {
    SubGhzWorker* instance = context;

    LevelDuration level_duration = level_duration_make(level, duration);
    if(instance->overrun) {
        instance->overrun = false;
        level_duration = level_duration_reset();
    }
    size_t ret =
        furi_stream_buffer_send(instance->stream, &level_duration, sizeof(LevelDuration), 0);
    if(sizeof(LevelDuration) != ret) instance->overrun = true;
}

/** Worker callback thread
 * 
 * @param context 
 * @return exit code 
 */
static int32_t subghz_worker_thread_callback(void* context) {
    SubGhzWorker* instance = context;

    LevelDuration level_duration;
    while(instance->running) {
        int ret = furi_stream_buffer_receive(
            instance->stream, &level_duration, sizeof(LevelDuration), 10);
        if(ret == sizeof(LevelDuration)) {
            if(level_duration_is_reset(level_duration)) {
                FURI_LOG_E(TAG, "Overrun buffer");
                if(instance->overrun_callback) instance->overrun_callback(instance->context);
            } else {
                bool level = level_duration_get_level(level_duration);
                uint32_t duration = level_duration_get_duration(level_duration);

                if((duration < instance->filter_duration) ||
                   (instance->filter_level_duration.level == level)) {
                    instance->filter_level_duration.duration += duration;

                } else if(instance->filter_level_duration.level != level) {
                    /*
                     * WORKER-01: rate-limit gate.
                     * Only fire the callback if enough time has elapsed since
                     * the last invocation.  This prevents CPU starvation when
                     * many remotes are active simultaneously (dense RF).
                     * furi_get_tick() returns FreeRTOS ticks (1 ms each).
                     */
                    uint32_t now = furi_get_tick();
                    uint32_t elapsed = now - instance->last_callback_tick;
                    if(elapsed >= SUBGHZ_WORKER_RATE_LIMIT_MS) {
                        if(instance->pair_callback)
                            instance->pair_callback(
                                instance->context,
                                instance->filter_level_duration.level,
                                instance->filter_level_duration.duration);
                        instance->last_callback_tick = now;
                        if(instance->dropped_count > 0) {
                            FURI_LOG_D(
                                TAG,
                                "Rate-limit: %lu samples dropped",
                                (unsigned long)instance->dropped_count);
                            instance->dropped_count = 0;
                        }
                    } else {
                        instance->dropped_count++;
                    }

                    instance->filter_level_duration.duration = duration;
                    instance->filter_level_duration.level = level;
                }
            }
        }
    }

    return 0;
}

SubGhzWorker* subghz_worker_alloc(void) {
    SubGhzWorker* instance = malloc(sizeof(SubGhzWorker));

    instance->thread =
        furi_thread_alloc_ex("SubGhzWorker", 2048, subghz_worker_thread_callback, instance);

    /*
     * WORKER-03 fix: set an explicit thread priority above Normal.
     * SubGhzWorker decodes RF captures in real-time; if it is preempted
     * by UI or other Normal-priority tasks, short pulses can be missed.
     * FuriThreadPriorityHigh (17) is one step above Normal (16) and below
     * Highest (18), leaving headroom for system-critical threads.
     * The UI thread runs at Normal priority, so this does not starve drawing.
     */
    furi_thread_set_priority(instance->thread, FuriThreadPriorityHigh);

    instance->stream =
        furi_stream_buffer_alloc(sizeof(LevelDuration) * 4096, sizeof(LevelDuration));

    //setting default filter in us
    instance->filter_duration = 30;

    /* WORKER-01: initialise rate-limit state */
    instance->last_callback_tick = 0;
    instance->dropped_count = 0;

    return instance;
}

void subghz_worker_free(SubGhzWorker* instance) {
    furi_check(instance);

    furi_stream_buffer_free(instance->stream);
    furi_thread_free(instance->thread);

    free(instance);
}

void subghz_worker_set_overrun_callback(
    SubGhzWorker* instance,
    SubGhzWorkerOverrunCallback callback) {
    furi_check(instance);
    instance->overrun_callback = callback;
}

void subghz_worker_set_pair_callback(SubGhzWorker* instance, SubGhzWorkerPairCallback callback) {
    furi_check(instance);
    instance->pair_callback = callback;
}

void subghz_worker_set_context(SubGhzWorker* instance, void* context) {
    furi_check(instance);
    instance->context = context;
}

void subghz_worker_start(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(!instance->running);

    instance->running = true;

    furi_thread_start(instance->thread);
}

void subghz_worker_stop(SubGhzWorker* instance) {
    furi_check(instance);
    furi_check(instance->running);

    instance->running = false;

    furi_thread_join(instance->thread);
}

bool subghz_worker_is_running(SubGhzWorker* instance) {
    furi_check(instance);
    return instance->running;
}

void subghz_worker_set_filter(SubGhzWorker* instance, uint16_t timeout) {
    furi_check(instance);
    instance->filter_duration = timeout;
}
