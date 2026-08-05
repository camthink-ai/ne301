/**
 * @file wake_scheduler.c
 * @brief Merged next-event / due-event lookup over capture and upload schedules.
 */

#include "wake_scheduler.h"

#include "drtc.h"
#include "json_config_mgr.h"
#include "debug.h"
#include "storage.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ==================== Persistent state ==================== */

#define WAKE_STATE_MAGIC   0x57414B45u  /* 'W' 'A' 'K' 'E' */
#define WAKE_STATE_NVS_KEY "wake/state"

typedef struct {
    uint32_t magic;
    uint64_t capture_handled_at;
    uint64_t flush_handled_at;
} wake_state_nvs_t;

static aicam_bool_t  s_state_loaded = AICAM_FALSE;
static wake_state_nvs_t s_state = { .magic = WAKE_STATE_MAGIC };

static void load_state_if_needed(void)
{
    if (s_state_loaded) return;
    wake_state_nvs_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    /* storage_nvs_read returns bytes read (>0) on success, negative -ERRNO on
     * failure / key-absent. */
    int ret = storage_nvs_read(NVS_USER, WAKE_STATE_NVS_KEY, &tmp, sizeof(tmp));
    if (ret > 0 && tmp.magic == WAKE_STATE_MAGIC) {
        s_state = tmp;
    } else {
        s_state.magic = WAKE_STATE_MAGIC;
        s_state.capture_handled_at = 0;
        s_state.flush_handled_at = 0;
    }
    s_state_loaded = AICAM_TRUE;
}

static void persist_state(void)
{
    s_state.magic = WAKE_STATE_MAGIC;
    /* storage_nvs_write returns bytes written (>0) on success, negative on
     * failure. */
    int ret = storage_nvs_write(NVS_USER, WAKE_STATE_NVS_KEY, &s_state, sizeof(s_state));
    if (ret < 0) {
        LOG_CORE_WARN("wake_scheduler: persist state failed (%d)", ret);
    }
}

static uint64_t get_handled_at(wake_duty_t duty)
{
    load_state_if_needed();
    switch (duty) {
    case WAKE_DUTY_CAPTURE:      return s_state.capture_handled_at;
    case WAKE_DUTY_UPLOAD_FLUSH: return s_state.flush_handled_at;
    default:                     return 0;
    }
}

/* ==================== Capture (timer_trigger) next/due math ==================== */

static uint32_t next_scheduled_interval(uint32_t start_time_sec,
                                        uint32_t interval_sec,
                                        uint32_t now_sec)
{
    if (interval_sec == 0) return start_time_sec;
    if (now_sec < start_time_sec) return start_time_sec;
    uint32_t cycle_now = now_sec - start_time_sec;
    uint32_t cycle_next = (cycle_now / interval_sec + 1) * interval_sec;
    return start_time_sec + cycle_next;
}

/**
 * Compute next capture-trigger absolute time given a "now" timestamp.
 * Mirrors system_controller_get_next_capture_at() but is config-driven so it
 * can be called from the scheduler without a controller handle.
 */
static uint64_t compute_next_capture(uint64_t now_unix_sec)
{
    work_mode_config_t cfg;
    if (json_config_get_work_mode_config(&cfg) != AICAM_OK) return 0;
    if (!cfg.timer_trigger.enable) return 0;

    /* Derive seconds-of-day from now_unix_sec without calling rtc_get_time() again,
     * so callers can pass an externally chosen "now" (e.g. in tests). */
    RTC_TIME_S now_rtc = rtc_get_time();
    uint64_t now_ts = rtc_get_timeStamp();
    uint32_t now_sec = now_rtc.hour * 3600 + now_rtc.minute * 60 + now_rtc.second;

    /* If caller passed a "now" newer than RTC, prefer RTC for sec-of-day math. */
    (void)now_unix_sec;

    const timer_trigger_config_t *tc = &cfg.timer_trigger;
    uint64_t midnight_ts = (now_ts > now_sec) ? (now_ts - now_sec) : 0;
    uint64_t out = 0;

    switch (tc->capture_mode) {
    case AICAM_TIMER_CAPTURE_MODE_INTERVAL:
        if (tc->interval_mode == AICAM_TIMER_INTERVAL_MODE_SCHEDULED) {
            uint32_t next = next_scheduled_interval(tc->start_time, tc->interval_sec, now_sec);
            if (next <= now_sec) {
                out = midnight_ts + 86400u + next;
            } else {
                out = midnight_ts + next;
            }
        } else if (tc->interval_sec > 0) {
            out = now_ts + tc->interval_sec;
        }
        break;
    case AICAM_TIMER_CAPTURE_MODE_ABSOLUTE: {
        if (tc->time_node_count == 0) break;
        uint32_t earliest = UINT32_MAX;
        for (uint32_t i = 0; i < tc->time_node_count && i < 10; i++) {
            if (tc->time_node[i] > now_sec && tc->time_node[i] < earliest) {
                earliest = tc->time_node[i];
            }
        }
        if (earliest != UINT32_MAX) {
            out = midnight_ts + earliest;
        } else {
            /* All past today → first node tomorrow */
            out = midnight_ts + 86400u + tc->time_node[0];
        }
        break;
    }
    default: break;
    }
    return out;
}

