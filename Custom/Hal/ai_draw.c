#include "ai_draw.h"
#include "dev_manager.h"
#include "aicam_error.h"
#include "mem.h"
#include "debug.h"
#include "common_utils.h"

#define NUMBER_COLORS 9
#define COLOR_HEAD COLOR_GREEN
#define COLOR_ARMS COLOR_BLUE
#define COLOR_TRUNK COLOR_MAGENTA
#define COLOR_LEGS COLOR_YELLOW
#define MPE_YOLOV8_PP_CONF_THRESHOLD (0.6000000000f)
#define SPE_MOVENET_PP_CONF_THRESHOLD (0.3000000000f)
/* Upper bound for the per-pose keypoint draw buffers (MPE results are
 * struct-bounded to 33; SPE counts come from model metadata) */
#define POSE_DRAW_MAX_KEYPOINTS 64

__attribute__((unused)) static const int bindings[][3] = {
  {15, 13, COLOR_LEGS},
  {13, 11, COLOR_LEGS},
  {16, 14, COLOR_LEGS},
  {14, 12, COLOR_LEGS},
  {11, 12, COLOR_TRUNK},
  { 5, 11, COLOR_TRUNK},
  { 6, 12, COLOR_TRUNK},
  { 5,  6, COLOR_ARMS},
  { 5,  7, COLOR_ARMS},
  { 6,  8, COLOR_ARMS},
  { 7,  9, COLOR_ARMS},
  { 8, 10, COLOR_ARMS},
  { 1,  2, COLOR_HEAD},
  { 0,  1, COLOR_HEAD},
  { 0,  2, COLOR_HEAD},
  { 1,  3, COLOR_HEAD},
  { 2,  4, COLOR_HEAD},
  { 3,  5, COLOR_HEAD},
  { 4,  6, COLOR_HEAD},
};

static void convert_value(uint32_t width, uint32_t height, float_t xi, float_t yi, int *xo, int *yo)
{
  *xo = (int) (width * xi);
  *yo = (int) (height * yi);
}


static int clamp_point(uint32_t width, uint32_t height, int *x, int *y)
{
  int xi = *x;
  int yi = *y;

  if (*x < 0)
    *x = 0;
  if (*y < 0)
    *y = 0;
  if (*x >= width)
    *x = width - 1;
  if (*y >= height)
    *y = height - 1;

  return (xi != *x) || (yi != *y);
}

/**
 * @brief Rebuild the skeleton bind table from a result's flattened connection pairs.
 *
 * Clears any binds left from a previous result, so a result without
 * connections does not draw a stale skeleton.
 *
 * @return 0 on success, -1 on allocation failure (binds left empty).
 */
static int pose_draw_update_binds(mpe_draw_conf_t *conf, const uint8_t *connections, uint8_t num_connections)
{
    if(conf->binds != NULL){
        hal_mem_free(conf->binds);
        conf->binds = NULL;
    }
    conf->num_binds = 0;

    if(connections == NULL || num_connections == 0){
        return 0;
    }

    conf->binds = hal_mem_alloc_fast(sizeof(mpe_draw_bind_t) * num_connections);
    if(conf->binds == NULL){
        return -1;
    }
    conf->num_binds = num_connections;
    for(int i = 0; i < conf->num_binds; i++){
        conf->binds[i].keypoint1 = connections[i*2 + 0];
        conf->binds[i].keypoint2 = connections[i*2 + 1];
        conf->binds[i].color = COLOR_GREEN;
    }
    return 0;
}

/**
 * @brief Draw keypoint dots, index labels and skeleton lines for one pose.
 *
 * Shared by the MPE and SPE result paths; keypoints below conf_threshold
 * or outside the normalized [0,1] range are skipped.
 */
