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
#include <time.h>

/* ==================== Persistent state ==================== */

#define WAKE_STATE_MAGIC   0x57414B45u  /* 'W' 'A' 'K' 'E' */
#define WAKE_STATE_NVS_KEY "wake/state"

/* Persisted last-handled markers only — the interval-capture grid phase now
 * lives in the work-mode config (timer_trigger.anchor_time / start_time),
 * so this state is purely a dedup high-water mark, never a schedule anchor. */
typedef struct {
    uint32_t magic;
    uint64_t capture_handled_at;
    uint64_t flush_handled_at;
} wake_state_nvs_t;

static aicam_bool_t  s_state_loaded = AICAM_FALSE;
static aicam_bool_t  s_state_dirty  = AICAM_FALSE;
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

/* A handled marker must never sit ahead of the due window: marks are only
 * ever made at points <= window end. A marker ahead of the clock can only
 * come from a backward clock step (the marker stayed on the old scale) or a
 * corrupted write — left alone it suppresses every event until wall time
 * passes it, silently killing both schedules (mark_handled never moves
 * backwards to self-heal). Detect and clear it here; every lookup funnels
 * through this, so the poison cannot outlive one query. */
static uint64_t healed_handled_at(wake_duty_t duty, uint64_t window_end_unix_sec)
{
    uint64_t m = get_handled_at(duty);
    if (m == 0 || m <= window_end_unix_sec) return m;

    load_state_if_needed();
    if (duty == WAKE_DUTY_CAPTURE)      s_state.capture_handled_at = 0;
    if (duty == WAKE_DUTY_UPLOAD_FLUSH) s_state.flush_handled_at = 0;
    LOG_CORE_WARN("wake_scheduler: duty %d marker %lu ahead of clock - cleared",
                  (int)duty, (unsigned long)m);
    persist_state();
    s_state_dirty = AICAM_FALSE;   /* any pending marks were just persisted */
    return 0;
}

/* ==================== Capture (timer_trigger) next/due math ==================== */

/* Daily capture window [start_time, end_time], both seconds-of-day.
 * Non-full-day windows are CLOSED: a grid node landing exactly on
 * end_time is captured (8:00->8:10 with a 5min interval yields 8:00,
 * 8:05 AND 8:10). Expressed as the raw span + 1 so every consumer's
 * strict `<` comparison includes the end second.
 *   end_time == start_time   -> full-day 24h lattice — NORMAL MODE'S
 *                               internal representation (it fabricates
 *                               end = start); the scheduled-mode web API
 *                               rejects equal start/end, where a full day
 *                               is 00:00-23:59 (closed). Stays HALF-open
 *                               at exactly 86400 so adjacent days tile
 *                               without a shared boundary node
 *   end_time >  start_time   -> same-day window (e.g. 08:00 -> 18:00)
 *   end_time <  start_time   -> window wraps past midnight (e.g. 20:00 -> 06:00;
 *                               00:00 with start > 00:00 ends at midnight)
 * Returns the inclusive window length in seconds, 1..86400. */
static uint32_t scheduled_window_len(uint32_t start_time_sec, uint32_t end_time_sec)
{
    if (end_time_sec == start_time_sec) return 86400u;
    if (end_time_sec > start_time_sec) return end_time_sec - start_time_sec + 1u;
    return end_time_sec + 86400u - start_time_sec + 1u;
}

/* Next SCHEDULED node strictly after now_sec, as that node's seconds-of-day
 * (belongs to the next calendar day when the window wraps midnight). The
 * lattice restarts at start_time every day (same semantics as the RTC job
 * chain in calculate_next_scheduled_interval_trigger). When today's window
 * is exhausted, returns start_time_sec — callers detect the wrap via
 * "returned value <= now_sec" and move it to tomorrow. */
