/**
 * @file wake_scheduler.h
 * @brief Unified next-wake / due-event lookup over capture and upload schedules.
 *
 * Two time-tables feed into one merged view:
 *   - timer_trigger      (work_mode_config_t.timer_trigger)        -> WAKE_DUTY_CAPTURE
 *   - capture upload     (capture_upload_config_t.schedule_minutes) -> WAKE_DUTY_UPLOAD_FLUSH
 *
 * Used in two places:
 *   - Before sleep: compute the minimum sleep_second so U0 wakes us at the
 *     next event. No "intent" is sent to U0 — semantic is recovered on wake.
 *   - On wake (RTC): look up which duties fall in [now-tolerance, now+tolerance]
 *     and execute them. NVS-backed last_handled_at prevents double-fire.
 */

#ifndef WAKE_SCHEDULER_H
#define WAKE_SCHEDULER_H

#include "aicam_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tolerance window around the scheduled point (seconds). */
#define WAKE_TOLERANCE_SEC          60u

/** Hard upper bound for next_event lookups — keeps cold-start safe. */
#define WAKE_SCHED_HORIZON_MAX_SEC  (7u * 24u * 3600u)

typedef enum {
    WAKE_DUTY_CAPTURE       = 0,  /**< from work_mode_config_t.timer_trigger     */
    WAKE_DUTY_UPLOAD_FLUSH  = 1,  /**< from capture_upload_config_t.schedule_min */
    WAKE_DUTY_MAX
} wake_duty_t;

typedef struct {
    wake_duty_t duty;
    uint64_t    due_unix_sec;     /**< absolute wall-clock time in seconds */
} wake_event_t;

/**
 * @brief Return the earliest event in (now, now + horizon].
 * @param now_unix_sec  current wall-clock time
 * @param horizon_sec   look-ahead window in seconds (clamped to MAX)
 * @param out           filled with the earliest event (may be NULL)
 * @return due_unix_sec, or 0 if no event in horizon
 */
uint64_t wake_scheduler_next_event(uint64_t now_unix_sec,
                                   uint32_t horizon_sec,
                                   wake_event_t *out);

/**
 * @brief Next scheduled upload-flush time only (not capture).
 *        Returns a fixed time-of-day (anchored to midnight), so using it as an
 *        alarm does NOT drift (unlike compute_next_capture's now+interval).
 *        Used by enter_sleep_mode to wake for flush nodes that fall between
 *        capture intervals. 0 if SCHEDULED mode not active or no nodes.
 */
uint64_t wake_scheduler_next_flush(uint64_t now_unix_sec);

/**
 * @brief Collect all distinct (by duty) events in [from, to].
 *        Skips events whose due_unix_sec <= last_handled_at[duty].
 * @param from_unix_sec  inclusive start
 * @param to_unix_sec    inclusive end
 * @param out_events     caller-provided buffer
 * @param max_events     buffer capacity
 * @return number of events written (0..max_events)
 */
int wake_scheduler_due_events(uint64_t from_unix_sec,
                              uint64_t to_unix_sec,
                              wake_event_t *out_events,
                              int max_events);

/**
 * @brief Persist that a duty was handled at the given absolute time.
 *        Subsequent due_events lookups will treat any event with
 *        due_unix_sec <= at_unix_sec as already-handled.
 */
void wake_scheduler_mark_handled(wake_duty_t duty, uint64_t at_unix_sec);

/**
 * @brief Clear persisted last_handled_at state. Call this after RTC stepping
 *        (NTP / manual time change).
 */
void wake_scheduler_reset_state(void);

/**
 * @brief Hint that the configuration changed and any cached next_event should
 *        be re-derived. The current implementation is stateless so this is a
 *        no-op, but callers should still call it for forward-compat.
 */
void wake_scheduler_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* WAKE_SCHEDULER_H */
