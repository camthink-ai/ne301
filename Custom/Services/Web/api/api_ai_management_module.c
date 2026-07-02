/**
 * @file api_ai_management_module.c
 * @brief AI Management API Module
 * @details API module for AI management using ai_service
 */

#include "web_api.h"
#include "ai_service.h"
#include "cJSON.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "api_ai_management_module.h"
#include "web_server.h"

/* ==================== Internal Functions ==================== */

/**
 * @brief Parse AI inference status from string
 */
static aicam_bool_t parse_ai_inference_status(const char* status_str) {
    if (!status_str) return AICAM_FALSE;
    
    if (strcmp(status_str, "enabled") == 0 || strcmp(status_str, "true") == 0 || strcmp(status_str, "1") == 0) {
        return AICAM_TRUE;
    }
    
    return AICAM_FALSE;
}

/* ==================== API Handlers ==================== */

/**
 * @brief Get AI management status handler
 */
static aicam_result_t ai_management_status_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get AI inference status
    aicam_bool_t ai_enabled = ai_get_inference_enabled();
    
    // Get AI model information
    nn_model_info_t model_info;
    
    aicam_result_t result = ai_get_model_info(&model_info);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI model info: %d", result);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get AI model info");
    }
    // Create response data
    cJSON* data = cJSON_CreateObject();
    
    
    // Add AI inference status
    cJSON_AddBoolToObject(data, "ai_enabled", ai_enabled);
    
    // Add model information
    cJSON* model = cJSON_CreateObject();
    nn_state_t model_state = nn_get_state();
    if (model_state == NN_STATE_READY || model_state == NN_STATE_RUNNING) {
        cJSON_AddStringToObject(model, "name", model_info.name);
        cJSON_AddStringToObject(model, "description", model_info.description);
        cJSON_AddStringToObject(model, "author", model_info.author);
        cJSON_AddStringToObject(model, "postprocess_type", model_info.postprocess_type);
        cJSON_AddStringToObject(model, "input_data_type", model_info.input_data_type);
        cJSON_AddStringToObject(model, "output_data_type", model_info.output_data_type);
        cJSON_AddStringToObject(model, "color_format", model_info.color_format);
        cJSON_AddStringToObject(model, "version", model_info.version);
        cJSON_AddStringToObject(model, "created_at", model_info.created_at);
        cJSON_AddStringToObject(model, "stedgeai_version", model_info.stedgeai_version[0] ? model_info.stedgeai_version : "unknown");
        cJSON_AddNumberToObject(model, "input_width", model_info.input_width);
        cJSON_AddNumberToObject(model, "input_height", model_info.input_height);
        cJSON_AddNumberToObject(model, "input_channels", model_info.input_channels);
        cJSON_AddNumberToObject(model, "model_size", model_info.model_size);
        cJSON_AddStringToObject(model, "status",  "loaded");
    } else {
        cJSON_AddStringToObject(model, "status", "unloaded");
    }
    cJSON_AddItemToObject(data, "model", model);
    
    // Add pipeline status
    cJSON* pipeline = cJSON_CreateObject();
    aicam_bool_t pipeline_running = ai_pipeline_is_running();
    cJSON_AddStringToObject(pipeline, "status", pipeline_running ? "running" : "stopped");
    cJSON_AddItemToObject(data, "pipeline", pipeline);
    
    // Add statistics
    // cJSON* stats = cJSON_CreateObject();
    // ai_service_stats_t service_stats;
    // if (ai_service_get_stats(&service_stats) == AICAM_OK) {
    //     cJSON_AddNumberToObject(stats, "total_frames_processed", service_stats.total_frames_processed);
    //     cJSON_AddNumberToObject(stats, "total_inferences", service_stats.total_inferences);
    //     cJSON_AddNumberToObject(stats, "average_inference_time_ms", service_stats.average_inference_time_ms);
    //     cJSON_AddNumberToObject(stats, "error_count", service_stats.error_count);
    //     cJSON_AddNumberToObject(stats, "uptime_seconds", service_stats.uptime_seconds);
    // } else {
    //     cJSON_AddNumberToObject(stats, "total_frames_processed", 0);
    //     cJSON_AddNumberToObject(stats, "total_inferences", 0);
    //     cJSON_AddNumberToObject(stats, "average_inference_time_ms", 0.0);
    //     cJSON_AddNumberToObject(stats, "error_count", 0);
    //     cJSON_AddNumberToObject(stats, "uptime_seconds", 0);
    // }
    // cJSON_AddItemToObject(data, "statistics", stats);
    
    api_response_success(ctx, cJSON_Print(data), "AI management status retrieved");

    cJSON_Delete(data);
    
    return AICAM_OK;
}

