/**
 * @file api_device_module.c
 * @brief Device API Module Implementation
 * @details Device management API implementation based on device_service
 */

#include "api_device_module.h"
#include "web_api.h"
#include "device_service.h"
#include "debug.h"
#include "cJSON.h"
#include "drtc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "buffer_mgr.h"
#include "stm32n6xx_hal_cortex.h"
#include "generic_file.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "wake_scheduler.h"
#include "ai_service.h"
#include "ota_service.h"
#include "ota_header.h"
#include "storage.h"
#include "upload_coordinator.h"
#include "version.h"
#include "fsbl_app_common.h"
#include "wifi.h"
/* ==================== Helper Functions ==================== */

static uint32_t restart_delay_seconds = 3;
static uint8_t s_restart_task_stack[1024];
static uint32_t s_restart_delay_storage;

/**
 * @brief Restart task function
 */
static void restart_task_function(void* arg) {
    uint32_t delay = *(uint32_t*)arg;

    osDelay(delay * 1000); // Convert seconds to milliseconds
    
    printf("Executing system restart...\r\n");
    
    // Perform system restart using HAL
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
}

/**
 * @brief Factory reset task function
 */
static void factory_reset_task_function(void* arg) {
    uint32_t delay = *(uint32_t*)arg;

    printf("Factory reset task started, delay: %lu seconds\r\n", delay);

    osDelay(delay * 1000); // Convert seconds to milliseconds
    
    printf("Executing factory reset...\r\n");
    
    // Trigger factory reset (includes LED blink, config reset, AI model clear, and system restart)
    aicam_result_t result = device_service_reset_to_factory_defaults();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reset to factory defaults: %d", result);
        // Fallback: force restart anyway
#if ENABLE_U0_MODULE
        u0_module_clear_wakeup_flag();
        u0_module_reset_chip_n6();
#endif
        HAL_NVIC_SystemReset();
    }
}

/**
 * @brief Convert light mode to string
 */
static const char* get_light_mode_string(light_mode_t mode) {
    switch (mode) {
        case LIGHT_MODE_OFF:
            return "off";
        case LIGHT_MODE_ON:
            return "on";
        case LIGHT_MODE_AUTO:
            return "auto";
        case LIGHT_MODE_CUSTOM:
            return "custom";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse light mode from string
 */
static light_mode_t parse_light_mode(const char* mode_str) {
    if (!mode_str) return LIGHT_MODE_AUTO;
    
    if (strcmp(mode_str, "off") == 0) {
        return LIGHT_MODE_OFF;
    } else if (strcmp(mode_str, "on") == 0) {
        return LIGHT_MODE_ON;
    } else if (strcmp(mode_str, "auto") == 0) {
        return LIGHT_MODE_AUTO;
    } else if (strcmp(mode_str, "custom") == 0) {
        return LIGHT_MODE_CUSTOM;
    }
    
    return LIGHT_MODE_AUTO; // Default
}

/**
 * @brief Check if device service is running
 */
static aicam_bool_t is_device_service_running(void) {
    service_state_t state = device_service_get_state();
    return (state == SERVICE_STATE_RUNNING);
}

/* ==================== API Handler Functions ==================== */

/**
 * @brief GET /api/v1/device/info - Get device information
 */
aicam_result_t device_info_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Get device information
    device_info_config_t device_info;
    aicam_result_t result = device_service_get_info(&device_info);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get device information");
    }
    
    // Create response JSON
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Add device basic information
    cJSON_AddStringToObject(response_json, "device_name", device_info.device_name);
    cJSON_AddStringToObject(response_json, "mac_address", device_info.mac_address);
    cJSON_AddStringToObject(response_json, "serial_number", device_info.serial_number);
    cJSON_AddStringToObject(response_json, "hardware_version", device_info.hardware_version);
    cJSON_AddStringToObject(response_json, "software_version", device_info.software_version);
    cJSON_AddStringToObject(response_json, "camera_module", device_info.camera_module);
    cJSON_AddStringToObject(response_json, "extension_modules", device_info.extension_modules);
    cJSON_AddStringToObject(response_json, "power_supply_type", device_info.power_supply_type);
    cJSON_AddNumberToObject(response_json, "battery_percent", device_info.battery_percent);
    cJSON_AddStringToObject(response_json, "communication_type", device_info.communication_type);
    
    // Add storage information
    cJSON_AddStringToObject(response_json, "storage_card_info", device_info.storage_card_info);
    cJSON_AddNumberToObject(response_json, "storage_usage_percent", device_info.storage_usage_percent);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Device information retrieved successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief GET /api/v1/device/storage - Get storage information
 */
aicam_result_t device_storage_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Get storage information
    storage_info_t storage_info;
    aicam_result_t result = device_service_storage_get_info(&storage_info);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get storage information");
    }
    
    // Create response JSON
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Add storage connection status
    cJSON_AddBoolToObject(response_json, "sd_card_connected", storage_info.sd_card_connected);

    // Add capacity information (in MB and GB)
    cJSON_AddNumberToObject(response_json, "total_capacity_mb", storage_info.total_capacity_mb);
    cJSON_AddNumberToObject(response_json, "available_capacity_mb", storage_info.available_capacity_mb);
    cJSON_AddNumberToObject(response_json, "used_capacity_mb", storage_info.used_capacity_mb);
    cJSON_AddNumberToObject(response_json, "usage_percent", storage_info.usage_percent);

    // Add capacity information in GB for user-friendly display
    cJSON_AddNumberToObject(response_json, "total_capacity_gb", (double)storage_info.total_capacity_mb / 1024.0);
    cJSON_AddNumberToObject(response_json, "available_capacity_gb", (double)storage_info.available_capacity_mb / 1024.0);
    cJSON_AddNumberToObject(response_json, "used_capacity_gb", (double)storage_info.used_capacity_mb / 1024.0);

    // Add cyclic overwrite policy
    cJSON_AddBoolToObject(response_json, "cyclic_overwrite_enabled", storage_info.cyclic_overwrite_enabled);
    cJSON_AddNumberToObject(response_json, "overwrite_threshold_percent", storage_info.overwrite_threshold_percent);

    // ── Internal Flash (LittleFS) storage ──
    cJSON_AddBoolToObject(response_json, "flash_fs_mounted", storage_info.flash_fs_mounted);
    cJSON_AddBoolToObject(response_json, "flash_fs_error", storage_info.flash_fs_error);
    cJSON_AddStringToObject(response_json, "flash_error", storage_info.flash_error);
    cJSON_AddNumberToObject(response_json, "flash_total_capacity_mb", storage_info.flash_total_capacity_mb);
    cJSON_AddNumberToObject(response_json, "flash_available_capacity_mb", storage_info.flash_available_capacity_mb);
    cJSON_AddNumberToObject(response_json, "flash_used_capacity_mb", storage_info.flash_used_capacity_mb);
    cJSON_AddNumberToObject(response_json, "flash_usage_percent", storage_info.flash_usage_percent);
    cJSON_AddNumberToObject(response_json, "flash_total_capacity_gb", (double)storage_info.flash_total_capacity_mb / 1024.0);
    cJSON_AddNumberToObject(response_json, "flash_available_capacity_gb", (double)storage_info.flash_available_capacity_mb / 1024.0);
    cJSON_AddNumberToObject(response_json, "flash_used_capacity_gb", (double)storage_info.flash_used_capacity_mb / 1024.0);
    cJSON_AddStringToObject(response_json, "flash_fs_type", storage_info.flash_fs_type);

    // Add status summary
    const char* status_summary;
    const char* primary_storage;
    if (storage_info.sd_card_connected) {
        primary_storage = "sd";
        if (storage_info.usage_percent > storage_info.overwrite_threshold_percent) {
            status_summary = storage_info.cyclic_overwrite_enabled ? "full_auto_overwrite" : "full_manual_cleanup";
        } else if (storage_info.usage_percent > 80.0f) {
            status_summary = "nearly_full";
        } else {
            status_summary = "normal";
        }
    } else if (storage_info.flash_fs_mounted) {
        primary_storage = "flash";
        if (storage_info.flash_usage_percent > 95.0f) {
            status_summary = "full";
        } else if (storage_info.flash_usage_percent > 80.0f) {
            status_summary = "warning";
        } else {
            status_summary = "normal";
        }
    } else {
        primary_storage = "none";
        status_summary = "no_card";
    }
    cJSON_AddStringToObject(response_json, "status", status_summary);
    cJSON_AddStringToObject(response_json, "primary_storage", primary_storage);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Storage information retrieved successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/device/storage/config - Configure storage settings
 */