static void pose_draw_keypoints(mpe_draw_conf_t *conf, device_t *draw,
                                const keypoint_t *keypoints, uint32_t nb,
                                float conf_threshold)
{
    if(keypoints == NULL || nb == 0 || nb > POSE_DRAW_MAX_KEYPOINTS){
        return;
    }

    int nb_keypoints = nb;
    int keypoint_x[nb_keypoints], keypoint_y[nb_keypoints];
    bool keypoint_valid[nb_keypoints];
    for (int i = 0; i < nb_keypoints; i++) {
        keypoint_valid[i] = false;
        float x = keypoints[i].x;
        float y = keypoints[i].y;
        if (keypoints[i].conf >= conf_threshold &&
            x >= 0 && y >= 0 && x <= 1 && y <= 1) {
            convert_value(conf->image_width, conf->image_height, x, y, &keypoint_x[i], &keypoint_y[i]);
            keypoint_valid[i] = true;
        }
    }

    draw_line_param_t line_param = {0};
    for (int i = 0; i < conf->num_binds; i++) {
        int k1 = conf->binds[i].keypoint1;
        int k2 = conf->binds[i].keypoint2;
        if (k1 < nb_keypoints && k2 < nb_keypoints && keypoint_valid[k1] && keypoint_valid[k2]) {
            line_param.p_dst = conf->p_dst;
            line_param.dst_width = conf->image_width;
            line_param.dst_height = conf->image_height;
            line_param.x1 = keypoint_x[k1];
            line_param.y1 = keypoint_y[k1];
            line_param.x2 = keypoint_x[k2];
            line_param.y2 = keypoint_y[k2];
            line_param.line_width = conf->line_width;
            line_param.color = conf->binds[i].color;
            device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));
        }
    }

    draw_printf_param_t print_param = {0};
    draw_dot_param_t dot_param = {0};
    for (int i = 0; i < nb_keypoints; i++) {
        if (keypoint_valid[i]) {

            snprintf(print_param.str, sizeof(print_param.str), "%d", i);
            print_param.p_font = &conf->font;
            print_param.p_dst = conf->p_dst;
            print_param.dst_width = conf->image_width;
            print_param.dst_height = conf->image_height;
            print_param.x_pos = keypoint_x[i] + conf->dot_width;
            print_param.y_pos = keypoint_y[i];
            device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

            dot_param.p_dst = conf->p_dst;
            dot_param.dst_width = conf->image_width;
            dot_param.dst_height = conf->image_height;
            dot_param.x_pos = keypoint_x[i];
            dot_param.y_pos = keypoint_y[i];
            dot_param.dot_width = conf->dot_width;
            dot_param.color = conf->color;
            device_ioctl(draw, DRAW_CMD_DOT, (uint8_t *)&dot_param, sizeof(draw_dot_param_t));
        }
    }
}
/**
 * @brief Initializes the drawing configuration.
 *
 * @param draw_conf The configuration to use. If this is NULL, the function
 *                  returns -1.
 *
 * @return 0 on success, -1 on failure.
 */
