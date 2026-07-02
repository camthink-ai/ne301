/**
 * @file ai_draw_service.h
 * @brief AI Drawing Service Interface
 * @details AI Drawing Service Interface, support AI detection result drawing
 */

#ifndef AI_DRAW_SERVICE_H
#define AI_DRAW_SERVICE_H

#include "aicam_types.h"
#include "nn.h"
#include "ai_draw.h"
#include "draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== AI Drawing Service Configuration ==================== */

/**
 * @brief AI drawing service configuration
 */
typedef struct {
    uint32_t image_width;                  // Image width
    uint32_t image_height;                 // Image height
    uint32_t line_width;                   // Default line width
    uint32_t box_line_width;               // Box border line width
    uint32_t dot_width;                    // Keypoint dot width
    uint32_t od_color;                     // Object detection box color
    uint32_t mpe_color;                    // MPE detection box color
    uint32_t spe_color;                    // SPE keypoint color
    uint32_t iseg_color;                   // ISEG detection base color
    uint8_t  iseg_mask_alpha;              // ISEG mask overlay alpha (0-255)
    uint8_t  iseg_mask_threshold;         // ISEG mask pixel threshold (0-255)
    aicam_bool_t enable_text;              // Enable text labels
    aicam_bool_t enable_keypoints;         // Enable keypoint drawing (MPE only)
} ai_draw_config_t;

/**
 * @brief AI drawing service context
 */
typedef struct {
    aicam_bool_t initialized;              // Initialization status
    ai_draw_config_t config;               // Drawing configuration
    mpe_draw_conf_t mpe_draw_conf;         // MPE drawing configuration
    spe_draw_conf_t spe_draw_conf;         // SPE drawing configuration
    od_draw_conf_t od_draw_conf;           // OD drawing configuration
    iseg_draw_conf_t iseg_draw_conf;       // ISEG drawing configuration
    DRAW_Font_t font_12;                   // 12pt font
    DRAW_Font_t font_16;                   // 16pt font
    device_t *draw_device;                 // Draw device handle
} ai_draw_service_t;

/* ==================== AI Drawing Service Functions ==================== */

/**
 * @brief Initialize AI drawing service
 * @param config Drawing configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_service_init(const ai_draw_config_t *config);

/**
 * @brief Deinitialize AI drawing service
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_service_deinit(void);

/**
 * @brief Draw AI detection results on frame buffer
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param result AI detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_results(uint8_t *fb, 
                               uint32_t fb_width, 
                               uint32_t fb_height, 
                               const nn_result_t *result);

/**
 * @brief Draw object detection results
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param od_result Object detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_od_results(uint8_t *fb, 
                                  uint32_t fb_width, 
                                  uint32_t fb_height, 
                                  const pp_od_out_t *od_result);

/**
 * @brief Draw MPE (Multi-Person Pose Estimation) results
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param mpe_result MPE detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_mpe_results(uint8_t *fb,
                                   uint32_t fb_width,
                                   uint32_t fb_height,
                                   const pp_mpe_out_t *mpe_result);

/**
 * @brief Draw SPE (Single-Person Pose Estimation) results
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param spe_result SPE detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_spe_results(uint8_t *fb,
                                   uint32_t fb_width,
                                   uint32_t fb_height,
                                   const pp_spe_out_t *spe_result);

/**
 * @brief Draw single object detection
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param detection Single detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_single_od(uint8_t *fb, 
                                 uint32_t fb_width, 
                                 uint32_t fb_height, 
                                 const od_detect_t *detection);

/**
 * @brief Draw single MPE detection
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param detection Single MPE detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_single_mpe(uint8_t *fb,
                                  uint32_t fb_width,
                                  uint32_t fb_height,
                                  const mpe_detect_t *detection);

/**
 * @brief Draw ISEG (Instance Segmentation) results
 * @param fb Frame buffer pointer
 * @param fb_width Frame buffer width
 * @param fb_height Frame buffer height
 * @param iseg_result ISEG detection result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_iseg_results(uint8_t *fb,
                                     uint32_t fb_width,
                                     uint32_t fb_height,
                                     const pp_iseg_out_t *iseg_result);

/* ==================== Configuration Functions ==================== */

/**
 * @brief Get default AI drawing configuration
 * @param config Configuration structure to fill
 */
void ai_draw_get_default_config(ai_draw_config_t *config);

/**
 * @brief Set AI drawing configuration
 * @param config New configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_set_config(const ai_draw_config_t *config);

/**
 * @brief Get AI drawing configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_draw_get_config(ai_draw_config_t *config);

/* ==================== Utility Functions ==================== */

/**
 * @brief Check if AI drawing service is initialized
 * @return Initialization status
 */
aicam_bool_t ai_draw_is_initialized(void);

/**
 * @brief Get AI drawing service context
 * @return Service context pointer or NULL if not initialized
 */
ai_draw_service_t* ai_draw_get_context(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_DRAW_SERVICE_H */