aicam_result_t device_storage_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract cyclic overwrite enabled flag
    cJSON* enabled_item = cJSON_GetObjectItem(request_json, "cyclic_overwrite_enabled");
    aicam_bool_t cyclic_enabled = AICAM_TRUE; // Default enabled
    if (enabled_item && cJSON_IsBool(enabled_item)) {
        cyclic_enabled = cJSON_IsTrue(enabled_item) ? AICAM_TRUE : AICAM_FALSE;
    }
    
    // Extract overwrite threshold
    cJSON* threshold_item = cJSON_GetObjectItem(request_json, "overwrite_threshold_percent");
    uint32_t threshold_percent = 80; // Default 80%
    if (threshold_item && cJSON_IsNumber(threshold_item)) {
        double threshold_value = cJSON_GetNumberValue(threshold_item);
        if (threshold_value >= 50.0 && threshold_value <= 95.0) {
            threshold_percent = (uint32_t)threshold_value;
        } else {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Threshold must be between 50% and 95%");
        }
    }
    
    // Apply storage configuration
    aicam_result_t result = device_service_storage_set_cyclic_overwrite(cyclic_enabled, threshold_percent);
    cJSON_Delete(request_json);
    
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to configure storage settings");
    }
    
    // Create success response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "Storage configuration updated successfully");
    cJSON_AddBoolToObject(response_json, "cyclic_overwrite_enabled", cyclic_enabled);
    cJSON_AddNumberToObject(response_json, "overwrite_threshold_percent", threshold_percent);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Storage configuration updated successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief GET/POST /api/v1/device/image/config - Get/Set image configuration
 */
aicam_result_t device_image_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    if (web_api_verify_method(ctx, "GET")) {
        // GET: Return current image configuration
        camera_config_t camera_config;
        aicam_result_t result = device_service_camera_get_config(&camera_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get image configuration");
        }
        
        // Create response JSON
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddNumberToObject(response_json, "brightness", camera_config.image_config.brightness);
        cJSON_AddNumberToObject(response_json, "contrast", camera_config.image_config.contrast);
        cJSON_AddBoolToObject(response_json, "horizontal_flip", camera_config.image_config.horizontal_flip);
        cJSON_AddBoolToObject(response_json, "vertical_flip", camera_config.image_config.vertical_flip);
        cJSON_AddNumberToObject(response_json, "aec", camera_config.image_config.aec);
        cJSON_AddNumberToObject(response_json, "isp_mode", camera_config.image_config.isp_mode);
        cJSON_AddBoolToObject(response_json, "grayscale", camera_config.image_config.grayscale);
        cJSON_AddNumberToObject(response_json, "fast_capture_skip_frames", camera_config.image_config.fast_capture_skip_frames);
        cJSON_AddNumberToObject(response_json, "fast_capture_resolution", camera_config.image_config.fast_capture_resolution);
        cJSON_AddNumberToObject(response_json, "fast_capture_jpeg_quality", camera_config.image_config.fast_capture_jpeg_quality);
        cJSON_AddBoolToObject(response_json, "capture_disable_comm", camera_config.image_config.capture_disable_comm);
        cJSON_AddBoolToObject(response_json, "capture_storage_ai", camera_config.image_config.capture_storage_ai);
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Image configuration retrieved successfully");
        
        // Cleanup
        cJSON_Delete(response_json);
        //hal_mem_free(json_string);
        
        return api_result;
        
    } else if (web_api_verify_method(ctx, "POST")) {
        // POST: Update image configuration
        
        // Parse JSON request body
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Get current configuration
        image_config_t image_config;
        aicam_result_t result = device_service_image_get_config(&image_config);
        if (result != AICAM_OK) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current image configuration");
        }

        // Keep a copy to detect whether pipeline-affecting fields changed
        image_config_t old_image_config = image_config;
        
        // Update brightness if provided
        cJSON* brightness_item = cJSON_GetObjectItem(request_json, "brightness");
        if (brightness_item && cJSON_IsNumber(brightness_item)) {
            double brightness_value = cJSON_GetNumberValue(brightness_item);
            if (brightness_value >= 0.0 && brightness_value <= 100.0) {
                image_config.brightness = (uint32_t)brightness_value;
            } else {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Brightness must be between 0 and 100");
            }
        }
        
        // Update contrast if provided
        cJSON* contrast_item = cJSON_GetObjectItem(request_json, "contrast");
        if (contrast_item && cJSON_IsNumber(contrast_item)) {
            double contrast_value = cJSON_GetNumberValue(contrast_item);
            if (contrast_value >= 0.0 && contrast_value <= 100.0) {
                image_config.contrast = (uint32_t)contrast_value;
            } else {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Contrast must be between 0 and 100");
            }
        }
        
        // Update horizontal flip if provided
        cJSON* h_flip_item = cJSON_GetObjectItem(request_json, "horizontal_flip");
        if (h_flip_item && cJSON_IsBool(h_flip_item)) {
            image_config.horizontal_flip = cJSON_IsTrue(h_flip_item) ? AICAM_TRUE : AICAM_FALSE;
        }
        
        // Update vertical flip if provided
        cJSON* v_flip_item = cJSON_GetObjectItem(request_json, "vertical_flip");
        if (v_flip_item && cJSON_IsBool(v_flip_item)) {
            image_config.vertical_flip = cJSON_IsTrue(v_flip_item) ? AICAM_TRUE : AICAM_FALSE;
        }
        

        // Update AEC if provided
        cJSON* aec_item = cJSON_GetObjectItem(request_json, "aec");
        if (aec_item && cJSON_IsNumber(aec_item)) {
            double aec_value = cJSON_GetNumberValue(aec_item);
            if (aec_value >= 0.0 && aec_value <= 100.0) {
                image_config.aec = (uint32_t)aec_value;
            }
        }

        cJSON* isp_mode_item = cJSON_GetObjectItem(request_json, "isp_mode");
        if (isp_mode_item && cJSON_IsNumber(isp_mode_item)) {
            double m = cJSON_GetNumberValue(isp_mode_item);
            if (m == (double)IMAGE_ISP_MODE_INDOOR || m == (double)IMAGE_ISP_MODE_OUTDOOR || m == (double)IMAGE_ISP_MODE_CUSTOM) {
                image_config.isp_mode = (uint32_t)m;
            } else {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "isp_mode must be 0 (outdoor), 1 (indoor), or 255 (custom)");
            }
        }

        cJSON* grayscale_item = cJSON_GetObjectItem(request_json, "grayscale");
        if (grayscale_item && cJSON_IsBool(grayscale_item)) {
            image_config.grayscale = cJSON_IsTrue(grayscale_item) ? AICAM_TRUE : AICAM_FALSE;
        }

        // Update fast capture skip frames if provided (0-300 to match CAM_CMD_SET_STARTUP_SKIP_FRAMES)
        cJSON* fast_skip_item = cJSON_GetObjectItem(request_json, "fast_capture_skip_frames");
        if (fast_skip_item && cJSON_IsNumber(fast_skip_item)) {
            double skip_value = cJSON_GetNumberValue(fast_skip_item);
            if (skip_value >= 0.0 && skip_value <= 300.0) {
                image_config.fast_capture_skip_frames = (uint32_t)skip_value;
            } else {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "fast_capture_skip_frames must be between 0 and 300");
            }
        }

        // Update fast capture resolution if provided (0:1280x720, 1:1920x1080, 2:2688x1520)
        cJSON* fast_res_item = cJSON_GetObjectItem(request_json, "fast_capture_resolution");
        if (fast_res_item && cJSON_IsNumber(fast_res_item)) {
            double res_value = cJSON_GetNumberValue(fast_res_item);
            if (res_value >= 0.0 && res_value <= 2.0) {
                image_config.fast_capture_resolution = (uint32_t)res_value;
            }
        }

        // Update fast capture JPEG quality if provided
        cJSON* fast_jq_item = cJSON_GetObjectItem(request_json, "fast_capture_jpeg_quality");
        if (fast_jq_item && cJSON_IsNumber(fast_jq_item)) {
            double jq_value = cJSON_GetNumberValue(fast_jq_item);
            if (jq_value >= 1 && jq_value <= 100.0) {
                image_config.fast_capture_jpeg_quality = (uint32_t)jq_value;
            }
        }

        cJSON* cap_dis_comm_item = cJSON_GetObjectItem(request_json, "capture_disable_comm");
        if (cap_dis_comm_item && cJSON_IsBool(cap_dis_comm_item)) {
            image_config.capture_disable_comm = cJSON_IsTrue(cap_dis_comm_item) ? AICAM_TRUE : AICAM_FALSE;
        }

        cJSON* cap_stor_ai_item = cJSON_GetObjectItem(request_json, "capture_storage_ai");
        if (cap_stor_ai_item && cJSON_IsBool(cap_stor_ai_item)) {
            image_config.capture_storage_ai = cJSON_IsTrue(cap_stor_ai_item) ? AICAM_TRUE : AICAM_FALSE;
        }

        cJSON_Delete(request_json);

        // Determine whether changes require restarting AI pipeline
        aicam_bool_t need_restart_pipeline = AICAM_FALSE;
        if (old_image_config.brightness != image_config.brightness ||
            old_image_config.contrast != image_config.contrast ||
            old_image_config.horizontal_flip != image_config.horizontal_flip ||
            old_image_config.vertical_flip != image_config.vertical_flip ||
            old_image_config.aec != image_config.aec) {
            need_restart_pipeline = AICAM_TRUE;
        }

        if (need_restart_pipeline) {
            // Stop pipeline before applying camera-related changes
            ai_pipeline_stop();
        }

        // Apply the updated configuration (always updates config + persistence)
        result = device_service_image_set_config(&image_config);

        if (need_restart_pipeline) {
            // Restart pipeline only when necessary
            ai_pipeline_start();
        }
        
        // Create success response
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddStringToObject(response_json, "message", "Image configuration updated successfully");
        cJSON_AddNumberToObject(response_json, "brightness", image_config.brightness);
        cJSON_AddNumberToObject(response_json, "contrast", image_config.contrast);
        cJSON_AddBoolToObject(response_json, "horizontal_flip", image_config.horizontal_flip);
        cJSON_AddBoolToObject(response_json, "vertical_flip", image_config.vertical_flip);
        cJSON_AddNumberToObject(response_json, "aec", image_config.aec);
        cJSON_AddNumberToObject(response_json, "isp_mode", image_config.isp_mode);
        cJSON_AddBoolToObject(response_json, "grayscale", image_config.grayscale);
        cJSON_AddNumberToObject(response_json, "fast_capture_skip_frames", image_config.fast_capture_skip_frames);
        cJSON_AddNumberToObject(response_json, "fast_capture_resolution", image_config.fast_capture_resolution);
        cJSON_AddNumberToObject(response_json, "fast_capture_jpeg_quality", image_config.fast_capture_jpeg_quality);
        cJSON_AddBoolToObject(response_json, "capture_disable_comm", image_config.capture_disable_comm);
        cJSON_AddBoolToObject(response_json, "capture_storage_ai", image_config.capture_storage_ai);
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Image configuration updated successfully");
        
        // Cleanup
        cJSON_Delete(response_json);
        //hal_mem_free(json_string);
        
        return api_result;
        
    } else {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET and POST methods are allowed");
    }
}

