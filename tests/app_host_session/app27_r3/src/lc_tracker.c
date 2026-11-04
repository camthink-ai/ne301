#include "lc_tracker.h"
#include "lc_line_cross.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct lc_tracker {
    lc_track_t          tracks[LC_MAX_TRACKS];
    uint32_t            next_id;
    lc_tracker_config_t cfg;
};

static uint8_t clamp_k(uint8_t k) { return k < 4u ? 4u : (k > LC_K_MAX ? LC_K_MAX : k); }
static float   max_dist_f(const lc_tracker_t* t) { return t->cfg.max_dist_permille / 1000.0f; }

static void append_trail(lc_track_t* trk, const lc_point_t* c, uint32_t now_ms, uint8_t k) {
    if (trk->trail_used != 0) {
        uint32_t elapsed = now_ms - trk->trail_last_ts;
        if (elapsed < LC_TRAIL_MIN_INTERVAL_MS) return;
        if (elapsed < LC_TRAIL_MAX_INTERVAL_MS) {
            float dx = c->x - trk->trail_last_pos.x;
            float dy = c->y - trk->trail_last_pos.y;
            if (sqrtf(dx * dx + dy * dy) < LC_TRAIL_MIN_DISTANCE) return;
        }
    }
    uint8_t slot = trk->trail_head;
    trk->trail[slot]     = *c;
    trk->trail_ts[slot]  = now_ms;
    trk->trail_head      = (uint8_t)((slot + 1) % k);
    if (trk->trail_used < k) trk->trail_used++;
    trk->trail_last_ts   = now_ms;
    trk->trail_last_pos  = *c;
    trk->trail_has_last  = 1;
}

lc_tracker_t* lc_tracker_create(const lc_tracker_config_t* cfg, uint32_t initial_next_id) {
    if (!cfg) return NULL;
    lc_tracker_t* t = (lc_tracker_t*)LC_MALLOC(sizeof(*t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    t->cfg.track_history_k = clamp_k(cfg->track_history_k);
    t->next_id = initial_next_id;
    return t;
}
void lc_tracker_destroy(lc_tracker_t* t) { LC_FREE(t); }
uint32_t lc_tracker_next_id(const lc_tracker_t* t) { return t ? t->next_id : 1u; }

uint16_t lc_tracker_active_count(const lc_tracker_t* t) {
    if (!t) return 0;
    uint16_t c = 0;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) if (t->tracks[i].id != 0) c++;
    return c;
}

static lc_track_t* find_track_slot(lc_tracker_t* t) {
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        if (t->tracks[i].id == 0) return &t->tracks[i];
    }
    return NULL;
}

