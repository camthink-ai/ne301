#ifndef LC_TRACKER_H
#define LC_TRACKER_H

#include "lc_types.h"
#include "lc_line_cross.h"

#define LC_HISTORY_MAX            16u
#define LC_TRAIL_MIN_INTERVAL_MS 100u
#define LC_TRAIL_MAX_INTERVAL_MS 500u
#define LC_TRAIL_MIN_DISTANCE   0.02f

struct lc_track_t {
    uint32_t   id;
    lc_point_t history[LC_K_MAX];
    uint32_t   history_ts[LC_K_MAX];
    uint8_t    history_head;
    uint8_t    history_used;
    lc_point_t trail[LC_HISTORY_MAX];
    uint32_t   trail_ts[LC_HISTORY_MAX];
    uint8_t    trail_head;
    uint8_t    trail_used;
    uint32_t   trail_last_ts;
    lc_point_t trail_last_pos;
    uint8_t    trail_has_last;
    uint8_t    age;
    uint8_t    miss_count;
    int8_t     last_side;
    uint8_t    counted_dir;
    uint8_t    segment_events;
    uint32_t   entered_at_ms;
    uint32_t   last_match_ts;
    uint32_t   last_report_ts;
    uint32_t   segment_id;
    lc_point_t last_pos;
};

typedef struct lc_tracker_config_t {
    uint16_t max_dist_permille;
    uint8_t  track_history_k;
    uint8_t  max_miss;
    uint8_t  k_confirm;
} lc_tracker_config_t;

typedef struct lc_tracker lc_tracker_t;

lc_tracker_t* lc_tracker_create(const lc_tracker_config_t* cfg, uint32_t initial_next_id);
void          lc_tracker_destroy(lc_tracker_t* t);
uint32_t      lc_tracker_next_id(const lc_tracker_t* t);

void lc_tracker_update(lc_tracker_t* t,
                       const lc_point_t* detects, uint8_t n_detects,
                       uint32_t now_ms,
                       lc_track_record_t*** out_records, uint16_t* out_n_records);

void lc_tracker_window_snapshot(lc_tracker_t* t, uint32_t window_end_ms,
                                lc_track_record_t*** out_records, uint16_t* out_n_records);

typedef struct {
    uint32_t         track_id;
    uint32_t         ts_ms;
    lc_cross_event_t direction;
    lc_point_t       pos;
} lc_cross_evt_t;

void lc_tracker_check_line_crossings(lc_tracker_t* t, lc_line_cross_t* lc,
                                     uint32_t now_ms,
                                     uint32_t* window_in_delta, uint32_t* window_out_delta,
                                     uint32_t* total_in_delta,  uint32_t* total_out_delta,
                                     lc_cross_evt_t* out_events, uint8_t max_events,
                                     uint8_t* out_n_events);

uint16_t lc_tracker_active_count(const lc_tracker_t* t);

typedef void (*lc_track_visitor_t)(const lc_track_t* trk, void* user);
void lc_tracker_for_each_stable(const lc_tracker_t* t, lc_track_visitor_t fn, void* user);
void lc_tracker_for_each_active(const lc_tracker_t* t, lc_track_visitor_t fn, void* user);

#endif
