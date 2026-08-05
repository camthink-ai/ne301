#include "quick_storage.h"
#include "quick_trace.h"
#include "json_config_internal.h"
#include "json_config_mgr.h"
#include "camera.h"
#include "cmsis_os2.h"
#include "mem.h"
#include "storage.h"
#include "sd_file.h"
#include "generic_file.h"
#include "common_utils.h"
#include "tx_api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const aicam_global_config_t default_config;

/* NVS reads for Quick Bootstrap only: same keys/format as json_config_nvs_*, no json_config init. */
static aicam_result_t qs_nvs_read_string(const char *key, char *value, size_t max_len)
{
    int result = storage_nvs_read(NVS_USER, key, value, max_len);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_uint32(const char *key, uint32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (uint32_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_uint8(const char *key, uint8_t *value)
{
    char value_str[4];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (uint8_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_bool(const char *key, aicam_bool_t *value)
{
    char value_str[2];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (strcmp(value_str, "1") == 0) ? AICAM_TRUE : AICAM_FALSE;
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_int32(const char *key, int32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (int32_t)strtol(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

/* Load `isp_config_t` from user NVS (same layout as json_config_load path). */
static void qs_load_isp_config_from_nvs(isp_config_t *isp)
{
    if (!isp) {
        return;
    }
    memset(isp, 0, sizeof(*isp));
    aicam_bool_t temp_bool = AICAM_FALSE;
    uint32_t temp_uint32 = 0;
    uint8_t temp_uint8 = 0;
    int32_t temp_int32 = 0;

    if (qs_nvs_read_bool(NVS_KEY_ISP_VALID, &temp_bool) != AICAM_OK) {
        return;
    }
    isp->valid = temp_bool;
    if (!isp->valid) {
        return;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_SR_ENABLE, &temp_bool) == AICAM_OK) {
        isp->stat_removal_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SR_HEADLINES, &temp_uint32) == AICAM_OK) {
        isp->stat_removal_head_lines = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SR_VALIDLINES, &temp_uint32) == AICAM_OK) {
        isp->stat_removal_valid_lines = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_DEMO_ENABLE, &temp_bool) == AICAM_OK) {
        isp->demosaic_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_TYPE, &temp_uint8) == AICAM_OK) {
        isp->demosaic_type = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_PEAK, &temp_uint8) == AICAM_OK) {
        isp->demosaic_peak = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_LINEV, &temp_uint8) == AICAM_OK) {
        isp->demosaic_line_v = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_LINEH, &temp_uint8) == AICAM_OK) {
        isp->demosaic_line_h = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_EDGE, &temp_uint8) == AICAM_OK) {
        isp->demosaic_edge = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_CONTRAST_ENABLE, &temp_bool) == AICAM_OK) {
        isp->contrast_enable = temp_bool;
    }
    (void)storage_nvs_read(NVS_USER, NVS_KEY_ISP_CONTRAST_LUT, isp->contrast_lut, sizeof(isp->contrast_lut));

    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_X, &temp_uint32) == AICAM_OK) {
        isp->stat_area_x = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_Y, &temp_uint32) == AICAM_OK) {
        isp->stat_area_y = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_W, &temp_uint32) == AICAM_OK) {
        isp->stat_area_width = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_H, &temp_uint32) == AICAM_OK) {
        isp->stat_area_height = temp_uint32;
    }

    if (qs_nvs_read_uint32(NVS_KEY_ISP_SENSOR_GAIN, &temp_uint32) == AICAM_OK) {
        isp->sensor_gain = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SENSOR_EXPO, &temp_uint32) == AICAM_OK) {
        isp->sensor_exposure = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BPA_ENABLE, &temp_bool) == AICAM_OK) {
        isp->bad_pixel_algo_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_BPA_THRESH, &temp_uint32) == AICAM_OK) {
        isp->bad_pixel_algo_threshold = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BP_ENABLE, &temp_bool) == AICAM_OK) {
        isp->bad_pixel_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BP_STRENGTH, &temp_uint8) == AICAM_OK) {
        isp->bad_pixel_strength = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BL_ENABLE, &temp_bool) == AICAM_OK) {
        isp->black_level_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_R, &temp_uint8) == AICAM_OK) {
        isp->black_level_r = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_G, &temp_uint8) == AICAM_OK) {
        isp->black_level_g = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_B, &temp_uint8) == AICAM_OK) {
        isp->black_level_b = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_AEC_ENABLE, &temp_bool) == AICAM_OK) {
        isp->aec_enable = temp_bool;
    }
    if (qs_nvs_read_int32(NVS_KEY_ISP_AEC_EXPCOMP, &temp_int32) == AICAM_OK) {
        isp->aec_exposure_compensation = temp_int32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_AEC_AFLK, &temp_uint32) == AICAM_OK) {
        isp->aec_anti_flicker_freq = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_AWB_ENABLE, &temp_bool) == AICAM_OK) {
        isp->awb_enable = temp_bool;
    }
    typedef struct {
        char label[ISP_AWB_PROFILES_MAX][ISP_AWB_LABEL_MAX_LEN];
        uint32_t ref_color_temp[ISP_AWB_PROFILES_MAX];
        uint32_t gain_r[ISP_AWB_PROFILES_MAX];
        uint32_t gain_g[ISP_AWB_PROFILES_MAX];
        uint32_t gain_b[ISP_AWB_PROFILES_MAX];
        int32_t ccm[ISP_AWB_PROFILES_MAX][3][3];
        uint8_t ref_rgb[ISP_AWB_PROFILES_MAX][3];
    } awb_data_t;
    awb_data_t awb_data;
    if (storage_nvs_read(NVS_USER, NVS_KEY_ISP_AWB_DATA, &awb_data, sizeof(awb_data)) >= 0) {
        memcpy(isp->awb_label, awb_data.label, sizeof(isp->awb_label));
        memcpy(isp->awb_ref_color_temp, awb_data.ref_color_temp, sizeof(isp->awb_ref_color_temp));
        memcpy(isp->awb_gain_r, awb_data.gain_r, sizeof(isp->awb_gain_r));
        memcpy(isp->awb_gain_g, awb_data.gain_g, sizeof(isp->awb_gain_g));
        memcpy(isp->awb_gain_b, awb_data.gain_b, sizeof(isp->awb_gain_b));
        memcpy(isp->awb_ccm, awb_data.ccm, sizeof(isp->awb_ccm));
        memcpy(isp->awb_ref_rgb, awb_data.ref_rgb, sizeof(isp->awb_ref_rgb));
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_GAIN_ENABLE, &temp_bool) == AICAM_OK) {
        isp->isp_gain_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_R, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_r = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_G, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_g = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_B, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_b = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_CCM_ENABLE, &temp_bool) == AICAM_OK) {
        isp->color_conv_enable = temp_bool;
    }
    (void)storage_nvs_read(NVS_USER, NVS_KEY_ISP_CCM_DATA, isp->color_conv_matrix, sizeof(isp->color_conv_matrix));

    if (qs_nvs_read_bool(NVS_KEY_ISP_GAMMA_ENABLE, &temp_bool) == AICAM_OK) {
        isp->gamma_enable = temp_bool;
    }

    if (qs_nvs_read_uint8(NVS_KEY_ISP_SENSOR_DELAY, &temp_uint8) == AICAM_OK) {
        isp->sensor_delay = temp_uint8;
    }

    typedef struct {
        uint32_t hl_ref, hl_expo1, hl_expo2;
        uint8_t hl_lum1, hl_lum2;
        uint32_t ll_ref, ll_expo1, ll_expo2;
        uint8_t ll_lum1, ll_lum2;
        float calib_factor;
    } lux_data_t;
    lux_data_t lux_data;
    if (storage_nvs_read(NVS_USER, NVS_KEY_ISP_LUX_DATA, &lux_data, sizeof(lux_data)) >= 0) {
        isp->lux_hl_ref = lux_data.hl_ref;
        isp->lux_hl_expo1 = lux_data.hl_expo1;
        isp->lux_hl_expo2 = lux_data.hl_expo2;
        isp->lux_hl_lum1 = lux_data.hl_lum1;
        isp->lux_hl_lum2 = lux_data.hl_lum2;
        isp->lux_ll_ref = lux_data.ll_ref;
        isp->lux_ll_expo1 = lux_data.ll_expo1;
        isp->lux_ll_expo2 = lux_data.ll_expo2;
        isp->lux_ll_lum1 = lux_data.ll_lum1;
        isp->lux_ll_lum2 = lux_data.ll_lum2;
        isp->lux_calib_factor = lux_data.calib_factor;
    }
}

