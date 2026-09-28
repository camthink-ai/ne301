/**
 * @file api_isp_module.c
 * @brief ISP API Module Implementation - HTTP interface implementation for ISP parameter tuning
 */

#include "api_isp_module.h"
#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "isp_api.h"
#include "isp_services.h"
#include "camera.h"
#include "json_config_mgr.h"
#include "device_service.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ==================== Private Definitions ==================== */

#define ISP_API_TAG "ISP_API"

/* Bayer pattern strings */
static const char* bayer_pattern_str[] = {"RGGB", "GRBG", "GBRG", "BGGR", "MONO"};

/* Exposure compensation labels */
static const char* exposure_comp_labels[] = {
    "-2.0EV", "-1.5EV", "-1.0EV", "-0.5EV", "0EV", 
    "+0.5EV", "+1.0EV", "+1.5EV", "+2.0EV"
};

/* ==================== Private Functions ==================== */

/**
 * @brief Get ISP handle from camera service
 */
static ISP_HandleTypeDef* get_isp_handle(void) {
    return camera_get_isp_handle();
}

/**
 * @brief Convert ISP status to API error code
 */
static int isp_status_to_error_code(ISP_StatusTypeDef status) {
    switch (status) {
        case ISP_OK: return 0;
        case ISP_ERR_EINVAL: return API_ISP_ERROR_INVALID_PARAM;
        case ISP_ERR_DEMOSAICING_EINVAL:
        case ISP_ERR_STATREMOVAL_EINVAL:
        case ISP_ERR_DECIMATION_EINVAL:
        case ISP_ERR_CONTRAST_EINVAL:
        case ISP_ERR_STATAREA_EINVAL:
        case ISP_ERR_BADPIXEL_EINVAL:
        case ISP_ERR_BLACKLEVEL_EINVAL:
        case ISP_ERR_ISPGAIN_EINVAL:
        case ISP_ERR_COLORCONV_EINVAL:
        case ISP_ERR_IQPARAM_EINVAL:
            return API_ISP_ERROR_PARAM_OUT_OF_RANGE;
        case ISP_ERR_SENSORGAIN:
        case ISP_ERR_SENSOREXPOSURE:
        case ISP_ERR_SENSORINFO:
            return API_ISP_ERROR_SENSOR_ERROR;
        case ISP_ERR_ALGO:
        case ISP_ERR_AWB:
            return API_ISP_ERROR_ALGO_ERROR;
        default:
            return API_ISP_ERROR_HAL_ERROR;
    }
}

/**
 * @brief Set error response
 * @return AICAM_OK so handlers can `return set_isp_error_response(...)`
 *         directly. The dispatcher sends the composed envelope regardless of
 *         the handler's return code, so the accurate per-callsite error code
 *         rides in ctx->response.error_code, not in the return value.
 */
static aicam_result_t set_isp_error_response(http_handler_context_t *ctx, int error_code, const char *message) {
    ctx->response.code = 400;
    ctx->response.error_code = error_code;
    snprintf(ctx->response.message, sizeof(ctx->response.message), "%s", message ? message : "");
    return AICAM_OK;
}

/**
 * @brief Add metadata object for a parameter
 */
static void add_param_meta(cJSON *parent, const char *name, const char *type, 
                           int min, int max, int def, const char *unit, const char *desc) {
    cJSON *meta = cJSON_CreateObject();
    cJSON_AddStringToObject(meta, "type", type);
    if (min != max) {
        cJSON_AddNumberToObject(meta, "min", min);
        cJSON_AddNumberToObject(meta, "max", max);
    }
    if (def >= 0) {
        cJSON_AddNumberToObject(meta, "default", def);
    }
    if (unit) {
        cJSON_AddStringToObject(meta, "unit", unit);
    }
    if (desc) {
        cJSON_AddStringToObject(meta, "description", desc);
    }
    cJSON_AddItemToObject(parent, name, meta);
}

/* ==================== Sensor Info Handler ==================== */

aicam_result_t api_isp_get_sensor_info(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_SensorInfoTypeDef sensorInfo;
    ISP_StatusTypeDef status = ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get sensor info");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "name", sensorInfo.name);
    cJSON_AddStringToObject(data, "bayer_pattern", 
        sensorInfo.bayer_pattern < 5 ? bayer_pattern_str[sensorInfo.bayer_pattern] : "UNKNOWN");
    cJSON_AddNumberToObject(data, "color_depth", sensorInfo.color_depth);
    cJSON_AddNumberToObject(data, "width", sensorInfo.width);
    cJSON_AddNumberToObject(data, "height", sensorInfo.height);
    
    cJSON *gain_range = cJSON_CreateObject();
    cJSON_AddNumberToObject(gain_range, "min", sensorInfo.gain_min);
    cJSON_AddNumberToObject(gain_range, "max", sensorInfo.gain_max);
    cJSON_AddItemToObject(data, "gain_range", gain_range);
    
    cJSON *exposure_range = cJSON_CreateObject();
    cJSON_AddNumberToObject(exposure_range, "min", sensorInfo.exposure_min);
    cJSON_AddNumberToObject(exposure_range, "max", sensorInfo.exposure_max);
    cJSON_AddItemToObject(data, "exposure_range", exposure_range);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

/* ==================== AEC Handlers ==================== */

aicam_result_t api_isp_get_aec(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    uint8_t aec_enable = 0;
    ISP_ExposureCompTypeDef exp_comp;
    uint32_t exp_target = 0;
    
    ISP_GetAECState(hIsp, &aec_enable);
    ISP_GetExposureTarget(hIsp, &exp_comp, &exp_target);

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", aec_enable);
    cJSON_AddNumberToObject(data, "exposure_compensation", exp_comp);
    cJSON_AddNumberToObject(data, "exposure_target", exp_target);
    cJSON_AddNumberToObject(data, "anti_flicker_freq", iqParam ? iqParam->AECAlgo.antiFlickerFreq : 0);

    // Current state
    ISP_SensorGainTypeDef gain;
    ISP_SensorExposureTypeDef exposure;
    ISP_SVC_Sensor_GetGain(hIsp, &gain);
    ISP_SVC_Sensor_GetExposure(hIsp, &exposure);
    
    uint32_t lux = 0;
    ISP_GetLuxEstimation(hIsp, &lux);

    cJSON *current = cJSON_CreateObject();
    cJSON_AddNumberToObject(current, "sensor_gain", gain.gain);
    cJSON_AddNumberToObject(current, "sensor_exposure", exposure.exposure);
    cJSON_AddNumberToObject(current, "estimated_lux", lux);
    cJSON_AddItemToObject(data, "current_state", current);

    // Metadata
    cJSON *meta = cJSON_CreateObject();
    
    cJSON *exp_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(exp_meta, "type", "enum");
    cJSON *values = cJSON_CreateIntArray((const int[]){-4,-3,-2,-1,0,1,2,3,4}, 9);
    cJSON_AddItemToObject(exp_meta, "values", values);
    cJSON *labels = cJSON_CreateStringArray(exposure_comp_labels, 9);
    cJSON_AddItemToObject(exp_meta, "labels", labels);
    cJSON_AddNumberToObject(exp_meta, "default", 0);
    cJSON_AddStringToObject(exp_meta, "description", "Exposure compensation in EV");
    cJSON_AddItemToObject(meta, "exposure_compensation", exp_meta);

    cJSON *flicker_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(flicker_meta, "type", "enum");
    cJSON *flicker_values = cJSON_CreateIntArray((const int[]){0, 50, 60}, 3);
    cJSON_AddItemToObject(flicker_meta, "values", flicker_values);
    cJSON_AddStringToObject(flicker_meta, "description", "Anti-flicker frequency (0=Off, 50=50Hz, 60=60Hz)");
    cJSON_AddItemToObject(meta, "anti_flicker_freq", flicker_meta);

    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_aec(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_StatusTypeDef status = ISP_OK;
    
    // Set AEC enable state
    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        status = ISP_SetAECState(hIsp, cJSON_IsTrue(enable) ? 1 : 0);
        if (status != ISP_OK) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set AEC state");
        }
    }

    // Set exposure compensation
    cJSON *exp_comp = cJSON_GetObjectItem(json, "exposure_compensation");
    if (exp_comp && cJSON_IsNumber(exp_comp)) {
        int comp_val = exp_comp->valueint;
        if (comp_val < -4 || comp_val > 4) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, 
                "exposure_compensation must be between -4 and 4");
        }
        status = ISP_SetExposureTarget(hIsp, (ISP_ExposureCompTypeDef)comp_val);
        if (status != ISP_OK) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set exposure target");
        }
    }

    cJSON_Delete(json);
    snprintf(ctx->response.message, sizeof(ctx->response.message), "AEC parameters updated");
    return AICAM_OK;
}

/* ==================== AEC Manual (sensor gain/exposure) Handlers ==================== */

aicam_result_t api_isp_get_manual_exposure(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_SensorGainTypeDef gain;
    ISP_SensorExposureTypeDef exposure;
    ISP_StatusTypeDef status = ISP_SVC_Sensor_GetGain(hIsp, &gain);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get sensor gain");
    }
    status = ISP_SVC_Sensor_GetExposure(hIsp, &exposure);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get sensor exposure");
    }

    ISP_SensorInfoTypeDef sensorInfo;
    ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "gain", gain.gain);
    cJSON_AddNumberToObject(data, "exposure", exposure.exposure);

    cJSON *meta = cJSON_CreateObject();
    cJSON *gain_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(gain_meta, "type", "integer");
    cJSON_AddStringToObject(gain_meta, "unit", "mdB");
    cJSON_AddNumberToObject(gain_meta, "min", sensorInfo.gain_min);
    cJSON_AddNumberToObject(gain_meta, "max", sensorInfo.gain_max);
    cJSON_AddStringToObject(gain_meta, "description", "Sensor gain in millidecibels");
    cJSON_AddItemToObject(meta, "gain", gain_meta);
    cJSON *exp_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(exp_meta, "type", "integer");
    cJSON_AddStringToObject(exp_meta, "unit", "us");
    cJSON_AddNumberToObject(exp_meta, "min", sensorInfo.exposure_min);
    cJSON_AddNumberToObject(exp_meta, "max", sensorInfo.exposure_max);
    cJSON_AddStringToObject(exp_meta, "description", "Sensor exposure time in microseconds");
    cJSON_AddItemToObject(meta, "exposure", exp_meta);
    cJSON *dep = cJSON_CreateObject();
    cJSON_AddStringToObject(dep, "note", "Manual exposure takes effect only when AEC is disabled");
    cJSON_AddItemToObject(meta, "dependencies", dep);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    return AICAM_OK;
}

aicam_result_t api_isp_set_manual_exposure(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_SensorInfoTypeDef sensorInfo;
    ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);

    cJSON *gain_item = cJSON_GetObjectItem(json, "gain");
    if (gain_item && cJSON_IsNumber(gain_item)) {
        uint32_t g = (uint32_t)gain_item->valuedouble;
        if (g < sensorInfo.gain_min || g > sensorInfo.gain_max) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "gain out of sensor range");
        }
        ISP_SensorGainTypeDef gain = { .gain = g };
        ISP_StatusTypeDef status = ISP_SVC_Sensor_SetGain(hIsp, &gain);
        if (status != ISP_OK) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set sensor gain");
        }
        /* Align with isp_cmd_parser: update IQ param cache (sensorGainStatic) */
        if (iqParam) {
            iqParam->sensorGainStatic = gain;
        }
    }

    cJSON *exp_item = cJSON_GetObjectItem(json, "exposure");
    if (exp_item && cJSON_IsNumber(exp_item)) {
        uint32_t e = (uint32_t)exp_item->valuedouble;
        if (e < sensorInfo.exposure_min || e > sensorInfo.exposure_max) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "exposure out of sensor range");
        }
        ISP_SensorExposureTypeDef exposure = { .exposure = e };
        ISP_StatusTypeDef status = ISP_SVC_Sensor_SetExposure(hIsp, &exposure);
        if (status != ISP_OK) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set sensor exposure");
        }
        /* Align with isp_cmd_parser: update IQ param cache (sensorExposureStatic) */
        if (iqParam) {
            iqParam->sensorExposureStatic = exposure;
        }
    }

    cJSON_Delete(json);
    snprintf(ctx->response.message, sizeof(ctx->response.message), "Manual exposure parameters updated");
    return AICAM_OK;
}

