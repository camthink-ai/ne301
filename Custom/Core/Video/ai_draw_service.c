/**
 * @file ai_draw_service.c
 * @brief AI Drawing Service Implementation
 * @details AI Drawing Service Implementation, support AI detection result drawing
 */

#include "ai_draw_service.h"
#include "buffer_mgr.h"
#include "debug.h"
#include "dev_manager.h"
#include "fonts.h"
#include <string.h>
#include <stdio.h>
#include "pixel_format_map.h"

/* ==================== Global Variables ==================== */

static ai_draw_service_t g_ai_draw_service = {0};

/* ==================== Internal Function Declarations ==================== */

static aicam_result_t ai_draw_init_fonts(void);
static aicam_result_t ai_draw_deinit_fonts(void);
static aicam_result_t ai_draw_setup_draw_device(void);
static aicam_result_t ai_draw_configure_od_drawing(void);
static aicam_result_t ai_draw_configure_mpe_drawing(void);
static aicam_result_t ai_draw_configure_spe_drawing(void);
static aicam_result_t ai_draw_configure_iseg_drawing(void);

/* ==================== AI Drawing Service Implementation ==================== */

aicam_result_t ai_draw_service_init(const ai_draw_config_t *config)
{
    if (!config) {
        LOG_SVC_ERROR("Invalid AI draw configuration");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (g_ai_draw_service.initialized) {
        LOG_SVC_WARN("AI draw service already initialized");
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Initializing AI draw service: %dx%d", config->image_width, config->image_height);
    
    // Initialize service context
    memset(&g_ai_draw_service, 0, sizeof(ai_draw_service_t));
    memcpy(&g_ai_draw_service.config, config, sizeof(ai_draw_config_t));
    
    // Setup draw device
    aicam_result_t result = ai_draw_setup_draw_device();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to setup draw device: %d", result);
        return result;
    }
    
    // Initialize fonts
    result = ai_draw_init_fonts();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize fonts: %d", result);
        return result;
    }
    
    // Configure OD drawing
    result = ai_draw_configure_od_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to configure OD drawing: %d", result);
        ai_draw_deinit_fonts();
        return result;
    }
    
    // Configure MPE drawing
    result = ai_draw_configure_mpe_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to configure MPE drawing: %d", result);
        od_draw_deinit(&g_ai_draw_service.od_draw_conf);
        ai_draw_deinit_fonts();
        return result;
    }

    // Configure SPE drawing
    result = ai_draw_configure_spe_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to configure SPE drawing: %d", result);
        mpe_draw_deinit(&g_ai_draw_service.mpe_draw_conf);
        od_draw_deinit(&g_ai_draw_service.od_draw_conf);
        ai_draw_deinit_fonts();
        return result;
    }

    // Configure ISEG drawing
    result = ai_draw_configure_iseg_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to configure ISEG drawing: %d", result);
        spe_draw_deinit(&g_ai_draw_service.spe_draw_conf);  // SPE owns its font
        mpe_draw_deinit(&g_ai_draw_service.mpe_draw_conf);
        od_draw_deinit(&g_ai_draw_service.od_draw_conf);
        ai_draw_deinit_fonts();
        return result;
    }

    g_ai_draw_service.initialized = AICAM_TRUE;
    
    LOG_SVC_INFO("AI draw service initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_draw_service_deinit(void)
{
    if (!g_ai_draw_service.initialized) {
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Deinitializing AI draw service");
    
    // Deinitialize MPE drawing
    mpe_draw_deinit(&g_ai_draw_service.mpe_draw_conf);

    // Deinitialize SPE drawing
    spe_draw_deinit(&g_ai_draw_service.spe_draw_conf);

    // Deinitialize ISEG drawing
    iseg_draw_deinit(&g_ai_draw_service.iseg_draw_conf);
    
    // Deinitialize OD drawing
    od_draw_deinit(&g_ai_draw_service.od_draw_conf);
    
    // Deinitialize fonts
    ai_draw_deinit_fonts();
    
    // Reset context
    memset(&g_ai_draw_service, 0, sizeof(ai_draw_service_t));
    
    LOG_SVC_INFO("AI draw service deinitialized");
    
    return AICAM_OK;
}

aicam_result_t ai_draw_results(uint8_t *fb, 
                               uint32_t fb_width, 
                               uint32_t fb_height, 
                               const nn_result_t *result)
{
    if (!fb || !result) {
        LOG_SVC_ERROR("Invalid parameters for AI draw results");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    aicam_result_t result_code = AICAM_OK;
    
    // Draw based on result type
    switch (result->type) {
        case PP_TYPE_OD:
            if (result->od.nb_detect > 0) {
                result_code = ai_draw_od_results(fb, fb_width, fb_height, &result->od);
            }
            break;
            
        case PP_TYPE_MPE:
            if (result->mpe.nb_detect > 0) {
                result_code = ai_draw_mpe_results(fb, fb_width, fb_height, &result->mpe);
            }
            break;

        case PP_TYPE_SPE:
            if (result->spe.nb_keypoints > 0) {
                result_code = ai_draw_spe_results(fb, fb_width, fb_height, &result->spe);
            }
            break;

        case PP_TYPE_ISEG:
            if (result->iseg.nb_detect > 0) {
                result_code = ai_draw_iseg_results(fb, fb_width, fb_height, &result->iseg);
            }
            break;
            
        default:
            LOG_SVC_WARN("Unsupported AI result type: %d", result->type);
            result_code = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    
    return result_code;
}

aicam_result_t ai_draw_od_results(uint8_t *fb, 
                                  uint32_t fb_width, 
                                  uint32_t fb_height, 
                                  const pp_od_out_t *od_result)
{
    if (!fb || !od_result) {
        LOG_SVC_ERROR("Invalid parameters for OD draw results");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update drawing configuration with current frame buffer
    g_ai_draw_service.od_draw_conf.p_dst = fb;
    g_ai_draw_service.od_draw_conf.image_width = fb_width;
    g_ai_draw_service.od_draw_conf.image_height = fb_height;
    
    // Draw each detection
    for (int i = 0; i < od_result->nb_detect; i++) {
        aicam_result_t result = od_draw_result(&g_ai_draw_service.od_draw_conf, (od_detect_t *)&od_result->detects[i]);
        if (result != 0) {
            LOG_SVC_ERROR("Failed to draw OD detection %d: %d", i, result);
            return AICAM_ERROR;
        }
    }
    
    return AICAM_OK;
}

aicam_result_t ai_draw_mpe_results(uint8_t *fb, 
                                   uint32_t fb_width, 
                                   uint32_t fb_height, 
                                   const pp_mpe_out_t *mpe_result)
{
    if (!fb || !mpe_result) {
        LOG_SVC_ERROR("Invalid parameters for MPE draw results");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update drawing configuration with current frame buffer
    g_ai_draw_service.mpe_draw_conf.p_dst = fb;
    g_ai_draw_service.mpe_draw_conf.image_width = fb_width;
    g_ai_draw_service.mpe_draw_conf.image_height = fb_height;
    
    // Draw each detection
    for (int i = 0; i < mpe_result->nb_detect; i++) {
        aicam_result_t result = mpe_draw_result(&g_ai_draw_service.mpe_draw_conf, &mpe_result->detects[i]);
        if (result != 0) {
            LOG_SVC_ERROR("Failed to draw MPE detection %d: %d", i, result);
            return AICAM_ERROR;
        }
    }
    
    return AICAM_OK;
}

aicam_result_t ai_draw_spe_results(uint8_t *fb,
                                   uint32_t fb_width,
                                   uint32_t fb_height,
                                   const pp_spe_out_t *spe_result)
{
    if (!fb || !spe_result) {
        LOG_SVC_ERROR("Invalid parameters for SPE draw results");
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Update drawing configuration with current frame buffer
    g_ai_draw_service.spe_draw_conf.p_dst = fb;
    g_ai_draw_service.spe_draw_conf.image_width = fb_width;
    g_ai_draw_service.spe_draw_conf.image_height = fb_height;

    // Draw the single pose instance
    aicam_result_t result = spe_draw_result(&g_ai_draw_service.spe_draw_conf, spe_result);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to draw SPE result: %d", result);
        return AICAM_ERROR;
    }

    return AICAM_OK;
}

aicam_result_t ai_draw_single_od(uint8_t *fb,
                                 uint32_t fb_width,
                                 uint32_t fb_height,
                                 const od_detect_t *detection)
{
    if (!fb || !detection) {
        LOG_SVC_ERROR("Invalid parameters for single OD draw");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update drawing configuration with current frame buffer
    g_ai_draw_service.od_draw_conf.p_dst = fb;
    g_ai_draw_service.od_draw_conf.image_width = fb_width;
    g_ai_draw_service.od_draw_conf.image_height = fb_height;
    
    // Draw single detection
    aicam_result_t result = od_draw_result(&g_ai_draw_service.od_draw_conf, (od_detect_t *)detection);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to draw single OD detection: %d", result);
        return AICAM_ERROR;
    }
    
    return AICAM_OK;
}

aicam_result_t ai_draw_single_mpe(uint8_t *fb, 
                                  uint32_t fb_width, 
                                  uint32_t fb_height, 
                                  const mpe_detect_t *detection)
{
    if (!fb || !detection) {
        LOG_SVC_ERROR("Invalid parameters for single MPE draw");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update drawing configuration with current frame buffer
    g_ai_draw_service.mpe_draw_conf.p_dst = fb;
    g_ai_draw_service.mpe_draw_conf.image_width = fb_width;
    g_ai_draw_service.mpe_draw_conf.image_height = fb_height;
    
    // Draw single detection
    aicam_result_t result = mpe_draw_result(&g_ai_draw_service.mpe_draw_conf, (mpe_detect_t *)detection);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to draw single MPE detection: %d", result);
        return AICAM_ERROR;
    }
    
    return AICAM_OK;
}

/* ==================== Configuration Functions ==================== */

void ai_draw_get_default_config(ai_draw_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(ai_draw_config_t));
    config->image_width = 1280;
    config->image_height = 720;
    config->line_width = 2;
    config->box_line_width = 2;
    config->dot_width = 10;  // matches the effective MPE keypoint dot size
    config->od_color = COLOR_RED;
    config->mpe_color = COLOR_BLUE;
    config->spe_color = COLOR_BLUE;
    config->iseg_color = COLOR_CYAN;
    config->iseg_mask_alpha = 102;
    config->iseg_mask_threshold = 128;
    config->enable_text = AICAM_TRUE;
    config->enable_keypoints = AICAM_TRUE;
}

aicam_result_t ai_draw_set_config(const ai_draw_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(&g_ai_draw_service.config, config, sizeof(ai_draw_config_t));
    
    // Reconfigure drawing settings
    aicam_result_t result = ai_draw_configure_od_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reconfigure OD drawing: %d", result);
        return result;
    }
    
    result = ai_draw_configure_mpe_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reconfigure MPE drawing: %d", result);
        return result;
    }

    result = ai_draw_configure_spe_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reconfigure SPE drawing: %d", result);
        return result;
    }

    result = ai_draw_configure_iseg_drawing();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reconfigure ISEG drawing: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("AI draw configuration updated");
    
    return AICAM_OK;
}

aicam_result_t ai_draw_get_config(ai_draw_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &g_ai_draw_service.config, sizeof(ai_draw_config_t));
    
    return AICAM_OK;
}

/* ==================== Utility Functions ==================== */

aicam_bool_t ai_draw_is_initialized(void)
{
    return g_ai_draw_service.initialized;
}

ai_draw_service_t* ai_draw_get_context(void)
{
    return g_ai_draw_service.initialized ? &g_ai_draw_service : NULL;
}

/* ==================== Internal Functions ==================== */

static aicam_result_t ai_draw_init_fonts(void)
{
    if (!g_ai_draw_service.draw_device) {
        LOG_SVC_ERROR("Draw device not available");
        return AICAM_ERROR;
    }
    
    // Setup 12pt font
    draw_fontsetup_param_t font_param = {0};
    font_param.p_font_in = &Font12;
    font_param.p_font = &g_ai_draw_service.font_12;
    aicam_result_t result = device_ioctl(g_ai_draw_service.draw_device, 
                                        DRAW_CMD_FONT_SETUP, 
                                        (uint8_t *)&font_param, 
                                        sizeof(draw_fontsetup_param_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to setup 12pt font: %d", result);
        return result;
    }
    
    // Setup 16pt font
    font_param.p_font_in = &Font16;
    font_param.p_font = &g_ai_draw_service.font_16;
    result = device_ioctl(g_ai_draw_service.draw_device, 
                         DRAW_CMD_FONT_SETUP, 
                         (uint8_t *)&font_param, 
                         sizeof(draw_fontsetup_param_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to setup 16pt font: %d", result);
        return result;
    }
    
    // Setup color mode
    draw_colormode_param_t draw_param = {0};
    draw_param.in_colormode = fmt_dcmipp_to_dma2d(DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1);
    draw_param.out_colormode = DMA2D_OUTPUT_RGB565;
    device_ioctl(g_ai_draw_service.draw_device, DRAW_CMD_SET_COLOR_MODE, (uint8_t *)&draw_param, sizeof(draw_colormode_param_t));

    LOG_SVC_DEBUG("Fonts initialized successfully");
    
    return AICAM_OK;
}

static aicam_result_t ai_draw_deinit_fonts(void)
{
    // Free font data
    if (g_ai_draw_service.font_12.data) {
        buffer_free(g_ai_draw_service.font_12.data);
        g_ai_draw_service.font_12.data = NULL;
    }
    
    if (g_ai_draw_service.font_16.data) {
        buffer_free(g_ai_draw_service.font_16.data);
        g_ai_draw_service.font_16.data = NULL;
    }
    
    LOG_SVC_DEBUG("Fonts deinitialized");
    
    return AICAM_OK;
}

static aicam_result_t ai_draw_setup_draw_device(void)
{
    // Find draw device
    g_ai_draw_service.draw_device = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!g_ai_draw_service.draw_device) {
        LOG_SVC_ERROR("Draw device not found");
        return AICAM_ERROR;
    }
    
    // Setup color mode
    draw_colormode_param_t draw_param = {0};
    draw_param.in_colormode = DMA2D_INPUT_RGB565;
    draw_param.out_colormode = DMA2D_OUTPUT_RGB565;
    aicam_result_t result = device_ioctl(g_ai_draw_service.draw_device, 
                                        DRAW_CMD_SET_COLOR_MODE, 
                                        (uint8_t *)&draw_param, 
                                        sizeof(draw_colormode_param_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set draw color mode: %d", result);
        return result;
    }
    
    LOG_SVC_DEBUG("Draw device setup completed");
    
    return AICAM_OK;
}

static aicam_result_t ai_draw_configure_od_drawing(void)
{
    // Configure OD drawing
    g_ai_draw_service.od_draw_conf.p_dst = NULL;  // Will be set during drawing
    g_ai_draw_service.od_draw_conf.color = g_ai_draw_service.config.od_color;
    g_ai_draw_service.od_draw_conf.image_width = g_ai_draw_service.config.image_width;
    g_ai_draw_service.od_draw_conf.image_height = g_ai_draw_service.config.image_height;
    g_ai_draw_service.od_draw_conf.line_width = g_ai_draw_service.config.line_width;
    g_ai_draw_service.od_draw_conf.font = g_ai_draw_service.font_12;
    
    // Initialize OD drawing
    aicam_result_t result = od_draw_init(&g_ai_draw_service.od_draw_conf);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to initialize OD drawing: %d", result);
        return AICAM_ERROR;
    }
    
    LOG_SVC_DEBUG("OD drawing configured");
    
    return AICAM_OK;
}

static aicam_result_t ai_draw_configure_mpe_drawing(void)
{
    // Configure MPE drawing
    g_ai_draw_service.mpe_draw_conf.p_dst = NULL;  // Will be set during drawing
    g_ai_draw_service.mpe_draw_conf.color = g_ai_draw_service.config.mpe_color;
    g_ai_draw_service.mpe_draw_conf.image_width = g_ai_draw_service.config.image_width;
    g_ai_draw_service.mpe_draw_conf.image_height = g_ai_draw_service.config.image_height;
    g_ai_draw_service.mpe_draw_conf.line_width = g_ai_draw_service.config.line_width;
    g_ai_draw_service.mpe_draw_conf.box_line_width = g_ai_draw_service.config.box_line_width;
    g_ai_draw_service.mpe_draw_conf.dot_width = g_ai_draw_service.config.dot_width;
    g_ai_draw_service.mpe_draw_conf.font = g_ai_draw_service.font_12;
    
    // Initialize MPE drawing
    aicam_result_t result = mpe_draw_init(&g_ai_draw_service.mpe_draw_conf);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to initialize MPE drawing: %d", result);
        return AICAM_ERROR;
    }
    
    LOG_SVC_DEBUG("MPE drawing configured");

    return AICAM_OK;
}

static aicam_result_t ai_draw_configure_spe_drawing(void)
{
    // Configure SPE drawing (the font is owned and set up by spe_draw_init;
    // do not alias the shared font_12 here, spe_draw_init would free it)
    g_ai_draw_service.spe_draw_conf.p_dst = NULL;  // Will be set during drawing
    g_ai_draw_service.spe_draw_conf.color = g_ai_draw_service.config.spe_color;
    g_ai_draw_service.spe_draw_conf.image_width = g_ai_draw_service.config.image_width;
    g_ai_draw_service.spe_draw_conf.image_height = g_ai_draw_service.config.image_height;
    g_ai_draw_service.spe_draw_conf.line_width = g_ai_draw_service.config.line_width;
    g_ai_draw_service.spe_draw_conf.dot_width = g_ai_draw_service.config.dot_width;

    // Initialize SPE drawing
    aicam_result_t result = spe_draw_init(&g_ai_draw_service.spe_draw_conf);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to initialize SPE drawing: %d", result);
        return AICAM_ERROR;
    }

    LOG_SVC_DEBUG("SPE drawing configured");

    return AICAM_OK;
}