static uint32_t next_scheduled_interval(uint32_t start_time_sec,
                                        uint32_t end_time_sec,
                                        uint32_t interval_sec,
                                        uint32_t now_sec)
{
    if (interval_sec == 0) return start_time_sec;
    uint32_t window = scheduled_window_len(start_time_sec, end_time_sec);
    /* Offset from the most recent window start — handles windows that wrap
     * past midnight: 02:00 is 6h into a 20:00->06:00 window. */
    uint32_t cyc = (now_sec + 86400u - start_time_sec) % 86400u;
    uint32_t nxt = cyc - (cyc % interval_sec) + interval_sec;
    if (nxt >= window) return start_time_sec;   /* window exhausted -> tomorrow's start */
    return (start_time_sec + nxt) % 86400u;
}

/* Resolve the daily lattice parameters for INTERVAL mode. Both interval
 * modes share one formula: nodes are start + k*interval inside the window
 * [start, start+window) and the lattice restarts at `start` every day.
 *   scheduled mode: start = start_time, end = end_time (end == start = the
 *                   explicit full-day setting; window may wrap past midnight)
 *   normal mode:    start = end = anchor_time (always a full-day window —
 *                   anchor 11:00 with a 5h interval yields 11:00 16:00
 *                   21:00 02:00 07:00, then restarts at 11:00, never 12:00)
 * Returns 0 when the normal-mode anchor hasn't been stamped yet (caller
 * falls back to the handled-marker lattice). */
static aicam_bool_t interval_lattice_params(const timer_trigger_config_t *tc,
                                            uint32_t *start_sec, uint32_t *end_sec)
{
    if (tc->interval_mode == AICAM_TIMER_INTERVAL_MODE_SCHEDULED) {
        *start_sec = tc->start_time;
        *end_sec = tc->end_time;
        return AICAM_TRUE;
    }
    if (tc->anchor_time == 0) return AICAM_FALSE;
    *start_sec = tc->anchor_time;
    *end_sec = *start_sec;
    return AICAM_TRUE;
}

/* Local weekday of a node timestamp, in the timer config's encoding:
 * 0=Monday ... 6=Sunday — the same math scheduler_manager's REPEAT_WEEKLY
 * trigger uses ((tm_wday + 6) % 7 on the timezone-shifted clock), so the
 * low-power path and the active RTC path always agree about which day a
 * timestamp lands on. `t` is on the RTC scale (midnight_ts + seconds-of-day). */
static int weekday_idx_of(uint64_t t)
{
    time_t local = (time_t)((int64_t)t + (int64_t)rtc_get_timezone() * 3600);
    struct tm tm;
    localtime_r(&local, &tm);
    return (tm.tm_wday + 6) % 7;
}

/* Does time node `node` fire on weekday `wd` (0=Mon..6=Sun)? weekdays[] is
 * per-node: 0 = every day, 1=Monday ... 7=Sunday — mirror of
 * map_weekdays_to_bits()/REPEAT_WEEKLY registration in system_service. */