/**
 * @brief GET/POST /api/v1/device/sys-clk/config - Read/write FSBL persisted CPU clock profile (takes effect after reboot)
 */
aicam_result_t device_sys_clk_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;

    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }

    if (web_api_verify_method(ctx, "GET")) {
        sys_clk_config_t cfg = {0};
        int ret = fsbl_app_read_sys_clk_config(&cfg);

        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }

        cJSON_AddBoolToObject(response_json, "valid", ret == 0);
        if (ret == 0) {
            cJSON_AddNumberToObject(response_json, "sys_clk_profile", (double)cfg.sys_clk_profile);
        } else {
            cJSON_AddNumberToObject(response_json, "sys_clk_profile", 0.0);
        }

        char* json_string = cJSON_Print(response_json);
        cJSON_Delete(response_json);
        if (!json_string) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }

        return api_response_success(ctx, json_string, "Sys clock configuration retrieved successfully");
    }

    if (web_api_verify_method(ctx, "POST")) {
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }

        cJSON* prof_item = cJSON_GetObjectItem(request_json, "sys_clk_profile");
        if (!prof_item || !cJSON_IsNumber(prof_item)) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "sys_clk_profile is required (number)");
        }

        uint32_t profile = (uint32_t)cJSON_GetNumberValue(prof_item);
        if (profile != FSBL_APP_SYSCLK_PROFILE_HSE_200MHZ &&
            profile != FSBL_APP_SYSCLK_PROFILE_HSE_400MHZ &&
            profile != FSBL_APP_SYSCLK_PROFILE_HSI_800MHZ &&
            profile != FSBL_APP_SYSCLK_PROFILE_HSE_800MHZ) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "sys_clk_profile must be 1 (HSE 200), 2 (HSE 400), 3 (HSI 800), or 4 (HSE 800)");
        }

        sys_clk_config_t cfg = {0};
        cfg.sys_clk_profile = profile;
        int wret = fsbl_app_write_sys_clk_config(&cfg);
        cJSON_Delete(request_json);

        if (wret != 0) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to write sys clock configuration");
        }

        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }

        cJSON_AddStringToObject(response_json, "message",
                                "Sys clock profile saved; reboot the device for FSBL to apply the new frequency");
        cJSON_AddNumberToObject(response_json, "sys_clk_profile", (double)profile);

        char* json_string = cJSON_Print(response_json);
        cJSON_Delete(response_json);
        if (!json_string) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }

        return api_response_success(ctx, json_string, "Sys clock configuration updated successfully");
    }

    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET and POST methods are allowed");
}

/**
 * @brief GET/POST /api/v1/device/light/config - Get/Set light configuration
 */
aicam_result_t device_light_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    if (web_api_verify_method(ctx, "GET")) {
        // GET: Return current light configuration
        light_config_t light_config;
        aicam_result_t result = device_service_light_get_config(&light_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get light configuration");
        }
        
        // Create response JSON
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddBoolToObject(response_json, "connected", light_config.connected);
        cJSON_AddStringToObject(response_json, "mode", get_light_mode_string(light_config.mode));
        cJSON_AddNumberToObject(response_json, "brightness_level", light_config.brightness_level);
        //cJSON_AddBoolToObject(response_json, "auto_trigger_enabled", light_config.auto_trigger_enabled);
        //cJSON_AddNumberToObject(response_json, "light_threshold", light_config.light_threshold);
        
        // Add custom schedule information
        cJSON* schedule_json = cJSON_CreateObject();
        if (schedule_json) {
            cJSON_AddNumberToObject(schedule_json, "start_hour", light_config.start_hour);
            cJSON_AddNumberToObject(schedule_json, "start_minute", light_config.start_minute);
            cJSON_AddNumberToObject(schedule_json, "end_hour", light_config.end_hour);
            cJSON_AddNumberToObject(schedule_json, "end_minute", light_config.end_minute);
            cJSON_AddItemToObject(response_json, "custom_schedule", schedule_json);
        }
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Light configuration retrieved successfully");
        
        // Cleanup
        cJSON_Delete(response_json);
        //hal_mem_free(json_string);
        
        return api_result;
        
    } else if (web_api_verify_method(ctx, "POST")) {
        // POST: Update light configuration
        
        // Parse JSON request body
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Get current configuration
        light_config_t light_config;
        aicam_result_t result = device_service_light_get_config(&light_config);
        if (result != AICAM_OK) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current light configuration");
        }
        
        // Update mode if provided
        cJSON* mode_item = cJSON_GetObjectItem(request_json, "mode");
        if (mode_item && cJSON_IsString(mode_item)) {
            light_config.mode = parse_light_mode(cJSON_GetStringValue(mode_item));
        }
        
        // Update brightness level if provided
        cJSON* brightness_item = cJSON_GetObjectItem(request_json, "brightness_level");
        if (brightness_item && cJSON_IsNumber(brightness_item)) {
            double brightness_value = cJSON_GetNumberValue(brightness_item);
            if (brightness_value >= 0.0 && brightness_value <= 100.0) {
                light_config.brightness_level = (uint32_t)brightness_value;
            } else {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Brightness level must be between 0 and 100");
            }
        }
        
        // Update auto trigger if provided
        // cJSON* auto_trigger_item = cJSON_GetObjectItem(request_json, "auto_trigger_enabled");
        // if (auto_trigger_item && cJSON_IsBool(auto_trigger_item)) {
        //     light_config.auto_trigger_enabled = cJSON_IsTrue(auto_trigger_item) ? AICAM_TRUE : AICAM_FALSE;
        // }
        
        // Update light threshold if provided
        // cJSON* threshold_item = cJSON_GetObjectItem(request_json, "light_threshold");
        // if (threshold_item && cJSON_IsNumber(threshold_item)) {
        //     double threshold_value = cJSON_GetNumberValue(threshold_item);
        //     if (threshold_value >= 0.0 && threshold_value <= 1000.0) {
        //         light_config.light_threshold = (uint32_t)threshold_value;
        //     } else {
        //         cJSON_Delete(request_json);
        //         return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Light threshold must be between 0 and 1000");
        //     }
        // }
        
        // Update custom schedule if provided
        cJSON* schedule_item = cJSON_GetObjectItem(request_json, "custom_schedule");
        if (schedule_item && cJSON_IsObject(schedule_item)) {
            cJSON* start_hour_item = cJSON_GetObjectItem(schedule_item, "start_hour");
            if (start_hour_item && cJSON_IsNumber(start_hour_item)) {
                double hour_value = cJSON_GetNumberValue(start_hour_item);
                if (hour_value >= 0.0 && hour_value <= 23.0) {
                    light_config.start_hour = (uint32_t)hour_value;
                }
            }
            
            cJSON* start_minute_item = cJSON_GetObjectItem(schedule_item, "start_minute");
            if (start_minute_item && cJSON_IsNumber(start_minute_item)) {
                double minute_value = cJSON_GetNumberValue(start_minute_item);
                if (minute_value >= 0.0 && minute_value <= 59.0) {
                    light_config.start_minute = (uint32_t)minute_value;
                }
            }
            
            cJSON* end_hour_item = cJSON_GetObjectItem(schedule_item, "end_hour");
            if (end_hour_item && cJSON_IsNumber(end_hour_item)) {
                double hour_value = cJSON_GetNumberValue(end_hour_item);
                if (hour_value >= 0.0 && hour_value <= 23.0) {
                    light_config.end_hour = (uint32_t)hour_value;
                }
            }
            
            cJSON* end_minute_item = cJSON_GetObjectItem(schedule_item, "end_minute");
            if (end_minute_item && cJSON_IsNumber(end_minute_item)) {
                double minute_value = cJSON_GetNumberValue(end_minute_item);
                if (minute_value >= 0.0 && minute_value <= 59.0) {
                    light_config.end_minute = (uint32_t)minute_value;
                }
            }
        }
        
        cJSON_Delete(request_json);
        
        // Apply the updated configuration
        result = device_service_light_set_config(&light_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set light configuration");
        }
        
        // Create success response with updated configuration
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddStringToObject(response_json, "message", "Light configuration updated successfully");
        cJSON_AddStringToObject(response_json, "mode", get_light_mode_string(light_config.mode));
        cJSON_AddNumberToObject(response_json, "brightness_level", light_config.brightness_level);
        //cJSON_AddBoolToObject(response_json, "auto_trigger_enabled", light_config.auto_trigger_enabled);
        //cJSON_AddNumberToObject(response_json, "light_threshold", light_config.light_threshold);
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Light configuration updated successfully");
        
        // Cleanup
        cJSON_Delete(response_json);
        //hal_mem_free(json_string);
        
        return api_result;
        
    } else {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET and POST methods are allowed");
    }
}