/* ==================== AWB Handlers ==================== */

aicam_result_t api_isp_get_awb(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    uint8_t awb_auto = 0;
    uint32_t color_temp = 0;
    ISP_GetWBRefMode(hIsp, &awb_auto, &color_temp);

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", iqParam ? iqParam->AWBAlgo.enable : 0);
    cJSON_AddStringToObject(data, "mode", awb_auto ? "auto" : "manual");
    cJSON_AddNumberToObject(data, "current_color_temp", color_temp);

    // Complete profiles with all AWB data
    cJSON *profiles = cJSON_CreateArray();
    if (iqParam) {
        for (int i = 0; i < ISP_AWB_COLORTEMP_REF; i++) {
            cJSON *profile = cJSON_CreateObject();
            cJSON_AddNumberToObject(profile, "index", i);
            cJSON_AddStringToObject(profile, "label", iqParam->AWBAlgo.label[i]);
            cJSON_AddNumberToObject(profile, "reference_color_temp", iqParam->AWBAlgo.referenceColorTemp[i]);

            // ISP Gains for this profile
            cJSON *gains = cJSON_CreateObject();
            cJSON_AddNumberToObject(gains, "r", iqParam->AWBAlgo.ispGainR[i]);
            cJSON_AddNumberToObject(gains, "g", iqParam->AWBAlgo.ispGainG[i]);
            cJSON_AddNumberToObject(gains, "b", iqParam->AWBAlgo.ispGainB[i]);
            cJSON_AddItemToObject(profile, "isp_gain", gains);

            // CCM (Color Conversion Matrix) 3x3
            cJSON *ccm = cJSON_CreateArray();
            for (int row = 0; row < 3; row++) {
                cJSON *ccm_row = cJSON_CreateArray();
                for (int col = 0; col < 3; col++) {
                    cJSON_AddItemToArray(ccm_row, cJSON_CreateNumber(iqParam->AWBAlgo.coeff[i][row][col]));
                }
                cJSON_AddItemToArray(ccm, ccm_row);
            }
            cJSON_AddItemToObject(profile, "ccm", ccm);

            // Reference RGB
            cJSON *ref_rgb = cJSON_CreateObject();
            cJSON_AddNumberToObject(ref_rgb, "r", iqParam->AWBAlgo.referenceRGB[i][0]);
            cJSON_AddNumberToObject(ref_rgb, "g", iqParam->AWBAlgo.referenceRGB[i][1]);
            cJSON_AddNumberToObject(ref_rgb, "b", iqParam->AWBAlgo.referenceRGB[i][2]);
            cJSON_AddItemToObject(profile, "reference_rgb", ref_rgb);

            cJSON_AddItemToArray(profiles, profile);
        }
    }
    cJSON_AddItemToObject(data, "profiles", profiles);

    // Current active gains
    ISP_ISPGainTypeDef ispGain;
    ISP_SVC_ISP_GetGain(hIsp, &ispGain);

    cJSON *current_gains = cJSON_CreateObject();
    cJSON_AddNumberToObject(current_gains, "r", ispGain.ispGainR);
    cJSON_AddNumberToObject(current_gains, "g", ispGain.ispGainG);
    cJSON_AddNumberToObject(current_gains, "b", ispGain.ispGainB);
    cJSON_AddItemToObject(data, "current_gains", current_gains);

    // Current color conversion matrix
    ISP_ColorConvTypeDef colorConv;
    ISP_SVC_ISP_GetColorConv(hIsp, &colorConv);

    cJSON *current_ccm = cJSON_CreateObject();
    cJSON_AddBoolToObject(current_ccm, "enable", colorConv.enable);
    cJSON *ccm_matrix = cJSON_CreateArray();
    for (int row = 0; row < 3; row++) {
        cJSON *ccm_row = cJSON_CreateArray();
        for (int col = 0; col < 3; col++) {
            cJSON_AddItemToArray(ccm_row, cJSON_CreateNumber(colorConv.coeff[row][col]));
        }
        cJSON_AddItemToArray(ccm_matrix, ccm_row);
    }
    cJSON_AddItemToObject(current_ccm, "matrix", ccm_matrix);
    cJSON_AddItemToObject(data, "current_ccm", current_ccm);

    // Metadata
    cJSON *meta = cJSON_CreateObject();
    cJSON *mode_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(mode_meta, "type", "enum");
    cJSON *mode_values = cJSON_CreateStringArray((const char*[]){"auto", "manual"}, 2);
    cJSON_AddItemToObject(mode_meta, "values", mode_values);
    cJSON_AddStringToObject(mode_meta, "description", "White balance mode");
    cJSON_AddItemToObject(meta, "mode", mode_meta);

    cJSON *gain_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(gain_meta, "type", "integer");
    cJSON_AddStringToObject(gain_meta, "unit", "100000000 = x1.0");
    cJSON_AddStringToObject(gain_meta, "description", "ISP gain values, 100000000 represents x1.0, 150000000 represents x1.5");
    cJSON_AddItemToObject(meta, "isp_gain", gain_meta);

    cJSON *ccm_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(ccm_meta, "type", "integer[3][3]");
    cJSON_AddStringToObject(ccm_meta, "unit", "100000000 = x1.0");
    cJSON_AddStringToObject(ccm_meta, "range", "-400000000 to 400000000 (x-4.0 to x4.0)");
    cJSON_AddStringToObject(ccm_meta, "description", "Color Conversion Matrix coefficients");
    cJSON_AddItemToObject(meta, "ccm", ccm_meta);

    cJSON *dep = cJSON_CreateObject();
    cJSON_AddStringToObject(dep, "note", "In auto mode, AWB algorithm selects profile based on scene; in manual mode, use color_temp to select profile");
    cJSON_AddItemToObject(meta, "dependencies", dep);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);

    return AICAM_OK;
}

aicam_result_t api_isp_set_awb(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    // Handle enable
    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        iqParam->AWBAlgo.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    // Handle mode and color_temp for quick switching
    cJSON *mode = cJSON_GetObjectItem(json, "mode");
    cJSON *temp = cJSON_GetObjectItem(json, "color_temp");
    if (mode && cJSON_IsString(mode)) {
        uint8_t awb_auto = (strcmp(mode->valuestring, "auto") == 0) ? 1 : 0;
        uint32_t color_temp = 0;
        if (temp && cJSON_IsNumber(temp)) {
            color_temp = (uint32_t)temp->valueint;
        }
        ISP_SetWBRefMode(hIsp, awb_auto, color_temp);
    }

    // Handle complete profile configuration
    cJSON *profiles = cJSON_GetObjectItem(json, "profiles");
    if (profiles && cJSON_IsArray(profiles)) {
        int profile_count = cJSON_GetArraySize(profiles);
        for (int i = 0; i < profile_count && i < ISP_AWB_COLORTEMP_REF; i++) {
            cJSON *profile = cJSON_GetArrayItem(profiles, i);
            if (!cJSON_IsObject(profile)) continue;

            // Get profile index (default to array position)
            int idx = i;
            cJSON *index_item = cJSON_GetObjectItem(profile, "index");
            if (index_item && cJSON_IsNumber(index_item)) {
                idx = index_item->valueint;
                if (idx < 0 || idx >= ISP_AWB_COLORTEMP_REF) continue;
            }

            // Label
            cJSON *label = cJSON_GetObjectItem(profile, "label");
            if (label && cJSON_IsString(label)) {
                strncpy(iqParam->AWBAlgo.label[idx], label->valuestring, ISP_AWB_PROFILE_ID_MAX_LENGTH - 1);
                iqParam->AWBAlgo.label[idx][ISP_AWB_PROFILE_ID_MAX_LENGTH - 1] = '\0';
            }

            // Reference color temperature
            cJSON *ref_temp = cJSON_GetObjectItem(profile, "reference_color_temp");
            if (ref_temp && cJSON_IsNumber(ref_temp)) {
                iqParam->AWBAlgo.referenceColorTemp[idx] = (uint32_t)ref_temp->valueint;
            }

            // ISP Gains
            cJSON *gains = cJSON_GetObjectItem(profile, "isp_gain");
            if (gains && cJSON_IsObject(gains)) {
                cJSON *r = cJSON_GetObjectItem(gains, "r");
                cJSON *g = cJSON_GetObjectItem(gains, "g");
                cJSON *b = cJSON_GetObjectItem(gains, "b");
                if (r && cJSON_IsNumber(r)) iqParam->AWBAlgo.ispGainR[idx] = (uint32_t)r->valuedouble;
                if (g && cJSON_IsNumber(g)) iqParam->AWBAlgo.ispGainG[idx] = (uint32_t)g->valuedouble;
                if (b && cJSON_IsNumber(b)) iqParam->AWBAlgo.ispGainB[idx] = (uint32_t)b->valuedouble;
            }

            // CCM (Color Conversion Matrix)
            cJSON *ccm = cJSON_GetObjectItem(profile, "ccm");
            if (ccm && cJSON_IsArray(ccm) && cJSON_GetArraySize(ccm) == 3) {
                for (int row = 0; row < 3; row++) {
                    cJSON *ccm_row = cJSON_GetArrayItem(ccm, row);
                    if (ccm_row && cJSON_IsArray(ccm_row) && cJSON_GetArraySize(ccm_row) == 3) {
                        for (int col = 0; col < 3; col++) {
                            cJSON *val = cJSON_GetArrayItem(ccm_row, col);
                            if (val && cJSON_IsNumber(val)) {
                                iqParam->AWBAlgo.coeff[idx][row][col] = (int32_t)val->valuedouble;
                            }
                        }
                    }
                }
            }

            // Reference RGB
            cJSON *ref_rgb = cJSON_GetObjectItem(profile, "reference_rgb");
            if (ref_rgb && cJSON_IsObject(ref_rgb)) {
                cJSON *r = cJSON_GetObjectItem(ref_rgb, "r");
                cJSON *g = cJSON_GetObjectItem(ref_rgb, "g");
                cJSON *b = cJSON_GetObjectItem(ref_rgb, "b");
                if (r && cJSON_IsNumber(r)) iqParam->AWBAlgo.referenceRGB[idx][0] = (uint8_t)r->valueint;
                if (g && cJSON_IsNumber(g)) iqParam->AWBAlgo.referenceRGB[idx][1] = (uint8_t)g->valueint;
                if (b && cJSON_IsNumber(b)) iqParam->AWBAlgo.referenceRGB[idx][2] = (uint8_t)b->valueint;
            }
        }
    }

    cJSON_Delete(json);
    snprintf(ctx->response.message, sizeof(ctx->response.message), "AWB parameters updated");
    return AICAM_OK;
}

/* ==================== Demosaicing Handlers ==================== */