/* Same field mapping as json_config_config_to_isp_param (no json_config runtime). */
static void qs_isp_config_to_iq_param(const isp_config_t *isp_config, ISP_IQParamTypeDef *isp_param)
{
    if (isp_config == NULL || isp_param == NULL) {
        return;
    }

    memset(isp_param, 0, sizeof(ISP_IQParamTypeDef));

    isp_param->statRemoval.enable = isp_config->stat_removal_enable;
    isp_param->statRemoval.nbHeadLines = isp_config->stat_removal_head_lines;
    isp_param->statRemoval.nbValidLines = isp_config->stat_removal_valid_lines;

    isp_param->demosaicing.enable = isp_config->demosaic_enable;
    isp_param->demosaicing.type = (ISP_DemosTypeTypeDef)isp_config->demosaic_type;
    isp_param->demosaicing.peak = isp_config->demosaic_peak;
    isp_param->demosaicing.lineV = isp_config->demosaic_line_v;
    isp_param->demosaicing.lineH = isp_config->demosaic_line_h;
    isp_param->demosaicing.edge = isp_config->demosaic_edge;

    isp_param->contrast.enable = isp_config->contrast_enable;
    isp_param->contrast.coeff.LUM_0 = isp_config->contrast_lut[0];
    isp_param->contrast.coeff.LUM_32 = isp_config->contrast_lut[1];
    isp_param->contrast.coeff.LUM_64 = isp_config->contrast_lut[2];
    isp_param->contrast.coeff.LUM_96 = isp_config->contrast_lut[3];
    isp_param->contrast.coeff.LUM_128 = isp_config->contrast_lut[4];
    isp_param->contrast.coeff.LUM_160 = isp_config->contrast_lut[5];
    isp_param->contrast.coeff.LUM_192 = isp_config->contrast_lut[6];
    isp_param->contrast.coeff.LUM_224 = isp_config->contrast_lut[7];
    isp_param->contrast.coeff.LUM_256 = isp_config->contrast_lut[8];

    isp_param->statAreaStatic.X0 = isp_config->stat_area_x;
    isp_param->statAreaStatic.Y0 = isp_config->stat_area_y;
    isp_param->statAreaStatic.XSize = isp_config->stat_area_width;
    isp_param->statAreaStatic.YSize = isp_config->stat_area_height;

    isp_param->sensorGainStatic.gain = isp_config->sensor_gain;
    isp_param->sensorExposureStatic.exposure = isp_config->sensor_exposure;

    isp_param->badPixelAlgo.enable = isp_config->bad_pixel_algo_enable;
    isp_param->badPixelAlgo.threshold = isp_config->bad_pixel_algo_threshold;

    isp_param->badPixelStatic.enable = isp_config->bad_pixel_enable;
    isp_param->badPixelStatic.strength = isp_config->bad_pixel_strength;

    isp_param->blackLevelStatic.enable = isp_config->black_level_enable;
    isp_param->blackLevelStatic.BLCR = isp_config->black_level_r;
    isp_param->blackLevelStatic.BLCG = isp_config->black_level_g;
    isp_param->blackLevelStatic.BLCB = isp_config->black_level_b;

    isp_param->AECAlgo.enable = isp_config->aec_enable;
    isp_param->AECAlgo.exposureCompensation = isp_config->aec_exposure_compensation;
    isp_param->AECAlgo.antiFlickerFreq = isp_config->aec_anti_flicker_freq;

    isp_param->AWBAlgo.enable = isp_config->awb_enable;
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
        memcpy(isp_param->AWBAlgo.label[i], isp_config->awb_label[i], ISP_AWB_LABEL_MAX_LEN);
        isp_param->AWBAlgo.referenceColorTemp[i] = isp_config->awb_ref_color_temp[i];
        isp_param->AWBAlgo.ispGainR[i] = isp_config->awb_gain_r[i];
        isp_param->AWBAlgo.ispGainG[i] = isp_config->awb_gain_g[i];
        isp_param->AWBAlgo.ispGainB[i] = isp_config->awb_gain_b[i];
        memcpy(isp_param->AWBAlgo.coeff[i], isp_config->awb_ccm[i], sizeof(isp_param->AWBAlgo.coeff[i]));
        memcpy(isp_param->AWBAlgo.referenceRGB[i], isp_config->awb_ref_rgb[i], sizeof(isp_param->AWBAlgo.referenceRGB[i]));
    }

    isp_param->ispGainStatic.enable = isp_config->isp_gain_enable;
    isp_param->ispGainStatic.ispGainR = isp_config->isp_gain_r;
    isp_param->ispGainStatic.ispGainG = isp_config->isp_gain_g;
    isp_param->ispGainStatic.ispGainB = isp_config->isp_gain_b;

    isp_param->colorConvStatic.enable = isp_config->color_conv_enable;
    memcpy(isp_param->colorConvStatic.coeff, isp_config->color_conv_matrix, sizeof(isp_param->colorConvStatic.coeff));

    isp_param->gamma.enable = isp_config->gamma_enable;

    isp_param->sensorDelay.delay = isp_config->sensor_delay;

    isp_param->luxRef.HL_LuxRef = isp_config->lux_hl_ref;
    isp_param->luxRef.HL_Expo1 = isp_config->lux_hl_expo1;
    isp_param->luxRef.HL_Lum1 = isp_config->lux_hl_lum1;
    isp_param->luxRef.HL_Expo2 = isp_config->lux_hl_expo2;
    isp_param->luxRef.HL_Lum2 = isp_config->lux_hl_lum2;
    isp_param->luxRef.LL_LuxRef = isp_config->lux_ll_ref;
    isp_param->luxRef.LL_Expo1 = isp_config->lux_ll_expo1;
    isp_param->luxRef.LL_Lum1 = isp_config->lux_ll_lum1;
    isp_param->luxRef.LL_Expo2 = isp_config->lux_ll_expo2;
    isp_param->luxRef.LL_Lum2 = isp_config->lux_ll_lum2;
    isp_param->luxRef.calibFactor = isp_config->lux_calib_factor;
}