/**
 * @brief Switch AI inference handler
 */
static aicam_result_t ai_management_switch_inference_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Parse request body
    cJSON* request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    // Get inference status from request
    const char* status_str = web_api_get_string(request, "ai_enabled");
    aicam_bool_t enable_inference = AICAM_FALSE;
    
    if (status_str) {
        enable_inference = parse_ai_inference_status(status_str);
    } else {
        // Try boolean value
        enable_inference = web_api_get_bool(request, "ai_enabled");
    }
    
    cJSON_Delete(request);
    
    // Get current status
    aicam_bool_t current_status = ai_get_inference_enabled();
    
    // Switch AI inference
    aicam_result_t result = AICAM_OK;
    if (enable_inference != current_status) {
        if (enable_inference) {
            result = ai_set_inference_enabled(AICAM_TRUE);
        } else {
            result = ai_set_inference_enabled(AICAM_FALSE);
        }
    }
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "AI inference status updated successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to update AI inference status");
    }
    
    return AICAM_OK;
}

/**
 * @brief Start AI pipeline handler
 */
static aicam_result_t ai_management_start_pipeline_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Start AI pipeline
    aicam_result_t result = ai_pipeline_start();
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "AI pipeline started successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to start AI pipeline");
    }
    
    return AICAM_OK;
}

/**
 * @brief Stop AI pipeline handler
 */
static aicam_result_t ai_management_stop_pipeline_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Stop AI pipeline
    aicam_result_t result = ai_pipeline_stop();
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "AI pipeline stopped successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to stop AI pipeline");
    }
    
    return AICAM_OK;
}

/**
 * @brief Get AI threshold configuration handler
 */
static aicam_result_t ai_management_get_thresholds_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get current threshold values
    uint32_t nms_threshold = ai_get_nms_threshold();
    uint32_t confidence_threshold = ai_get_confidence_threshold();
    
    // Create response data
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "nms_threshold", nms_threshold);
    cJSON_AddNumberToObject(data, "confidence_threshold", confidence_threshold);
    cJSON_AddBoolToObject(data, "overlay_results", ai_get_overlay_results());

    // Add threshold descriptions
    cJSON* descriptions = cJSON_CreateObject();
    cJSON_AddStringToObject(descriptions, "nms_threshold", "Non-Maximum Suppression threshold (0-100)");
    cJSON_AddStringToObject(descriptions, "confidence_threshold", "AI confidence threshold (0-100)");
    cJSON_AddStringToObject(descriptions, "overlay_results", "Draw AI results onto encoded video frames");
    cJSON_AddItemToObject(data, "descriptions", descriptions);
    
    api_response_success(ctx, cJSON_Print(data), "AI threshold configuration retrieved");
    
    cJSON_Delete(data);
    
    return AICAM_OK;
}

/**
 * @brief Set AI threshold configuration handler
 */