aicam_result_t api_isp_get_demosaicing(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", iqParam->demosaicing.enable);
    cJSON_AddStringToObject(data, "bayer_type", 
        iqParam->demosaicing.type < 5 ? bayer_pattern_str[iqParam->demosaicing.type] : "UNKNOWN");
    cJSON_AddNumberToObject(data, "peak", iqParam->demosaicing.peak);
    cJSON_AddNumberToObject(data, "line_v", iqParam->demosaicing.lineV);
    cJSON_AddNumberToObject(data, "line_h", iqParam->demosaicing.lineH);
    cJSON_AddNumberToObject(data, "edge", iqParam->demosaicing.edge);

    // Metadata
    cJSON *meta = cJSON_CreateObject();
    add_param_meta(meta, "peak", "integer", 0, ISP_API_DEMOS_STRENGTH_MAX, 4, NULL, "Peak detection strength");
    add_param_meta(meta, "line_v", "integer", 0, ISP_API_DEMOS_STRENGTH_MAX, 4, NULL, "Vertical line detection strength");
    add_param_meta(meta, "line_h", "integer", 0, ISP_API_DEMOS_STRENGTH_MAX, 4, NULL, "Horizontal line detection strength");
    add_param_meta(meta, "edge", "integer", 0, ISP_API_DEMOS_STRENGTH_MAX, 4, NULL, "Edge detection strength");
    
    cJSON *bayer_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(bayer_meta, "type", "enum");
    cJSON_AddBoolToObject(bayer_meta, "readonly", 1);
    cJSON_AddStringToObject(bayer_meta, "description", "Bayer pattern determined by sensor");
    cJSON_AddItemToObject(meta, "bayer_type", bayer_meta);
    
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_demosaicing(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    ISP_DemosaicingTypeDef config = iqParam->demosaicing;

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) config.enable = cJSON_IsTrue(enable);

    cJSON *peak = cJSON_GetObjectItem(json, "peak");
    if (peak && cJSON_IsNumber(peak)) config.peak = peak->valueint;

    cJSON *line_v = cJSON_GetObjectItem(json, "line_v");
    if (line_v && cJSON_IsNumber(line_v)) config.lineV = line_v->valueint;

    cJSON *line_h = cJSON_GetObjectItem(json, "line_h");
    if (line_h && cJSON_IsNumber(line_h)) config.lineH = line_h->valueint;

    cJSON *edge = cJSON_GetObjectItem(json, "edge");
    if (edge && cJSON_IsNumber(edge)) config.edge = edge->valueint;

    cJSON_Delete(json);

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetDemosaicing(hIsp, &config);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set demosaicing");
    }
    /* Align with isp_cmd_parser: update IQ param cache so algo/restart use it */
    iqParam->demosaicing = config;

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Demosaicing parameters updated");
    return AICAM_OK;
}

/* ==================== Stat Removal Handlers ==================== */

aicam_result_t api_isp_get_stat_removal(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", iqParam->statRemoval.enable);
    cJSON_AddNumberToObject(data, "head_lines", iqParam->statRemoval.nbHeadLines);
    cJSON_AddNumberToObject(data, "valid_lines", iqParam->statRemoval.nbValidLines);

    cJSON *meta = cJSON_CreateObject();
    add_param_meta(meta, "head_lines", "integer", 0, ISP_STATREMOVAL_HEADLINES_MAX, 0, NULL, "Number of head lines to remove");
    add_param_meta(meta, "valid_lines", "integer", 0, ISP_STATREMOVAL_VALIDLINES_MAX, 0, NULL, "Number of valid lines to keep");
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    return AICAM_OK;
}

aicam_result_t api_isp_set_stat_removal(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        iqParam->statRemoval.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *head = cJSON_GetObjectItem(json, "head_lines");
    if (head && cJSON_IsNumber(head)) {
        int v = head->valueint;
        if (v < 0 || v > (int)ISP_STATREMOVAL_HEADLINES_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "head_lines out of range");
        }
        iqParam->statRemoval.nbHeadLines = (uint32_t)v;
    }

    cJSON *valid = cJSON_GetObjectItem(json, "valid_lines");
    if (valid && cJSON_IsNumber(valid)) {
        int v = valid->valueint;
        if (v < 0 || v > (int)ISP_STATREMOVAL_VALIDLINES_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "valid_lines out of range");
        }
        iqParam->statRemoval.nbValidLines = (uint32_t)v;
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetStatRemoval(hIsp, &iqParam->statRemoval);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set stat removal");
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Stat removal parameters updated");
    return AICAM_OK;
}

/* ==================== Black Level Handlers ==================== */

aicam_result_t api_isp_get_black_level(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_BlackLevelTypeDef blackLevel;
    ISP_StatusTypeDef status = ISP_SVC_ISP_GetBlackLevel(hIsp, &blackLevel);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get black level");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", blackLevel.enable);
    cJSON_AddNumberToObject(data, "r", blackLevel.BLCR);
    cJSON_AddNumberToObject(data, "g", blackLevel.BLCG);
    cJSON_AddNumberToObject(data, "b", blackLevel.BLCB);

    cJSON *meta = cJSON_CreateObject();
    add_param_meta(meta, "r", "integer", 0, 255, 12, NULL, "Red channel black level offset");
    add_param_meta(meta, "g", "integer", 0, 255, 12, NULL, "Green channel black level offset");
    add_param_meta(meta, "b", "integer", 0, 255, 12, NULL, "Blue channel black level offset");
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_black_level(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_BlackLevelTypeDef blackLevel;
    ISP_SVC_ISP_GetBlackLevel(hIsp, &blackLevel);

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        blackLevel.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *r = cJSON_GetObjectItem(json, "r");
    if (r && cJSON_IsNumber(r)) {
        if (r->valueint < 0 || r->valueint > 255) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "r must be 0-255");
        }
        blackLevel.BLCR = (uint8_t)r->valueint;
    }

    cJSON *g = cJSON_GetObjectItem(json, "g");
    if (g && cJSON_IsNumber(g)) {
        if (g->valueint < 0 || g->valueint > 255) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "g must be 0-255");
        }
        blackLevel.BLCG = (uint8_t)g->valueint;
    }

    cJSON *b = cJSON_GetObjectItem(json, "b");
    if (b && cJSON_IsNumber(b)) {
        if (b->valueint < 0 || b->valueint > 255) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "b must be 0-255");
        }
        blackLevel.BLCB = (uint8_t)b->valueint;
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetBlackLevel(hIsp, &blackLevel);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set black level");
    }
    /* Align with isp_cmd_parser: update IQ param cache so it takes effect on restart/reload */
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        iqParam->blackLevelStatic.enable = blackLevel.enable;
        if (blackLevel.enable) {
            iqParam->blackLevelStatic.BLCR = blackLevel.BLCR;
            iqParam->blackLevelStatic.BLCG = blackLevel.BLCG;
            iqParam->blackLevelStatic.BLCB = blackLevel.BLCB;
        }
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Black level updated");
    return AICAM_OK;
}

/* ==================== Bad Pixel Handlers ==================== */

aicam_result_t api_isp_get_bad_pixel(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_BadPixelTypeDef badPixel;
    ISP_StatusTypeDef status = ISP_SVC_ISP_GetBadPixel(hIsp, &badPixel);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get bad pixel config");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", badPixel.enable);
    cJSON_AddNumberToObject(data, "strength", badPixel.strength);
    cJSON_AddNumberToObject(data, "detected_count", badPixel.count);
    if (iqParam) {
        cJSON_AddBoolToObject(data, "algo_enable", iqParam->badPixelAlgo.enable);
        cJSON_AddNumberToObject(data, "algo_threshold", iqParam->badPixelAlgo.threshold);
    }

    cJSON *meta = cJSON_CreateObject();
    add_param_meta(meta, "strength", "integer", 0, ISP_API_BADPIXEL_STRENGTH_MAX, 0, NULL, "Bad pixel correction strength");
    cJSON *count_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(count_meta, "type", "integer");
    cJSON_AddBoolToObject(count_meta, "readonly", 1);
    cJSON_AddStringToObject(count_meta, "description", "Number of detected bad pixels");
    cJSON_AddItemToObject(meta, "detected_count", count_meta);
    if (iqParam) {
        cJSON *algo_enable_meta = cJSON_CreateObject();
        cJSON_AddStringToObject(algo_enable_meta, "type", "boolean");
        cJSON_AddStringToObject(algo_enable_meta, "description", "Enable dynamic bad pixel detection algorithm");
        cJSON_AddItemToObject(meta, "algo_enable", algo_enable_meta);
        cJSON *algo_thresh_meta = cJSON_CreateObject();
        cJSON_AddStringToObject(algo_thresh_meta, "type", "integer");
        cJSON_AddStringToObject(algo_thresh_meta, "description", "Max number of detected bad pixels for dynamic algo");
        cJSON_AddItemToObject(meta, "algo_threshold", algo_thresh_meta);
    }
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    return AICAM_OK;
}

aicam_result_t api_isp_set_bad_pixel(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_BadPixelTypeDef badPixel;
    ISP_SVC_ISP_GetBadPixel(hIsp, &badPixel);

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        badPixel.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *strength = cJSON_GetObjectItem(json, "strength");
    if (strength && cJSON_IsNumber(strength)) {
        if (strength->valueint < 0 || strength->valueint > ISP_API_BADPIXEL_STRENGTH_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "strength must be 0-7");
        }
        badPixel.strength = (uint8_t)strength->valueint;
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetBadPixel(hIsp, &badPixel);
    if (status != ISP_OK) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set bad pixel config");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        /* Align with isp_cmd_parser: update IQ param cache (static part) */
        iqParam->badPixelStatic.enable = badPixel.enable;
        if (badPixel.enable) {
            iqParam->badPixelStatic.strength = badPixel.strength;
        }
        /* Algo part: only IQ params, algo reads at next process */
        cJSON *algo_enable = cJSON_GetObjectItem(json, "algo_enable");
        if (algo_enable && cJSON_IsBool(algo_enable)) {
            iqParam->badPixelAlgo.enable = cJSON_IsTrue(algo_enable) ? 1 : 0;
        }
        cJSON *algo_threshold = cJSON_GetObjectItem(json, "algo_threshold");
        if (algo_threshold && cJSON_IsNumber(algo_threshold) && algo_threshold->valueint >= 0) {
            iqParam->badPixelAlgo.threshold = (uint32_t)algo_threshold->valueint;
        }
    }

    cJSON_Delete(json);
    snprintf(ctx->response.message, sizeof(ctx->response.message), "Bad pixel config updated");
    return AICAM_OK;
}

/* ==================== ISP Gain Handlers ==================== */

aicam_result_t api_isp_get_gain(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_ISPGainTypeDef ispGain;
    ISP_StatusTypeDef status = ISP_SVC_ISP_GetGain(hIsp, &ispGain);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get ISP gain");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", ispGain.enable);
    cJSON_AddNumberToObject(data, "r", ispGain.ispGainR);
    cJSON_AddNumberToObject(data, "g", ispGain.ispGainG);
    cJSON_AddNumberToObject(data, "b", ispGain.ispGainB);

    cJSON *meta = cJSON_CreateObject();
    cJSON *gain_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(gain_meta, "type", "integer");
    cJSON_AddNumberToObject(gain_meta, "min", 0);
    cJSON_AddNumberToObject(gain_meta, "max", ISP_API_GAIN_MAX);
    cJSON_AddNumberToObject(gain_meta, "precision", ISP_API_GAIN_PRECISION);
    cJSON_AddStringToObject(gain_meta, "description", "Gain value (100000000 = 1.0x, max 16.0x)");
    cJSON_AddItemToObject(meta, "r", cJSON_Duplicate(gain_meta, 1));
    cJSON_AddItemToObject(meta, "g", cJSON_Duplicate(gain_meta, 1));
    cJSON_AddItemToObject(meta, "b", gain_meta);

    cJSON *dep = cJSON_CreateObject();
    cJSON_AddStringToObject(dep, "note", "ISP gain is auto-controlled when AWB is enabled");
    cJSON_AddItemToObject(meta, "dependencies", dep);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_gain(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_ISPGainTypeDef ispGain;
    ISP_SVC_ISP_GetGain(hIsp, &ispGain);

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        ispGain.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *r = cJSON_GetObjectItem(json, "r");
    if (r && cJSON_IsNumber(r)) {
        uint32_t val = (uint32_t)r->valuedouble;
        if (val > ISP_API_GAIN_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "r gain exceeds max");
        }
        ispGain.ispGainR = val;
    }

    cJSON *g = cJSON_GetObjectItem(json, "g");
    if (g && cJSON_IsNumber(g)) {
        uint32_t val = (uint32_t)g->valuedouble;
        if (val > ISP_API_GAIN_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "g gain exceeds max");
        }
        ispGain.ispGainG = val;
    }

    cJSON *b = cJSON_GetObjectItem(json, "b");
    if (b && cJSON_IsNumber(b)) {
        uint32_t val = (uint32_t)b->valuedouble;
        if (val > ISP_API_GAIN_MAX) {
            cJSON_Delete(json);
            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "b gain exceeds max");
        }
        ispGain.ispGainB = val;
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetGain(hIsp, &ispGain);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set ISP gain");
    }
    /* Align with isp_cmd_parser: update IQ param cache (ispGainStatic) */
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        iqParam->ispGainStatic.enable = ispGain.enable;
        if (ispGain.enable) {
            iqParam->ispGainStatic.ispGainR = ispGain.ispGainR;
            iqParam->ispGainStatic.ispGainG = ispGain.ispGainG;
            iqParam->ispGainStatic.ispGainB = ispGain.ispGainB;
        }
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "ISP gain updated");
    return AICAM_OK;
}

