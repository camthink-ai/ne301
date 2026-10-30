#ifndef LC_LINE_CROSS_H
#define LC_LINE_CROSS_H

#include "lc_types.h"

typedef struct lc_track_t lc_track_t;

typedef struct {
    lc_point_t L1, L2;
    lc_point_t n;
    int        _pad;
} lc_line_cross_t;

lc_line_cross_t* lc_line_cross_create(float x1, float y1,
                                      float x2, float y2,
                                      float outside_x, float outside_y);
void             lc_line_cross_destroy(lc_line_cross_t* lc);

lc_cross_event_t lc_line_cross_check(lc_line_cross_t* lc,
                                     lc_track_t* trk,
                                     uint8_t prev_idx, uint8_t now_idx);

#endif