int quick_storage_fill_isp_iq_param(uint32_t isp_mode, uint8_t grayscale,
                                    ISP_IQParamTypeDef *isp_param)
{
    aicam_bool_t gray_on = (grayscale != 0u) ? AICAM_TRUE : AICAM_FALSE;

    if (!isp_param) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (isp_mode == QS_IMAGE_ISP_MODE_CUSTOM) {
        isp_config_t cfg = {0};
        qs_load_isp_config_from_nvs(&cfg);
        if (cfg.valid) {
            qs_isp_config_to_iq_param(&cfg, isp_param);
            camera_apply_grayscale_iq(isp_param, gray_on);
            return AICAM_OK;
        }
    }

    cam_iq_scene_t scene = CAM_IQ_SCENE_INDOOR;
    if (isp_mode == QS_IMAGE_ISP_MODE_OUTDOOR) {
        scene = CAM_IQ_SCENE_OUTDOOR;
    } else if (isp_mode == QS_IMAGE_ISP_MODE_CUSTOM) {
        /* Align with device_service_build_isp_iq_param: custom without valid NVS profile. */
        scene = CAM_IQ_SCENE_INDOOR;
    }
    camera_fill_isp_iq_scene(scene, isp_param);
    camera_apply_grayscale_iq(isp_param, gray_on);
    return AICAM_OK;
}

typedef struct {
    qs_write_task_param_t param;  /* shallow copy of scalar fields + file_name */
} qs_write_task_item_t;

static osMessageQueueId_t s_write_q = NULL;
static osThreadId_t s_write_tid = NULL;
static aicam_bool_t s_quick_storage_inited = AICAM_FALSE;

/* Avoid MemAlloc() inside osMessageQueueNew (ThreadX CMSIS wrapper) by providing
 * both queue storage and control block statically.
 */
static TX_QUEUE s_write_q_cb ALIGN_32 IN_PSRAM;
static uint8_t s_write_q_mem[
    MAX_WRITE_TASK_QUEUE_SIZE * (((sizeof(qs_write_task_item_t) + (sizeof(ULONG) - 1U)) / sizeof(ULONG)) * sizeof(ULONG))
] ALIGN_32;
static const osMessageQueueAttr_t s_write_q_attr = {
    .name = "qs_write_q",
    .mq_mem = s_write_q_mem,
    .mq_size = sizeof(s_write_q_mem),
    .cb_mem = &s_write_q_cb,
    .cb_size = sizeof(s_write_q_cb),
};

static void u32_to_ipv4(uint32_t v, uint8_t out[4])
{
    if (!out) return;
    out[0] = (uint8_t)((v >> 24) & 0xFF);
    out[1] = (uint8_t)((v >> 16) & 0xFF);
    out[2] = (uint8_t)((v >> 8) & 0xFF);
    out[3] = (uint8_t)(v & 0xFF);
}

static int qs_check_free_space(uint8_t disk_type, size_t bytes_needed)
{
    uint32_t need_kb = (uint32_t)((bytes_needed + 1023U) / 1024U);

    if (disk_type == 2) {
        sd_disk_info_t info = {0};
        if (sd_get_disk_info(&info) != 0) return AICAM_ERROR_IO;
        if (info.mode != SD_MODE_NORMAL) return AICAM_ERROR_UNAVAILABLE;
        if (info.free_KBytes < need_kb) return AICAM_ERROR_QUOTA_EXCEEDED;
        return AICAM_OK;
    }

    storage_disk_info_t info = {0};
    if (storage_get_disk_info(&info) != 0) return AICAM_ERROR_IO;
    if (!info.mounted) return AICAM_ERROR_UNAVAILABLE;
    if (info.free_KBytes < need_kb) return AICAM_ERROR_QUOTA_EXCEEDED;
    return AICAM_OK;
}

static int qs_check_free_space_auto(size_t bytes_needed)
{
    FS_Type_t cur = file_get_current_type();
    if (cur == FS_SD) {
        return qs_check_free_space(2, bytes_needed);
    }
    if (cur == FS_FLASH) {
        return qs_check_free_space(1, bytes_needed);
    }
    return AICAM_ERROR_UNAVAILABLE;
}

static int qs_write_one(const qs_write_task_item_t *item)
{
    if (!item) return AICAM_ERROR_INVALID_PARAM;

    const qs_write_task_param_t *p = &item->param;
    if (!p->file_name[0] || !p->data || p->data_len == 0) return AICAM_ERROR_INVALID_PARAM;

    /* disk_type: 0=auto(generic), 1=flash, 2=sd */
    uint8_t disk_type = p->disk_type;
    if (disk_type > 2) return AICAM_ERROR_INVALID_PARAM;

    if (disk_type == 2) {
        sd_disk_info_t sdinfo = {0};
        if (sd_get_disk_info(&sdinfo) != 0) return AICAM_ERROR_IO;
        if (sdinfo.mode != SD_MODE_NORMAL) return AICAM_ERROR_UNAVAILABLE;
    }

    /* space check before open/write */
    if (disk_type == 0) {
        int rc = qs_check_free_space_auto(p->data_len);
        if (rc != AICAM_OK) return rc;
    } else {
        int rc = qs_check_free_space(disk_type, p->data_len);
        if (rc != AICAM_OK) return rc;
    }

    const char *mode = (p->mode == 0) ? "ab" : "wb";
    void *fd = NULL;
    int wret = AICAM_OK;

    if (disk_type == 0) {
        fd = file_fopen(p->file_name, mode);
        if (!fd) return AICAM_ERROR_IO;
        if (file_fwrite(fd, p->data, p->data_len) != (int)p->data_len) wret = AICAM_ERROR_IO;
        (void)file_fflush(fd);
        (void)file_fclose(fd);
        return wret;
    }

    FS_Type_t fs_type = (disk_type == 2) ? FS_SD : FS_FLASH;
    fd = disk_file_fopen(fs_type, p->file_name, mode);
    if (!fd) return AICAM_ERROR_IO;
    if (disk_file_fwrite(fs_type, fd, p->data, p->data_len) != (int)p->data_len) wret = AICAM_ERROR_IO;
    (void)disk_file_fflush(fs_type, fd);
    (void)disk_file_fclose(fs_type, fd);
    return wret;
}

static void qs_write_thread(void *arg)
{
    (void)arg;
    QT_TRACE("[QST] ", "write start");
    for (;;) {
        qs_write_task_item_t item = {0};
        if (osMessageQueueGet(s_write_q, &item, NULL, osWaitForever) != osOK) {
            continue;
        }

        qt_prof_t prof;
        qt_prof_init(&prof);
        QT_TRACE("[QST] ", "write %s %luB d%u m%u",
                 item.param.file_name,
                 (unsigned long)item.param.data_len,
                 (unsigned)item.param.disk_type,
                 (unsigned)item.param.mode);
        int result = qs_write_one(&item);
        if (result != AICAM_OK) {
            QT_TRACE("[QST] ", "write rc=%d", result);
        }
        qt_prof_step(&prof, "[QST] write:done ");
        if (item.param.callback) {
            item.param.callback(result, item.param.callback_param);
        }
    }
}

