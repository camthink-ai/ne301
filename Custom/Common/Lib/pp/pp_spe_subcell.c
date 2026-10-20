/**
 * @file pp_spe_subcell.c
 * @brief Sub-cell refinement of single-pose heatmap peaks
 * @details The MoveNet decode places each keypoint at the centre of its argmax cell.
 *          On each axis, a parabola through the peak and its two neighbours moves the
 *          point by the vertex offset, clipped to half a cell. An axis at an edge cell
 *          is not refined, and the confidence is left as decoded. The offset is
 *          unchanged by int8's affine dequantization, so the int8 path works on the raw
 *          values. The heatmap is square, as the decode assumes: a cell's flat index is
 *          row * height + col, and its centre is ((col + 0.5) / height, (row + 0.5) / width).
 */

#include "pp_spe_subcell.h"

#define SUBCELL_MAX_OFFSET 0.5f

static float32_t clip_offset(float32_t offset)
{
    if (offset > SUBCELL_MAX_OFFSET) {
        return SUBCELL_MAX_OFFSET;
    }
    if (offset < -SUBCELL_MAX_OFFSET) {
        return -SUBCELL_MAX_OFFSET;
    }
    return offset;
}

static float32_t vertex_offset_int8(int32_t prev, int32_t peak, int32_t next)
{
    int32_t den = prev - 2 * peak + next;
    if (den >= 0) {
        return 0.0f;
    }
    return clip_offset(0.5f * (float32_t)(prev - next) / (float32_t)den);
}

static float32_t vertex_offset_float(float32_t prev, float32_t peak, float32_t next)
{
    float32_t den = prev - 2.0f * peak + next;
    if (!(den < 0.0f)) {
        return 0.0f;
    }
    return clip_offset(0.5f * (prev - next) / den);
}

/* Recover the argmax cell from the decoded centre; false if it lies outside the map */
static int keypoint_cell(const spe_pp_outBuffer_t *kp, uint32_t width, uint32_t height,
                         uint32_t *col, uint32_t *row)
{
    *col = (uint32_t)(kp->x_center * (float32_t)height);
    *row = (uint32_t)(kp->y_center * (float32_t)width);
    return *col < height && *row < width;
}

static void set_keypoint(spe_pp_outBuffer_t *kp, uint32_t width, uint32_t height,
                         uint32_t col, uint32_t row, float32_t dx, float32_t dy)
{
    kp->x_center = ((float32_t)col + 0.5f + dx) / (float32_t)height;
    kp->y_center = ((float32_t)row + 0.5f + dy) / (float32_t)width;
}

void pp_spe_subcell_refine_int8(const int8_t *heatmap, uint32_t width, uint32_t height,
                                uint32_t nb_keypoints, spe_pp_outBuffer_t *kp)
{
    const uint32_t col_step = nb_keypoints;
    const uint32_t row_step = height * nb_keypoints;

    for (uint32_t k = 0; k < nb_keypoints; k++) {
        uint32_t col, row;
        if (!keypoint_cell(&kp[k], width, height, &col, &row) ||
            heatmap[row * row_step + col * col_step + k] == INT8_MIN) {
            /* Every cell at -128: the int8 argmax records no index for the channel,
             * so report its first cell, as a scalar argmax would */
            set_keypoint(&kp[k], width, height, 0, 0, 0.0f, 0.0f);
            continue;
        }

        const int8_t *p = &heatmap[row * row_step + col * col_step + k];
        float32_t dx = 0.0f;
        float32_t dy = 0.0f;
        if (col > 0 && col < height - 1) {
            dx = vertex_offset_int8(*(p - col_step), *p, *(p + col_step));
        }
        if (row > 0 && row < width - 1) {
            dy = vertex_offset_int8(*(p - row_step), *p, *(p + row_step));
        }
        set_keypoint(&kp[k], width, height, col, row, dx, dy);
    }
}

void pp_spe_subcell_refine_float(const float32_t *heatmap, uint32_t width, uint32_t height,
                                 uint32_t nb_keypoints, spe_pp_outBuffer_t *kp)
{
    const uint32_t col_step = nb_keypoints;
    const uint32_t row_step = height * nb_keypoints;

    for (uint32_t k = 0; k < nb_keypoints; k++) {
        uint32_t col, row;
        if (!keypoint_cell(&kp[k], width, height, &col, &row)) {
            continue;
        }

        const float32_t *p = &heatmap[row * row_step + col * col_step + k];
        float32_t dx = 0.0f;
        float32_t dy = 0.0f;
        if (col > 0 && col < height - 1) {
            dx = vertex_offset_float(*(p - col_step), *p, *(p + col_step));
        }
        if (row > 0 && row < width - 1) {
            dy = vertex_offset_float(*(p - row_step), *p, *(p + row_step));
        }
        set_keypoint(&kp[k], width, height, col, row, dx, dy);
    }
}