static void archive_track(lc_tracker_t* t, uint16_t idx, lc_seg_end_t why, uint32_t now_ms,
                          lc_track_record_t*** out_records, uint16_t* out_n_records) {
    lc_track_t* trk = &t->tracks[idx];
    uint8_t k = t->cfg.track_history_k;
    uint32_t from = trk->last_report_ts;
    uint32_t to   = now_ms;
    uint32_t dur  = to - from;
    uint32_t interval;
    if (dur < 1000u)        interval = 0;
    else if (dur < 5000u)   interval = 333u;
    else                    interval = 1000u;

    uint8_t n_pts = 0;
    uint32_t next_ts = from;
    for (uint8_t i = 0; i < trk->history_used; ++i) {
        uint8_t slot = (uint8_t)((trk->history_head + k - trk->history_used + i) % k);
        if (trk->history_ts[slot] <= from) continue;
        if (trk->history_ts[slot] > to)    break;
        if (interval == 0 || trk->history_ts[slot] >= next_ts) {
            n_pts++;
            next_ts = trk->history_ts[slot] + interval;
        }
    }
    lc_track_record_t* rec = (lc_track_record_t*)LC_MALLOC(LC_TRACK_RECORD_SIZE(n_pts));
    if (!rec) { goto done; }
    rec->track_id      = trk->id;
    rec->segment_id    = trk->segment_id;
    rec->entered_at_ms = trk->entered_at_ms;
    rec->seg_start_ms  = from;
    rec->seg_end_ms    = to;
    rec->seg_end_type  = why;
    rec->events        = trk->segment_events;
    rec->nb_points     = n_pts;
    uint32_t* pts_ts   = lc_track_record_point_ts(rec);
    uint8_t filled = 0;
    next_ts = from;
    for (uint8_t i = 0; i < trk->history_used; ++i) {
        uint8_t slot = (uint8_t)((trk->history_head + k - trk->history_used + i) % k);
        if (trk->history_ts[slot] <= from) continue;
        if (trk->history_ts[slot] > to)    break;
        if (interval == 0 || trk->history_ts[slot] >= next_ts) {
            rec->points[filled]  = trk->history[slot];
            pts_ts[filled]       = trk->history_ts[slot];
            filled++;
            next_ts = trk->history_ts[slot] + interval;
        }
    }

    lc_track_record_t** grown = (lc_track_record_t**)LC_MALLOC(((*out_n_records) + 1) * sizeof(lc_track_record_t*));
    if (grown) {
        if (*out_records) {
            memcpy(grown, *out_records, (*out_n_records) * sizeof(lc_track_record_t*));
            LC_FREE(*out_records);
        }
        *out_records = grown;
        (*out_records)[*out_n_records] = rec;
        (*out_n_records)++;
    }
    else { LC_FREE(rec); }

done:
    if (why == LC_SEG_DEPARTED) {
        memset(trk, 0, sizeof(*trk));
    } else {
        trk->last_report_ts = to;
        trk->segment_id++;
        trk->segment_events = 0;
    }
}

void lc_tracker_update(lc_tracker_t* t, const lc_point_t* detects, uint8_t n_detects,
                       uint32_t now_ms,
                       lc_track_record_t*** out_records, uint16_t* out_n_records) {
    if (!t) return;
    uint8_t k = t->cfg.track_history_k;
    float thresh = max_dist_f(t);
    uint8_t matched[64];
    if (n_detects > 64) n_detects = 64;
    memset(matched, 0, n_detects);

    uint16_t order[LC_MAX_TRACKS];
    uint16_t no = 0;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) if (t->tracks[i].id != 0) order[no++] = i;
    for (uint16_t i = 1; i < no; ++i) {
        uint16_t key = order[i]; uint16_t j = i;
        while (j > 0) {
            lc_track_t* a = &t->tracks[order[j-1]];
            lc_track_t* b = &t->tracks[key];
            int cmp = (a->last_match_ts == b->last_match_ts)
                      ? (a->miss_count == b->miss_count
                         ? (a->id == b->id ? 0 : (a->id < b->id ? -1 : 1))
                         : (a->miss_count < b->miss_count ? -1 : 1))
                      : (a->last_match_ts > b->last_match_ts ? -1 : 1);
            if (cmp > 0) { order[j] = order[j-1]; --j; } else break;
        }
        order[j] = key;
    }

    for (uint16_t oi = 0; oi < no; ++oi) {
        lc_track_t* trk = &t->tracks[order[oi]];
        float best_d = 1e9f; int best_j = -1;
        for (uint8_t j = 0; j < n_detects; ++j) {
            if (matched[j]) continue;
            float dx = detects[j].x - trk->last_pos.x;
            float dy = detects[j].y - trk->last_pos.y;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < best_d) { best_d = d; best_j = (int)j; }
        }
        if (best_j >= 0 && best_d < thresh) {
            matched[best_j] = 1;
            uint8_t slot = trk->history_head;
            trk->history[slot]     = detects[best_j];
            trk->history_ts[slot]  = now_ms;
            trk->history_head      = (uint8_t)((slot + 1) % k);
            if (trk->history_used < k) trk->history_used++;
            trk->last_pos      = detects[best_j];
            trk->last_match_ts = now_ms;
            if (trk->age != UINT8_MAX) {
                trk->age++;
            }
            trk->miss_count    = 0;
            append_trail(trk, &detects[best_j], now_ms, k);
        } else {
            trk->miss_count++;
        }
    }

    for (uint8_t j = 0; j < n_detects; ++j) {
        if (matched[j]) continue;
        lc_track_t* slot = find_track_slot(t);
        if (!slot) break;
        memset(slot, 0, sizeof(*slot));
        slot->id             = t->next_id++;
        slot->history[0]     = detects[j];
        slot->history_ts[0]  = now_ms;
        slot->history_head   = 1;
        slot->history_used   = 1;
        slot->age            = 1;
        slot->miss_count     = 0;
        slot->last_side      = 0;
        slot->counted_dir    = 0;
        slot->entered_at_ms  = now_ms;
        slot->last_match_ts  = now_ms;
        slot->last_report_ts = now_ms;
        slot->segment_id     = 0;
        slot->last_pos       = detects[j];
        slot->trail[0]       = detects[j];
        slot->trail_ts[0]    = now_ms;
        slot->trail_head     = 1;
        slot->trail_used     = 1;
        slot->trail_last_ts  = now_ms;
        slot->trail_last_pos = detects[j];
        slot->trail_has_last = 1;
    }

    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        lc_track_t* trk = &t->tracks[i];
        if (trk->id == 0) continue;
        if (trk->miss_count > t->cfg.max_miss) {
            archive_track(t, i, LC_SEG_DEPARTED, now_ms, out_records, out_n_records);
        }
    }
}