int quick_storage_read_snapshot_config(qs_snapshot_config_t *snapshot_config)
{
    if (!snapshot_config) return AICAM_ERROR_INVALID_PARAM;

    /* Defaults aligned with `default_config` in json_config_mgr.c */
    memset(snapshot_config, 0, sizeof(*snapshot_config));
    snapshot_config->ai_enabled = (uint8_t)default_config.ai_debug.ai_enabled;
    snapshot_config->ai_1_active = (uint8_t)default_config.ai_debug.ai_1_active;
    snapshot_config->ai_pipe_width = 0;
    snapshot_config->ai_pipe_height = 0;
    snapshot_config->confidence_threshold = default_config.ai_debug.confidence_threshold;
    snapshot_config->nms_threshold = default_config.ai_debug.nms_threshold;

    snapshot_config->light_mode = (uint8_t)default_config.device_service.light_config.mode;
    snapshot_config->light_threshold = default_config.device_service.light_config.light_threshold;
    snapshot_config->light_brightness = default_config.device_service.light_config.brightness_level;
    snapshot_config->light_start_time =
        (default_config.device_service.light_config.start_hour * 3600U) +
        (default_config.device_service.light_config.start_minute * 60U);
    snapshot_config->light_end_time =
        (default_config.device_service.light_config.end_hour * 3600U) +
        (default_config.device_service.light_config.end_minute * 60U);

    if (default_config.device_service.image_config.horizontal_flip &&
        default_config.device_service.image_config.vertical_flip) snapshot_config->mirror_flip = 3;
    else if (default_config.device_service.image_config.horizontal_flip) snapshot_config->mirror_flip = 2;
    else if (default_config.device_service.image_config.vertical_flip) snapshot_config->mirror_flip = 1;
    else snapshot_config->mirror_flip = 0;

    snapshot_config->fast_capture_skip_frames = default_config.device_service.image_config.fast_capture_skip_frames;
    snapshot_config->fast_capture_resolution = default_config.device_service.image_config.fast_capture_resolution;
    snapshot_config->fast_capture_jpeg_quality = default_config.device_service.image_config.fast_capture_jpeg_quality;
    snapshot_config->capture_storage_ai = default_config.device_service.image_config.capture_storage_ai;
    snapshot_config->isp_mode = default_config.device_service.image_config.isp_mode;
    snapshot_config->grayscale = (uint8_t)default_config.device_service.image_config.grayscale;

    aicam_result_t result;
    aicam_bool_t temp_bool = AICAM_FALSE;
    uint8_t temp_u8 = 0;
    uint32_t temp_u32 = 0;

    // result = qs_nvs_read_bool(NVS_KEY_AI_ENABLE, &temp_bool);
    // if (result == AICAM_OK) snapshot_config->ai_enabled = (uint8_t)temp_bool;
    // Match application layer: AI enabled by default
    snapshot_config->ai_enabled = AICAM_TRUE;

    if (snapshot_config->ai_enabled) {
        result = qs_nvs_read_bool(NVS_KEY_AI_1_ACTIVE, &temp_bool);
        if (result == AICAM_OK) snapshot_config->ai_1_active = (uint8_t)temp_bool;

        /* AI pipe dimensions: keep 0 if read fails or invalid; set after model info is known */
        result = qs_nvs_read_uint32(NVS_KEY_AI_PIPE_WIDTH, &temp_u32);
        if (result == AICAM_OK) snapshot_config->ai_pipe_width = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_AI_PIPE_HEIGHT, &temp_u32);
        if (result == AICAM_OK) snapshot_config->ai_pipe_height = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_CONFIDENCE, &temp_u32);
        if (result == AICAM_OK) snapshot_config->confidence_threshold = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_NMS_THRESHOLD, &temp_u32);
        if (result == AICAM_OK) snapshot_config->nms_threshold = temp_u32;
    }

    result = qs_nvs_read_uint8(NVS_KEY_LIGHT_MODE, &temp_u8);
    if (result == AICAM_OK) snapshot_config->light_mode = temp_u8;

    if (snapshot_config->light_mode == LIGHT_MODE_AUTO) {
        result = qs_nvs_read_uint32(NVS_KEY_LIGHT_THRESHOLD, &temp_u32);
        if (result == AICAM_OK) snapshot_config->light_threshold = temp_u32;
    }

    if (snapshot_config->light_mode != LIGHT_MODE_OFF) {
        result = qs_nvs_read_uint32(NVS_KEY_LIGHT_BRIGHTNESS, &temp_u32);
        if (result == AICAM_OK) snapshot_config->light_brightness = temp_u32;
    }

    if (snapshot_config->light_mode == LIGHT_MODE_CUSTOM) {
        /* light custom schedule: store as seconds from 00:00 */
        uint32_t sh = 0, sm = 0, eh = 0, em = 0;
        if (qs_nvs_read_uint32(NVS_KEY_LIGHT_START_HOUR, &sh) != AICAM_OK) sh = default_config.device_service.light_config.start_hour;
        if (qs_nvs_read_uint32(NVS_KEY_LIGHT_START_MIN, &sm) != AICAM_OK) sm = default_config.device_service.light_config.start_minute;
        if (qs_nvs_read_uint32(NVS_KEY_LIGHT_END_HOUR, &eh) != AICAM_OK) eh = default_config.device_service.light_config.end_hour;
        if (qs_nvs_read_uint32(NVS_KEY_LIGHT_END_MIN, &em) != AICAM_OK) em = default_config.device_service.light_config.end_minute;
        snapshot_config->light_start_time = (sh * 3600U) + (sm * 60U);
        snapshot_config->light_end_time = (eh * 3600U) + (em * 60U);
    }

    {
        /* Mirror/flip: from boolean HFLIP/VFLIP */
        aicam_bool_t hflip = default_config.device_service.image_config.horizontal_flip;
        aicam_bool_t vflip = default_config.device_service.image_config.vertical_flip;
        (void)qs_nvs_read_bool(NVS_KEY_IMAGE_HFLIP, &hflip);
        (void)qs_nvs_read_bool(NVS_KEY_IMAGE_VFLIP, &vflip);
        if (hflip && vflip) snapshot_config->mirror_flip = 3;
        else if (hflip) snapshot_config->mirror_flip = 2;
        else if (vflip) snapshot_config->mirror_flip = 1;
        else snapshot_config->mirror_flip = 0;
    }

    result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_SKIP_FRAMES, &temp_u32);
    if (result == AICAM_OK) snapshot_config->fast_capture_skip_frames = temp_u32;

    result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_RESOLUTION, &temp_u32);
    if (result == AICAM_OK) snapshot_config->fast_capture_resolution = temp_u32;

    result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_JPEG_QUALITY, &temp_u32);
    if (result == AICAM_OK) snapshot_config->fast_capture_jpeg_quality = temp_u32;

    result = qs_nvs_read_bool(NVS_KEY_CAPTURE_STORAGE_AI, &temp_bool);
    if (result == AICAM_OK) snapshot_config->capture_storage_ai = (uint8_t)temp_bool;

    result = qs_nvs_read_uint32(NVS_KEY_IMAGE_ISP_MODE, &temp_u32);
    if (result == AICAM_OK) snapshot_config->isp_mode = temp_u32;
    if (snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_INDOOR &&
        snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_OUTDOOR &&
        snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_CUSTOM) {
        snapshot_config->isp_mode = default_config.device_service.image_config.isp_mode;
    }

    result = qs_nvs_read_bool(NVS_KEY_IMAGE_GRAYSCALE, &temp_bool);
    if (result == AICAM_OK) {
        snapshot_config->grayscale = (uint8_t)temp_bool;
    }

    return AICAM_OK;
}