/**
 * Collect all capture-trigger absolute times that fall in [from, to].
 * For INTERVAL with interval_sec, the events are k*interval_sec offsets — emit
 * up to `max` of them. For ABSOLUTE/SCHEDULED, emit each time_node in the window.
 */
static int collect_capture_in_range(uint64_t from_unix_sec, uint64_t to_unix_sec,
                                    uint64_t *out_times, int max)
{
    if (max <= 0 || from_unix_sec > to_unix_sec) return 0;

    work_mode_config_t cfg;
    if (json_config_get_work_mode_config(&cfg) != AICAM_OK) return 0;
    if (!cfg.timer_trigger.enable) return 0;

    RTC_TIME_S now_rtc = rtc_get_time();
    uint64_t now_ts = rtc_get_timeStamp();
    uint32_t now_sec = now_rtc.hour * 3600 + now_rtc.minute * 60 + now_rtc.second;
    uint64_t midnight_ts = (now_ts > now_sec) ? (now_ts - now_sec) : 0;

    const timer_trigger_config_t *tc = &cfg.timer_trigger;
    int n = 0;

    /* RTC not set yet (timestamp=0) → can't compute schedule, bail out.
     * Without this, `base = now_ts - interval` underflows to a huge uint64
     * and the while-loop below spins ~2^64/interval times → hang. */
    if (now_ts == 0) return 0;

    switch (tc->capture_mode) {
    case AICAM_TIMER_CAPTURE_MODE_INTERVAL: {
        if (tc->interval_sec == 0) break;
        uint64_t base = (tc->interval_mode == AICAM_TIMER_INTERVAL_MODE_SCHEDULED)
                            ? (midnight_ts + tc->start_time)
                            : (now_ts - tc->interval_sec); /* arbitrary anchor */
        if (base > from_unix_sec) {
            /* step backwards to ≤ from. Cap iterations to avoid runaway
             * loop if base underflowed or interval is tiny. */
            int back_iter = 0;
            while (base > from_unix_sec && base >= tc->interval_sec && back_iter < 10000) {
                base -= tc->interval_sec;
                back_iter++;
            }
            if (back_iter >= 10000) break;   /* safety: give up rather than hang */
        }
        /* step forward to first ≥ from */
        int fwd_iter = 0;
        while (base < from_unix_sec && fwd_iter < 10000) {
            base += tc->interval_sec;
            fwd_iter++;
        }
        while (base <= to_unix_sec && n < max) {
            out_times[n++] = base;
            base += tc->interval_sec;
        }
        break;
    }
    case AICAM_TIMER_CAPTURE_MODE_ABSOLUTE: {
        /* Check today's nodes and tomorrow's first node, broaden if window > 1 day. */
        for (uint32_t i = 0; i < tc->time_node_count && i < 10 && n < max; i++) {
            uint64_t t_today = midnight_ts + tc->time_node[i];
            if (t_today >= from_unix_sec && t_today <= to_unix_sec) {
                out_times[n++] = t_today;
            }
            uint64_t t_tomorrow = t_today + 86400u;
            if (t_tomorrow >= from_unix_sec && t_tomorrow <= to_unix_sec && n < max) {
                out_times[n++] = t_tomorrow;
            }
        }
        break;
    }
    default: break;
    }
    return n;
}