int mpe_draw_init(mpe_draw_conf_t *mpe_conf)
{
    int ret;
    if(mpe_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_init invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }
    mpe_conf->color = COLOR_YELLOW;
    mpe_conf->line_width = 2;
    mpe_conf->box_line_width = 2;
    mpe_conf->dot_width = 10;

    draw_fontsetup_param_t font_param = {0};
    /* set 12 font */
    if(mpe_conf->font.data){
        hal_mem_free(mpe_conf->font.data);
        mpe_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &mpe_conf->font;
    ret = device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    if(ret < 0){
        LOG_DRV_ERROR("mpe_draw_init failed\r\n");
        if(mpe_conf->binds){
            hal_mem_free(mpe_conf->binds);
            mpe_conf->binds = NULL;
        }
        return -1;
    }
    return 0;
}

int mpe_draw_deinit(mpe_draw_conf_t *mpe_conf)
{
    if(mpe_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_deinit invalid param\r\n");
        return -1;
    }
    if(mpe_conf->binds){
        hal_mem_free(mpe_conf->binds);
        mpe_conf->binds = NULL;
    }

    if(mpe_conf->font.data){
        hal_mem_free(mpe_conf->font.data);
        mpe_conf->font.data = NULL;
    }
    return 0;
}

int mpe_draw_result(mpe_draw_conf_t *mpe_conf, mpe_detect_t *result)
{
    if(mpe_conf == NULL || result == NULL){
        LOG_DRV_ERROR("mpe_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    if(pose_draw_update_binds(mpe_conf, result->keypoint_connections, result->num_connections) != 0){
        LOG_DRV_ERROR("mpe_draw_result failed\r\n");
        return -1;
    }

    int x0, y0, w, h;
    convert_value(mpe_conf->image_width, mpe_conf->image_height, result->x, result->y, &x0, &y0);
    convert_value(mpe_conf->image_width, mpe_conf->image_height, result->width, result->height, &w, &h);
    clamp_point(mpe_conf->image_width, mpe_conf->image_height, &x0, &y0);

    draw_rect_param_t rect_param = {0};
    rect_param.p_dst = mpe_conf->p_dst;
    rect_param.dst_width = mpe_conf->image_width;
    rect_param.dst_height = mpe_conf->image_height;
    rect_param.x_pos = x0;
    rect_param.y_pos = y0;
    rect_param.width = w;
    rect_param.height = h;
    rect_param.line_width = mpe_conf->box_line_width;
    rect_param.color = mpe_conf->color;
    if(rect_param.x_pos + rect_param.width > rect_param.dst_width){
        rect_param.width = rect_param.dst_width - rect_param.x_pos - mpe_conf->line_width;
    }
    if(rect_param.y_pos + rect_param.height > rect_param.dst_height){
        rect_param.height = rect_param.dst_height - rect_param.y_pos - mpe_conf->line_width;
    }
    device_ioctl(draw, DRAW_CMD_RECT, (uint8_t *)&rect_param, sizeof(draw_rect_param_t));

    draw_printf_param_t print_param = {0};
    snprintf(print_param.str, sizeof(print_param.str), "%s %5.2f", result->class_name, result->conf);
    print_param.p_font = &mpe_conf->font;
    print_param.p_dst = mpe_conf->p_dst;
    print_param.dst_width = mpe_conf->image_width;
    print_param.dst_height = mpe_conf->image_height;
    print_param.x_pos = x0 + mpe_conf->line_width;
    print_param.y_pos = y0 + mpe_conf->line_width;
    device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

    pose_draw_keypoints(mpe_conf, draw, result->keypoints, result->nb_keypoints,
                        MPE_YOLOV8_PP_CONF_THRESHOLD);

    return 0;
}



int od_draw_init(od_draw_conf_t *od_conf)
{
    if(od_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_init invalid param\r\n");
        return -1;
    }
    od_conf->color = COLOR_GREEN;
    od_conf->line_width = 4;
    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    draw_fontsetup_param_t font_param = {0};
    /* set 12 font */
    if(od_conf->font.data){
        hal_mem_free(od_conf->font.data);
        od_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &od_conf->font;
    device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    return 0;
}


int od_draw_deinit(od_draw_conf_t *od_conf)
{
    if(od_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_deinit invalid param\r\n");
        return -1;
    }

    if(od_conf->font.data){
        hal_mem_free(od_conf->font.data);
        od_conf->font.data = NULL;
    }
    
    return 0;
}
int od_draw_result(od_draw_conf_t *od_conf, od_detect_t *result)
{

    int x0, y0;
    // int x1, y1;
    int w, h;

    draw_printf_param_t print_param = {0};
    draw_rect_param_t rect_param = {0};

    if(od_conf == NULL || result == NULL){
        LOG_DRV_ERROR("mpe_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    convert_value(od_conf->image_width, od_conf->image_height, result->x, result->y, &x0, &y0);
    convert_value(od_conf->image_width, od_conf->image_height, result->width, result->height, &w, &h);

    clamp_point(od_conf->image_width, od_conf->image_height, &x0, &y0);
    // clamp_point(od_conf->image_width, od_conf->image_height, &w, &y1);
    //draw box
    rect_param.p_dst = od_conf->p_dst;
    rect_param.dst_width = od_conf->image_width;
    rect_param.dst_height = od_conf->image_height;
    rect_param.x_pos = x0;
    rect_param.y_pos = y0;
    rect_param.width = w;
    rect_param.height = h;
    rect_param.line_width = od_conf->line_width;
    rect_param.color = od_conf->color;
    if(rect_param.x_pos + rect_param.width > od_conf->image_width){
        rect_param.width = od_conf->image_width - rect_param.x_pos - od_conf->line_width;
    }
    if(rect_param.y_pos + rect_param.height > od_conf->image_height){
        rect_param.height = od_conf->image_height - rect_param.y_pos - od_conf->line_width;
    }
    device_ioctl(draw, DRAW_CMD_RECT, (uint8_t *)&rect_param, sizeof(draw_rect_param_t));

    // draw_colorrect_param_t colorrect_param = {0};
    // colorrect_param.p_dst = od_conf->p_dst;
    // colorrect_param.dst_width = od_conf->image_width;
    // colorrect_param.dst_height = od_conf->image_height;
    // colorrect_param.x_pos = x0;
    // colorrect_param.y_pos = y0;
    // colorrect_param.width = w;
    // colorrect_param.height = h;
    // colorrect_param.color = od_conf->color;
    // colorrect_param.alpha = 128;
    // if(colorrect_param.x_pos + colorrect_param.width > od_conf->image_width){
    //     colorrect_param.width = od_conf->image_width - colorrect_param.x_pos - od_conf->line_width;
    // }
    // if(colorrect_param.y_pos + colorrect_param.height > od_conf->image_height){
    //     colorrect_param.height = od_conf->image_height - colorrect_param.y_pos - od_conf->line_width;
    // }
    // device_ioctl(draw, DRAW_CMD_BLEND_COLOR_RECT, (uint8_t *)&colorrect_param, sizeof(draw_colorrect_param_t));

    snprintf(print_param.str, sizeof(print_param.str), "%s", result->class_name);

    // int text_len = strlen(print_param.str);
    // int text_width = text_len * od_conf->font.width;
    // int font_height = od_conf->font.height;

    int text_x = x0 + od_conf->line_width;
    int text_y = y0 + od_conf->line_width;

    print_param.p_font = &od_conf->font;
    print_param.p_dst = od_conf->p_dst;
    print_param.dst_width = od_conf->image_width;
    print_param.dst_height = od_conf->image_height;
    print_param.x_pos = text_x;
    print_param.y_pos = text_y;
    device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

    return 0;

}

/* ==================== ISEG Drawing ==================== */

static const uint32_t iseg_color_palette[NUMBER_COLORS] = {
    COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
    COLOR_CYAN, COLOR_MAGENTA, COLOR_GRAY, COLOR_LIGHTGRAY, COLOR_DARKGRAY
};

int iseg_draw_init(iseg_draw_conf_t *iseg_conf)
{
    if (iseg_conf == NULL) {
        LOG_DRV_ERROR("iseg_draw_init invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (draw == NULL) {
        return -1;
    }

    iseg_conf->line_width = 2;
    iseg_conf->draw_dev = draw;

    draw_fontsetup_param_t font_param = {0};
    if (iseg_conf->font.data) {
        hal_mem_free(iseg_conf->font.data);
        iseg_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &iseg_conf->font;
    int ret = device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    if (ret < 0) {
        LOG_DRV_ERROR("iseg_draw_init failed\r\n");
        return -1;
    }
    return 0;
}

int iseg_draw_deinit(iseg_draw_conf_t *iseg_conf)
{
    if (iseg_conf == NULL) {
        LOG_DRV_ERROR("iseg_draw_deinit invalid param\r\n");
        return -1;
    }
    if (iseg_conf->font.data) {
        hal_mem_free(iseg_conf->font.data);
        iseg_conf->font.data = NULL;
    }
    return 0;
}

/**
 * @brief Integer square root using Newton's method.
 * @param n Input value (must be a perfect square for exact result)
 * @return Integer square root of n
 */
static uint32_t isqrt32(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/**
 * @brief Blend a single pixel with ARGB8888 color onto an RGB565 framebuffer.
 * @param fb Framebuffer pointer (RGB565 format)
 * @param fb_width Framebuffer width in pixels
 * @param x Pixel x coordinate
 * @param y Pixel y coordinate
 * @param color_argb Source color in ARGB8888 format
 * @param alpha Blend alpha (0-255)
 */
static void blend_rgb565_pixel(uint8_t *fb, uint32_t fb_width, uint32_t fb_height,
                                int x, int y,
                                uint32_t color_argb, uint8_t alpha)
{
    if (alpha == 0) return;
    if (x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height) return;
    uint16_t *pixel = (uint16_t *)(fb + (y * fb_width + x) * 2);
    uint16_t dst = *pixel;

    /* Unpack destination RGB565 */
    uint16_t dst_r = (dst >> 11) & 0x1F;
    uint16_t dst_g = (dst >> 5) & 0x3F;
    uint16_t dst_b = dst & 0x1F;

    /* Unpack source ARGB8888 to RGB565 channels */
    uint16_t src_r = (color_argb >> 19) & 0x1F;
    uint16_t src_g = (color_argb >> 10) & 0x3F;
    uint16_t src_b = (color_argb >> 3) & 0x1F;

    /* Alpha blend (integer math) */
    uint16_t inv_alpha = 255 - alpha;
    uint16_t out_r = (uint16_t)((src_r * alpha + dst_r * inv_alpha) / 255);
    uint16_t out_g = (uint16_t)((src_g * alpha + dst_g * inv_alpha) / 255);
    uint16_t out_b = (uint16_t)((src_b * alpha + dst_b * inv_alpha) / 255);

    /* Repack to RGB565 */
    *pixel = (out_r << 11) | (out_g << 5) | out_b;
}

int iseg_draw_result(iseg_draw_conf_t *iseg_conf, iseg_detect_t *result, uint32_t instance_index)
{
    if (iseg_conf == NULL || result == NULL) {
        LOG_DRV_ERROR("iseg_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = (device_t *)iseg_conf->draw_dev;
    if (draw == NULL) {
        return -1;
    }

    int x0, y0, w, h;
    convert_value(iseg_conf->image_width, iseg_conf->image_height, result->x, result->y, &x0, &y0);
    convert_value(iseg_conf->image_width, iseg_conf->image_height, result->width, result->height, &w, &h);
    clamp_point(iseg_conf->image_width, iseg_conf->image_height, &x0, &y0);

    uint32_t instance_color = iseg_color_palette[instance_index % NUMBER_COLORS];
    uint8_t base_alpha = iseg_conf->mask_alpha;

    /* Bilinear interpolation mask rendering for smooth anti-aliased edges.
       Scans mask for non-zero region, maps to image pixels, and blends
       with proportional alpha at mask boundaries using 8-bit fixed-point math. */
    if (result->mask != NULL && result->mask_size > 0) {
        uint32_t mask_side = isqrt32(result->mask_size);
        if (mask_side > 1 && mask_side * mask_side == result->mask_size) {
            uint32_t img_w = iseg_conf->image_width;
            uint32_t img_h = iseg_conf->image_height;

            /* Scan mask for non-zero region bounding box */
            int m_y0 = (int)mask_side, m_y1 = -1;
            int m_x0 = (int)mask_side, m_x1 = -1;
            for (uint32_t my = 0; my < mask_side; my++) {
                for (uint32_t mx = 0; mx < mask_side; mx++) {
                    if (result->mask[my * mask_side + mx]) {
                        if ((int)my < m_y0) m_y0 = (int)my;
                        if ((int)my > m_y1) m_y1 = (int)my;
                        if ((int)mx < m_x0) m_x0 = (int)mx;
                        if ((int)mx > m_x1) m_x1 = (int)mx;
                    }
                }
            }

            if (m_y1 >= 0) {
                /* Add 1-mask-pixel margin for interpolation at edges */
                int margin_px = (int)img_w / (int)mask_side + 1;
                int margin_py = (int)img_h / (int)mask_side + 1;
                int px_start = MAX(0, m_x0 * (int)img_w / (int)mask_side - margin_px);
                int px_end = MIN((int)img_w - 1, (m_x1 + 1) * (int)img_w / (int)mask_side + margin_px);
                int py_start = MAX(0, m_y0 * (int)img_h / (int)mask_side - margin_py);
                int py_end = MIN((int)img_h - 1, (m_y1 + 1) * (int)img_h / (int)mask_side + margin_py);

                /* Clip mask rendering to detection bounding box.
                   YOLOv8-seg standard: mask is cropped to bbox to prevent bleeding. */
                int bbox_x1 = MIN((int)img_w - 1, x0 + w);
                int bbox_y1 = MIN((int)img_h - 1, y0 + h);
                px_start = MAX(px_start, x0);
                px_end = MIN(px_end, bbox_x1);
                py_start = MAX(py_start, y0);
                py_end = MIN(py_end, bbox_y1);

                if (px_start <= px_end && py_start <= py_end) {
                    /* Coordinate mapping step (16.16 fixed point) */
                    int32_t step_x = ((int32_t)mask_side << 16) / (int32_t)img_w;
                    int32_t step_y = ((int32_t)mask_side << 16) / (int32_t)img_h;

                    for (int py = py_start; py <= py_end; py++) {
                        int32_t fy = (int32_t)py * step_y;
                        int my0 = (int)(fy >> 16);
                        int my1 = MIN(my0 + 1, (int)mask_side - 1);
                        uint8_t fy8 = (uint8_t)((fy >> 8) & 0xFF);
                        uint8_t fy8i = 255u - fy8;
                        uint8_t *row0 = result->mask + my0 * mask_side;
                        uint8_t *row1 = result->mask + my1 * mask_side;

                        int32_t fx = (int32_t)px_start * step_x;
                        for (int px = px_start; px <= px_end; px++, fx += step_x) {
                            int mx0 = (int)(fx >> 16);
                            int mx1 = MIN(mx0 + 1, (int)mask_side - 1);
                            uint8_t fx8 = (uint8_t)((fx >> 8) & 0xFF);
                            uint8_t fx8i = 255u - fx8;

                            uint8_t v00 = row0[mx0];
                            uint8_t v10 = row0[mx1];
                            uint8_t v01 = row1[mx0];
                            uint8_t v11 = row1[mx1];

                            uint32_t w00 = (uint32_t)fx8i * fy8i;
                            uint32_t w10 = (uint32_t)fx8 * fy8i;
                            uint32_t w01 = (uint32_t)fx8i * fy8;
                            uint32_t w11 = (uint32_t)fx8 * fy8;
                            uint32_t interp = ((uint32_t)v00 * w00 + (uint32_t)v10 * w10 +
                                               (uint32_t)v01 * w01 + (uint32_t)v11 * w11) / 255u;

                            if (interp > 0) {
                                uint8_t min_alpha = base_alpha >> 2;
                                uint8_t a = min_alpha + (uint8_t)((uint32_t)(base_alpha - min_alpha) * interp / 255u);
                                blend_rgb565_pixel(iseg_conf->p_dst, img_w, img_h,
                                                  px, py, instance_color, a);
                            }
                        }
                    }
                }
            }
        }
    } else {
        /* Fallback: semi-transparent filled bbox when no mask data */
        draw_colorrect_param_t colorrect_param = {0};
        colorrect_param.p_dst = iseg_conf->p_dst;
        colorrect_param.dst_width = iseg_conf->image_width;
        colorrect_param.dst_height = iseg_conf->image_height;
        colorrect_param.x_pos = x0;
        colorrect_param.y_pos = y0;
        colorrect_param.width = w;
        colorrect_param.height = h;
        colorrect_param.color = instance_color;
        colorrect_param.alpha = base_alpha;
        if (colorrect_param.x_pos + colorrect_param.width > colorrect_param.dst_width) {
            colorrect_param.width = colorrect_param.dst_width - colorrect_param.x_pos;
        }
        if (colorrect_param.y_pos + colorrect_param.height > colorrect_param.dst_height) {
            colorrect_param.height = colorrect_param.dst_height - colorrect_param.y_pos;
        }
        device_ioctl(draw, DRAW_CMD_BLEND_COLOR_RECT, (uint8_t *)&colorrect_param, sizeof(draw_colorrect_param_t));
    }

    /* Class label + confidence (no bounding box — mask+label only) */
    draw_printf_param_t print_param = {0};
    snprintf(print_param.str, sizeof(print_param.str), "%s %5.2f", result->class_name, result->conf);
    print_param.p_font = &iseg_conf->font;
    print_param.p_dst = iseg_conf->p_dst;
    print_param.dst_width = iseg_conf->image_width;
    print_param.dst_height = iseg_conf->image_height;
    print_param.x_pos = x0 + iseg_conf->line_width;
    print_param.y_pos = y0 + iseg_conf->line_width;
    device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

    return 0;
}

/* ==================== SPE Drawing ==================== */

int spe_draw_init(spe_draw_conf_t *spe_conf)
{
    if(spe_conf == NULL){
        LOG_DRV_ERROR("spe_draw_init invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    /* Apply defaults only where the caller left values unset */
    if(spe_conf->color == 0){
        spe_conf->color = COLOR_BLUE;
    }
    if(spe_conf->line_width == 0){
        spe_conf->line_width = 2;
    }
    if(spe_conf->dot_width == 0){
        spe_conf->dot_width = 10;
    }

    /* The font is owned by this conf: free a previous setup on re-init */
    draw_fontsetup_param_t font_param = {0};
    if(spe_conf->font.data){
        hal_mem_free(spe_conf->font.data);
        spe_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &spe_conf->font;
    int ret = device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    if(ret < 0){
        LOG_DRV_ERROR("spe_draw_init failed\r\n");
        return -1;
    }
    return 0;
}

int spe_draw_deinit(spe_draw_conf_t *spe_conf)
{
    return mpe_draw_deinit(spe_conf);
}

int spe_draw_result(spe_draw_conf_t *spe_conf, const pp_spe_out_t *result)
{
    if(spe_conf == NULL || result == NULL){
        LOG_DRV_ERROR("spe_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    if(pose_draw_update_binds(spe_conf, result->keypoint_connections, result->num_connections) != 0){
        LOG_DRV_ERROR("spe_draw_result failed\r\n");
        return -1;
    }

    /* No bounding box or class label: SPE is detector-free (keypoints + skeleton only) */
    pose_draw_keypoints(spe_conf, draw, result->keypoints, result->nb_keypoints,
                        SPE_MOVENET_PP_CONF_THRESHOLD);

    return 0;
}