int quick_storage_read_work_mode_config(qs_work_mode_config_t *work_mode_config)
{
    if (!work_mode_config) return AICAM_ERROR_INVALID_PARAM;
    memset(work_mode_config, 0, sizeof(*work_mode_config));

    /* Defaults aligned with `default_config` */
    work_mode_config->work_mode = (uint8_t)default_config.work_mode_config.work_mode;
    work_mode_config->image_mode_enabled = (uint8_t)default_config.work_mode_config.image_mode.enable;

    work_mode_config->pir_trigger_enabled = (uint8_t)default_config.work_mode_config.pir_trigger.enable;
    work_mode_config->pir_trigger_type = (uint8_t)default_config.work_mode_config.pir_trigger.trigger_type;
    work_mode_config->pir_trigger_sensitivity = (uint8_t)default_config.work_mode_config.pir_trigger.sensitivity_level;
    work_mode_config->pir_trigger_ignore_time = (uint8_t)default_config.work_mode_config.pir_trigger.ignore_time_s;
    work_mode_config->pir_trigger_pulse_count = (uint8_t)default_config.work_mode_config.pir_trigger.pulse_count;
    work_mode_config->pir_trigger_window_time = (uint8_t)default_config.work_mode_config.pir_trigger.window_time_s;

    work_mode_config->timer_trigger_enabled = (uint8_t)default_config.work_mode_config.timer_trigger.enable;
    work_mode_config->timer_trigger_capture_mode = (uint8_t)default_config.work_mode_config.timer_trigger.capture_mode;
    work_mode_config->timer_trigger_interval_sec = default_config.work_mode_config.timer_trigger.interval_sec;
    work_mode_config->timer_trigger_time_node_count = default_config.work_mode_config.timer_trigger.time_node_count;
    for (uint32_t i = 0; i < 10; i++) {
        work_mode_config->timer_trigger_time_node[i] = default_config.work_mode_config.timer_trigger.time_node[i];
        work_mode_config->timer_trigger_weekdays[i] = default_config.work_mode_config.timer_trigger.weekdays[i];
    }

    work_mode_config->remote_trigger_enabled = (uint8_t)default_config.work_mode_config.remote_trigger.enable;

    aicam_result_t result;
    aicam_bool_t temp_bool = AICAM_FALSE;
    uint32_t temp_u32 = 0;
    uint8_t temp_u8 = 0;

    result = qs_nvs_read_uint32(NVS_KEY_WORK_MODE, &temp_u32);
    if (result == AICAM_OK) work_mode_config->work_mode = (uint8_t)temp_u32;

    result = qs_nvs_read_bool(NVS_KEY_IMAGE_MODE_ENABLE, &temp_bool);
    if (result == AICAM_OK) work_mode_config->image_mode_enabled = (uint8_t)temp_bool;

    result = qs_nvs_read_bool(NVS_KEY_PIR_ENABLE, &temp_bool);
    if (result == AICAM_OK) work_mode_config->pir_trigger_enabled = (uint8_t)temp_bool;

    if (work_mode_config->pir_trigger_enabled) {
        result = qs_nvs_read_uint8(NVS_KEY_PIR_TRIGGER_TYPE, &temp_u8);
        if (result == AICAM_OK) work_mode_config->pir_trigger_type = temp_u8;

        result = qs_nvs_read_uint8(NVS_KEY_PIR_SENSITIVITY, &temp_u8);
        if (result == AICAM_OK) work_mode_config->pir_trigger_sensitivity = temp_u8;

        result = qs_nvs_read_uint8(NVS_KEY_PIR_IGNORE_TIME, &temp_u8);
        if (result == AICAM_OK) work_mode_config->pir_trigger_ignore_time = temp_u8;

        result = qs_nvs_read_uint8(NVS_KEY_PIR_PULSE_COUNT, &temp_u8);
        if (result == AICAM_OK) work_mode_config->pir_trigger_pulse_count = temp_u8;

        result = qs_nvs_read_uint8(NVS_KEY_PIR_WINDOW_TIME, &temp_u8);
        if (result == AICAM_OK) work_mode_config->pir_trigger_window_time = temp_u8;
    }

    result = qs_nvs_read_bool(NVS_KEY_TIMER_ENABLE, &temp_bool);
    if (result == AICAM_OK) work_mode_config->timer_trigger_enabled = (uint8_t)temp_bool;

    if (work_mode_config->timer_trigger_enabled) {
        result = qs_nvs_read_uint8(NVS_KEY_TIMER_CAPTURE_MODE, &temp_u8);
        if (result == AICAM_OK) work_mode_config->timer_trigger_capture_mode = temp_u8;

        result = qs_nvs_read_uint32(NVS_KEY_TIMER_INTERVAL, &temp_u32);
        if (result == AICAM_OK) work_mode_config->timer_trigger_interval_sec = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_TIMER_NODE_COUNT, &temp_u32);
        if (result == AICAM_OK) {
            if (temp_u32 > 10) temp_u32 = 10;
            work_mode_config->timer_trigger_time_node_count = temp_u32;
        }

        for (uint32_t i = 0; i < work_mode_config->timer_trigger_time_node_count; i++) {
            char key_name[32];
            snprintf(key_name, sizeof(key_name), "%s%u", NVS_KEY_TIMER_NODE_PREFIX, (unsigned int)i);
            if (qs_nvs_read_uint32(key_name, &temp_u32) == AICAM_OK) {
                work_mode_config->timer_trigger_time_node[i] = temp_u32;
            }

            snprintf(key_name, sizeof(key_name), "%s%u", NVS_KEY_TIMER_WEEKDAYS_PREFIX, (unsigned int)i);
            if (qs_nvs_read_uint8(key_name, &temp_u8) == AICAM_OK) {
                work_mode_config->timer_trigger_weekdays[i] = temp_u8;
            }
        }
    }

    result = qs_nvs_read_bool(NVS_KEY_REMOTE_TRIGGER_ENABLE, &temp_bool);
    if (result == AICAM_OK) work_mode_config->remote_trigger_enabled = (uint8_t)temp_bool;

    return AICAM_OK;
}

int quick_storage_read_comm_pref_type(qs_comm_pref_type_t *comm_pref_type)
{
    uint32_t temp_u32 = 0;
    aicam_bool_t temp_bool = AICAM_FALSE;
    if (!comm_pref_type) return AICAM_ERROR_INVALID_PARAM;

    if (qs_nvs_read_bool(NVS_KEY_CAPTURE_DISABLE_COMM, &temp_bool) == AICAM_OK && temp_bool) {
        *comm_pref_type = COMM_PREF_TYPE_DISABLE;
        return AICAM_OK;
    }
    if (qs_nvs_read_bool(NVS_KEY_COMM_AUTO_PRIORITY, &temp_bool) == AICAM_OK && temp_bool) {
        *comm_pref_type = COMM_PREF_TYPE_AUTO;
        return AICAM_OK;
    }
    if (qs_nvs_read_uint32(NVS_KEY_COMM_PREFERRED_TYPE, &temp_u32) != AICAM_OK) {
        *comm_pref_type = COMM_PREF_TYPE_AUTO;
        return AICAM_OK;
    }

    switch (temp_u32) {
        case COMM_PREF_TYPE_WIFI:
        case COMM_PREF_TYPE_CELLULAR:
        case COMM_PREF_TYPE_POE:
        case COMM_PREF_TYPE_HALOW:
            *comm_pref_type = (qs_comm_pref_type_t)temp_u32;
            break;
        default:
            *comm_pref_type = COMM_PREF_TYPE_AUTO;
            break;
    }
    return AICAM_OK;
}

