/**
 * @file gauge_reading.h
 * @brief Analog pointer gauge reading from 4 keypoints (center/min/max/tip).
 * @details Pure-geometry module: given the four keypoints of a pointer gauge
 *          (pivot + min/max scale marks + needle tip), compute the reading by
 *          angular interpolation. Direction (CW/CCW) is auto-detected by
 *          selecting the arc that contains the tip, so no manual sweep-direction
 *          config is needed.
 *
 *          The end-user-facing value is a percentage: value_min=0, value_max=100
 *          yields ratio*100 (e.g. ratio 0.6 -> 60%, meaning the needle sits 60%
 *          of the scale arc away from min). Physical-unit mapping, if needed,
 *          is the frontend's job.
 *
 *          Reference implementation: Script/gauge_reading.py (validated by
 *          Script/test_gauge_reading.py). This C port must produce identical
 *          results for the same inputs.
 */
#ifndef GAUGE_READING_H
#define GAUGE_READING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 2D point with confidence. Normalized or pixel coords; all four points must
 *  share the same coordinate system. */
typedef struct {
    float x;
    float y;
    float conf;
} gauge_point_t;

/** Result of a gauge reading computation. valid==0 means the reading could not
 *  be computed (missing/coincident keypoints, degenerate scale arc, ...). */
typedef struct {
    uint8_t      valid;       /**< 1 = reading meaningful, 0 = not computed   */
    float        value;       /**< reading in [value_min, value_max] (percent) */
    float        ratio;       /**< needle position along arc, 0..1             */
    const char  *direction;   /**< "ccw" | "cw"                                */
    float        angle_min;   /**< min point angle wrt center, rad [0,2π)      */
    float        angle_max;
    float        angle_tip;
} gauge_reading_t;

/**
 * @brief Compute analog gauge reading from 4 keypoints.
 * @param center     pivot point.
 * @param p_min      minimum-scale mark.
 * @param p_max      maximum-scale mark.
 * @param p_tip      needle tip.
 * @param value_min  range lower bound (use 0.0f for percentage output).
 * @param value_max  range upper bound (use 100.0f for percentage output).
 * @param min_conf   keypoint confidence floor; any keypoint below this makes
 *                   the result invalid (pass 0.0f to skip the check).
 * @param out        output, filled in on success.
 * @retval 0  success (out->valid == 1).
 * @retval -1 reading cannot be computed (out zeroed, valid == 0).
 */
int gauge_reading_compute(const gauge_point_t *center,
                          const gauge_point_t *p_min,
                          const gauge_point_t *p_max,
                          const gauge_point_t *p_tip,
                          float value_min, float value_max,
                          float min_conf,
                          gauge_reading_t *out);

#ifdef __cplusplus
}
#endif
#endif /* GAUGE_READING_H */