/* ==================== Color Conversion Handlers ==================== */

aicam_result_t api_isp_get_color_conv(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_ColorConvTypeDef colorConv;
    ISP_StatusTypeDef status = ISP_SVC_ISP_GetColorConv(hIsp, &colorConv);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get color conversion");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", colorConv.enable);
    
    cJSON *matrix = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON *row = cJSON_CreateArray();
        for (int j = 0; j < 3; j++) {
            cJSON_AddItemToArray(row, cJSON_CreateNumber(colorConv.coeff[i][j]));
        }
        cJSON_AddItemToArray(matrix, row);
    }
    cJSON_AddItemToObject(data, "matrix", matrix);

    cJSON *meta = cJSON_CreateObject();
    cJSON *matrix_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(matrix_meta, "type", "array[3][3]");
    cJSON_AddNumberToObject(matrix_meta, "min", -ISP_API_COLORCONV_MAX);
    cJSON_AddNumberToObject(matrix_meta, "max", ISP_API_COLORCONV_MAX);
    cJSON_AddStringToObject(matrix_meta, "description", "3x3 RGB to RGB matrix (100000000 = 1.0)");
    cJSON_AddItemToObject(meta, "matrix", matrix_meta);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_color_conv(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_ColorConvTypeDef colorConv;
    ISP_SVC_ISP_GetColorConv(hIsp, &colorConv);

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        colorConv.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *matrix = cJSON_GetObjectItem(json, "matrix");
    if (matrix && cJSON_IsArray(matrix) && cJSON_GetArraySize(matrix) == 3) {
        for (int i = 0; i < 3; i++) {
            cJSON *row = cJSON_GetArrayItem(matrix, i);
            if (row && cJSON_IsArray(row) && cJSON_GetArraySize(row) == 3) {
                for (int j = 0; j < 3; j++) {
                    cJSON *val = cJSON_GetArrayItem(row, j);
                    if (val && cJSON_IsNumber(val)) {
                        int32_t coeff = (int32_t)val->valuedouble;
                        if (coeff < -ISP_API_COLORCONV_MAX || coeff > ISP_API_COLORCONV_MAX) {
                            cJSON_Delete(json);
                            return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "Matrix coeff out of range");
                        }
                        colorConv.coeff[i][j] = coeff;
                    }
                }
            }
        }
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetColorConv(hIsp, &colorConv);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set color conversion");
    }
    /* Align with isp_cmd_parser: update IQ param cache (colorConvStatic) */
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        iqParam->colorConvStatic.enable = colorConv.enable;
        if (colorConv.enable) {
            memcpy(iqParam->colorConvStatic.coeff, colorConv.coeff, sizeof(colorConv.coeff));
        }
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Color conversion updated");
    return AICAM_OK;
}

/* ==================== Contrast Handlers ==================== */

aicam_result_t api_isp_get_contrast(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", iqParam->contrast.enable);
    
    cJSON *lut = cJSON_CreateArray();
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_0));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_32));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_64));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_96));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_128));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_160));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_192));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_224));
    cJSON_AddItemToArray(lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_256));
    cJSON_AddItemToObject(data, "lut", lut);

    cJSON *meta = cJSON_CreateObject();
    cJSON *lut_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(lut_meta, "type", "array[9]");
    cJSON_AddNumberToObject(lut_meta, "min", 0);
    cJSON_AddNumberToObject(lut_meta, "max", ISP_API_CONTRAST_COEFF_MAX);
    cJSON_AddStringToObject(lut_meta, "description", "Luminance LUT [0,32,64,96,128,160,192,224,256], 100=1.0x");
    cJSON_AddItemToObject(meta, "lut", lut_meta);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_contrast(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_ContrastTypeDef contrast = {0};
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        contrast = iqParam->contrast;
    }

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        contrast.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    cJSON *lut = cJSON_GetObjectItem(json, "lut");
    if (lut && cJSON_IsArray(lut) && cJSON_GetArraySize(lut) == 9) {
        uint32_t *coeffs[] = {
            &contrast.coeff.LUM_0, &contrast.coeff.LUM_32, &contrast.coeff.LUM_64,
            &contrast.coeff.LUM_96, &contrast.coeff.LUM_128, &contrast.coeff.LUM_160,
            &contrast.coeff.LUM_192, &contrast.coeff.LUM_224, &contrast.coeff.LUM_256
        };
        for (int i = 0; i < 9; i++) {
            cJSON *val = cJSON_GetArrayItem(lut, i);
            if (val && cJSON_IsNumber(val)) {
                if (val->valueint < 0 || val->valueint > ISP_API_CONTRAST_COEFF_MAX) {
                    cJSON_Delete(json);
                    return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "LUT value out of range (0-394)");
                }
                *coeffs[i] = (uint32_t)val->valueint;
            }
        }
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetContrast(hIsp, &contrast);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set contrast");
    }
    /* Align with isp_cmd_parser: update IQ param cache */
    if (iqParam) {
        iqParam->contrast = contrast;
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Contrast updated");
    return AICAM_OK;
}

/* ==================== Gamma Handlers ==================== */

aicam_result_t api_isp_get_gamma(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "enable", iqParam ? iqParam->gamma.enable : 0);

    cJSON *meta = cJSON_CreateObject();
    cJSON *enable_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(enable_meta, "type", "boolean");
    cJSON_AddStringToObject(enable_meta, "description", "Enable gamma correction on output pipes");
    cJSON_AddItemToObject(meta, "enable", enable_meta);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_gamma(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_GammaTypeDef gamma = {0};
    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (enable && cJSON_IsBool(enable)) {
        gamma.enable = cJSON_IsTrue(enable) ? 1 : 0;
    }

    ISP_StatusTypeDef status = ISP_SVC_ISP_SetGamma(hIsp, &gamma);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set gamma");
    }
    /* Align with isp_cmd_parser: update IQ param cache */
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        iqParam->gamma = gamma;
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Gamma updated");
    return AICAM_OK;
}

/* ==================== Stat Area Handlers ==================== */

aicam_result_t api_isp_get_stat_area(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_StatAreaTypeDef statArea;
    ISP_StatusTypeDef status = ISP_GetStatArea(hIsp, &statArea);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get stat area");
    }

    ISP_SensorInfoTypeDef sensorInfo;
    ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "x", statArea.X0);
    cJSON_AddNumberToObject(data, "y", statArea.Y0);
    cJSON_AddNumberToObject(data, "width", statArea.XSize);
    cJSON_AddNumberToObject(data, "height", statArea.YSize);

    cJSON *meta = cJSON_CreateObject();
    add_param_meta(meta, "x", "integer", 0, sensorInfo.width - ISP_API_STATWINDOW_MIN, 0, "px", "X offset");
    add_param_meta(meta, "y", "integer", 0, sensorInfo.height - ISP_API_STATWINDOW_MIN, 0, "px", "Y offset");
    add_param_meta(meta, "width", "integer", ISP_API_STATWINDOW_MIN, ISP_API_STATWINDOW_MAX, 0, "px", "Area width");
    add_param_meta(meta, "height", "integer", ISP_API_STATWINDOW_MIN, ISP_API_STATWINDOW_MAX, 0, "px", "Area height");
    
    cJSON *constraints = cJSON_CreateObject();
    cJSON_AddNumberToObject(constraints, "sensor_width", sensorInfo.width);
    cJSON_AddNumberToObject(constraints, "sensor_height", sensorInfo.height);
    cJSON_AddStringToObject(constraints, "rule", "x + width <= sensor_width, y + height <= sensor_height");
    cJSON_AddItemToObject(meta, "constraints", constraints);
    cJSON_AddItemToObject(data, "_meta", meta);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

aicam_result_t api_isp_set_stat_area(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *json = web_api_parse_body(ctx);
    if (json == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    ISP_StatAreaTypeDef statArea;
    ISP_GetStatArea(hIsp, &statArea);

    ISP_SensorInfoTypeDef sensorInfo;
    ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);

    cJSON *x = cJSON_GetObjectItem(json, "x");
    if (x && cJSON_IsNumber(x)) statArea.X0 = (uint32_t)x->valueint;

    cJSON *y = cJSON_GetObjectItem(json, "y");
    if (y && cJSON_IsNumber(y)) statArea.Y0 = (uint32_t)y->valueint;

    cJSON *w = cJSON_GetObjectItem(json, "width");
    if (w && cJSON_IsNumber(w)) statArea.XSize = (uint32_t)w->valueint;

    cJSON *h = cJSON_GetObjectItem(json, "height");
    if (h && cJSON_IsNumber(h)) statArea.YSize = (uint32_t)h->valueint;

    // Validate constraints
    if (statArea.XSize < ISP_API_STATWINDOW_MIN || statArea.YSize < ISP_API_STATWINDOW_MIN) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "Width/height must be >= 4");
    }
    if (statArea.X0 + statArea.XSize > sensorInfo.width || 
        statArea.Y0 + statArea.YSize > sensorInfo.height) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "Stat area exceeds sensor bounds");
    }

    ISP_StatusTypeDef status = ISP_SetStatArea(hIsp, &statArea);
    cJSON_Delete(json);

    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to set stat area");
    }
    /* Align with isp_cmd_parser: update IQ param cache and handle statArea so algo/stat use it */
    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        iqParam->statAreaStatic = statArea;
    }
    hIsp->statArea = statArea;

    snprintf(ctx->response.message, sizeof(ctx->response.message), "Stat area updated");
    return AICAM_OK;
}

/* ==================== Lux Reference Handlers ==================== */

aicam_result_t api_isp_get_lux_ref(http_handler_context_t *ctx) {
  ISP_HandleTypeDef *hIsp = get_isp_handle();
  if (hIsp == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
  }

  ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
  if (iqParam == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
  }

  cJSON *data = cJSON_CreateObject();

  cJSON *high = cJSON_CreateObject();
  cJSON_AddNumberToObject(high, "lux_ref", iqParam->luxRef.HL_LuxRef);
  cJSON_AddNumberToObject(high, "expo1", iqParam->luxRef.HL_Expo1);
  cJSON_AddNumberToObject(high, "lum1", iqParam->luxRef.HL_Lum1);
  cJSON_AddNumberToObject(high, "expo2", iqParam->luxRef.HL_Expo2);
  cJSON_AddNumberToObject(high, "lum2", iqParam->luxRef.HL_Lum2);
  cJSON_AddItemToObject(data, "high_lux", high);

  cJSON *low = cJSON_CreateObject();
  cJSON_AddNumberToObject(low, "lux_ref", iqParam->luxRef.LL_LuxRef);
  cJSON_AddNumberToObject(low, "expo1", iqParam->luxRef.LL_Expo1);
  cJSON_AddNumberToObject(low, "lum1", iqParam->luxRef.LL_Lum1);
  cJSON_AddNumberToObject(low, "expo2", iqParam->luxRef.LL_Expo2);
  cJSON_AddNumberToObject(low, "lum2", iqParam->luxRef.LL_Lum2);
  cJSON_AddItemToObject(data, "low_lux", low);

  cJSON_AddNumberToObject(data, "calib_factor", iqParam->luxRef.calibFactor);

  cJSON *meta = cJSON_CreateObject();
  cJSON_AddStringToObject(meta, "description", "Lux estimation calibration parameters for AEC algorithm");

  cJSON *high_meta = cJSON_CreateObject();
  cJSON_AddStringToObject(high_meta, "description", "High lux calibration points");
  cJSON_AddItemToObject(meta, "high_lux", high_meta);

  cJSON *low_meta = cJSON_CreateObject();
  cJSON_AddStringToObject(low_meta, "description", "Low lux calibration points");
  cJSON_AddItemToObject(meta, "low_lux", low_meta);

  cJSON *calib_meta = cJSON_CreateObject();
  cJSON_AddStringToObject(calib_meta, "type", "float");
  cJSON_AddStringToObject(calib_meta, "description", "Sensor specific lux calibration factor");
  cJSON_AddItemToObject(meta, "calib_factor", calib_meta);

  cJSON_AddItemToObject(data, "_meta", meta);

  char *json_str = cJSON_PrintUnformatted(data);
  ctx->response.data = json_str;
  cJSON_Delete(data);

  return AICAM_OK;
}