int quick_storage_read_known_wifi_networks(qs_wifi_network_info_t *known_wifi_networks, uint32_t *count)
{
    if (!known_wifi_networks || !count) return AICAM_ERROR_INVALID_PARAM;

    memset(known_wifi_networks, 0, sizeof(qs_wifi_network_info_t) * MAX_KNOWN_WIFI_NETWORKS);
    *count = 0;

    uint32_t temp_u32 = 0;
    if (qs_nvs_read_uint32(NVS_KEY_NETWORK_KNOWN_COUNT, &temp_u32) != AICAM_OK) {
        return AICAM_OK;
    }

    if (temp_u32 > MAX_KNOWN_WIFI_NETWORKS) temp_u32 = MAX_KNOWN_WIFI_NETWORKS;
    *count = temp_u32;

    for (uint32_t i = 0; i < *count; i++) {
        char key_name[32];

        snprintf(key_name, sizeof(key_name), "net_%u_ssid", (unsigned int)i);
        (void)qs_nvs_read_string(key_name, known_wifi_networks[i].ssid, sizeof(known_wifi_networks[i].ssid));

        snprintf(key_name, sizeof(key_name), "net_%u_bssid", (unsigned int)i);
        (void)qs_nvs_read_string(key_name, known_wifi_networks[i].bssid, sizeof(known_wifi_networks[i].bssid));

        snprintf(key_name, sizeof(key_name), "net_%u_pwd", (unsigned int)i);
        (void)qs_nvs_read_string(key_name, known_wifi_networks[i].password, sizeof(known_wifi_networks[i].password));

        snprintf(key_name, sizeof(key_name), "net_%u_time", (unsigned int)i);
        (void)qs_nvs_read_uint32(key_name, &known_wifi_networks[i].last_connected_time);
    }

    return AICAM_OK;
}

int quick_storage_read_netif_config(qs_comm_pref_type_t comm_pref_type, netif_config_t *netif_config)
{
    if (!netif_config) return AICAM_ERROR_INVALID_PARAM;

    static char s_host_name_buf[NETIF_HOST_NAME_SIZE];
    memset(s_host_name_buf, 0, sizeof(s_host_name_buf));
    netif_config->host_name = s_host_name_buf;

    uint32_t temp_u32 = 0;
    aicam_bool_t temp_bool = AICAM_FALSE;

    if (comm_pref_type == COMM_PREF_TYPE_WIFI) {
        (void)qs_nvs_read_string(NVS_KEY_NETWORK_SSID, netif_config->wireless_cfg.ssid, sizeof(netif_config->wireless_cfg.ssid));
        (void)qs_nvs_read_string(NVS_KEY_NETWORK_PASSWORD, netif_config->wireless_cfg.pw, sizeof(netif_config->wireless_cfg.pw));
        netif_config->wireless_cfg.channel = 0;
        netif_config->wireless_cfg.security = WIRELESS_SECURITY_UNKNOWN;
        netif_config->wireless_cfg.encryption = WIRELESS_DEFAULT_ENCRYPTION;
        netif_config->ip_mode = NETIF_IP_MODE_DHCP;
        return AICAM_OK;
    }

    if (comm_pref_type == COMM_PREF_TYPE_CELLULAR) {
        (void)qs_nvs_read_string(NVS_KEY_CELLULAR_APN, netif_config->cellular_cfg.apn, sizeof(netif_config->cellular_cfg.apn));
        (void)qs_nvs_read_string(NVS_KEY_CELLULAR_USERNAME, netif_config->cellular_cfg.user, sizeof(netif_config->cellular_cfg.user));
        (void)qs_nvs_read_string(NVS_KEY_CELLULAR_PASSWORD, netif_config->cellular_cfg.passwd, sizeof(netif_config->cellular_cfg.passwd));
        (void)qs_nvs_read_string(NVS_KEY_CELLULAR_PIN, netif_config->cellular_cfg.pin, sizeof(netif_config->cellular_cfg.pin));
        if (qs_nvs_read_uint8(NVS_KEY_CELLULAR_AUTH, &netif_config->cellular_cfg.authentication) != AICAM_OK) {
            netif_config->cellular_cfg.authentication = 0;
        }
        if (qs_nvs_read_bool(NVS_KEY_CELLULAR_ROAMING, &temp_bool) == AICAM_OK) {
            netif_config->cellular_cfg.is_enable_roam = (uint8_t)temp_bool;
        }
        if (qs_nvs_read_uint8(NVS_KEY_CELLULAR_OPERATOR, &netif_config->cellular_cfg.isp_selected) != AICAM_OK) {
            netif_config->cellular_cfg.isp_selected = 0;
        }
        (void)qs_nvs_read_string(NVS_KEY_CELLULAR_PLMN, netif_config->cellular_cfg.plmn, sizeof(netif_config->cellular_cfg.plmn));
        netif_config->ip_mode = NETIF_IP_MODE_DHCP;
        return AICAM_OK;
    }

    if (comm_pref_type == COMM_PREF_TYPE_POE) {
        /* PoE config in NVS stores IPv4 as packed uint32 (A.B.C.D => (A<<24|B<<16|C<<8|D)) */
        if (qs_nvs_read_uint32(NVS_KEY_POE_IP_MODE, &temp_u32) == AICAM_OK) {
            netif_config->ip_mode = (temp_u32 == 0) ? NETIF_IP_MODE_DHCP : NETIF_IP_MODE_STATIC;
        } else {
            netif_config->ip_mode = NETIF_IP_MODE_DHCP;
        }

        if (qs_nvs_read_uint32(NVS_KEY_POE_IP_ADDR, &temp_u32) == AICAM_OK) u32_to_ipv4(temp_u32, netif_config->ip_addr);
        if (qs_nvs_read_uint32(NVS_KEY_POE_NETMASK, &temp_u32) == AICAM_OK) u32_to_ipv4(temp_u32, netif_config->netmask);
        if (qs_nvs_read_uint32(NVS_KEY_POE_GATEWAY, &temp_u32) == AICAM_OK) u32_to_ipv4(temp_u32, netif_config->gw);

        (void)qs_nvs_read_string(NVS_KEY_POE_HOSTNAME, s_host_name_buf, sizeof(s_host_name_buf));
        return AICAM_OK;
    }

#if NETIF_WIFI_HALOW_IS_ENABLE
    if (comm_pref_type == COMM_PREF_TYPE_HALOW) {
        (void)qs_nvs_read_string(NVS_KEY_HALOW_SSID, netif_config->wireless_cfg.ssid,
                                 sizeof(netif_config->wireless_cfg.ssid));
        (void)qs_nvs_read_string(NVS_KEY_HALOW_PASSWORD, netif_config->wireless_cfg.pw,
                                 sizeof(netif_config->wireless_cfg.pw));
        if (qs_nvs_read_uint32(NVS_KEY_HALOW_SECURITY, &temp_u32) == AICAM_OK) {
            netif_config->wireless_cfg.security = (wireless_security_t)temp_u32;
        } else {
            netif_config->wireless_cfg.security = WIRELESS_SAE;
        }
        (void)qs_nvs_read_string(NVS_KEY_HALOW_COUNTRY_CODE, netif_config->halow_cfg.country_code,
                                sizeof(netif_config->halow_cfg.country_code));
        {
            char bssid_str[18];
            if (qs_nvs_read_string(NVS_KEY_HALOW_BSSID, bssid_str, sizeof(bssid_str)) == AICAM_OK &&
                bssid_str[0] != '\0') {
                unsigned int bssid_bytes[6];
                if (sscanf(bssid_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                           &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                           &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) == 6) {
                    for (int i = 0; i < 6; i++) {
                        netif_config->wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
                    }
                }
            }
        }
        if (qs_nvs_read_uint32(NVS_KEY_HALOW_IP_MODE, &temp_u32) == AICAM_OK) {
            netif_config->ip_mode = (temp_u32 == POE_IP_MODE_STATIC) ?
                                    NETIF_IP_MODE_STATIC : NETIF_IP_MODE_DHCP;
        } else {
            netif_config->ip_mode = NETIF_IP_MODE_DHCP;
        }
        if (qs_nvs_read_uint32(NVS_KEY_HALOW_IP_ADDR, &temp_u32) == AICAM_OK) {
            u32_to_ipv4(temp_u32, netif_config->ip_addr);
        }
        if (qs_nvs_read_uint32(NVS_KEY_HALOW_NETMASK, &temp_u32) == AICAM_OK) {
            u32_to_ipv4(temp_u32, netif_config->netmask);
        }
        if (qs_nvs_read_uint32(NVS_KEY_HALOW_GATEWAY, &temp_u32) == AICAM_OK) {
            u32_to_ipv4(temp_u32, netif_config->gw);
        }
        return AICAM_OK;
    }
#endif

    /* AUTO/DISABLE: caller decides; we return empty config */
    netif_config->ip_mode = NETIF_IP_MODE_DHCP;
    return AICAM_OK;
}

