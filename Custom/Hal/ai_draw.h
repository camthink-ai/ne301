#ifndef _AI_DRAW_H
#define _AI_DRAW_H

#include <stdint.h>
#include "nn.h"
#include "draw.h"

typedef struct {
    uint8_t keypoint1;
    uint8_t keypoint2;
    uint32_t color;
} mpe_draw_bind_t;

typedef struct {
    uint8_t *p_dst;
    uint8_t num_binds;
    mpe_draw_bind_t* binds;
    uint32_t color;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t line_width;
    uint32_t box_line_width;
    uint32_t dot_width;
    DRAW_Font_t font;
} mpe_draw_conf_t;


typedef struct {
    uint8_t *p_dst;
    uint32_t color;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t line_width;
    DRAW_Font_t font;
} od_draw_conf_t;

int mpe_draw_init(mpe_draw_conf_t *mpe_conf);
int mpe_draw_deinit(mpe_draw_conf_t *mpe_conf);
int mpe_draw_result(mpe_draw_conf_t *mpe_conf, mpe_detect_t *result);
int od_draw_init(od_draw_conf_t *od_conf);
int od_draw_deinit(od_draw_conf_t *od_conf);
int od_draw_result(od_draw_conf_t *od_conf, od_detect_t *result);

typedef struct {
    uint8_t *p_dst;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t line_width;
    DRAW_Font_t font;
    uint8_t mask_alpha;
    uint8_t mask_threshold;  /**< Mask pixel activation threshold (0-255), default 128 */
    void *draw_dev;          /**< Cached draw device handle */
} iseg_draw_conf_t;

int iseg_draw_init(iseg_draw_conf_t *iseg_conf);
int iseg_draw_deinit(iseg_draw_conf_t *iseg_conf);
int iseg_draw_result(iseg_draw_conf_t *iseg_conf, iseg_detect_t *result, uint32_t instance_index);

/* SPE drawing shares the MPE keypoint/skeleton renderer (box_line_width unused: SPE has no box) */
typedef mpe_draw_conf_t spe_draw_conf_t;

int spe_draw_init(spe_draw_conf_t *spe_conf);
int spe_draw_deinit(spe_draw_conf_t *spe_conf);
int spe_draw_result(spe_draw_conf_t *spe_conf, const pp_spe_out_t *result);
#endif