aicam_result_t ai_draw_iseg_results(uint8_t *fb,
                                     uint32_t fb_width,
                                     uint32_t fb_height,
                                     const pp_iseg_out_t *iseg_result)
{
    if (!fb || !iseg_result) {
        LOG_SVC_ERROR("Invalid parameters for ISEG draw results");
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_ai_draw_service.initialized) {
        LOG_SVC_ERROR("AI draw service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    g_ai_draw_service.iseg_draw_conf.p_dst = fb;
    g_ai_draw_service.iseg_draw_conf.image_width = fb_width;
    g_ai_draw_service.iseg_draw_conf.image_height = fb_height;

    for (int i = 0; i < iseg_result->nb_detect; i++) {
        aicam_result_t result = iseg_draw_result(&g_ai_draw_service.iseg_draw_conf,
                                                  (iseg_detect_t *)&iseg_result->detects[i], i);
        if (result != 0) {
            LOG_SVC_ERROR("Failed to draw ISEG detection %d: %d", i, result);
            return AICAM_ERROR;
        }
    }

    return AICAM_OK;
}

static aicam_result_t ai_draw_configure_iseg_drawing(void)
{
    g_ai_draw_service.iseg_draw_conf.p_dst = NULL;
    g_ai_draw_service.iseg_draw_conf.image_width = g_ai_draw_service.config.image_width;
    g_ai_draw_service.iseg_draw_conf.image_height = g_ai_draw_service.config.image_height;
    g_ai_draw_service.iseg_draw_conf.line_width = g_ai_draw_service.config.line_width;
    g_ai_draw_service.iseg_draw_conf.font = g_ai_draw_service.font_12;
    g_ai_draw_service.iseg_draw_conf.mask_alpha = g_ai_draw_service.config.iseg_mask_alpha;
    g_ai_draw_service.iseg_draw_conf.mask_threshold = g_ai_draw_service.config.iseg_mask_threshold;

    aicam_result_t result = iseg_draw_init(&g_ai_draw_service.iseg_draw_conf);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to initialize ISEG drawing: %d", result);
        return AICAM_ERROR;
    }

    LOG_SVC_DEBUG("ISEG drawing configured");

    return AICAM_OK;
}