/* ==================== Upload (capture_upload.schedule_minutes) next/due ==================== */

static uint64_t compute_next_upload_flush(uint64_t now_unix_sec)
{
    capture_upload_config_t cfg;
    if (json_config_get_capture_upload_config(&cfg) != AICAM_OK) return 0;
    if (cfg.mode != CAPTURE_MODE_SCHEDULED) return 0;
    if (cfg.schedule_node_count == 0) return 0;

    RTC_TIME_S now_rtc = rtc_get_time();
    uint64_t now_ts = rtc_get_timeStamp();
    uint32_t now_sec_of_day = now_rtc.hour * 3600 + now_rtc.minute * 60 + now_rtc.second;
    uint64_t midnight_ts = (now_ts > now_sec_of_day) ? (now_ts - now_sec_of_day) : 0;
    (void)now_unix_sec;

    uint32_t earliest_today = UINT32_MAX;
    uint32_t earliest_any   = UINT32_MAX;
    for (uint8_t i = 0; i < cfg.schedule_node_count && i < CAPTURE_SCHEDULE_MAX_NODES; i++) {
        uint32_t sec = (uint32_t)cfg.schedule_minutes[i] * 60u;
        if (sec > now_sec_of_day && sec < earliest_today) earliest_today = sec;
        if (sec < earliest_any) earliest_any = sec;
    }

    if (earliest_today != UINT32_MAX) return midnight_ts + earliest_today;
    if (earliest_any   != UINT32_MAX) return midnight_ts + 86400u + earliest_any;
    return 0;
}

static int collect_upload_in_range(uint64_t from_unix_sec, uint64_t to_unix_sec,
                                   uint64_t *out_times, int max)
{
    if (max <= 0 || from_unix_sec > to_unix_sec) return 0;

    capture_upload_config_t cfg;
    if (json_config_get_capture_upload_config(&cfg) != AICAM_OK) return 0;
    if (cfg.mode != CAPTURE_MODE_SCHEDULED) return 0;
    if (cfg.schedule_node_count == 0) return 0;

    RTC_TIME_S now_rtc = rtc_get_time();
    uint64_t now_ts = rtc_get_timeStamp();
    uint32_t now_sec_of_day = now_rtc.hour * 3600 + now_rtc.minute * 60 + now_rtc.second;
    uint64_t midnight_ts = (now_ts > now_sec_of_day) ? (now_ts - now_sec_of_day) : 0;

    int n = 0;
    for (uint8_t i = 0; i < cfg.schedule_node_count && i < CAPTURE_SCHEDULE_MAX_NODES && n < max; i++) {
        uint32_t sec_of_day = (uint32_t)cfg.schedule_minutes[i] * 60u;
        uint64_t t_today    = midnight_ts + sec_of_day;
        uint64_t t_tomorrow = t_today + 86400u;
        uint64_t t_yesterday = (t_today >= 86400u) ? (t_today - 86400u) : 0;
        if (t_yesterday >= from_unix_sec && t_yesterday <= to_unix_sec) out_times[n++] = t_yesterday;
        if (n < max && t_today >= from_unix_sec && t_today <= to_unix_sec) out_times[n++] = t_today;
        if (n < max && t_tomorrow >= from_unix_sec && t_tomorrow <= to_unix_sec) out_times[n++] = t_tomorrow;
    }
    return n;
}

/* ==================== Public API ==================== */

uint64_t wake_scheduler_next_flush(uint64_t now_unix_sec)
{
    uint64_t t = compute_next_upload_flush(now_unix_sec);
    if (t > 0) {
        uint64_t handled = get_handled_at(WAKE_DUTY_UPLOAD_FLUSH);
        /* If the earliest flush node was already handled THIS wake (e.g. capture
         * and flush coincided - the wake handler drained the flush), skip it so
         * we don't redundantly wake for it again on the next sleep. Return 0 so
         * the rtc capture alarm handles the next wake; the NEXT flush node will
         * be picked up once its time arrives (compute_next_upload_flush returns
         * nodes strictly after now, so once this node's time passes, the next
         * one is returned automatically). */
        if (handled > 0 && t <= handled) {
            return 0;
        }
    }
    return t;
}