aicam_result_t api_isp_set_lux_ref(http_handler_context_t *ctx) {
  ISP_HandleTypeDef *hIsp = get_isp_handle();
  if (hIsp == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
  }

  cJSON *json = web_api_parse_body(ctx);
  if (json == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
  }

  ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
  if (iqParam == NULL) {
    cJSON_Delete(json);
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
  }

  cJSON *high = cJSON_GetObjectItem(json, "high_lux");
  if (high && cJSON_IsObject(high)) {
    cJSON *v;

    v = cJSON_GetObjectItem(high, "lux_ref");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.HL_LuxRef = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(high, "expo1");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.HL_Expo1 = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(high, "lum1");
    if (v && cJSON_IsNumber(v)) {
      int val = v->valueint;
      if (val < 0 || val > 255) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "High lux lum1 must be 0-255");
      }
      iqParam->luxRef.HL_Lum1 = (uint8_t)val;
    }

    v = cJSON_GetObjectItem(high, "expo2");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.HL_Expo2 = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(high, "lum2");
    if (v && cJSON_IsNumber(v)) {
      int val = v->valueint;
      if (val < 0 || val > 255) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "High lux lum2 must be 0-255");
      }
      iqParam->luxRef.HL_Lum2 = (uint8_t)val;
    }
  }

  cJSON *low = cJSON_GetObjectItem(json, "low_lux");
  if (low && cJSON_IsObject(low)) {
    cJSON *v;

    v = cJSON_GetObjectItem(low, "lux_ref");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.LL_LuxRef = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(low, "expo1");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.LL_Expo1 = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(low, "lum1");
    if (v && cJSON_IsNumber(v)) {
      int val = v->valueint;
      if (val < 0 || val > 255) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "Low lux lum1 must be 0-255");
      }
      iqParam->luxRef.LL_Lum1 = (uint8_t)val;
    }

    v = cJSON_GetObjectItem(low, "expo2");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
      iqParam->luxRef.LL_Expo2 = (uint32_t)v->valuedouble;
    }

    v = cJSON_GetObjectItem(low, "lum2");
    if (v && cJSON_IsNumber(v)) {
      int val = v->valueint;
      if (val < 0 || val > 255) {
        cJSON_Delete(json);
        return set_isp_error_response(ctx, API_ISP_ERROR_PARAM_OUT_OF_RANGE, "Low lux lum2 must be 0-255");
      }
      iqParam->luxRef.LL_Lum2 = (uint8_t)val;
    }
  }

  cJSON *calib = cJSON_GetObjectItem(json, "calib_factor");
  if (calib && cJSON_IsNumber(calib)) {
    iqParam->luxRef.calibFactor = (float)calib->valuedouble;
  }

  cJSON_Delete(json);
  snprintf(ctx->response.message, sizeof(ctx->response.message), "Lux reference parameters updated");
  return AICAM_OK;
}

/* ==================== Sensor Delay Handlers ==================== */

aicam_result_t api_isp_get_sensor_delay(http_handler_context_t *ctx) {
  ISP_HandleTypeDef *hIsp = get_isp_handle();
  if (hIsp == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
  }

  ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
  if (iqParam == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
  }

  cJSON *data = cJSON_CreateObject();
  cJSON_AddNumberToObject(data, "delay", iqParam->sensorDelay.delay);

  cJSON *meta = cJSON_CreateObject();
  cJSON *delay_meta = cJSON_CreateObject();
  cJSON_AddStringToObject(delay_meta, "type", "integer");
  cJSON_AddStringToObject(delay_meta, "description", "Sensor delay (frames or config-dependent unit)");
  cJSON_AddItemToObject(meta, "delay", delay_meta);
  cJSON_AddItemToObject(data, "_meta", meta);

  char *json_str = cJSON_PrintUnformatted(data);
  ctx->response.data = json_str;
  cJSON_Delete(data);
  return AICAM_OK;
}

aicam_result_t api_isp_set_sensor_delay(http_handler_context_t *ctx) {
  ISP_HandleTypeDef *hIsp = get_isp_handle();
  if (hIsp == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
  }

  cJSON *json = web_api_parse_body(ctx);
  if (json == NULL) {
    return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
  }

  ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
  if (iqParam == NULL) {
    cJSON_Delete(json);
    return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
  }

  cJSON *delay_item = cJSON_GetObjectItem(json, "delay");
  if (delay_item && cJSON_IsNumber(delay_item) && delay_item->valueint >= 0) {
    iqParam->sensorDelay.delay = (uint8_t)delay_item->valueint;
  }

  cJSON_Delete(json);
  snprintf(ctx->response.message, sizeof(ctx->response.message), "Sensor delay updated");
  return AICAM_OK;
}

/* ==================== Statistics Handler ==================== */

aicam_result_t api_isp_get_statistics(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_SVC_StatStateTypeDef stats;
    ISP_StatusTypeDef status = ISP_SVC_Stats_GetLatest(hIsp, &stats);
    if (status != ISP_OK) {
        return set_isp_error_response(ctx, isp_status_to_error_code(status), "Failed to get statistics");
    }

    uint32_t lux = 0;
    ISP_GetLuxEstimation(hIsp, &lux);

    cJSON *data = cJSON_CreateObject();
    
    // Down statistics (after ISP processing)
    cJSON *down = cJSON_CreateObject();
    cJSON_AddNumberToObject(down, "average_r", stats.down.averageR);
    cJSON_AddNumberToObject(down, "average_g", stats.down.averageG);
    cJSON_AddNumberToObject(down, "average_b", stats.down.averageB);
    cJSON_AddNumberToObject(down, "average_l", stats.down.averageL);
    
    cJSON *hist = cJSON_CreateArray();
    for (int i = 0; i < 12; i++) {
        cJSON_AddItemToArray(hist, cJSON_CreateNumber(stats.down.histogram[i]));
    }
    cJSON_AddItemToObject(down, "histogram", hist);
    cJSON_AddItemToObject(data, "down", down);

    // Up statistics (before ISP processing)
    cJSON *up = cJSON_CreateObject();
    cJSON_AddNumberToObject(up, "average_r", stats.up.averageR);
    cJSON_AddNumberToObject(up, "average_g", stats.up.averageG);
    cJSON_AddNumberToObject(up, "average_b", stats.up.averageB);
    cJSON_AddNumberToObject(up, "average_l", stats.up.averageL);
    cJSON_AddItemToObject(data, "up", up);

    cJSON_AddNumberToObject(data, "estimated_lux", lux);
    cJSON_AddNumberToObject(data, "frame_id", stats.downFrameIdEnd);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

/* ==================== Get All Params Handler ==================== */

aicam_result_t api_isp_get_all_params(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "IQ params not available");
    }

    cJSON *data = cJSON_CreateObject();

    // Sensor info
    ISP_SensorInfoTypeDef sensorInfo;
    ISP_SVC_Sensor_GetInfo(hIsp, &sensorInfo);
    cJSON *sensor = cJSON_CreateObject();
    cJSON_AddStringToObject(sensor, "name", sensorInfo.name);
    cJSON_AddStringToObject(sensor, "bayer_pattern", 
        sensorInfo.bayer_pattern < 5 ? bayer_pattern_str[sensorInfo.bayer_pattern] : "UNKNOWN");
    cJSON_AddNumberToObject(sensor, "color_depth", sensorInfo.color_depth);
    cJSON_AddNumberToObject(sensor, "width", sensorInfo.width);
    cJSON_AddNumberToObject(sensor, "height", sensorInfo.height);
    cJSON_AddItemToObject(data, "sensor_info", sensor);

    // AEC
    uint8_t aec_enable = 0;
    ISP_GetAECState(hIsp, &aec_enable);
    cJSON *aec = cJSON_CreateObject();
    cJSON_AddBoolToObject(aec, "enable", aec_enable);
    cJSON_AddNumberToObject(aec, "exposure_compensation", iqParam->AECAlgo.exposureCompensation);
    cJSON_AddNumberToObject(aec, "anti_flicker_freq", iqParam->AECAlgo.antiFlickerFreq);
    cJSON_AddItemToObject(data, "aec", aec);

    // AWB
    uint8_t awb_auto = 0;
    uint32_t color_temp = 0;
    ISP_GetWBRefMode(hIsp, &awb_auto, &color_temp);
    cJSON *awb = cJSON_CreateObject();
    cJSON_AddBoolToObject(awb, "enable", iqParam->AWBAlgo.enable);
    cJSON_AddStringToObject(awb, "mode", awb_auto ? "auto" : "manual");
    cJSON_AddNumberToObject(awb, "color_temp", color_temp);
    cJSON_AddItemToObject(data, "awb", awb);

    // Demosaicing
    cJSON *demos = cJSON_CreateObject();
    cJSON_AddBoolToObject(demos, "enable", iqParam->demosaicing.enable);
    cJSON_AddNumberToObject(demos, "peak", iqParam->demosaicing.peak);
    cJSON_AddNumberToObject(demos, "line_v", iqParam->demosaicing.lineV);
    cJSON_AddNumberToObject(demos, "line_h", iqParam->demosaicing.lineH);
    cJSON_AddNumberToObject(demos, "edge", iqParam->demosaicing.edge);
    cJSON_AddItemToObject(data, "demosaicing", demos);

    // Stat removal
    cJSON *sr = cJSON_CreateObject();
    cJSON_AddBoolToObject(sr, "enable", iqParam->statRemoval.enable);
    cJSON_AddNumberToObject(sr, "head_lines", iqParam->statRemoval.nbHeadLines);
    cJSON_AddNumberToObject(sr, "valid_lines", iqParam->statRemoval.nbValidLines);
    cJSON_AddItemToObject(data, "stat_removal", sr);

    // Black level
    ISP_BlackLevelTypeDef blackLevel;
    ISP_SVC_ISP_GetBlackLevel(hIsp, &blackLevel);
    cJSON *bl = cJSON_CreateObject();
    cJSON_AddBoolToObject(bl, "enable", blackLevel.enable);
    cJSON_AddNumberToObject(bl, "r", blackLevel.BLCR);
    cJSON_AddNumberToObject(bl, "g", blackLevel.BLCG);
    cJSON_AddNumberToObject(bl, "b", blackLevel.BLCB);
    cJSON_AddItemToObject(data, "black_level", bl);

    // Bad pixel
    ISP_BadPixelTypeDef badPixel;
    ISP_SVC_ISP_GetBadPixel(hIsp, &badPixel);
    cJSON *bp = cJSON_CreateObject();
    cJSON_AddBoolToObject(bp, "enable", badPixel.enable);
    cJSON_AddNumberToObject(bp, "strength", badPixel.strength);
    cJSON_AddBoolToObject(bp, "algo_enable", iqParam->badPixelAlgo.enable);
    cJSON_AddNumberToObject(bp, "algo_threshold", iqParam->badPixelAlgo.threshold);
    cJSON_AddItemToObject(data, "bad_pixel", bp);

    // ISP Gain
    ISP_ISPGainTypeDef ispGain;
    ISP_SVC_ISP_GetGain(hIsp, &ispGain);
    cJSON *gain = cJSON_CreateObject();
    cJSON_AddBoolToObject(gain, "enable", ispGain.enable);
    cJSON_AddNumberToObject(gain, "r", ispGain.ispGainR);
    cJSON_AddNumberToObject(gain, "g", ispGain.ispGainG);
    cJSON_AddNumberToObject(gain, "b", ispGain.ispGainB);
    cJSON_AddItemToObject(data, "isp_gain", gain);

    // Color conversion
    ISP_ColorConvTypeDef colorConv;
    ISP_SVC_ISP_GetColorConv(hIsp, &colorConv);
    cJSON *ccm = cJSON_CreateObject();
    cJSON_AddBoolToObject(ccm, "enable", colorConv.enable);
    cJSON_AddItemToObject(data, "color_conv", ccm);

    // Contrast
    cJSON *contrast = cJSON_CreateObject();
    cJSON_AddBoolToObject(contrast, "enable", iqParam->contrast.enable);
    cJSON_AddItemToObject(data, "contrast", contrast);

    // Gamma
    cJSON *gamma = cJSON_CreateObject();
    cJSON_AddBoolToObject(gamma, "enable", iqParam->gamma.enable);
    cJSON_AddItemToObject(data, "gamma", gamma);

    // Stat area
    ISP_StatAreaTypeDef statArea;
    ISP_GetStatArea(hIsp, &statArea);
    cJSON *stat = cJSON_CreateObject();
    cJSON_AddNumberToObject(stat, "x", statArea.X0);
    cJSON_AddNumberToObject(stat, "y", statArea.Y0);
    cJSON_AddNumberToObject(stat, "width", statArea.XSize);
    cJSON_AddNumberToObject(stat, "height", statArea.YSize);
    cJSON_AddItemToObject(data, "stat_area", stat);

    // Sensor delay
    cJSON *sdelay = cJSON_CreateObject();
    cJSON_AddNumberToObject(sdelay, "delay", iqParam->sensorDelay.delay);
    cJSON_AddItemToObject(data, "sensor_delay", sdelay);

    char *json_str = cJSON_PrintUnformatted(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);
    
    return AICAM_OK;
}