static aicam_bool_t node_fires_on_weekday(const timer_trigger_config_t *tc,
                                          uint32_t node, int wd)
{
    if (node >= tc->time_node_count) return AICAM_FALSE;
    uint8_t sel = tc->weekdays[node];
    if (sel == 0) return AICAM_TRUE;
    return (wd == (int)(sel - 1)) ? AICAM_TRUE : AICAM_FALSE;
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
    case AICAM_TIMER_CAPTURE_MODE_INTERVAL: {
        uint32_t start_sec, end_sec;
        if (tc->interval_sec > 0 &&
            interval_lattice_params(tc, &start_sec, &end_sec)) {
            uint32_t next = next_scheduled_interval(start_sec, end_sec,
                                                    tc->interval_sec, now_sec);
            if (next <= now_sec) {
                out = midnight_ts + 86400u + next;
            } else {
                out = midnight_ts + next;
            }
        } else if (tc->interval_sec > 0) {
            /* Anchor not stamped yet (config apply hasn't run — e.g. this
             * lookup raced ahead of system_service_start): fall back to
             * the last-REAL-capture marker, then to now+interval. */
            uint64_t last = get_handled_at(WAKE_DUTY_CAPTURE);
            out = (last != 0) ? (last + tc->interval_sec) : 0;
            if (out <= now_ts || out == 0) {
                out = now_ts + tc->interval_sec;
            }
        }
        break;
    }
    case AICAM_TIMER_CAPTURE_MODE_ABSOLUTE: {
        if (tc->time_node_count == 0) break;
        /* Walk forward day by day (a week always covers every weekday) and
         * take the earliest node that both fires on that day's weekday and
         * is still ahead of now. The active path registers absolute nodes
         * REPEAT_WEEKLY with map_weekdays_to_bits(); arming sleep without
         * the same filter would wake the device on non-selected days and
         * turn a weekly schedule into a daily one. */
        int wd_today = weekday_idx_of(midnight_ts);
        for (int d = 0; d < 7; d++) {
            int wd = (wd_today + d) % 7;
            uint32_t earliest = UINT32_MAX;
            for (uint32_t i = 0; i < tc->time_node_count && i < 10; i++) {
                if (!node_fires_on_weekday(tc, i, wd)) continue;
                if (d == 0 && tc->time_node[i] <= now_sec) continue;
                if (tc->time_node[i] < earliest) earliest = tc->time_node[i];
            }
            if (earliest != UINT32_MAX) {
                out = midnight_ts + (uint64_t)d * 86400u + earliest;
                break;
            }
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
        uint32_t start_sec, end_sec;
        if (interval_lattice_params(tc, &start_sec, &end_sec)) {
            /* Daily timetable: anchor + k*interval inside [anchor,
             * anchor+window), where anchor is the window start owning the
             * query (today's, or yesterday's when today's is still ahead) —
             * shared by both interval modes
             * (normal mode passes end=start -> full day; for non-full-day
             * windows the inclusive window length bakes in the CLOSED end:
             * a node landing exactly on end_time is emitted). The window may wrap
             * past midnight (20:00->06:00 spans into tomorrow). After the
             * day's last point the next node is TOMORROW'S start — the
             * lattice restarts daily, so no phantom points land between
             * the window end and the next start. Because the grid lives in
             * the persisted config, unrelated wakes (upload flush, PIR) and
             * reboots can never re-base it to "wake time + interval". */
            uint32_t window = scheduled_window_len(start_sec, end_sec);
            /* Anchor on the window instance that OWNS the query start: when
             * today's start is still ahead of `from`, the live instance
             * opened YESTERDAY — a wrapping window (20:00->06:00) queried
             * during its post-midnight leg, or a full-day lattice queried
             * in the pre-anchor hours (anchor 11:00 owns the 02:00/07:00
             * nodes of the instance that opened yesterday 11:00). The
             * lattice phase repeats daily, so roll the anchor back one day
             * instead of enumerating today's future instance, whose nodes
             * all sit past `to` and would hide the due ones. */
            uint64_t anchor = midnight_ts + start_sec;
            if (anchor > from_unix_sec && anchor >= 86400u) anchor -= 86400u;
            uint64_t win_end = anchor + window;
            uint64_t p = anchor;
            if (p < from_unix_sec) {
                uint64_t span = from_unix_sec - p;
                p += (span / tc->interval_sec) * tc->interval_sec;
                if (p < from_unix_sec) p += tc->interval_sec;
            }
            while (p < win_end && p <= to_unix_sec && n < max) {
                out_times[n++] = p;
                p += tc->interval_sec;
            }
            /* First node after the enumerated instance: the next day's
             * window start (no phantom points land between window end and
             * the next start). */
            uint64_t t_next_day = anchor + 86400u;
            if (t_next_day >= from_unix_sec && t_next_day <= to_unix_sec && n < max) {
                out_times[n++] = t_next_day;
            }
            break;
        }
        /* Anchor not stamped yet: fall back to the last-REAL-capture
         * marker, then to now-anchored. */
        uint64_t last = get_handled_at(WAKE_DUTY_CAPTURE);
        uint64_t base = (last != 0) ? last : (now_ts - tc->interval_sec);
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
        int wd_today = weekday_idx_of(midnight_ts);
        for (uint32_t i = 0; i < tc->time_node_count && i < 10 && n < max; i++) {
            uint64_t t_today = midnight_ts + tc->time_node[i];
            /* Per-node weekday filter (weekdays[i]: 0=all, 1=Mon..7=Sun) —
             * without it the wake handler would judge a Wednesday-only node
             * due on every day and capture on non-selected ones. Today and
             * tomorrow are checked against their own weekdays. */
            if (node_fires_on_weekday(tc, i, wd_today) &&
                t_today >= from_unix_sec && t_today <= to_unix_sec) {
                out_times[n++] = t_today;
            }
            uint64_t t_tomorrow = t_today + 86400u;
            if (node_fires_on_weekday(tc, i, (wd_today + 1) % 7) &&
                t_tomorrow >= from_unix_sec && t_tomorrow <= to_unix_sec && n < max) {
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
        /* Heal reference = now + tolerance: markers are made at due nodes
         * that may sit up to WAKE_TOLERANCE_SEC ahead of the clock, so a
         * plain `now` would falsely clear a mark made seconds ago (sleep
         * entered right after the wake handler marked a node in the forward
         * half of the tolerance window) and the flush would re-fire. */
        uint64_t handled = healed_handled_at(WAKE_DUTY_UPLOAD_FLUSH,
                                             now_unix_sec + WAKE_TOLERANCE_SEC);
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

uint64_t wake_scheduler_next_capture(uint64_t now_unix_sec)
{
    /* First grid/node point >= now, from the same mode-aware math as the due
     * lookup (anchor-based interval lattice, SCHEDULED daily timetable,
     * ABSOLUTE nodes) — feeds the GET next_capture_at readout (sleep
     * arming uses wake_scheduler_next_event / the cyc-based
     * compute_next_capture instead). */
    uint64_t t = 0;
    if (collect_capture_in_range(now_unix_sec,
                                 now_unix_sec + WAKE_SCHED_HORIZON_MAX_SEC,
                                 &t, 1) > 0) {
        /* Marker ahead of the clock (backward-step edge): the "next" point
         * is already handled — don't re-anchor the job into the past. */
        if (t <= healed_handled_at(WAKE_DUTY_CAPTURE,
                                   now_unix_sec + WAKE_SCHED_HORIZON_MAX_SEC)) {
            return 0;
        }
        return t;
    }
    return 0;
}

int wake_scheduler_due_events(uint64_t now_unix_sec,
                              uint64_t from_unix_sec, uint64_t to_unix_sec,
                              wake_event_t *out_events, int max_events)
{
    if (!out_events || max_events <= 0) return 0;
    if (from_unix_sec > to_unix_sec) return 0;

    uint64_t cap_handled = healed_handled_at(WAKE_DUTY_CAPTURE, to_unix_sec);
    uint64_t flu_handled = healed_handled_at(WAKE_DUTY_UPLOAD_FLUSH, to_unix_sec);

    /* One event PER DUTY (the header contract): every consumer only needs to
     * know whether a duty falls in the window plus one timestamp to mark
     * handled. Emitting every lattice point starves the second duty once the
     * first fills the caller's small buffer — e.g. a 1-minute capture
     * interval keeps >=2 capture events inside the ±60s poll window, the
     * WAKE_DUTY_MAX-sized buffer filled with capture events alone, and the
     * flush event was silently dropped: scheduled upload never fired in
     * full-speed mode.
     *
     * Within the window prefer the LATEST node that has already ARRIVED
     * (node <= now) and fall back to the latest future node only when
     * nothing arrived is pending. Taking the plain latest claimed the
     * NEXT node at every wake (mark_handled is a monotonic maximum), so a
     * burst of absolute nodes spaced inside the tolerance window lost its
     * tail: 14:29/14:30/14:31 captures + a 14:30 flush produced exactly two
     * photos — the 14:31 alarm woke, found every node <= the marker taken by
     * the 14:30 wake, and slept without capturing. The fallback preserves
     * the early-wake semantics (U0/RTC wake up to WAKE_TOLERANCE_SEC ahead
     * of the node still fires it instead of sleeping out the remainder). */
    uint64_t cap_due = 0, cap_fut = 0;
    uint64_t flu_due = 0, flu_fut = 0;

    uint64_t cap_times[8];
    int n_cap = collect_capture_in_range(from_unix_sec, to_unix_sec, cap_times, 8);
    for (int i = 0; i < n_cap; i++) {
        if (cap_times[i] <= cap_handled) continue;
        if (cap_times[i] > cap_fut) cap_fut = cap_times[i];
        if (cap_times[i] <= now_unix_sec && cap_times[i] > cap_due) cap_due = cap_times[i];
    }
    if (cap_due == 0) cap_due = cap_fut;

    uint64_t flu_times[8];
    int n_flu = collect_upload_in_range(from_unix_sec, to_unix_sec, flu_times, 8);
    for (int i = 0; i < n_flu; i++) {
        if (flu_times[i] <= flu_handled) continue;
        if (flu_times[i] > flu_fut) flu_fut = flu_times[i];
        if (flu_times[i] <= now_unix_sec && flu_times[i] > flu_due) flu_due = flu_times[i];
    }
    if (flu_due == 0) flu_due = flu_fut;

    int n = 0;
    if (cap_due != 0 && n < max_events) {
        out_events[n].duty = WAKE_DUTY_CAPTURE;
        out_events[n].due_unix_sec = cap_due;
        n++;
    }
    if (flu_due != 0 && n < max_events) {
        out_events[n].duty = WAKE_DUTY_UPLOAD_FLUSH;
        out_events[n].due_unix_sec = flu_due;
        n++;
    }

    /* Verdict line for field diagnosis: which points were judged due, and
     * the dedup markers they were judged against. */
    LOG_CORE_INFO("wake_due: cap=%lu(%lu) flu=%lu(%lu) cap_handled=%lu flu_handled=%lu",
                  (unsigned long)cap_due,
                  (unsigned long)((cap_due != 0) ? (cap_due % 86400u) : 0),
                  (unsigned long)flu_due,
                  (unsigned long)((flu_due != 0) ? (flu_due % 86400u) : 0),
                  (unsigned long)cap_handled,
                  (unsigned long)flu_handled);
    /* keep output sorted by due time (n <= 2) */
    if (n == 2 && out_events[0].due_unix_sec > out_events[1].due_unix_sec) {
        wake_event_t tmp = out_events[0];
        out_events[0] = out_events[1];
        out_events[1] = tmp;
    }
    return n;
}

void wake_scheduler_mark_handled(wake_duty_t duty, uint64_t at_unix_sec)
{
    load_state_if_needed();
    switch (duty) {
    case WAKE_DUTY_CAPTURE:
        if (at_unix_sec > s_state.capture_handled_at) {
            s_state.capture_handled_at = at_unix_sec;
            s_state_dirty = AICAM_TRUE;
        }
        break;
    case WAKE_DUTY_UPLOAD_FLUSH:
        if (at_unix_sec > s_state.flush_handled_at) {
            s_state.flush_handled_at = at_unix_sec;
            s_state_dirty = AICAM_TRUE;
        }
        break;
    default: return;
    }
    /* NVS write is deferred to wake_scheduler_flush_state(): one wake cycle
     * handles capture + flush, and each persist_state() is an NVS append
     * (30-byte ate + data, eventually a 4K sector erase) — coalesce the two
     * marks into a single write. Callers flush right after processing, before
     * any long drain. */
}

aicam_bool_t wake_scheduler_is_handled(wake_duty_t duty, uint64_t at_unix_sec)
{
    /* Heal relative to the queried node: a marker AHEAD of it is either a
     * backward clock step or corruption — clear it (healed_handled_at
     * persists) instead of letting it suppress this capture. After healing
     * the marker is <= at, so "handled" reduces to equality. */
    return (healed_handled_at(duty, at_unix_sec) >= at_unix_sec)
               ? AICAM_TRUE : AICAM_FALSE;
}

void wake_scheduler_flush_state(void)
{
    if (!s_state_dirty) return;
    s_state.magic = WAKE_STATE_MAGIC;
    persist_state();
    s_state_dirty = AICAM_FALSE;
}

void wake_scheduler_invalidate(void)
{
    /* stateless lookup — nothing to invalidate today. */
}