/**
 * @brief POST /api/v1/device/light/control - Manual light control
 */
aicam_result_t device_light_control_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Check if light is connected
    if (!device_service_light_is_connected()) {
        return api_response_error(ctx, API_ERROR_NOT_FOUND, "Light device is not connected");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract enable flag
    cJSON* enable_item = cJSON_GetObjectItem(request_json, "enable");
    if (!enable_item || !cJSON_IsBool(enable_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'enable' field");
    }
    
    aicam_bool_t enable = cJSON_IsTrue(enable_item) ? AICAM_TRUE : AICAM_FALSE;
    cJSON_Delete(request_json);
    
    // Control the light
    aicam_result_t result = device_service_light_control(enable);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to control light");
    }
    
    // Create success response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", enable ? "Light turned on" : "Light turned off");
    cJSON_AddBoolToObject(response_json, "enabled", enable);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Light control executed successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief GET/POST /api/v1/device/camera/config - Get/Set camera configuration
 */
aicam_result_t device_camera_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    if (web_api_verify_method(ctx, "GET")) {
        // GET: Return current camera configuration
        camera_config_t camera_config;
        aicam_result_t result = device_service_camera_get_config(&camera_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get camera configuration");
        }
        
        // Create response JSON
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddBoolToObject(response_json, "enabled", camera_config.enabled);
        cJSON_AddNumberToObject(response_json, "width", camera_config.width);
        cJSON_AddNumberToObject(response_json, "height", camera_config.height);
        cJSON_AddNumberToObject(response_json, "fps", camera_config.fps);
        
        // Add image configuration
        cJSON* image_config_json = cJSON_CreateObject();
        if (image_config_json) {
            cJSON_AddNumberToObject(image_config_json, "brightness", camera_config.image_config.brightness);
            cJSON_AddNumberToObject(image_config_json, "contrast", camera_config.image_config.contrast);
            cJSON_AddBoolToObject(image_config_json, "horizontal_flip", camera_config.image_config.horizontal_flip);
            cJSON_AddBoolToObject(image_config_json, "vertical_flip", camera_config.image_config.vertical_flip);
            cJSON_AddNumberToObject(image_config_json, "aec", camera_config.image_config.aec);
            cJSON_AddNumberToObject(image_config_json, "isp_mode", camera_config.image_config.isp_mode);
            cJSON_AddBoolToObject(image_config_json, "grayscale", camera_config.image_config.grayscale);
            cJSON_AddNumberToObject(image_config_json, "fast_capture_skip_frames", camera_config.image_config.fast_capture_skip_frames);
            cJSON_AddNumberToObject(image_config_json, "fast_capture_resolution", camera_config.image_config.fast_capture_resolution);
            cJSON_AddNumberToObject(image_config_json, "fast_capture_jpeg_quality", camera_config.image_config.fast_capture_jpeg_quality);
            cJSON_AddBoolToObject(image_config_json, "capture_disable_comm", camera_config.image_config.capture_disable_comm);
            cJSON_AddBoolToObject(image_config_json, "capture_storage_ai", camera_config.image_config.capture_storage_ai);
            cJSON_AddItemToObject(response_json, "image_config", image_config_json);
        }
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Camera configuration retrieved successfully");
        
        // Cleanup
        cJSON_Delete(response_json);
        //hal_mem_free(json_string);
        
        return api_result;
        
    } else if (web_api_verify_method(ctx, "POST")) {
        // POST: Update camera configuration
        
        // Parse JSON request body
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Get current configuration
        camera_config_t camera_config;
        aicam_result_t result = device_service_camera_get_config(&camera_config);
        if (result != AICAM_OK) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current camera configuration");
        }
        
        // Update basic camera parameters if provided
        cJSON* width_item = cJSON_GetObjectItem(request_json, "width");
        if (width_item && cJSON_IsNumber(width_item)) {
            double width_value = cJSON_GetNumberValue(width_item);
            if (width_value > 0 && width_value <= 4096) {
                camera_config.width = (uint32_t)width_value;
            }
        }
        
        cJSON* height_item = cJSON_GetObjectItem(request_json, "height");
        if (height_item && cJSON_IsNumber(height_item)) {
            double height_value = cJSON_GetNumberValue(height_item);
            if (height_value > 0 && height_value <= 4096) {
                camera_config.height = (uint32_t)height_value;
            }
        }
        
        cJSON* fps_item = cJSON_GetObjectItem(request_json, "fps");
        if (fps_item && cJSON_IsNumber(fps_item)) {
            double fps_value = cJSON_GetNumberValue(fps_item);
            if (fps_value > 0 && fps_value <= 60) {
                camera_config.fps = (uint32_t)fps_value;
            }
        }
        
        
        // Update image configuration if provided
        cJSON* image_config_item = cJSON_GetObjectItem(request_json, "image_config");
        if (image_config_item && cJSON_IsObject(image_config_item)) {
            // Update brightness (0-100)
            cJSON* brightness_item = cJSON_GetObjectItem(image_config_item, "brightness");
            if (brightness_item && cJSON_IsNumber(brightness_item)) {
                double brightness_value = cJSON_GetNumberValue(brightness_item);
                if (brightness_value >= 0 && brightness_value <= 100) {
                    camera_config.image_config.brightness = (uint32_t)brightness_value;
                }
            }
            
            // Update contrast (0-100)
            cJSON* contrast_item = cJSON_GetObjectItem(image_config_item, "contrast");
            if (contrast_item && cJSON_IsNumber(contrast_item)) {
                double contrast_value = cJSON_GetNumberValue(contrast_item);
                if (contrast_value >= 0 && contrast_value <= 100) {
                    camera_config.image_config.contrast = (uint32_t)contrast_value;
                }
            }
            
            // Update horizontal flip
            cJSON* horizontal_flip_item = cJSON_GetObjectItem(image_config_item, "horizontal_flip");
            if (horizontal_flip_item && cJSON_IsBool(horizontal_flip_item)) {
                camera_config.image_config.horizontal_flip = cJSON_IsTrue(horizontal_flip_item) ? AICAM_TRUE : AICAM_FALSE;
            }
            
            // Update vertical flip
            cJSON* vertical_flip_item = cJSON_GetObjectItem(image_config_item, "vertical_flip");
            if (vertical_flip_item && cJSON_IsBool(vertical_flip_item)) {
                camera_config.image_config.vertical_flip = cJSON_IsTrue(vertical_flip_item) ? AICAM_TRUE : AICAM_FALSE;
            }
            
            // Update AEC (0=manual, 1=auto)
            cJSON* aec_item = cJSON_GetObjectItem(image_config_item, "aec");
            if (aec_item && cJSON_IsNumber(aec_item)) {
                double aec_value = cJSON_GetNumberValue(aec_item);
                if (aec_value == 0.0 || aec_value == 1.0) {
                    camera_config.image_config.aec = (uint32_t)aec_value;
                }
            }

            cJSON* isp_mode_nested = cJSON_GetObjectItem(image_config_item, "isp_mode");
            if (isp_mode_nested && cJSON_IsNumber(isp_mode_nested)) {
                double m = cJSON_GetNumberValue(isp_mode_nested);
                if (m == (double)IMAGE_ISP_MODE_INDOOR || m == (double)IMAGE_ISP_MODE_OUTDOOR || m == (double)IMAGE_ISP_MODE_CUSTOM) {
                    camera_config.image_config.isp_mode = (uint32_t)m;
                }
            }

            cJSON* grayscale_nested = cJSON_GetObjectItem(image_config_item, "grayscale");
            if (grayscale_nested && cJSON_IsBool(grayscale_nested)) {
                camera_config.image_config.grayscale =
                    cJSON_IsTrue(grayscale_nested) ? AICAM_TRUE : AICAM_FALSE;
            }

            cJSON* cap_dis_comm_item = cJSON_GetObjectItem(image_config_item, "capture_disable_comm");
            if (cap_dis_comm_item && cJSON_IsBool(cap_dis_comm_item)) {
                camera_config.image_config.capture_disable_comm =
                    cJSON_IsTrue(cap_dis_comm_item) ? AICAM_TRUE : AICAM_FALSE;
            }

            cJSON* cap_stor_ai_item = cJSON_GetObjectItem(image_config_item, "capture_storage_ai");
            if (cap_stor_ai_item && cJSON_IsBool(cap_stor_ai_item)) {
                camera_config.image_config.capture_storage_ai =
                    cJSON_IsTrue(cap_stor_ai_item) ? AICAM_TRUE : AICAM_FALSE;
            }
        }
        
        cJSON_Delete(request_json);
        
        // Apply the updated configuration
        result = device_service_camera_set_config(&camera_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set camera configuration");
        }
        
        // Create success response
        cJSON* response_json = cJSON_CreateObject();
        if (!response_json) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
        }
        
        cJSON_AddStringToObject(response_json, "message", "Camera configuration updated successfully");
        cJSON_AddNumberToObject(response_json, "width", camera_config.width);
        cJSON_AddNumberToObject(response_json, "height", camera_config.height);
        cJSON_AddNumberToObject(response_json, "fps", camera_config.fps);
        
        // Add updated image configuration to response
        cJSON* image_config_response = cJSON_CreateObject();
        if (image_config_response) {
            cJSON_AddNumberToObject(image_config_response, "brightness", camera_config.image_config.brightness);
            cJSON_AddNumberToObject(image_config_response, "contrast", camera_config.image_config.contrast);
            cJSON_AddBoolToObject(image_config_response, "horizontal_flip", camera_config.image_config.horizontal_flip);
            cJSON_AddBoolToObject(image_config_response, "vertical_flip", camera_config.image_config.vertical_flip);
            cJSON_AddNumberToObject(image_config_response, "aec", camera_config.image_config.aec);
            cJSON_AddNumberToObject(image_config_response, "isp_mode", camera_config.image_config.isp_mode);
            cJSON_AddBoolToObject(image_config_response, "grayscale", camera_config.image_config.grayscale);
            cJSON_AddNumberToObject(image_config_response, "fast_capture_skip_frames", camera_config.image_config.fast_capture_skip_frames);
            cJSON_AddNumberToObject(image_config_response, "fast_capture_resolution", camera_config.image_config.fast_capture_resolution);
            cJSON_AddNumberToObject(image_config_response, "fast_capture_jpeg_quality", camera_config.image_config.fast_capture_jpeg_quality);
            cJSON_AddBoolToObject(image_config_response, "capture_disable_comm", camera_config.image_config.capture_disable_comm);
            cJSON_AddBoolToObject(image_config_response, "capture_storage_ai", camera_config.image_config.capture_storage_ai);
            cJSON_AddItemToObject(response_json, "image_config", image_config_response);
        }
        
        // Send response
        char* json_string = cJSON_Print(response_json);
        if (!json_string) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t api_result = api_response_success(ctx, json_string, "Camera configuration updated successfully");
        
        // Cleanup
        cJSON_Delete(response_json);

        
        return api_result;
        
    } else {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET and POST methods are allowed");
    }
}