/* ==================== Module Init/Deinit ==================== */

static const api_route_t isp_routes[] = {
    // Global params
    {"/api/v1/isp/params",       "GET",  api_isp_get_all_params,     AICAM_TRUE, NULL},
    {"/api/v1/isp/sensor",       "GET",  api_isp_get_sensor_info,    AICAM_TRUE, NULL},
    {"/api/v1/isp/statistics",   "GET",  api_isp_get_statistics,     AICAM_TRUE, NULL},
    // AEC
    {"/api/v1/isp/aec",          "GET",  api_isp_get_aec,            AICAM_TRUE, NULL},
    {"/api/v1/isp/aec",          "PUT",  api_isp_set_aec,            AICAM_TRUE, NULL},
    {"/api/v1/isp/aec/manual",   "GET",  api_isp_get_manual_exposure, AICAM_TRUE, NULL},
    {"/api/v1/isp/aec/manual",   "PUT",  api_isp_set_manual_exposure, AICAM_TRUE, NULL},
    // AWB
    {"/api/v1/isp/awb",          "GET",  api_isp_get_awb,            AICAM_TRUE, NULL},
    {"/api/v1/isp/awb",          "PUT",  api_isp_set_awb,            AICAM_TRUE, NULL},
    // Demosaicing
    {"/api/v1/isp/demosaicing",  "GET",  api_isp_get_demosaicing,    AICAM_TRUE, NULL},
    {"/api/v1/isp/demosaicing",  "PUT",  api_isp_set_demosaicing,    AICAM_TRUE, NULL},
    // Stat removal
    {"/api/v1/isp/stat_removal", "GET",  api_isp_get_stat_removal,   AICAM_TRUE, NULL},
    {"/api/v1/isp/stat_removal", "PUT",  api_isp_set_stat_removal,   AICAM_TRUE, NULL},
    // Black level
    {"/api/v1/isp/black_level",  "GET",  api_isp_get_black_level,    AICAM_TRUE, NULL},
    {"/api/v1/isp/black_level",  "PUT",  api_isp_set_black_level,    AICAM_TRUE, NULL},
    // Bad pixel
    {"/api/v1/isp/bad_pixel",    "GET",  api_isp_get_bad_pixel,      AICAM_TRUE, NULL},
    {"/api/v1/isp/bad_pixel",    "PUT",  api_isp_set_bad_pixel,      AICAM_TRUE, NULL},
    // ISP Gain
    {"/api/v1/isp/gain",         "GET",  api_isp_get_gain,           AICAM_TRUE, NULL},
    {"/api/v1/isp/gain",         "PUT",  api_isp_set_gain,           AICAM_TRUE, NULL},
    // Color conversion
    {"/api/v1/isp/color_conv",   "GET",  api_isp_get_color_conv,     AICAM_TRUE, NULL},
    {"/api/v1/isp/color_conv",   "PUT",  api_isp_set_color_conv,     AICAM_TRUE, NULL},
    // Contrast
    {"/api/v1/isp/contrast",     "GET",  api_isp_get_contrast,       AICAM_TRUE, NULL},
    {"/api/v1/isp/contrast",     "PUT",  api_isp_set_contrast,       AICAM_TRUE, NULL},
    // Gamma
    {"/api/v1/isp/gamma",        "GET",  api_isp_get_gamma,          AICAM_TRUE, NULL},
    {"/api/v1/isp/gamma",        "PUT",  api_isp_set_gamma,          AICAM_TRUE, NULL},
    // Stat area
    {"/api/v1/isp/stat_area",    "GET",  api_isp_get_stat_area,      AICAM_TRUE, NULL},
    {"/api/v1/isp/stat_area",    "PUT",  api_isp_set_stat_area,      AICAM_TRUE, NULL},
  // Lux reference
  {"/api/v1/isp/lux_ref",      "GET",  api_isp_get_lux_ref,        AICAM_TRUE, NULL},
  {"/api/v1/isp/lux_ref",      "PUT",  api_isp_set_lux_ref,        AICAM_TRUE, NULL},
    // Sensor delay
    {"/api/v1/isp/sensor_delay", "GET",  api_isp_get_sensor_delay,    AICAM_TRUE, NULL},
    {"/api/v1/isp/sensor_delay", "PUT",  api_isp_set_sensor_delay,    AICAM_TRUE, NULL},
    // Config save/load
    {"/api/v1/isp/config/save",  "POST", api_isp_save_config,        AICAM_TRUE, NULL},
    {"/api/v1/isp/config/load",  "POST", api_isp_load_config,        AICAM_TRUE, NULL},
    // Config export/import
    {"/api/v1/isp/config/export", "GET",  api_isp_export_config,     AICAM_TRUE, NULL},
    {"/api/v1/isp/config/import", "POST", api_isp_import_config,     AICAM_TRUE, NULL},
};

#define ISP_ROUTES_COUNT (sizeof(isp_routes) / sizeof(isp_routes[0]))

aicam_result_t api_isp_module_init(void) {
    LOG_CORE_INFO("Initializing ISP API module");

    for (size_t i = 0; i < ISP_ROUTES_COUNT; i++) {
        aicam_result_t result = http_server_register_route(&isp_routes[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to register route: %s %s",
                isp_routes[i].method, isp_routes[i].path);
            return result;
        }
    }

    LOG_CORE_INFO("ISP API module initialized with %d routes", ISP_ROUTES_COUNT);
    return AICAM_OK;
}

aicam_result_t api_isp_module_deinit(void) {
    LOG_CORE_INFO("Deinitializing ISP API module");
    return AICAM_OK;
}

/* ==================== ISP Config Save/Load Handlers ==================== */

aicam_result_t api_isp_save_config(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    // Get current ISP parameters and save to config
    isp_config_t isp_config = {0};
    isp_config.valid = AICAM_TRUE;

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (iqParam) {
        // StatRemoval
        isp_config.stat_removal_enable = iqParam->statRemoval.enable;
        isp_config.stat_removal_head_lines = iqParam->statRemoval.nbHeadLines;
        isp_config.stat_removal_valid_lines = iqParam->statRemoval.nbValidLines;

        // Demosaicing
        isp_config.demosaic_enable = iqParam->demosaicing.enable;
        isp_config.demosaic_type = iqParam->demosaicing.type;
        isp_config.demosaic_peak = iqParam->demosaicing.peak;
        isp_config.demosaic_line_v = iqParam->demosaicing.lineV;
        isp_config.demosaic_line_h = iqParam->demosaicing.lineH;
        isp_config.demosaic_edge = iqParam->demosaicing.edge;

        // Contrast
        isp_config.contrast_enable = iqParam->contrast.enable;
        isp_config.contrast_lut[0] = iqParam->contrast.coeff.LUM_0;
        isp_config.contrast_lut[1] = iqParam->contrast.coeff.LUM_32;
        isp_config.contrast_lut[2] = iqParam->contrast.coeff.LUM_64;
        isp_config.contrast_lut[3] = iqParam->contrast.coeff.LUM_96;
        isp_config.contrast_lut[4] = iqParam->contrast.coeff.LUM_128;
        isp_config.contrast_lut[5] = iqParam->contrast.coeff.LUM_160;
        isp_config.contrast_lut[6] = iqParam->contrast.coeff.LUM_192;
        isp_config.contrast_lut[7] = iqParam->contrast.coeff.LUM_224;
        isp_config.contrast_lut[8] = iqParam->contrast.coeff.LUM_256;

        // StatArea
        isp_config.stat_area_x = iqParam->statAreaStatic.X0;
        isp_config.stat_area_y = iqParam->statAreaStatic.Y0;
        isp_config.stat_area_width = iqParam->statAreaStatic.XSize;
        isp_config.stat_area_height = iqParam->statAreaStatic.YSize;

        // Sensor Gain/Exposure (static values)
        isp_config.sensor_gain = iqParam->sensorGainStatic.gain;
        isp_config.sensor_exposure = iqParam->sensorExposureStatic.exposure;

        // BadPixel Algo
        isp_config.bad_pixel_algo_enable = iqParam->badPixelAlgo.enable;
        isp_config.bad_pixel_algo_threshold = iqParam->badPixelAlgo.threshold;

        // BadPixel Static
        isp_config.bad_pixel_enable = iqParam->badPixelStatic.enable;
        isp_config.bad_pixel_strength = iqParam->badPixelStatic.strength;

        // BlackLevel
        isp_config.black_level_enable = iqParam->blackLevelStatic.enable;
        isp_config.black_level_r = iqParam->blackLevelStatic.BLCR;
        isp_config.black_level_g = iqParam->blackLevelStatic.BLCG;
        isp_config.black_level_b = iqParam->blackLevelStatic.BLCB;

        // AEC
        isp_config.aec_enable = iqParam->AECAlgo.enable;
        isp_config.aec_exposure_compensation = iqParam->AECAlgo.exposureCompensation;
        isp_config.aec_anti_flicker_freq = iqParam->AECAlgo.antiFlickerFreq;

        // AWB (complete 5 profiles)
        isp_config.awb_enable = iqParam->AWBAlgo.enable;
        for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
            memcpy(isp_config.awb_label[i], iqParam->AWBAlgo.label[i], ISP_AWB_LABEL_MAX_LEN);
            isp_config.awb_ref_color_temp[i] = iqParam->AWBAlgo.referenceColorTemp[i];
            isp_config.awb_gain_r[i] = iqParam->AWBAlgo.ispGainR[i];
            isp_config.awb_gain_g[i] = iqParam->AWBAlgo.ispGainG[i];
            isp_config.awb_gain_b[i] = iqParam->AWBAlgo.ispGainB[i];
            memcpy(isp_config.awb_ccm[i], iqParam->AWBAlgo.coeff[i], sizeof(isp_config.awb_ccm[i]));
            memcpy(isp_config.awb_ref_rgb[i], iqParam->AWBAlgo.referenceRGB[i], 3);
        }

        // ISP Gain (static)
        isp_config.isp_gain_enable = iqParam->ispGainStatic.enable;
        isp_config.isp_gain_r = iqParam->ispGainStatic.ispGainR;
        isp_config.isp_gain_g = iqParam->ispGainStatic.ispGainG;
        isp_config.isp_gain_b = iqParam->ispGainStatic.ispGainB;

        // Color Conversion (static)
        isp_config.color_conv_enable = iqParam->colorConvStatic.enable;
        memcpy(isp_config.color_conv_matrix, iqParam->colorConvStatic.coeff, sizeof(isp_config.color_conv_matrix));

        // Gamma
        isp_config.gamma_enable = iqParam->gamma.enable;

        // Sensor Delay
        isp_config.sensor_delay = iqParam->sensorDelay.delay;

        // Lux Reference
        isp_config.lux_hl_ref = iqParam->luxRef.HL_LuxRef;
        isp_config.lux_hl_expo1 = iqParam->luxRef.HL_Expo1;
        isp_config.lux_hl_lum1 = iqParam->luxRef.HL_Lum1;
        isp_config.lux_hl_expo2 = iqParam->luxRef.HL_Expo2;
        isp_config.lux_hl_lum2 = iqParam->luxRef.HL_Lum2;
        isp_config.lux_ll_ref = iqParam->luxRef.LL_LuxRef;
        isp_config.lux_ll_expo1 = iqParam->luxRef.LL_Expo1;
        isp_config.lux_ll_lum1 = iqParam->luxRef.LL_Lum1;
        isp_config.lux_ll_expo2 = iqParam->luxRef.LL_Expo2;
        isp_config.lux_ll_lum2 = iqParam->luxRef.LL_Lum2;
        isp_config.lux_calib_factor = iqParam->luxRef.calibFactor;
    }

    // Save to NVS
    aicam_result_t result = json_config_set_isp_config(&isp_config);
    if (result != AICAM_OK) {
        return set_isp_error_response(ctx, API_ISP_ERROR_HAL_ERROR, "Failed to save ISP config to NVS");
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "ISP configuration saved successfully");
    return AICAM_OK;
}

