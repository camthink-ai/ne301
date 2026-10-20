/**
 * @file pp_spe_subcell.h
 * @brief Sub-cell refinement of single-pose heatmap peaks
 */

#ifndef _PP_SPE_SUBCELL_H
#define _PP_SPE_SUBCELL_H

#include <stdint.h>
#include "spe_pp_output_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Refine the argmax keypoints of an int8 heatmap tensor in place.
 * @param heatmap      raw output tensor [1, H, W, K], K innermost (as the argmax reads it)
 * @param width        heatmap_width
 * @param height       heatmap_height (the row stride, as in the argmax's decode)
 * @param nb_keypoints K
 * @param kp           the argmax output: cell centres, confidence untouched
 */
void pp_spe_subcell_refine_int8(const int8_t *heatmap, uint32_t width, uint32_t height,
                                uint32_t nb_keypoints, spe_pp_outBuffer_t *kp);

/** @brief The same on a float32 heatmap tensor. */
void pp_spe_subcell_refine_float(const float32_t *heatmap, uint32_t width, uint32_t height,
                                 uint32_t nb_keypoints, spe_pp_outBuffer_t *kp);

#ifdef __cplusplus
}
#endif

#endif /* _PP_SPE_SUBCELL_H */