/**
 * @brief POST /api/v1/system/time - Set system time
 */
aicam_result_t system_time_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract timestamp (optional)
    time_t timestamp = 0;
    cJSON* timestamp_item = cJSON_GetObjectItem(request_json, "timestamp");
    if (timestamp_item && cJSON_IsNumber(timestamp_item)) {
        uint64_t timestamp_value = (uint64_t)cJSON_GetNumberValue(timestamp_item);
        if (timestamp_value > 0) {
            timestamp = (time_t)timestamp_value;
        } else {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid timestamp value");
        }
    } else {
        // If no timestamp provided, use current system time
        timestamp = time(NULL);
    }
    
    // Extract timezone (optional)
    int timezone_offset_hours = 0;
    cJSON* timezone_item = cJSON_GetObjectItem(request_json, "timezone");
    if (timezone_item && cJSON_IsString(timezone_item)) {
        const char* timezone_str = cJSON_GetStringValue(timezone_item);
        
        // Simple timezone parsing - support common timezones
        if (strcmp(timezone_str, "Asia/Shanghai") == 0 || strcmp(timezone_str, "Asia/Beijing") == 0) {
            timezone_offset_hours = 8; // UTC+8
        } else if (strcmp(timezone_str, "America/New_York") == 0) {
            timezone_offset_hours = -5; // UTC-5 (EST) / UTC-4 (EDT) - simplified to EST
        } else if (strcmp(timezone_str, "America/Los_Angeles") == 0) {
            timezone_offset_hours = -8; // UTC-8 (PST) / UTC-7 (PDT) - simplified to PST
        } else if (strcmp(timezone_str, "Europe/London") == 0) {
            timezone_offset_hours = 0; // UTC+0 (GMT) / UTC+1 (BST) - simplified to GMT
        } else if (strcmp(timezone_str, "Europe/Paris") == 0) {
            timezone_offset_hours = 1; // UTC+1 (CET) / UTC+2 (CEST) - simplified to CET
        } else if (strcmp(timezone_str, "Asia/Tokyo") == 0) {
            timezone_offset_hours = 9; // UTC+9
        } else if (strncmp(timezone_str, "UTC", 3) == 0) {
            // Parse UTC+N or UTC-N format
            if (strlen(timezone_str) > 3) {
                char sign = timezone_str[3];
                if (sign == '+' || sign == '-') {
                    int offset = atoi(&timezone_str[4]);
                    timezone_offset_hours = (sign == '+') ? offset : -offset;
                }
            }
        } else {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, 
                "Unsupported timezone. Supported: Asia/Shanghai, America/New_York, America/Los_Angeles, Europe/London, Europe/Paris, Asia/Tokyo, UTC±N");
        }
    }
    
    cJSON_Delete(request_json);
    
    // Set system time using RTC
    rtc_setup_by_timestamp(timestamp, timezone_offset_hours);

    /* RTC stepped - invalidate wake_scheduler's last-handled-at state so
     * we don't accidentally suppress freshly-due events on the new clock. */
    wake_scheduler_reset_state();

    // Get the actual set time for response
    uint64_t current_timestamp = rtc_get_timeStamp();
    uint64_t local_timestamp = rtc_get_local_timestamp();

    
    // Create success response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "System time updated successfully");
    cJSON_AddNumberToObject(response_json, "utc_timestamp", (double)current_timestamp);
    cJSON_AddNumberToObject(response_json, "local_timestamp", (double)local_timestamp);
    cJSON_AddNumberToObject(response_json, "timezone_offset_hours", timezone_offset_hours);
    
    // Add human-readable time strings
    char utc_time_str[64];
    char local_time_str[64];
    
    struct tm* utc_tm = gmtime((time_t*)&current_timestamp);
    struct tm* local_tm = localtime((time_t*)&local_timestamp);
    
    if (utc_tm) {
        strftime(utc_time_str, sizeof(utc_time_str), "%Y-%m-%d %H:%M:%S UTC", utc_tm);
        cJSON_AddStringToObject(response_json, "utc_time", utc_time_str);
    }
    
    if (local_tm) {
        strftime(local_time_str, sizeof(local_time_str), "%Y-%m-%d %H:%M:%S", local_tm);
        cJSON_AddStringToObject(response_json, "local_time", local_time_str);
    }
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "System time updated successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/device/name - Set device name
 */