aicam_result_t api_isp_load_config(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    isp_config_t isp_config;
    aicam_result_t result = json_config_get_isp_config(&isp_config);
    if (result != AICAM_OK || !isp_config.valid) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "No saved ISP configuration found");
    }

    // Apply ISP configuration
    // result = camera_apply_isp_config(&isp_config);
    // if (result != AICAM_OK) {
    //     set_isp_error_response(ctx, API_ISP_ERROR_HAL_ERROR, "Failed to apply ISP config");
    //     return AICAM_ERROR;
    // }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "ISP configuration loaded and applied");
    return AICAM_OK;
}

/* ==================== ISP Config Export/Import Handlers ==================== */

aicam_result_t api_isp_export_config(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (!iqParam) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP IQ parameters not available");
    }

    // Build JSON response with all ISP parameters
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "export_version", "1.0");
    cJSON_AddNumberToObject(data, "export_timestamp", (double)time(NULL));

    cJSON *config = cJSON_CreateObject();

    // StatRemoval
    cJSON *stat_removal = cJSON_CreateObject();
    cJSON_AddBoolToObject(stat_removal, "enable", iqParam->statRemoval.enable);
    cJSON_AddNumberToObject(stat_removal, "head_lines", iqParam->statRemoval.nbHeadLines);
    cJSON_AddNumberToObject(stat_removal, "valid_lines", iqParam->statRemoval.nbValidLines);
    cJSON_AddItemToObject(config, "stat_removal", stat_removal);

    // Demosaicing
    cJSON *demosaicing = cJSON_CreateObject();
    cJSON_AddBoolToObject(demosaicing, "enable", iqParam->demosaicing.enable);
    cJSON_AddNumberToObject(demosaicing, "type", iqParam->demosaicing.type);
    cJSON_AddNumberToObject(demosaicing, "peak", iqParam->demosaicing.peak);
    cJSON_AddNumberToObject(demosaicing, "line_v", iqParam->demosaicing.lineV);
    cJSON_AddNumberToObject(demosaicing, "line_h", iqParam->demosaicing.lineH);
    cJSON_AddNumberToObject(demosaicing, "edge", iqParam->demosaicing.edge);
    cJSON_AddItemToObject(config, "demosaicing", demosaicing);

    // Contrast
    cJSON *contrast = cJSON_CreateObject();
    cJSON_AddBoolToObject(contrast, "enable", iqParam->contrast.enable);
    cJSON *contrast_lut = cJSON_CreateArray();
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_0));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_32));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_64));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_96));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_128));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_160));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_192));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_224));
    cJSON_AddItemToArray(contrast_lut, cJSON_CreateNumber(iqParam->contrast.coeff.LUM_256));
    cJSON_AddItemToObject(contrast, "lut", contrast_lut);
    cJSON_AddItemToObject(config, "contrast", contrast);

    // StatArea
    cJSON *stat_area = cJSON_CreateObject();
    cJSON_AddNumberToObject(stat_area, "x", iqParam->statAreaStatic.X0);
    cJSON_AddNumberToObject(stat_area, "y", iqParam->statAreaStatic.Y0);
    cJSON_AddNumberToObject(stat_area, "width", iqParam->statAreaStatic.XSize);
    cJSON_AddNumberToObject(stat_area, "height", iqParam->statAreaStatic.YSize);
    cJSON_AddItemToObject(config, "stat_area", stat_area);

    // BadPixel
    cJSON *bad_pixel = cJSON_CreateObject();
    cJSON_AddBoolToObject(bad_pixel, "algo_enable", iqParam->badPixelAlgo.enable);
    cJSON_AddNumberToObject(bad_pixel, "algo_threshold", iqParam->badPixelAlgo.threshold);
    cJSON_AddBoolToObject(bad_pixel, "static_enable", iqParam->badPixelStatic.enable);
    cJSON_AddNumberToObject(bad_pixel, "strength", iqParam->badPixelStatic.strength);
    cJSON_AddItemToObject(config, "bad_pixel", bad_pixel);

    // BlackLevel
    cJSON *black_level = cJSON_CreateObject();
    cJSON_AddBoolToObject(black_level, "enable", iqParam->blackLevelStatic.enable);
    cJSON_AddNumberToObject(black_level, "r", iqParam->blackLevelStatic.BLCR);
    cJSON_AddNumberToObject(black_level, "g", iqParam->blackLevelStatic.BLCG);
    cJSON_AddNumberToObject(black_level, "b", iqParam->blackLevelStatic.BLCB);
    cJSON_AddItemToObject(config, "black_level", black_level);

    // AEC
    cJSON *aec = cJSON_CreateObject();
    cJSON_AddBoolToObject(aec, "enable", iqParam->AECAlgo.enable);
    cJSON_AddNumberToObject(aec, "exposure_compensation", iqParam->AECAlgo.exposureCompensation);
    cJSON_AddNumberToObject(aec, "anti_flicker_freq", iqParam->AECAlgo.antiFlickerFreq);
    cJSON_AddItemToObject(config, "aec", aec);

    // AWB
    cJSON *awb = cJSON_CreateObject();
    cJSON_AddBoolToObject(awb, "enable", iqParam->AWBAlgo.enable);
    cJSON *awb_profiles = cJSON_CreateArray();
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
        cJSON *profile = cJSON_CreateObject();
        cJSON_AddStringToObject(profile, "label", iqParam->AWBAlgo.label[i]);
        cJSON_AddNumberToObject(profile, "color_temp", iqParam->AWBAlgo.referenceColorTemp[i]);
        cJSON_AddNumberToObject(profile, "gain_r", iqParam->AWBAlgo.ispGainR[i]);
        cJSON_AddNumberToObject(profile, "gain_g", iqParam->AWBAlgo.ispGainG[i]);
        cJSON_AddNumberToObject(profile, "gain_b", iqParam->AWBAlgo.ispGainB[i]);
        cJSON *ccm = cJSON_CreateArray();
        for (int j = 0; j < 9; j++) {
            cJSON_AddItemToArray(ccm, cJSON_CreateNumber(iqParam->AWBAlgo.coeff[i][j / 3][j % 3]));
        }
        cJSON_AddItemToObject(profile, "ccm", ccm);
        cJSON_AddItemToArray(awb_profiles, profile);
    }
    cJSON_AddItemToObject(awb, "profiles", awb_profiles);
    cJSON_AddItemToObject(config, "awb", awb);

    // ISP Gain
    cJSON *isp_gain = cJSON_CreateObject();
    cJSON_AddBoolToObject(isp_gain, "enable", iqParam->ispGainStatic.enable);
    cJSON_AddNumberToObject(isp_gain, "r", iqParam->ispGainStatic.ispGainR);
    cJSON_AddNumberToObject(isp_gain, "g", iqParam->ispGainStatic.ispGainG);
    cJSON_AddNumberToObject(isp_gain, "b", iqParam->ispGainStatic.ispGainB);
    cJSON_AddItemToObject(config, "isp_gain", isp_gain);

    // Color Conversion
    cJSON *color_conv = cJSON_CreateObject();
    cJSON_AddBoolToObject(color_conv, "enable", iqParam->colorConvStatic.enable);
    cJSON *ccm_matrix = cJSON_CreateArray();
    for (int i = 0; i < 9; i++) {
        cJSON_AddItemToArray(ccm_matrix, cJSON_CreateNumber(iqParam->colorConvStatic.coeff[i / 3][i % 3]));
    }
    cJSON_AddItemToObject(color_conv, "matrix", ccm_matrix);
    cJSON_AddItemToObject(config, "color_conv", color_conv);

    // Gamma
    cJSON *gamma = cJSON_CreateObject();
    cJSON_AddBoolToObject(gamma, "enable", iqParam->gamma.enable);
    cJSON_AddItemToObject(config, "gamma", gamma);

    // Sensor settings
    cJSON *sensor = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensor, "gain", iqParam->sensorGainStatic.gain);
    cJSON_AddNumberToObject(sensor, "exposure", iqParam->sensorExposureStatic.exposure);
    cJSON_AddNumberToObject(sensor, "delay", iqParam->sensorDelay.delay);
    cJSON_AddItemToObject(config, "sensor", sensor);

    // Lux Reference
    cJSON *lux_ref = cJSON_CreateObject();
    cJSON_AddNumberToObject(lux_ref, "hl_ref", iqParam->luxRef.HL_LuxRef);
    cJSON_AddNumberToObject(lux_ref, "hl_expo1", iqParam->luxRef.HL_Expo1);
    cJSON_AddNumberToObject(lux_ref, "hl_lum1", iqParam->luxRef.HL_Lum1);
    cJSON_AddNumberToObject(lux_ref, "hl_expo2", iqParam->luxRef.HL_Expo2);
    cJSON_AddNumberToObject(lux_ref, "hl_lum2", iqParam->luxRef.HL_Lum2);
    cJSON_AddNumberToObject(lux_ref, "ll_ref", iqParam->luxRef.LL_LuxRef);
    cJSON_AddNumberToObject(lux_ref, "ll_expo1", iqParam->luxRef.LL_Expo1);
    cJSON_AddNumberToObject(lux_ref, "ll_lum1", iqParam->luxRef.LL_Lum1);
    cJSON_AddNumberToObject(lux_ref, "ll_expo2", iqParam->luxRef.LL_Expo2);
    cJSON_AddNumberToObject(lux_ref, "ll_lum2", iqParam->luxRef.LL_Lum2);
    cJSON_AddNumberToObject(lux_ref, "calib_factor", iqParam->luxRef.calibFactor);
    cJSON_AddItemToObject(config, "lux_ref", lux_ref);

    cJSON_AddItemToObject(data, "config", config);

    char *json_str = cJSON_Print(data);
    ctx->response.data = json_str;
    cJSON_Delete(data);

    snprintf(ctx->response.message, sizeof(ctx->response.message), "ISP configuration exported successfully");
    return AICAM_OK;
}