static aicam_result_t ai_management_set_thresholds_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Parse request body
    cJSON* request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    aicam_result_t result = AICAM_OK;
    cJSON* response_data = cJSON_CreateObject();
    cJSON* errors = cJSON_CreateArray();
    cJSON* updated = cJSON_CreateArray();
    
    // Update NMS threshold if provided
    cJSON* nms_item = cJSON_GetObjectItem(request, "nms_threshold");
    if (nms_item && cJSON_IsNumber(nms_item)) {
        double nms_value = cJSON_GetNumberValue(nms_item);
        if (nms_value >= 0 && nms_value <= 100) {
            aicam_result_t nms_result = ai_set_nms_threshold((uint32_t)nms_value);
            if (nms_result == AICAM_OK) {
                cJSON_AddStringToObject(response_data, "nms_threshold", "updated");
                cJSON_AddItemToArray(updated, cJSON_CreateString("nms_threshold"));
            } else {
                cJSON_AddStringToObject(response_data, "nms_threshold", "failed");
                cJSON_AddItemToArray(errors, cJSON_CreateString("Failed to set NMS threshold"));
            }
        } else {
            cJSON_AddStringToObject(response_data, "nms_threshold", "invalid_range");
            cJSON_AddItemToArray(errors, cJSON_CreateString("NMS threshold must be between 0 and 100"));
        }
    }
    
    // Update confidence threshold if provided
    cJSON* confidence_item = cJSON_GetObjectItem(request, "confidence_threshold");
    if (confidence_item && cJSON_IsNumber(confidence_item)) {
        double confidence_value = cJSON_GetNumberValue(confidence_item);
        if (confidence_value >= 0 && confidence_value <= 100) {
            aicam_result_t conf_result = ai_set_confidence_threshold((uint32_t)confidence_value);
            if (conf_result == AICAM_OK) {
                cJSON_AddStringToObject(response_data, "confidence_threshold", "updated");
                cJSON_AddItemToArray(updated, cJSON_CreateString("confidence_threshold"));
            } else {
                cJSON_AddStringToObject(response_data, "confidence_threshold", "failed");
                cJSON_AddItemToArray(errors, cJSON_CreateString("Failed to set confidence threshold"));
            }
        } else {
            cJSON_AddStringToObject(response_data, "confidence_threshold", "invalid_range");
            cJSON_AddItemToArray(errors, cJSON_CreateString("Confidence threshold must be between 0 and 100"));
        }
    }

    // Update overlay results if provided
    cJSON* overlay_item = cJSON_GetObjectItem(request, "overlay_results");
    if (overlay_item && cJSON_IsBool(overlay_item)) {
        aicam_bool_t overlay_value = cJSON_IsTrue(overlay_item) ? AICAM_TRUE : AICAM_FALSE;
        aicam_result_t overlay_result = ai_set_overlay_results(overlay_value);
        if (overlay_result == AICAM_OK) {
            cJSON_AddStringToObject(response_data, "overlay_results", "updated");
            cJSON_AddItemToArray(updated, cJSON_CreateString("overlay_results"));
        } else {
            cJSON_AddStringToObject(response_data, "overlay_results", "failed");
            cJSON_AddItemToArray(errors, cJSON_CreateString("Failed to set overlay results"));
        }
    }

    // Add current values to response
    cJSON_AddNumberToObject(response_data, "current_nms_threshold", ai_get_nms_threshold());
    cJSON_AddNumberToObject(response_data, "current_confidence_threshold", ai_get_confidence_threshold());
    cJSON_AddBoolToObject(response_data, "current_overlay_results", ai_get_overlay_results());
    
    // Add errors and updated arrays
    cJSON_AddItemToObject(response_data, "errors", errors);
    cJSON_AddItemToObject(response_data, "updated", updated);
    
    cJSON_Delete(request);
    
    // Determine response message
    const char* message;
    if (cJSON_GetArraySize(errors) > 0) {
        if (cJSON_GetArraySize(updated) > 0) {
            message = "AI threshold configuration partially updated";
        } else {
            message = "Failed to update AI threshold configuration";
            result = AICAM_ERROR;
        }
    } else {
        message = "AI threshold configuration updated successfully";
    }
    
    api_response_success(ctx, cJSON_Print(response_data), message);
    
    cJSON_Delete(response_data);
    
    return result;
}

/* ==================== Module Definition ==================== */

// AI management module routes
static const api_route_t ai_management_module_routes[] = {
    {
        .path = API_PATH_PREFIX "/ai/status",
        .method = "GET",
        .handler = ai_management_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/ai/toggle",
        .method = "POST",
        .handler = ai_management_switch_inference_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/ai/pipeline/start",
        .method = "POST",
        .handler = ai_management_start_pipeline_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/ai/pipeline/stop",
        .method = "POST",
        .handler = ai_management_stop_pipeline_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/ai/params",
        .method = "GET",
        .handler = ai_management_get_thresholds_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/ai/params",
        .method = "POST",
        .handler = ai_management_set_thresholds_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/* ==================== Public API ==================== */

/**
 * @brief Register AI management module
 */
aicam_result_t web_api_register_ai_management_module(void) {
    LOG_CORE_INFO("Registering AI management module");
    
    for (int i = 0; i < sizeof(ai_management_module_routes) / sizeof(ai_management_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&ai_management_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to register AI management module: %d", result);
            return result;
        }
    }
    
    LOG_CORE_INFO("AI management module registered successfully");
    return AICAM_OK;
}