aicam_result_t device_name_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract device name
    cJSON* name_item = cJSON_GetObjectItem(request_json, "device_name");
    if (!name_item || !cJSON_IsString(name_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'device_name' field");
    }
    
    const char* new_device_name = cJSON_GetStringValue(name_item);
    if (!new_device_name || strlen(new_device_name) == 0 || strlen(new_device_name) >= 64) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Device name must be 1-63 characters");
    }
    
    // Validate device name (only allow alphanumeric, hyphen, underscore)
    for (size_t i = 0; i < strlen(new_device_name); i++) {
        char c = new_device_name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ')) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, 
                "Device name can only contain letters, numbers, hyphens, underscores, and spaces");
        }
    }
    
    cJSON_Delete(request_json);
    
    // Get current device information
    device_info_config_t device_info;
    aicam_result_t result = device_service_get_info(&device_info);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current device information");
    }
    
    // Update device name
    strncpy(device_info.device_name, new_device_name, sizeof(device_info.device_name) - 1);
    device_info.device_name[sizeof(device_info.device_name) - 1] = '\0';
    
    // Update device information
    result = device_service_update_info(&device_info);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to update device name");
    }
    
    // Create success response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "Device name updated successfully");
    cJSON_AddStringToObject(response_json, "device_name", device_info.device_name);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Device name updated successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief GET /api/v1/system/logs - Get system logs
 */
aicam_result_t system_logs_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Read logs from the log file
    const char* log_filename = "aicam.log"; // Default log file name
    void* log_file = file_fopen(log_filename, "r");
    if (!log_file) {
        // If main log file doesn't exist, try rotated files
        log_file = file_fopen("aicam.log.1", "r");
        if (!log_file) {
            cJSON* response_json = cJSON_CreateObject();
            if (!response_json) {
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
            }
            cJSON_AddStringToObject(response_json, "content", "");
            cJSON_AddNumberToObject(response_json, "size", 0);
            char* json_string = cJSON_Print(response_json);
            cJSON_Delete(response_json);
            return api_response_success(ctx, json_string, "No log files get");
        }
    }
    
    // Read all log content
    char* log_content = NULL;
    size_t log_size = 0;
    char buffer[1024];
    int bytes_read;
    
    // First pass: calculate total size
    while ((bytes_read = file_fread(log_file, buffer, sizeof(buffer))) > 0) {
        log_size += bytes_read;
    }
    
    if (log_size == 0) {
        file_fclose(log_file);
        return api_response_error(ctx, API_ERROR_NOT_FOUND, "Log file is empty");
    }
    
    // Allocate memory for log content
    log_content = buffer_calloc(1, log_size + 1);
    if (!log_content) {
        file_fclose(log_file);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for log content");
    }
    
    // Second pass: read content
    file_fseek(log_file, 0, SEEK_SET);
    size_t total_read = 0;
    while ((bytes_read = file_fread(log_file, buffer, sizeof(buffer))) > 0) {
        memcpy(log_content + total_read, buffer, bytes_read);
        total_read += bytes_read;
    }
    log_content[log_size] = '\0';
    
    file_fclose(log_file);
    
    // Create simple response JSON
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        buffer_free(log_content);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Add log content to JSON (cJSON will copy the string content)
    cJSON* content_item = cJSON_CreateString(log_content);
    if (!content_item) {
        cJSON_Delete(response_json);
        buffer_free(log_content);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create content string");
    }
    cJSON_AddItemToObject(response_json, "content", content_item);
    cJSON_AddNumberToObject(response_json, "size", log_size);
    
    buffer_free(log_content);
    log_content = NULL;
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "System logs retrieved successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //free(json_string);
    
    return api_result;
}

/**
 * @brief GET /api/v1/system/logs/export - Export all log files
 */
aicam_result_t system_logs_export_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Create response JSON for all log files
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON* log_files_array = cJSON_CreateArray();
    if (!log_files_array) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create log files array");
    }
    
    // List of log files to export, todo: get from configuration file
    const char* log_files[] = {
        "aicam.log",
        "aicam.log.1",
        "aicam.log.2",
        "aicam.log.3",
        "aicam.log.4",
        "aicam.log.5",
    };
    
    int total_files = sizeof(log_files) / sizeof(log_files[0]);
    int exported_files = 0;
    size_t total_size = 0;
    
    for (int i = 0; i < total_files; i++) {
        const char* filename = log_files[i];
        
        // Try to open the log file
        void* log_file = file_fopen(filename, "r");
        if (!log_file) {
            // File doesn't exist, skip it
            continue;
        }
        
        // Get file size
        size_t file_size = 0;
        char buffer[1024];
        int bytes_read;
        
        // First pass: calculate file size
        while ((bytes_read = file_fread(log_file, buffer, sizeof(buffer))) > 0) {
            file_size += bytes_read;
        }
        
        if (file_size == 0) {
            file_fclose(log_file);
            continue;
        }
        
        // Allocate memory for file content
        char* file_content = buffer_calloc(1, file_size + 1);
        if (!file_content) {
            file_fclose(log_file);
            continue;
        }
        
        // Second pass: read content
        file_fseek(log_file, 0, SEEK_SET);
        size_t total_read = 0;
        while ((bytes_read = file_fread(log_file, buffer, sizeof(buffer))) > 0) {
            memcpy(file_content + total_read, buffer, bytes_read);
            total_read += bytes_read;
        }
        file_content[file_size] = '\0';
        
        file_fclose(log_file);
        
        // Create log file entry
        cJSON* log_file_entry = cJSON_CreateObject();
        if (log_file_entry) {
            cJSON_AddStringToObject(log_file_entry, "filename", filename);
            cJSON_AddNumberToObject(log_file_entry, "size", file_size);
            
            // Add file content
            cJSON* content_item = cJSON_CreateString(file_content);
            if (content_item) {
                cJSON_AddItemToObject(log_file_entry, "content", content_item);
                cJSON_AddItemToArray(log_files_array, log_file_entry);
                exported_files++;
                total_size += file_size;
            } else {
                cJSON_Delete(log_file_entry);
            }
        }
        
        // Free file content
        buffer_free(file_content);
    }
    
    // Add summary information
    cJSON_AddItemToObject(response_json, "log_files", log_files_array);
    cJSON_AddNumberToObject(response_json, "total_files", exported_files);
    cJSON_AddNumberToObject(response_json, "total_size", total_size);

    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Log files exported successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/restart - System restart
 */
aicam_result_t system_restart_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Parse JSON request body (optional parameters)
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        // If no JSON body, use default restart
        request_json = cJSON_CreateObject();
    }
    
    // Extract restart delay (optional, in seconds)
    cJSON* delay_item = cJSON_GetObjectItem(request_json, "delay_seconds");
    if (delay_item && cJSON_IsNumber(delay_item)) {
        double delay_value = cJSON_GetNumberValue(delay_item);
        if (delay_value >= 0 && delay_value <= 60) {
            restart_delay_seconds = (uint32_t)delay_value;
        } else {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Delay must be between 0 and 60 seconds");
        }
    }
    
    cJSON_Delete(request_json);
    
    // Create immediate response before restart
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddNumberToObject(response_json, "delay_seconds", restart_delay_seconds);


    
    // Send response immediately
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "System restart initiated successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    //hal_mem_free(json_string);
    
    // Log restart request
    LOG_SVC_INFO("System restart requested via API - Delay: %u seconds", restart_delay_seconds);

    // Schedule system restart with delay
    if (restart_delay_seconds > 0) {
        LOG_SVC_INFO("System will restart in %u seconds...", restart_delay_seconds);

        s_restart_delay_storage = restart_delay_seconds;

        // Use pre-allocated static stack to avoid memory fragmentation after OTA
        osThreadAttr_t restart_task_attr = {
            .name = "restart_task",
            .stack_size = 1024,
            .priority = osPriorityHigh,
            .stack_mem = s_restart_task_stack
        };

        osThreadId_t restart_task = osThreadNew(restart_task_function, &s_restart_delay_storage, &restart_task_attr);

        if (!restart_task) {
            LOG_SVC_WARN("Failed to create restart task, using inline delayed restart");
            osDelay(restart_delay_seconds * 1000);
#if ENABLE_U0_MODULE
            u0_module_clear_wakeup_flag();
            u0_module_reset_chip_n6();
#endif
            HAL_NVIC_SystemReset();
        }
    } else {
        // Immediate restart
        LOG_SVC_INFO("Executing immediate system restart...");
#if ENABLE_U0_MODULE
        u0_module_clear_wakeup_flag();
        u0_module_reset_chip_n6();
#endif
        HAL_NVIC_SystemReset();
    }
    
    return api_result;
}