/* Align with mqtt_service: only non-empty NVS strings become non-NULL pointers. */
static char *qs_mqtt_nonempty_or_null(char *s)
{
    if (s == NULL || s[0] == '\0') {
        return NULL;
    }
    return s;
}

int quick_storage_read_mqtt_all_config(qs_mqtt_all_config_t *mqtt_all_config)
{
    if (!mqtt_all_config) return AICAM_ERROR_INVALID_PARAM;
    memset(mqtt_all_config, 0, sizeof(*mqtt_all_config));

    /* backing store for ms_mqtt_config_t pointers */
    static char s_hostname[128];
    static char s_client_id[64];
    static char s_username[64];
    static char s_password[128];
    static char s_ca_path[128];
    static char s_ca_data[128];
    static char s_client_cert_path[128];
    static char s_client_cert_data[128];
    static char s_client_key_path[128];
    static char s_client_key_data[128];
    static char s_lwt_topic[MAX_TOPIC_LENGTH];
    static char s_lwt_message[256];

    memset(s_hostname, 0, sizeof(s_hostname));
    memset(s_client_id, 0, sizeof(s_client_id));
    memset(s_username, 0, sizeof(s_username));
    memset(s_password, 0, sizeof(s_password));
    memset(s_ca_path, 0, sizeof(s_ca_path));
    memset(s_ca_data, 0, sizeof(s_ca_data));
    memset(s_client_cert_path, 0, sizeof(s_client_cert_path));
    memset(s_client_cert_data, 0, sizeof(s_client_cert_data));
    memset(s_client_key_path, 0, sizeof(s_client_key_path));
    memset(s_client_key_data, 0, sizeof(s_client_key_data));
    memset(s_lwt_topic, 0, sizeof(s_lwt_topic));
    memset(s_lwt_message, 0, sizeof(s_lwt_message));

    uint32_t temp_u32 = 0;
    uint8_t temp_u8 = 0;

    (void)qs_nvs_read_string(NVS_KEY_MQTT_RECV_TOPIC, mqtt_all_config->data_receive_topic, sizeof(mqtt_all_config->data_receive_topic));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_REPORT_TOPIC, mqtt_all_config->data_report_topic, sizeof(mqtt_all_config->data_report_topic));

    if (qs_nvs_read_uint32(NVS_KEY_MQTT_RECV_QOS, &temp_u32) == AICAM_OK) mqtt_all_config->data_receive_qos = (uint8_t)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_REPORT_QOS, &temp_u32) == AICAM_OK) mqtt_all_config->data_report_qos = (uint8_t)temp_u32;

    /* Base */
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_PROTOCOL_VER, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.base.protocol_ver = temp_u8;
    (void)qs_nvs_read_string(NVS_KEY_MQTT_HOST, s_hostname, sizeof(s_hostname));
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_PORT, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.base.port = (uint16_t)temp_u32;
    (void)qs_nvs_read_string(NVS_KEY_MQTT_CLIENT_ID, s_client_id, sizeof(s_client_id));
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_CLEAN_SESSION, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.base.clean_session = temp_u8;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_KEEPALIVE, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.base.keepalive = (int)temp_u32;

    /* Authentication */
    (void)qs_nvs_read_string(NVS_KEY_MQTT_USERNAME, s_username, sizeof(s_username));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_PASSWORD, s_password, sizeof(s_password));

    (void)qs_nvs_read_string(NVS_KEY_MQTT_CA_CERT_PATH, s_ca_path, sizeof(s_ca_path));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_CA_CERT_DATA, s_ca_data, sizeof(s_ca_data));
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_CA_CERT_LEN, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.authentication.ca_len = (size_t)temp_u32;

    (void)qs_nvs_read_string(NVS_KEY_MQTT_CLIENT_CERT_PATH, s_client_cert_path, sizeof(s_client_cert_path));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_CLIENT_CERT_DATA, s_client_cert_data, sizeof(s_client_cert_data));
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_CLIENT_CERT_LEN, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.authentication.client_cert_len = (size_t)temp_u32;

    (void)qs_nvs_read_string(NVS_KEY_MQTT_CLIENT_KEY_PATH, s_client_key_path, sizeof(s_client_key_path));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_CLIENT_KEY_DATA, s_client_key_data, sizeof(s_client_key_data));
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_CLIENT_KEY_LEN, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.authentication.client_key_len = (size_t)temp_u32;

    if (qs_nvs_read_uint8(NVS_KEY_MQTT_VERIFY_HOSTNAME, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.authentication.is_verify_hostname = temp_u8;

    /* LWT */
    (void)qs_nvs_read_string(NVS_KEY_MQTT_LWT_TOPIC, s_lwt_topic, sizeof(s_lwt_topic));
    (void)qs_nvs_read_string(NVS_KEY_MQTT_LWT_MESSAGE, s_lwt_message, sizeof(s_lwt_message));
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_LWT_MSG_LEN, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.last_will.msg_len = (int)temp_u32;
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_LWT_QOS, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.last_will.qos = (int)temp_u8;
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_LWT_RETAIN, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.last_will.retain = (int)temp_u8;

    /* Task */
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_TASK_PRIORITY, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.task.priority = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_TASK_STACK, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.task.stack_size = (int)temp_u32;

    /* Network */
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_DISABLE_RECONNECT, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.disable_auto_reconnect = temp_u8;
    if (qs_nvs_read_uint8(NVS_KEY_MQTT_OUTBOX_LIMIT, &temp_u8) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.outbox_limit = temp_u8;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_OUTBOX_RESEND_IV, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.outbox_resend_interval_ms = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_OUTBOX_EXPIRE, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.outbox_expired_timeout = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_RECONNECT_INTERVAL, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.reconnect_interval_ms = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_TIMEOUT, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.timeout_ms = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_BUFFER_SIZE, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.buffer_size = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_TX_BUF_SIZE, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.tx_buf_size = (int)temp_u32;
    if (qs_nvs_read_uint32(NVS_KEY_MQTT_RX_BUF_SIZE, &temp_u32) == AICAM_OK) mqtt_all_config->ms_mqtt_config.network.rx_buf_size = (int)temp_u32;

    {
        ms_mqtt_config_t *mc = &mqtt_all_config->ms_mqtt_config;

        mc->base.hostname = qs_mqtt_nonempty_or_null(s_hostname);
        mc->base.client_id = qs_mqtt_nonempty_or_null(s_client_id);
        mc->authentication.username = qs_mqtt_nonempty_or_null(s_username);
        mc->authentication.password = qs_mqtt_nonempty_or_null(s_password);
        mc->authentication.ca_path = qs_mqtt_nonempty_or_null(s_ca_path);
        mc->authentication.client_cert_path = qs_mqtt_nonempty_or_null(s_client_cert_path);
        mc->authentication.client_key_path = qs_mqtt_nonempty_or_null(s_client_key_path);

        mc->authentication.ca_data = qs_mqtt_nonempty_or_null(s_ca_data);
        if (mc->authentication.ca_data == NULL) {
            mc->authentication.ca_len = 0;
        }
        mc->authentication.client_cert_data = qs_mqtt_nonempty_or_null(s_client_cert_data);
        if (mc->authentication.client_cert_data == NULL) {
            mc->authentication.client_cert_len = 0;
        }
        mc->authentication.client_key_data = qs_mqtt_nonempty_or_null(s_client_key_data);
        if (mc->authentication.client_key_data == NULL) {
            mc->authentication.client_key_len = 0;
        }

        mc->last_will.topic = qs_mqtt_nonempty_or_null(s_lwt_topic);
        mc->last_will.msg = qs_mqtt_nonempty_or_null(s_lwt_message);
        if (mc->last_will.topic == NULL) {
            mc->last_will.msg = NULL;
            mc->last_will.msg_len = 0;
        } else if (mc->last_will.msg == NULL) {
            mc->last_will.msg_len = 0;
        }
    }

    return AICAM_OK;
}

int quick_storage_read_device_info(qs_device_info_t *device_info)
{
    if (!device_info) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    memset(device_info, 0, sizeof(*device_info));

    (void)snprintf(device_info->device_name, sizeof(device_info->device_name), "%s",
                   default_config.device_info.device_name);
    (void)snprintf(device_info->mac_address, sizeof(device_info->mac_address), "%s",
                   default_config.device_info.mac_address);
    (void)snprintf(device_info->serial_number, sizeof(device_info->serial_number), "%s",
                   default_config.device_info.serial_number);
    (void)snprintf(device_info->hardware_version, sizeof(device_info->hardware_version), "%s",
                   default_config.device_info.hardware_version);

    (void)qs_nvs_read_string(NVS_KEY_DEVICE_INFO_NAME, device_info->device_name,
                                      sizeof(device_info->device_name));
    (void)qs_nvs_read_string(NVS_KEY_DEVICE_INFO_MAC, device_info->mac_address,
                                      sizeof(device_info->mac_address));
    (void)qs_nvs_read_string(NVS_KEY_DEVICE_INFO_SERIAL, device_info->serial_number,
                                      sizeof(device_info->serial_number));
    (void)qs_nvs_read_string(NVS_KEY_DEVICE_INFO_HW_VER, device_info->hardware_version,
                                      sizeof(device_info->hardware_version));

    return AICAM_OK;
}

int quick_storage_init(void)
{
    if (s_quick_storage_inited) return AICAM_OK;

    if (s_write_q == NULL) {
        s_write_q = osMessageQueueNew(MAX_WRITE_TASK_QUEUE_SIZE, sizeof(qs_write_task_item_t), &s_write_q_attr);
        if (s_write_q == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    if (s_write_tid == NULL) {
        /* CMSIS-RTOS2 may require user-provided stacks to be 8-byte aligned.
         * Also avoid relying on heap allocation during early boot.
         */
        static uint8_t s_write_stack[4 * 1024] ALIGN_32;
        static const osThreadAttr_t attr = {
            .name = "qs_write",
            .stack_mem = s_write_stack,
            .priority = (osPriority_t)osPriorityNormal,
            .stack_size = sizeof(s_write_stack),
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_write_tid = osThreadNew(qs_write_thread, NULL, &attr);
        if (s_write_tid == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    s_quick_storage_inited = AICAM_TRUE;
    QT_TRACE("[QST] ", "init ok");
    return AICAM_OK;
}

int quick_storage_add_write_task(qs_write_task_param_t *write_task_param)
{
    if (!write_task_param) return AICAM_ERROR_INVALID_PARAM;
    if (!write_task_param->data || write_task_param->data_len == 0) return AICAM_ERROR_INVALID_PARAM;
    if (write_task_param->disk_type > 2) return AICAM_ERROR_INVALID_PARAM;
    if (!write_task_param->file_name[0]) return AICAM_ERROR_INVALID_PARAM;

    if (!s_quick_storage_inited || s_write_q == NULL || s_write_tid == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    int rc = AICAM_OK;

    /* Pre-check SD connection if explicitly targeting SD */
    if (write_task_param->disk_type == 2) {
        sd_disk_info_t info = {0};
        if (sd_get_disk_info(&info) != 0) return AICAM_ERROR_IO;
        if (info.mode != SD_MODE_NORMAL) return AICAM_ERROR_UNAVAILABLE;
    }

    /* Space check before enqueuing (avoid allocating/queueing tasks that must fail) */
    if (write_task_param->disk_type == 0) {
        rc = qs_check_free_space_auto(write_task_param->data_len);
    } else {
        rc = qs_check_free_space(write_task_param->disk_type, write_task_param->data_len);
    }
    if (rc != AICAM_OK) return rc;

    qs_write_task_item_t item = {0};
    memcpy(&item.param, write_task_param, sizeof(item.param));

    if (osMessageQueuePut(s_write_q, &item, 0, 0) != osOK) {
        return AICAM_ERROR_FULL;
    }

    QT_TRACE("[QST] ", "q %s %luB d%u",
             write_task_param->file_name,
             (unsigned long)write_task_param->data_len,
             (unsigned)write_task_param->disk_type);
    return AICAM_OK;
}

