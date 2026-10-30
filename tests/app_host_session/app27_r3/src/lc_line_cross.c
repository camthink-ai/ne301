#include "lc_line_cross.h"
#include "lc_tracker.h"
#include <math.h>
#include <string.h>

static inline float dot2(lc_point_t a, lc_point_t b) { return a.x*b.x + a.y*b.y; }
static inline lc_point_t sub2(lc_point_t a, lc_point_t b) { return (lc_point_t){a.x-b.x, a.y-b.y}; }
static inline int sign_nonzero(float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); }

lc_line_cross_t* lc_line_cross_create(float x1, float y1, float x2, float y2,
                                      float outside_x, float outside_y) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.01f) return NULL;

    float ux = dx / len, uy = dy / len;
    lc_point_t n_cand = { -uy, ux };
    lc_point_t out_rel = { outside_x - x1, outside_y - y1 };
    lc_point_t n;
    if (n_cand.x * out_rel.x + n_cand.y * out_rel.y > 0.0f) {
        n = (lc_point_t){ -n_cand.x, -n_cand.y };
    } else {
        n = n_cand;
    }

    lc_line_cross_t* lc = (lc_line_cross_t*)LC_MALLOC(sizeof(*lc));
    if (!lc) return NULL;
    lc->L1 = (lc_point_t){x1, y1};
    lc->L2 = (lc_point_t){x2, y2};
    lc->n  = n;
    return lc;
}

void lc_line_cross_destroy(lc_line_cross_t* lc) { LC_FREE(lc); }

lc_cross_event_t lc_line_cross_check(lc_line_cross_t* lc, lc_track_t* trk,
                                     uint8_t prev_idx, uint8_t now_idx) {
    if (!lc || !trk) return LC_CROSS_NONE;

    lc_point_t p_now  = trk->history[now_idx];
    lc_point_t p_prev = trk->history[prev_idx];

    lc_point_t d_now  = sub2(p_now,  lc->L1);
    lc_point_t d_prev = sub2(p_prev, lc->L1);
    int side_now  = sign_nonzero(dot2(d_now,  lc->n));
    int side_prev = sign_nonzero(dot2(d_prev, lc->n));

    if (side_now == 0 || side_prev == 0 || side_now == side_prev) {
        return LC_CROSS_NONE;
    }

    lc_point_t disp = sub2(p_now, p_prev);
    float proj = dot2(disp, lc->n);
    if (proj > 0.0f && !(trk->counted_dir & LC_BIT_IN)) {
        trk->counted_dir |= LC_BIT_IN;
        trk->counted_dir &= (uint8_t)~LC_BIT_OUT;
        trk->segment_events |= LC_BIT_IN;
        return LC_CROSS_IN;
    }
    if (proj < 0.0f && !(trk->counted_dir & LC_BIT_OUT)) {
        trk->counted_dir |= LC_BIT_OUT;
        trk->counted_dir &= (uint8_t)~LC_BIT_IN;
        trk->segment_events |= LC_BIT_OUT;
        return LC_CROSS_OUT;
    }
    return LC_CROSS_NONE;
}
