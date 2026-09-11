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
 * @brief Next capture node at or after now (same time scale as
 *        rtc_get_timeStamp). Both interval modes share the daily lattice:
 *        normal mode uses timer_trigger.anchor_time (sec-of-day; rolling
 *        window [anchor, anchor+24h) — nodes flow past midnight and the
 *        restart is at the anchor instant, not 00:00), SCHEDULED uses the
 *        closed [start_time, end_time] daily window; ABSOLUTE uses its
 *        config nodes. 0 = capture schedule inactive. Serves the
 *        next_capture_at readout (GET work-mode status); sleep arming goes
 *        through wake_scheduler_next_event instead.
 */
uint64_t wake_scheduler_next_capture(uint64_t now_unix_sec);

/**
 * @brief Collect the distinct (by duty) events in [from, to] — at most ONE
 *        event per duty. Consumers only need "is this duty due" plus a
 *        timestamp to mark; returning every lattice point would starve the
 *        second duty in small caller buffers (1-min capture intervals filled
 *        the buffer with capture events alone and the flush event was
 *        dropped).
 *        Selection prefers the LATEST node that has already ARRIVED
 *        (node <= now_unix_sec) and only falls back to the latest still-
 *        future node in the window when nothing arrived is pending: claiming
 *        a future node while an arrived one waits marks it handled at this
 *        wake, so its own (already-armed) alarm then wakes, judges everything
 *        handled and sleeps without capturing — the last node of every
 *        absolute burst spaced inside the tolerance window was swallowed.
 *        The fallback keeps the early-wake semantics: a U0/RTC wake landing
 *        up to WAKE_TOLERANCE_SEC ahead of the node still fires it
 *        immediately instead of sleeping out the remainder.
 *        Skips events whose due_unix_sec <= last_handled_at[duty].
 * @param now_unix_sec  current wall-clock time (arrival cutoff, NOT the
 *                     window bounds — callers pass [now-60, now+60])
 * @param from_unix_sec  inclusive start
 * @param to_unix_sec    inclusive end
 * @param out_events     caller-provided buffer
 * @param max_events     buffer capacity
 * @return number of events written (0..max_events)
 */
int wake_scheduler_due_events(uint64_t now_unix_sec,
                              uint64_t from_unix_sec,
                              uint64_t to_unix_sec,
                              wake_event_t *out_events,
                              int max_events);

/**
 * @brief Persist that a duty was handled at the given absolute time.
 *        Subsequent due_events lookups will treat any event with
 *        due_unix_sec <= at_unix_sec as already-handled.
 * @note RAM-only until the next wake_scheduler_flush_state(): the mark takes
 *       effect (dedup) immediately in-process; callers flush right after
 *       processing so the state survives a power cut.
 */
void wake_scheduler_mark_handled(wake_duty_t duty, uint64_t at_unix_sec);

/**
 * @brief Query whether the duty's node at `at_unix_sec` is already marked
 *        handled (due <= handled marker). Used by the RTC job-chain
 *        callbacks: a U0 wake inside the ±WAKE_TOLERANCE_SEC window may have
 *        taken the node EARLY; if the device then stays awake past the node
 *        the chain job fires anyway — this query suppresses that duplicate.
 *        A marker sitting ahead of the queried node (backward clock step) is
 *        healed (cleared) rather than trusted, so a poison marker cannot
 *        silently suppress captures.
 */
aicam_bool_t wake_scheduler_is_handled(wake_duty_t duty, uint64_t at_unix_sec);

/**
 * @brief Write the deferred mark_handled state to NVS, if dirty.
 *        Coalesces one wake cycle's capture + flush marks into a single
 *        NVS append.
 */
void wake_scheduler_flush_state(void);

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