/**
 * @brief GET /api/v1/device/config/export - Export complete device configuration
 */
aicam_result_t device_config_export_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Get current global configuration
    aicam_global_config_t global_config;
    aicam_result_t result = json_config_get_config(&global_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current configuration");
    }
    
    // Allocate buffer for JSON serialization
    char* json_buffer = buffer_calloc(1, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (!json_buffer) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for export");
    }
    
    // Serialize configuration to JSON string
    result = json_config_serialize_to_string(&global_config, json_buffer, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (result != AICAM_OK) {
        buffer_free(json_buffer);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize configuration");
    }
    
    // Create response wrapper with metadata
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        buffer_free(json_buffer);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Add metadata
    cJSON_AddStringToObject(response_json, "export_version", "1.0");
    cJSON_AddNumberToObject(response_json, "export_timestamp", (double)rtc_get_timeStamp());
    
    // Parse the serialized config and add as "config" object
    cJSON* config_obj = cJSON_Parse(json_buffer);
    if (config_obj) {
        cJSON_AddItemToObject(response_json, "config", config_obj);
    } else {
        // Fallback: add as raw string if parsing fails
        cJSON_AddStringToObject(response_json, "config_raw", json_buffer);
    }
    
    buffer_free(json_buffer);
    
    // Serialize final response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Configuration exported successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    
    return api_result;
}

/**
 * @brief POST /api/v1/device/config/import - Import complete device configuration
 */
aicam_result_t device_config_import_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if device service is running
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract config object or config_raw string
    cJSON* config_item = cJSON_GetObjectItem(request_json, "config");
    char* config_json_str = NULL;
    aicam_bool_t should_free_config_str = AICAM_FALSE;
    
    if (cJSON_IsObject(config_item)) {
        // Config provided as object, serialize it to string
        config_json_str = cJSON_PrintUnformatted(config_item);
        should_free_config_str = AICAM_TRUE;
    } else {
        // Try config_raw as string
        cJSON* config_raw_item = cJSON_GetObjectItem(request_json, "config_raw");
        if (cJSON_IsString(config_raw_item)) {
            config_json_str = cJSON_GetStringValue(config_raw_item);
        }
    }
    
    if (!config_json_str) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'config' or 'config_raw' field");
    }
    
    // Parse configuration from JSON string
    aicam_global_config_t new_config;
    json_config_validation_options_t validation_opts = {
        .validate_json_syntax = AICAM_TRUE,
        .validate_data_types = AICAM_TRUE,
        .validate_value_ranges = AICAM_TRUE,
        .validate_checksum = AICAM_FALSE,  // Don't validate checksum on import
        .strict_mode = AICAM_FALSE
    };
    
    aicam_result_t result = json_config_parse_from_string(config_json_str, &new_config, &validation_opts);
    
    if (should_free_config_str) {
        cJSON_free(config_json_str);
    }
    cJSON_Delete(request_json);
    
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Failed to parse configuration data");
    }
    
    // Update timestamp and recalculate checksum
    new_config.timestamp = rtc_get_timeStamp();
    uint32_t new_checksum;
    json_config_calculate_checksum(&new_config, &new_checksum);
    new_config.checksum = new_checksum;
    
    // Apply the new configuration
    result = json_config_set_config(&new_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply configuration");
    }
    
    // Create success response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "Configuration imported successfully");
    cJSON_AddNumberToObject(response_json, "config_version", new_config.config_version);
    cJSON_AddNumberToObject(response_json, "timestamp", (double)new_config.timestamp);
    cJSON_AddNumberToObject(response_json, "checksum", new_config.checksum);
    cJSON_AddBoolToObject(response_json, "saved_to_file", result == AICAM_OK);
    
    // Send response
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Configuration imported successfully");
    
    // Cleanup
    cJSON_Delete(response_json);
    
    LOG_SVC_INFO("Device configuration imported successfully");

    return api_result;
}

/**
 * @brief GET/POST /api/v1/device/preference/stream_tab - Get or set preferred stream tab
 */
aicam_result_t device_pref_stream_tab_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;

    if (!web_api_verify_method(ctx, "GET") && !web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET and POST methods are allowed");
    }

    if (web_api_verify_method(ctx, "GET")) {
        char value[8] = "rtmp";
        json_config_nvs_read_string(NVS_KEY_PREF_STREAM_TAB, value, sizeof(value));
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "stream_tab", value);
        char *str = cJSON_Print(json);
        cJSON_Delete(json);
        return api_response_success(ctx, str, "OK");
    }

    // POST - save preference
    cJSON *req = web_api_parse_body(ctx);
    if (!req) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    cJSON *item = cJSON_GetObjectItem(req, "stream_tab");
    if (!item || !cJSON_IsString(item)) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "stream_tab is required");
    }
    const char *tab = item->valuestring;
    if (strcmp(tab, "rtmp") != 0 && strcmp(tab, "rtsp") != 0) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "stream_tab must be 'rtmp' or 'rtsp'");
    }
    json_config_nvs_write_string(NVS_KEY_PREF_STREAM_TAB, tab);
    cJSON_Delete(req);
    return api_response_success(ctx, "{}", "Preference saved");
}

/**
 * @brief POST /api/v1/system/factory-reset - Factory reset system
 */
aicam_result_t system_factory_reset_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract delay_seconds field
    cJSON* delay_seconds_item = cJSON_GetObjectItem(request_json, "delay_seconds");
    if (!delay_seconds_item || !cJSON_IsNumber(delay_seconds_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid delay_seconds field");
    }
    
    restart_delay_seconds = (uint32_t)cJSON_GetNumberValue(delay_seconds_item);
    if (restart_delay_seconds < 0 || restart_delay_seconds > 60) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Delay must be between 0 and 60 seconds");
    }

    cJSON_Delete(request_json);
    
    LOG_SVC_INFO("Factory reset requested via API, delay: %u seconds", restart_delay_seconds);
    
    // Send response immediately before reset
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "Factory reset initiated");
    cJSON_AddNumberToObject(response_json, "restart_delay_seconds", restart_delay_seconds);
    
    char* json_string = cJSON_Print(response_json);
    if (!json_string) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Factory reset initiated");
    cJSON_Delete(response_json);
    
    // Allocate factory reset task stack
    uint8_t* factory_reset_stack = buffer_calloc(1, 2048);
    if (!factory_reset_stack) {
        LOG_SVC_ERROR("Failed to allocate factory reset stack");
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate factory reset stack");
    }
    
    // Create factory reset task with delay
    osThreadAttr_t factory_reset_task_attr = {
        .name = "factory_reset",
        .stack_size = 2048,
        .priority = osPriorityHigh,
        .stack_mem = factory_reset_stack
    };
    
    osThreadId_t factory_reset_task = osThreadNew(factory_reset_task_function, &restart_delay_seconds, &factory_reset_task_attr);
    if (!factory_reset_task) {
        LOG_SVC_ERROR("Failed to create factory reset task, executing immediately");
        buffer_free(factory_reset_stack);
        
        // Execute factory reset immediately
        aicam_result_t result = device_service_reset_to_factory_defaults();
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to reset to factory defaults: %d", result);
            // Fallback: force restart
#if ENABLE_U0_MODULE
            u0_module_clear_wakeup_flag();
            u0_module_reset_chip_n6();
#endif
            HAL_NVIC_SystemReset();
        }
    }
    
    return api_result;
}

/**
 * @brief Helper function to get firmware version with suffix
 */