void lc_tracker_window_snapshot(lc_tracker_t* t, uint32_t window_end_ms,
                                lc_track_record_t*** out_records, uint16_t* out_n_records) {
    if (!t) return;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        if (t->tracks[i].id == 0) continue;
        archive_track(t, i, LC_SEG_CROSSING, window_end_ms, out_records, out_n_records);
    }
}

void lc_tracker_check_line_crossings(lc_tracker_t* t, lc_line_cross_t* lc, uint32_t now_ms,
                                     uint32_t* win_in, uint32_t* win_out,
                                     uint32_t* tot_in,  uint32_t* tot_out,
                                     lc_cross_evt_t* out_events, uint8_t max_events,
                                     uint8_t* out_n_events) {
    if (!t || !lc) return;
    if (out_n_events) *out_n_events = 0;
    uint8_t k = t->cfg.track_history_k;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        lc_track_t* trk = &t->tracks[i];
        if (trk->id == 0) continue;
        if (trk->history_used < 2) continue;
        uint8_t now_idx  = (uint8_t)((trk->history_head + k - 1u) % k);
        uint8_t prev_idx = (uint8_t)((now_idx + k - 1u) % k);
        lc_cross_event_t ev = lc_line_cross_check(lc, trk, prev_idx, now_idx);
        if (ev == LC_CROSS_IN)       { if (win_in)  (*win_in)++;  if (tot_in)  (*tot_in)++; }
        else if (ev == LC_CROSS_OUT) { if (win_out) (*win_out)++; if (tot_out) (*tot_out)++; }
        else continue;
        if (out_events && max_events > 0 && out_n_events && *out_n_events < max_events) {
            lc_cross_evt_t* e = &out_events[(*out_n_events)++];
            e->track_id  = trk->id;
            e->ts_ms     = now_ms;
            e->direction = (ev == LC_CROSS_IN) ? LC_CROSS_IN : LC_CROSS_OUT;
            e->pos       = trk->last_pos;
        }
    }
}

void lc_tracker_for_each_stable(const lc_tracker_t* t, lc_track_visitor_t fn, void* user) {
    if (!t || !fn) return;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        const lc_track_t* trk = &t->tracks[i];
        if (trk->id != 0 && trk->age >= t->cfg.k_confirm) fn(trk, user);
    }
}

void lc_tracker_for_each_active(const lc_tracker_t* t, lc_track_visitor_t fn, void* user) {
    if (!t || !fn) return;
    for (uint16_t i = 0; i < LC_MAX_TRACKS; ++i) {
        const lc_track_t* trk = &t->tracks[i];
        if (trk->id != 0) fn(trk, user);
    }
}