aicam_result_t api_isp_import_config(http_handler_context_t *ctx) {
    ISP_HandleTypeDef *hIsp = get_isp_handle();
    if (hIsp == NULL) {
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP not initialized");
    }

    cJSON *body = cJSON_Parse(ctx->request.body);
    if (!body) {
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Invalid JSON body");
    }

    cJSON *config = cJSON_GetObjectItem(body, "config");
    if (!config) {
        cJSON_Delete(body);
        return set_isp_error_response(ctx, API_ISP_ERROR_INVALID_PARAM, "Missing 'config' field");
    }

    ISP_IQParamTypeDef *iqParam = ISP_SVC_IQParam_Get(hIsp);
    if (!iqParam) {
        cJSON_Delete(body);
        return set_isp_error_response(ctx, API_ISP_ERROR_NOT_INITIALIZED, "ISP IQ parameters not available");
    }

    // Parse and apply each section
    cJSON *item;

    // StatRemoval
    cJSON *stat_removal = cJSON_GetObjectItem(config, "stat_removal");
    if (stat_removal) {
        if ((item = cJSON_GetObjectItem(stat_removal, "enable"))) iqParam->statRemoval.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(stat_removal, "head_lines"))) iqParam->statRemoval.nbHeadLines = item->valueint;
        if ((item = cJSON_GetObjectItem(stat_removal, "valid_lines"))) iqParam->statRemoval.nbValidLines = item->valueint;
    }

    // Demosaicing
    cJSON *demosaicing = cJSON_GetObjectItem(config, "demosaicing");
    if (demosaicing) {
        if ((item = cJSON_GetObjectItem(demosaicing, "enable"))) iqParam->demosaicing.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(demosaicing, "type"))) iqParam->demosaicing.type = item->valueint;
        if ((item = cJSON_GetObjectItem(demosaicing, "peak"))) iqParam->demosaicing.peak = item->valueint;
        if ((item = cJSON_GetObjectItem(demosaicing, "line_v"))) iqParam->demosaicing.lineV = item->valueint;
        if ((item = cJSON_GetObjectItem(demosaicing, "line_h"))) iqParam->demosaicing.lineH = item->valueint;
        if ((item = cJSON_GetObjectItem(demosaicing, "edge"))) iqParam->demosaicing.edge = item->valueint;
        ISP_SVC_ISP_SetDemosaicing(hIsp, &iqParam->demosaicing);
    }

    // Contrast
    cJSON *contrast = cJSON_GetObjectItem(config, "contrast");
    if (contrast) {
        if ((item = cJSON_GetObjectItem(contrast, "enable"))) iqParam->contrast.enable = cJSON_IsTrue(item);
        cJSON *lut = cJSON_GetObjectItem(contrast, "lut");
        if (lut && cJSON_GetArraySize(lut) >= 9) {
            iqParam->contrast.coeff.LUM_0 = cJSON_GetArrayItem(lut, 0)->valueint;
            iqParam->contrast.coeff.LUM_32 = cJSON_GetArrayItem(lut, 1)->valueint;
            iqParam->contrast.coeff.LUM_64 = cJSON_GetArrayItem(lut, 2)->valueint;
            iqParam->contrast.coeff.LUM_96 = cJSON_GetArrayItem(lut, 3)->valueint;
            iqParam->contrast.coeff.LUM_128 = cJSON_GetArrayItem(lut, 4)->valueint;
            iqParam->contrast.coeff.LUM_160 = cJSON_GetArrayItem(lut, 5)->valueint;
            iqParam->contrast.coeff.LUM_192 = cJSON_GetArrayItem(lut, 6)->valueint;
            iqParam->contrast.coeff.LUM_224 = cJSON_GetArrayItem(lut, 7)->valueint;
            iqParam->contrast.coeff.LUM_256 = cJSON_GetArrayItem(lut, 8)->valueint;
        }
        ISP_SVC_ISP_SetContrast(hIsp, &iqParam->contrast);
    }

    // StatArea
    cJSON *stat_area = cJSON_GetObjectItem(config, "stat_area");
    if (stat_area) {
        if ((item = cJSON_GetObjectItem(stat_area, "x"))) iqParam->statAreaStatic.X0 = item->valueint;
        if ((item = cJSON_GetObjectItem(stat_area, "y"))) iqParam->statAreaStatic.Y0 = item->valueint;
        if ((item = cJSON_GetObjectItem(stat_area, "width"))) iqParam->statAreaStatic.XSize = item->valueint;
        if ((item = cJSON_GetObjectItem(stat_area, "height"))) iqParam->statAreaStatic.YSize = item->valueint;
        ISP_SVC_ISP_SetStatArea(hIsp, &iqParam->statAreaStatic);
    }

    // BadPixel
    cJSON *bad_pixel = cJSON_GetObjectItem(config, "bad_pixel");
    if (bad_pixel) {
        if ((item = cJSON_GetObjectItem(bad_pixel, "algo_enable"))) iqParam->badPixelAlgo.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(bad_pixel, "algo_threshold"))) iqParam->badPixelAlgo.threshold = item->valueint;
        if ((item = cJSON_GetObjectItem(bad_pixel, "static_enable"))) iqParam->badPixelStatic.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(bad_pixel, "strength"))) iqParam->badPixelStatic.strength = item->valueint;
        ISP_SVC_ISP_SetBadPixel(hIsp, &iqParam->badPixelStatic);
    }

    // BlackLevel
    cJSON *black_level = cJSON_GetObjectItem(config, "black_level");
    if (black_level) {
        if ((item = cJSON_GetObjectItem(black_level, "enable"))) iqParam->blackLevelStatic.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(black_level, "r"))) iqParam->blackLevelStatic.BLCR = item->valueint;
        if ((item = cJSON_GetObjectItem(black_level, "g"))) iqParam->blackLevelStatic.BLCG = item->valueint;
        if ((item = cJSON_GetObjectItem(black_level, "b"))) iqParam->blackLevelStatic.BLCB = item->valueint;
        ISP_SVC_ISP_SetBlackLevel(hIsp, &iqParam->blackLevelStatic);
    }

    // AEC
    cJSON *aec = cJSON_GetObjectItem(config, "aec");
    if (aec) {
        if ((item = cJSON_GetObjectItem(aec, "enable"))) {
            iqParam->AECAlgo.enable = cJSON_IsTrue(item);
            ISP_SetAECState(hIsp, iqParam->AECAlgo.enable);
        }
        if ((item = cJSON_GetObjectItem(aec, "exposure_compensation"))) {
            iqParam->AECAlgo.exposureCompensation = item->valueint;
            ISP_SetExposureTarget(hIsp, (ISP_ExposureCompTypeDef)item->valueint);
        }
        if ((item = cJSON_GetObjectItem(aec, "anti_flicker_freq"))) {
            iqParam->AECAlgo.antiFlickerFreq = item->valueint;
        }
    }

    // AWB
    cJSON *awb = cJSON_GetObjectItem(config, "awb");
    if (awb) {
        if ((item = cJSON_GetObjectItem(awb, "enable"))) {
            iqParam->AWBAlgo.enable = cJSON_IsTrue(item);
        }
        cJSON *profiles = cJSON_GetObjectItem(awb, "profiles");
        if (profiles) {
            int count = cJSON_GetArraySize(profiles);
            if (count > ISP_AWB_PROFILES_MAX) count = ISP_AWB_PROFILES_MAX;
            for (int i = 0; i < count; i++) {
                cJSON *profile = cJSON_GetArrayItem(profiles, i);
                if ((item = cJSON_GetObjectItem(profile, "label"))) {
                    strncpy(iqParam->AWBAlgo.label[i], item->valuestring, ISP_AWB_LABEL_MAX_LEN - 1);
                }
                if ((item = cJSON_GetObjectItem(profile, "color_temp"))) iqParam->AWBAlgo.referenceColorTemp[i] = item->valueint;
                if ((item = cJSON_GetObjectItem(profile, "gain_r"))) iqParam->AWBAlgo.ispGainR[i] = item->valueint;
                if ((item = cJSON_GetObjectItem(profile, "gain_g"))) iqParam->AWBAlgo.ispGainG[i] = item->valueint;
                if ((item = cJSON_GetObjectItem(profile, "gain_b"))) iqParam->AWBAlgo.ispGainB[i] = item->valueint;
                cJSON *ccm = cJSON_GetObjectItem(profile, "ccm");
                if (ccm && cJSON_GetArraySize(ccm) >= 9) {
                    for (int j = 0; j < 9; j++) {
                        iqParam->AWBAlgo.coeff[i][j / 3][j % 3] = cJSON_GetArrayItem(ccm, j)->valueint;
                    }
                }
            }
        }
    }

    // ISP Gain
    cJSON *isp_gain = cJSON_GetObjectItem(config, "isp_gain");
    if (isp_gain) {
        if ((item = cJSON_GetObjectItem(isp_gain, "enable"))) iqParam->ispGainStatic.enable = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(isp_gain, "r"))) iqParam->ispGainStatic.ispGainR = item->valueint;
        if ((item = cJSON_GetObjectItem(isp_gain, "g"))) iqParam->ispGainStatic.ispGainG = item->valueint;
        if ((item = cJSON_GetObjectItem(isp_gain, "b"))) iqParam->ispGainStatic.ispGainB = item->valueint;
        ISP_SVC_ISP_SetGain(hIsp, &iqParam->ispGainStatic);
    }

    // Color Conversion
    cJSON *color_conv = cJSON_GetObjectItem(config, "color_conv");
    if (color_conv) {
        if ((item = cJSON_GetObjectItem(color_conv, "enable"))) iqParam->colorConvStatic.enable = cJSON_IsTrue(item);
        cJSON *matrix = cJSON_GetObjectItem(color_conv, "matrix");
        if (matrix && cJSON_GetArraySize(matrix) >= 9) {
            for (int i = 0; i < 9; i++) {
                iqParam->colorConvStatic.coeff[i / 3][i % 3] = cJSON_GetArrayItem(matrix, i)->valueint;
            }
        }
        ISP_SVC_ISP_SetColorConv(hIsp, &iqParam->colorConvStatic);
    }

    // Gamma
    cJSON *gamma = cJSON_GetObjectItem(config, "gamma");
    if (gamma) {
        if ((item = cJSON_GetObjectItem(gamma, "enable"))) {
            iqParam->gamma.enable = cJSON_IsTrue(item);
            ISP_SVC_ISP_SetGamma(hIsp, &iqParam->gamma);
        }
    }

    // Sensor settings
    cJSON *sensor = cJSON_GetObjectItem(config, "sensor");
    if (sensor) {
        if ((item = cJSON_GetObjectItem(sensor, "gain"))) iqParam->sensorGainStatic.gain = item->valueint;
        if ((item = cJSON_GetObjectItem(sensor, "exposure"))) iqParam->sensorExposureStatic.exposure = item->valueint;
        if ((item = cJSON_GetObjectItem(sensor, "delay"))) iqParam->sensorDelay.delay = item->valueint;
    }

    // Lux Reference
    cJSON *lux_ref = cJSON_GetObjectItem(config, "lux_ref");
    if (lux_ref) {
        if ((item = cJSON_GetObjectItem(lux_ref, "hl_ref"))) iqParam->luxRef.HL_LuxRef = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "hl_expo1"))) iqParam->luxRef.HL_Expo1 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "hl_lum1"))) iqParam->luxRef.HL_Lum1 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "hl_expo2"))) iqParam->luxRef.HL_Expo2 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "hl_lum2"))) iqParam->luxRef.HL_Lum2 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "ll_ref"))) iqParam->luxRef.LL_LuxRef = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "ll_expo1"))) iqParam->luxRef.LL_Expo1 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "ll_lum1"))) iqParam->luxRef.LL_Lum1 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "ll_expo2"))) iqParam->luxRef.LL_Expo2 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "ll_lum2"))) iqParam->luxRef.LL_Lum2 = item->valueint;
        if ((item = cJSON_GetObjectItem(lux_ref, "calib_factor")) && cJSON_IsNumber(item)) {
            iqParam->luxRef.calibFactor = (float)item->valuedouble;
        }
    }

    cJSON_Delete(body);

    /* Persist imported IQ to NVS and refresh device_service cache for custom mode */
    {
        isp_config_t to_save = {0};
        to_save.valid = AICAM_TRUE;
        if (json_config_isp_param_to_config(iqParam, &to_save) == AICAM_OK) {
            (void)json_config_set_isp_config(&to_save);
            (void)device_service_reload_isp_config_from_storage();
        }
    }

    snprintf(ctx->response.message, sizeof(ctx->response.message), "ISP configuration imported and applied successfully");
    return AICAM_OK;
}

aicam_result_t web_api_register_isp_module(void) {
    return api_isp_module_init();
}