static void get_firmware_version_string(FirmwareType fw_type, char *version_str, size_t size)
{
    SystemState *sys_state = ota_get_system_state();
    if (!sys_state) {
        snprintf(version_str, size, "unknown");
        return;
    }
    
    int active_slot = sys_state->active_slot[fw_type];
    const slot_info_t *slot_info = ota_get_slot_info(fw_type, active_slot);

    if(fw_type == FIRMWARE_FSBL) {
        ota_version_to_string(slot_info->version, version_str, size);
        return;
    }

    if (!slot_info) {
        snprintf(version_str, size, "unknown");
        return;
    }
    
    // Try to read OTA header from flash to get full version with suffix
    uint32_t partition = get_active_partition(fw_type);
    if (partition != 0) {
        ota_header_t header = {0};
        int ret = storage_flash_read(partition, (void *)&header, sizeof(ota_header_t));
        if (ret == 0 && ota_header_verify(&header) == 0) {
            // Got valid header, extract full version with suffix
            if (ota_header_get_full_version(&header, version_str, size) == 0) {
                return;
            }
        }
    }
    
    // Fallback to numeric version from slot info
    ota_version_to_string(slot_info->version, version_str, size);
}

/**
 * @brief Get system firmware versions handler
 * GET /api/v1/device/firmware-versions
 * Returns: { "fsbl": "1.0.0.0_beta", "app": "1.0.0.913_beta", "web": "1.0.2.22", "model": "1.0.0.0_beta", "wakecore": "1.0.0.913_beta" }
 */
static aicam_result_t firmware_versions_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Only allow GET method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    // Create response JSON object
    cJSON *response = cJSON_CreateObject();
    if (!response) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    char version_str[64];
    
    // FSBL version
    get_firmware_version_string(FIRMWARE_FSBL, version_str, sizeof(version_str));
    cJSON_AddStringToObject(response, "fsbl", version_str);
    
    // APP version
    get_firmware_version_string(FIRMWARE_APP, version_str, sizeof(version_str));
    cJSON_AddStringToObject(response, "app", version_str);
    
    // WEB version
    get_firmware_version_string(FIRMWARE_WEB, version_str, sizeof(version_str));
    cJSON_AddStringToObject(response, "web", version_str);
    
    // MODEL version (use AI_2 if active, otherwise AI_1)
    FirmwareType model_type = json_config_get_ai_1_active() ? FIRMWARE_AI_2 : FIRMWARE_AI_1;
    get_firmware_version_string(model_type, version_str, sizeof(version_str));
    cJSON_AddStringToObject(response, "model", version_str);
    
    // WAKECORE version (use current running version)
#if ENABLE_U0_MODULE
    {
        ms_bridging_version_t wakecore_version = {0};
        char wakecore_version_str[32] = "unknown";
        if (u0_module_get_version(&wakecore_version) == 0) {
            snprintf(wakecore_version_str, sizeof(wakecore_version_str), "%d.%d.%d.%d", 
                     wakecore_version.major, wakecore_version.minor, wakecore_version.patch, wakecore_version.build);
        }
        cJSON_AddStringToObject(response, "wakecore", wakecore_version_str);
    }
#else
    cJSON_AddStringToObject(response, "wakecore", "N/A");
#endif

    // WiFi (SiWG917) firmware version — flash-stored .rps + running chip fw
    {
        char wifi_flash_ver[32] = "N/A";
        char wifi_running_ver[32] = "N/A";
        wifi_get_flash_version(wifi_flash_ver, sizeof(wifi_flash_ver));
        wifi_get_running_version(wifi_running_ver, sizeof(wifi_running_ver));
        cJSON_AddStringToObject(response, "wifi", wifi_flash_ver);
        cJSON_AddStringToObject(response, "wifi_running", wifi_running_ver);
        // Expected versions compiled from version.mk via version.h
        cJSON_AddStringToObject(response, "expected_fsbl", EXPECTED_FSBL_VERSION_STRING);
        cJSON_AddStringToObject(response, "expected_wifi", EXPECTED_WIFI_VERSION_STRING);
    }

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to format response");
    }
    
    // Send response
    return api_response_success(ctx, json_str, "Firmware versions retrieved successfully");
}

/**
 * @brief GET /api/v1/device/version-check — compare running versions against
 *        the EXPECTED_* macros compiled into the APP firmware.
 *
 * Returns an object like:
 *   { "fsbl_mismatch": false, "wifi_mismatch": true,
 *     "expected_fsbl": "1.0.3.0",    "current_fsbl": "1.0.3.0",
 *     "expected_wifi": "2.15.5.2",   "current_wifi": "2.14.5.207" }
 *
 * The web UI uses this after login to prompt the user to upgrade out-of-date
 * FSBL or WiFi firmware (FSBL check takes priority).
 */
static aicam_result_t version_check_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "GET"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET");

    // --- FSBL version (from OTA slot) ---
    SystemState *state = ota_get_system_state();
    char current_fsbl[32] = "unknown";
    if (state) {
        ota_version_to_string(
            state->slot[FIRMWARE_FSBL][state->active_slot[FIRMWARE_FSBL]].version,
            current_fsbl, sizeof(current_fsbl));
    }

    // --- WiFi version (from running chip) ---
    char current_wifi[32] = "N/A";
    wifi_get_running_version(current_wifi, sizeof(current_wifi));

    int fsbl_mismatch = (strcmp(current_fsbl, EXPECTED_FSBL_VERSION_STRING) != 0);
    int wifi_mismatch = (strcmp(current_wifi, EXPECTED_WIFI_VERSION_STRING) != 0);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "fsbl_mismatch", fsbl_mismatch);
    cJSON_AddBoolToObject(resp, "wifi_mismatch", wifi_mismatch);
    cJSON_AddStringToObject(resp, "expected_fsbl", EXPECTED_FSBL_VERSION_STRING);
    cJSON_AddStringToObject(resp, "current_fsbl", current_fsbl);
    cJSON_AddStringToObject(resp, "expected_wifi", EXPECTED_WIFI_VERSION_STRING);
    cJSON_AddStringToObject(resp, "current_wifi", current_wifi);

    char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return api_response_success(ctx, json_str, "OK");
}

/**
 * @brief POST /api/v1/device/storage/format - Format internal flash LittleFS.
 * @details Destructive: erases ALL files on the internal flash LittleFS volume
 *          (logs, captures, uploaded assets). NVS (device config) lives in a
 *          separate partition and is unaffected. Requires auth (route-level).
 *          After formatting, the captures tree is rebuilt and the record-count
 *          cache invalidated. The caller is expected to re-fetch storage info.
 */
aicam_result_t device_storage_format_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (!is_device_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Device service is not running");
    }

    LOG_SVC_WARN("device: formatting internal flash LittleFS (all flash data erased)");
    storage_format();
    /* Rebuild the captures directory tree and invalidate the (now-empty)
     * record-count cache. */
    upload_coordinator_reload_config();

    cJSON *resp = cJSON_CreateObject();
    if (!resp) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    cJSON_AddBoolToObject(resp, "success", 1);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "Flash formatted");
}

/* ==================== Route Registration ==================== */

/**
 * @brief Device API module routes
 */
static const api_route_t device_module_routes[] = {
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/info",
        .handler = device_info_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/firmware-versions",
        .handler = firmware_versions_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/version-check",
        .handler = version_check_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET", 
        .path = API_PATH_PREFIX "/device/storage",
        .handler = device_storage_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/storage/config",
        .handler = device_storage_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/storage/format",
        .handler = device_storage_format_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/image/config",
        .handler = device_image_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/image/config",
        .handler = device_image_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/sys-clk/config",
        .handler = device_sys_clk_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/sys-clk/config",
        .handler = device_sys_clk_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/light/config",
        .handler = device_light_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/light/config",
        .handler = device_light_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/light/control",
        .handler = device_light_control_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/camera/config",
        .handler = device_camera_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/camera/config",
        .handler = device_camera_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/time",
        .handler = system_time_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/name",
        .handler = device_name_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/system/logs",
        .handler = system_logs_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/system/logs/export",
        .handler = system_logs_export_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/restart",
        .handler = system_restart_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/factory-reset",
        .handler = system_factory_reset_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/config/export",
        .handler = device_config_export_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/config/import",
        .handler = device_config_import_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/device/preference/stream_tab",
        .handler = device_pref_stream_tab_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/device/preference/stream_tab",
        .handler = device_pref_stream_tab_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register device API module
 */
aicam_result_t web_api_register_device_module(void) {
    LOG_SVC_INFO("Registering Device API module...");
    
    // Register each route
    for (size_t i = 0; i < sizeof(device_module_routes) / sizeof(device_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&device_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", device_module_routes[i].path, result);
            return result;
        }
    }
    
    LOG_SVC_INFO("Device API module registered successfully (%zu routes)", 
                sizeof(device_module_routes) / sizeof(device_module_routes[0]));
    
    return AICAM_OK;
}
