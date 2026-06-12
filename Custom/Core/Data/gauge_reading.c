/**
 * @file gauge_reading.c
 * @brief Analog pointer gauge reading — C port of Script/gauge_reading.py.
 * @details Algorithm: compute each of min/max/tip's angle wrt center (atan2,
 *          normalized to [0,2π)). Take the CCW arc from min to max
 *          (delta_max). If tip's CCW distance from min (delta_tip) falls within
 *          that arc, direction is CCW and ratio = delta_tip/delta_max;
 *          otherwise tip lies on the complementary arc, direction is CW, and
 *          ratio = tip_cw/cw_span. ratio is clamped to [0,1] and mapped to the
 *          configured range. See gauge_reading.h for the percentage convention.
 */
#include "gauge_reading.h"
#include <math.h>

#ifndef GAUGE_M_PI
#define GAUGE_M_PI  3.14159265358979323846f
#endif

#define GAUGE_TWO_PI  (2.0f * GAUGE_M_PI)
#define GAUGE_EPS     1.0e-6f

static float gauge_norm_angle(float a)
{
    a = fmodf(a, GAUGE_TWO_PI);
    if (a < 0.0f) {
        a += GAUGE_TWO_PI;
    }
    return a;
}

static int gauge_coincident(const gauge_point_t *a, const gauge_point_t *b)
{
    return (fabsf(a->x - b->x) < GAUGE_EPS) && (fabsf(a->y - b->y) < GAUGE_EPS);
}

int gauge_reading_compute(const gauge_point_t *center,
                          const gauge_point_t *p_min,
                          const gauge_point_t *p_max,
                          const gauge_point_t *p_tip,
                          float value_min, float value_max,
                          float min_conf,
                          gauge_reading_t *out)
{
    float a_min;
    float a_max;
    float a_tip;
    float delta_max;
    float delta_tip;
    float ratio;
    const char *direction;

    if (out == NULL) {
        return -1;
    }
    out->valid     = 0u;
    out->value     = 0.0f;
    out->ratio     = 0.0f;
    out->direction = "ccw";
    out->angle_min = 0.0f;
    out->angle_max = 0.0f;
    out->angle_tip = 0.0f;

    if ((center == NULL) || (p_min == NULL) || (p_max == NULL) || (p_tip == NULL)) {
        return -1;
    }
    if (value_max <= value_min) {
        return -1;
    }

    if (min_conf > 0.0f) {
        if ((center->conf < min_conf) || (p_min->conf < min_conf) ||
            (p_max->conf < min_conf) || (p_tip->conf < min_conf)) {
            return -1;
        }
    }

    /* A scale/tip point coincident with the pivot makes its angle undefined. */
    if (gauge_coincident(p_min, center) ||
        gauge_coincident(p_max, center) ||
        gauge_coincident(p_tip, center)) {
        return -1;
    }

    a_min = gauge_norm_angle(atan2f(p_min->y - center->y, p_min->x - center->x));
    a_max = gauge_norm_angle(atan2f(p_max->y - center->y, p_max->x - center->x));
    a_tip = gauge_norm_angle(atan2f(p_tip->y - center->y, p_tip->x - center->x));

    delta_max = gauge_norm_angle(a_max - a_min);   /* CCW span min->max, [0,2π) */
    if (fabsf(delta_max) < GAUGE_EPS) {
        return -1;  /* min & max same direction -> degenerate scale arc */
    }
    delta_tip = gauge_norm_angle(a_tip - a_min);   /* CCW distance min->tip */

    if (delta_tip <= (delta_max + GAUGE_EPS)) {
        direction = "ccw";
        ratio = delta_tip / delta_max;
    } else {
        float cw_span = GAUGE_TWO_PI - delta_max;
        float tip_cw  = GAUGE_TWO_PI - delta_tip;   /* CW distance min->tip */
        direction = "cw";
        ratio = tip_cw / cw_span;
    }

    ratio = fmaxf(0.0f, fminf(1.0f, ratio));

    out->ratio     = ratio;
    out->value     = value_min + ratio * (value_max - value_min);
    out->direction = direction;
    out->angle_min = a_min;
    out->angle_max = a_max;
    out->angle_tip = a_tip;
    out->valid     = 1u;
    return 0;
}