uint64_t wake_scheduler_next_event(uint64_t now_unix_sec, uint32_t horizon_sec, wake_event_t *out)
{
    if (horizon_sec == 0 || horizon_sec > WAKE_SCHED_HORIZON_MAX_SEC) {
        horizon_sec = WAKE_SCHED_HORIZON_MAX_SEC;
    }
    uint64_t horizon_end = now_unix_sec + horizon_sec;

    uint64_t t_cap   = compute_next_capture(now_unix_sec);
    uint64_t t_flush = compute_next_upload_flush(now_unix_sec);

    /* drop events past horizon */
    if (t_cap   > horizon_end) t_cap   = 0;
    if (t_flush > horizon_end) t_flush = 0;
    /* drop events not strictly in the future */
    if (t_cap   != 0 && t_cap   <= now_unix_sec) t_cap   = 0;
    if (t_flush != 0 && t_flush <= now_unix_sec) t_flush = 0;

    uint64_t earliest = 0;
    wake_duty_t duty = WAKE_DUTY_MAX;

    if (t_cap != 0 && (earliest == 0 || t_cap < earliest)) {
        earliest = t_cap;
        duty = WAKE_DUTY_CAPTURE;
    }
    if (t_flush != 0 && (earliest == 0 || t_flush < earliest)) {
        earliest = t_flush;
        duty = WAKE_DUTY_UPLOAD_FLUSH;
    }

    if (out) {
        out->duty = duty;
        out->due_unix_sec = earliest;
    }
    return earliest;
}

int wake_scheduler_due_events(uint64_t from_unix_sec, uint64_t to_unix_sec,
                              wake_event_t *out_events, int max_events)
{
    if (!out_events || max_events <= 0) return 0;
    if (from_unix_sec > to_unix_sec) return 0;

    uint64_t cap_handled = get_handled_at(WAKE_DUTY_CAPTURE);
    uint64_t flu_handled = get_handled_at(WAKE_DUTY_UPLOAD_FLUSH);

    int n = 0;

    /* Capture events */
    uint64_t cap_times[8];
    int n_cap = collect_capture_in_range(from_unix_sec, to_unix_sec, cap_times, 8);
    for (int i = 0; i < n_cap && n < max_events; i++) {
        if (cap_times[i] <= cap_handled) continue;
        out_events[n].duty = WAKE_DUTY_CAPTURE;
        out_events[n].due_unix_sec = cap_times[i];
        n++;
    }

    /* Upload-flush events */
    uint64_t flu_times[8];
    int n_flu = collect_upload_in_range(from_unix_sec, to_unix_sec, flu_times, 8);
    for (int i = 0; i < n_flu && n < max_events; i++) {
        if (flu_times[i] <= flu_handled) continue;
        out_events[n].duty = WAKE_DUTY_UPLOAD_FLUSH;
        out_events[n].due_unix_sec = flu_times[i];
        n++;
    }

    /* simple insertion sort by due time (n is small) */
    for (int i = 1; i < n; i++) {
        wake_event_t tmp = out_events[i];
        int j = i - 1;
        while (j >= 0 && out_events[j].due_unix_sec > tmp.due_unix_sec) {
            out_events[j + 1] = out_events[j];
            j--;
        }
        out_events[j + 1] = tmp;
    }
    return n;
}

void wake_scheduler_mark_handled(wake_duty_t duty, uint64_t at_unix_sec)
{
    load_state_if_needed();
    switch (duty) {
    case WAKE_DUTY_CAPTURE:
        if (at_unix_sec > s_state.capture_handled_at) s_state.capture_handled_at = at_unix_sec;
        break;
    case WAKE_DUTY_UPLOAD_FLUSH:
        if (at_unix_sec > s_state.flush_handled_at)   s_state.flush_handled_at   = at_unix_sec;
        break;
    default: return;
    }
    persist_state();
}

void wake_scheduler_reset_state(void)
{
    s_state.magic = WAKE_STATE_MAGIC;
    s_state.capture_handled_at = 0;
    s_state.flush_handled_at = 0;
    s_state_loaded = AICAM_TRUE;
    persist_state();
    LOG_CORE_INFO("wake_scheduler: state reset");
}

void wake_scheduler_invalidate(void)
{
    /* stateless lookup — nothing to invalidate today. */
}
