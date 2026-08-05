/**
 * @file api_work_mode_module.c
 * @brief Work Mode API Module
 * @details API module for work mode management using system_service
 */

#include "web_api.h"
#include "system_service.h"
#include "cJSON.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "api_work_mode_module.h"
#include "web_server.h"
#include "json_config_mgr.h"
#include "buffer_mgr.h"

/* ==================== Internal Functions ==================== */

/**
 * @brief Validate system service context
 * @param ctx HTTP handler context for error responses
 * @param service_ctx System service context to validate
 * @return AICAM_OK if valid, error code otherwise
 */
static aicam_result_t validate_system_service_context(http_handler_context_t* ctx, system_service_context_t* service_ctx) {
    if (!service_ctx) {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System service not initialized");
        return AICAM_ERROR;
    }
    
    // Check if system service is properly running (this will validate the internal state)
    aicam_result_t status = system_service_get_status();
    if (status != AICAM_OK) {
        api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "System service not ready");
        return AICAM_ERROR;
    }
    
    return AICAM_OK;
}

/**
 * @brief Get system controller safely
 * @return System controller handle or NULL if not available
 */
static system_controller_t* get_system_controller(void) {
    return system_service_get_controller();
}

/**
 * @brief Get work mode string
 */
static const char* get_work_mode_string(aicam_work_mode_t mode) {
    switch (mode) {
        case AICAM_WORK_MODE_IMAGE:
            return "image";
        case AICAM_WORK_MODE_VIDEO_STREAM:
            return "video_stream";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse work mode from string
 */
static aicam_work_mode_t parse_work_mode(const char* mode_str) {
    if (!mode_str) return AICAM_WORK_MODE_IMAGE;
    
    if (strcmp(mode_str, "image") == 0) {
        return AICAM_WORK_MODE_IMAGE;
    } else if (strcmp(mode_str, "video_stream") == 0) {
        return AICAM_WORK_MODE_VIDEO_STREAM;
    }
    
    return AICAM_WORK_MODE_IMAGE; // Default
}

/**
 * @brief Get power mode string
 */
static const char* get_power_mode_string(power_mode_t mode) {
    switch (mode) {
        case POWER_MODE_LOW_POWER:
            return "low_power";
        case POWER_MODE_FULL_SPEED:
            return "full_speed";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse power mode from string
 */
static power_mode_t parse_power_mode(const char* mode_str) {
    if (!mode_str) return POWER_MODE_LOW_POWER;
    
    if (strcmp(mode_str, "low_power") == 0) {
        return POWER_MODE_LOW_POWER;
    } else if (strcmp(mode_str, "full_speed") == 0) {
        return POWER_MODE_FULL_SPEED;
    }
    
    return POWER_MODE_LOW_POWER; // Default
}

/**
 * @brief Get trigger type string
 */
//static const char* get_trigger_type_string(aicam_trigger_type_t trigger_type) {
//    switch (trigger_type) {
static const char* get_trigger_type_string(aicam_trigger_type_t type) {
    switch (type) {
        case AICAM_TRIGGER_TYPE_RISING: return "rising_edge";
        case AICAM_TRIGGER_TYPE_FALLING: return "falling_edge";
        case AICAM_TRIGGER_TYPE_BOTH_EDGES: return "both_edges";
        case AICAM_TRIGGER_TYPE_HIGH: return "high_level";
        case AICAM_TRIGGER_TYPE_LOW: return "low_level";
        default: return "rising_edge";
    }
}

/**
 * @brief Parse trigger type from string
 */
static aicam_trigger_type_t parse_trigger_type(const char* type_str) {
    if (!type_str) return AICAM_TRIGGER_TYPE_RISING;

    if (strcmp(type_str, "rising_edge") == 0) return AICAM_TRIGGER_TYPE_RISING;
    if (strcmp(type_str, "falling_edge") == 0) return AICAM_TRIGGER_TYPE_FALLING;
    if (strcmp(type_str, "both_edges") == 0) return AICAM_TRIGGER_TYPE_BOTH_EDGES;
    if (strcmp(type_str, "high_level") == 0) return AICAM_TRIGGER_TYPE_HIGH;
    if (strcmp(type_str, "low_level") == 0) return AICAM_TRIGGER_TYPE_LOW;

    return AICAM_TRIGGER_TYPE_RISING; // Default to rising_edge
}

/**
 * @brief Get capture mode string
 */
static const char* get_capture_mode_string(aicam_timer_capture_mode_t capture_mode) {
    switch (capture_mode) {
        case AICAM_TIMER_CAPTURE_MODE_NONE: return "none";
        case AICAM_TIMER_CAPTURE_MODE_INTERVAL: return "interval";
        case AICAM_TIMER_CAPTURE_MODE_ABSOLUTE: return "once";
        default: return "unknown";
    }
}

/**
 * @brief Parse capture mode from string
 */
static aicam_timer_capture_mode_t parse_capture_mode(const char* mode_str) {
    if (!mode_str) return AICAM_TIMER_CAPTURE_MODE_NONE;
    
    if (strcmp(mode_str, "none") == 0) return AICAM_TIMER_CAPTURE_MODE_NONE;
    if (strcmp(mode_str, "interval") == 0) return AICAM_TIMER_CAPTURE_MODE_INTERVAL;
    if (strcmp(mode_str, "once") == 0) return AICAM_TIMER_CAPTURE_MODE_ABSOLUTE;
    
    return AICAM_TIMER_CAPTURE_MODE_NONE; // Default to none
}


/**
 * @brief Parse time node from string
 */
static uint32_t parse_time_node(const char* time_node_str) {
    if (!time_node_str) return 0;
    //format: HH:MM
    int hour, minute;
    sscanf(time_node_str, "%d:%d", &hour, &minute);
    return hour * 60 * 60 + minute * 60;
}

/**
 * @brief Get time node string
 */
static char* get_time_node_string(uint32_t time_node) {
    time_node /= 60;
    char* time_node_str = (char*)buffer_calloc(1, 10);
    memset(time_node_str, 0, 10);
    sprintf(time_node_str, "%02d:%02d", (int)time_node / 60 , (int)time_node % 60);
    return time_node_str;
}

/* ==================== API Handlers ==================== */

/**
 * @brief Get work mode status handler
 */
static aicam_result_t work_mode_status_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    // Get current work mode
    aicam_work_mode_t current_mode = system_controller_get_work_mode(controller);

    // Get work mode configuration
    work_mode_config_t mode_config;
    aicam_result_t result = system_controller_get_work_config(controller, &mode_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get work mode configuration");
    }

    // Create response data
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "current_mode", get_work_mode_string(current_mode));
    //cJSON_AddNumberToObject(data, "mode_id", current_mode);
    
    // Add complete configuration based on work_mode_config_t structure
    cJSON* config = cJSON_CreateObject();
    
    // Always include both image_mode and video_stream_mode configurations
    // Image mode configuration
    cJSON* image_config = cJSON_CreateObject();
    cJSON_AddBoolToObject(image_config, "enable", mode_config.image_mode.enable);
    
    // PIR trigger configuration
    // cJSON* pir_trigger = cJSON_CreateObject();
    // cJSON_AddBoolToObject(pir_trigger, "enable", mode_config.pir_trigger.enable);
    // cJSON_AddNumberToObject(pir_trigger, "pin_number", mode_config.pir_trigger.pin_number);
    // cJSON_AddStringToObject(pir_trigger, "trigger_type", get_trigger_type_string(mode_config.pir_trigger.trigger_type));  
    // cJSON_AddItemToObject(image_config, "pir_trigger", pir_trigger);
    
    // Timer trigger configuration
    // cJSON* timer_trigger = cJSON_CreateObject();
    // cJSON_AddBoolToObject(timer_trigger, "enable", mode_config.timer_trigger.enable);
    // cJSON_AddStringToObject(timer_trigger, "capture_mode", get_capture_mode_string(mode_config.timer_trigger.capture_mode));
    // cJSON_AddNumberToObject(timer_trigger, "interval_sec", mode_config.timer_trigger.interval_sec);
    // cJSON_AddNumberToObject(timer_trigger, "time_node_count", mode_config.timer_trigger.time_node_count);
    
    // Time nodes array (only include active time nodes based on time_node_count)
    // cJSON* time_nodes = cJSON_CreateArray();
    // uint32_t active_nodes = mode_config.timer_trigger.time_node_count;
    // if (active_nodes > 10) active_nodes = 10; // Safety check
    // for (uint32_t i = 0; i < active_nodes; i++) {
    //     cJSON_AddItemToArray(time_nodes, cJSON_CreateNumber(mode_config.timer_trigger.time_node[i]));
    // }
    // cJSON_AddItemToObject(timer_trigger, "time_node", time_nodes);
    // cJSON_AddItemToObject(image_config, "timer_trigger", timer_trigger);
    
    // IO triggers configuration
    // cJSON* io_triggers = cJSON_CreateArray();
    // for (int i = 0; i < IO_TRIGGER_MAX; i++) {
    //     cJSON* io_trigger = cJSON_CreateObject();
    //     cJSON_AddNumberToObject(io_trigger, "id", i);
    //     cJSON_AddNumberToObject(io_trigger, "pin_number", mode_config.io_trigger[i].pin_number);
    //     cJSON_AddBoolToObject(io_trigger, "enable", mode_config.io_trigger[i].enable);
    //     cJSON_AddBoolToObject(io_trigger, "input_enable", mode_config.io_trigger[i].input_enable);
    //     cJSON_AddBoolToObject(io_trigger, "output_enable", mode_config.io_trigger[i].output_enable);
    //     cJSON_AddStringToObject(io_trigger, "input_trigger_type", get_trigger_type_string(mode_config.io_trigger[i].input_trigger_type));
    //     cJSON_AddStringToObject(io_trigger, "output_trigger_type", get_trigger_type_string(mode_config.io_trigger[i].output_trigger_type));
    //     cJSON_AddItemToArray(io_triggers, io_trigger);
    // }
    // cJSON_AddItemToObject(image_config, "io_trigger", io_triggers);
    
    cJSON_AddItemToObject(config, "image_mode", image_config);
    
    // Video stream mode configuration
    cJSON* video_stream_config = cJSON_CreateObject();
    cJSON_AddBoolToObject(video_stream_config, "enable", mode_config.video_stream_mode.enable);
    cJSON_AddStringToObject(video_stream_config, "rtsp_server_url", mode_config.video_stream_mode.rtsp_server_url);
    cJSON_AddItemToObject(config, "video_stream_mode", video_stream_config);
    
    cJSON_AddItemToObject(data, "configuration", config);
    
    // Generate JSON string and clean up
    char *json_string = cJSON_Print(data);
    if (json_string) {
        api_response_success(ctx, json_string, "Work mode status retrieved");
        //hal_mem_free(json_string); // Free the JSON string allocated by cJSON_Print
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to generate JSON response");
    }

    cJSON_Delete(data);
    
    return AICAM_OK;
}

/**
 * @brief Switch work mode handler
 */
static aicam_result_t work_mode_switch_handler(http_handler_context_t* ctx) {
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
    
    cJSON* mode_item = cJSON_GetObjectItem(request, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'mode' parameter");
    }
    const char* mode_str = cJSON_GetStringValue(mode_item);
    
    aicam_work_mode_t new_mode = parse_work_mode(mode_str);
    if (new_mode >= AICAM_WORK_MODE_MAX) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid mode value");
    }
    
    cJSON_Delete(request);
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }
    
    // Switch work mode
    aicam_result_t result = system_controller_set_work_mode(controller, new_mode);
    if (result == AICAM_OK) {        
        api_response_success(ctx, NULL, "Work mode switched successfully");
    } else {  
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to switch work mode");
    }
    
    return AICAM_OK;
}

/**
 * @brief Get power mode status handler
 */
static aicam_result_t power_mode_status_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    // Get current power mode
    power_mode_t current_mode = system_controller_get_power_mode(controller);

    // Get power mode configuration using new API
    power_mode_config_t power_config;
    aicam_result_t result = system_service_get_power_mode_config(&power_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get power mode configuration");
    }

    // Create response data
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "current_mode", get_power_mode_string(current_mode));
    
    // Add complete power mode configuration
    cJSON* config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "current_mode", get_power_mode_string(power_config.current_mode));
    cJSON_AddStringToObject(config, "default_mode", get_power_mode_string(power_config.default_mode));
    cJSON_AddNumberToObject(config, "low_power_timeout_ms", power_config.low_power_timeout_ms);
    cJSON_AddNumberToObject(config, "last_activity_time", power_config.last_activity_time);
    cJSON_AddNumberToObject(config, "mode_switch_count", power_config.mode_switch_count);
    cJSON_AddItemToObject(data, "configuration", config);

    
    // Generate JSON string and clean up
    char *json_string = cJSON_Print(data);
    if (json_string) {
        api_response_success(ctx, json_string, "Power mode status retrieved");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to generate JSON response");
    }

    cJSON_Delete(data);
    
    return AICAM_OK;
}

/**
 * @brief Switch power mode handler
 */
static aicam_result_t power_mode_switch_handler(http_handler_context_t* ctx) {
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
    
    cJSON* mode_item = cJSON_GetObjectItem(request, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'mode' parameter");
    }
    const char* mode_str = cJSON_GetStringValue(mode_item);
    
    power_mode_t new_mode = parse_power_mode(mode_str);
    if (new_mode >= POWER_MODE_MAX) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid power mode value");
    }
    
    cJSON_Delete(request);
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    //trigger type is manual
    power_trigger_type_t trigger_type = POWER_TRIGGER_MANUAL;
    
    // Switch power mode using new API
    aicam_result_t result = system_service_set_current_power_mode(new_mode, trigger_type);
    if (result == AICAM_OK) {        
        api_response_success(ctx, NULL, "Power mode switched successfully");
    } else {  
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to switch power mode");
    }
    
    return AICAM_OK;
}

/**
 * @brief Get power mode configuration handler
 */
static aicam_result_t power_mode_config_get_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get power mode configuration using new API
    power_mode_config_t power_config;
    aicam_result_t result = system_service_get_power_mode_config(&power_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get power mode configuration");
    }

    // Create response data
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "current_mode", get_power_mode_string(power_config.current_mode));
    cJSON_AddStringToObject(data, "default_mode", get_power_mode_string(power_config.default_mode));
    cJSON_AddNumberToObject(data, "low_power_timeout_ms", power_config.low_power_timeout_ms);
    cJSON_AddNumberToObject(data, "last_activity_time", power_config.last_activity_time);
    cJSON_AddNumberToObject(data, "mode_switch_count", power_config.mode_switch_count);
    
    // Generate JSON string and clean up
    char *json_string = cJSON_Print(data);
    if (json_string) {
        api_response_success(ctx, json_string, "Power mode configuration retrieved");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to generate JSON response");
    }

    cJSON_Delete(data);
    
    return AICAM_OK;
}

/**
 * @brief Set power mode configuration handler
 */
static aicam_result_t power_mode_config_set_handler(http_handler_context_t* ctx) {
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
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        cJSON_Delete(request);
        return validation_result;
    }
    
    // Load current configuration
    power_mode_config_t config;
    aicam_result_t result = system_service_get_power_mode_config(&config);
    if (result != AICAM_OK) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current power mode configuration");
    }
    
    // Parse configuration from request
    cJSON* current_mode_item = cJSON_GetObjectItem(request, "current_mode");
    if (current_mode_item && cJSON_IsString(current_mode_item)) {
        const char* current_mode_str = cJSON_GetStringValue(current_mode_item);
        config.current_mode = parse_power_mode(current_mode_str);
    }
    
    cJSON* default_mode_item = cJSON_GetObjectItem(request, "default_mode");
    if (default_mode_item && cJSON_IsString(default_mode_item)) {
        const char* default_mode_str = cJSON_GetStringValue(default_mode_item);
        config.default_mode = parse_power_mode(default_mode_str);
    }
    
    cJSON* timeout_item = cJSON_GetObjectItem(request, "low_power_timeout_ms");
    if (timeout_item && cJSON_IsNumber(timeout_item)) {
        int timeout_ms = (int)cJSON_GetNumberValue(timeout_item);
        if (timeout_ms > 0) {
            config.low_power_timeout_ms = timeout_ms;
        }
    }
    
    cJSON_Delete(request);
    
    // Update configuration using new API
    result = system_service_set_power_mode_config(&config);
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "Power mode configuration updated successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to update power mode configuration");
    }
    
    return AICAM_OK;
}

/**
 * @brief Get image mode triggers handler
 */
static aicam_result_t work_mode_triggers_get_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    // Get current work mode configuration
    work_mode_config_t config;
    aicam_result_t result = system_controller_get_work_config(controller, &config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get work mode configuration");
    }
    
    // Create JSON response
    cJSON* response = cJSON_CreateObject();
    if (!response) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create JSON response");
    }
    
    // Add work mode
    // const char* work_mode_str = get_work_mode_string(config.work_mode);
    // cJSON_AddStringToObject(response, "work_mode", work_mode_str);
    
    // Add image mode configuration
    // cJSON* image_mode = cJSON_CreateObject();
    // cJSON_AddBoolToObject(image_mode, "enable", config.image_mode.enable);
    
    // Timer trigger configuration
    cJSON* timer_trigger = cJSON_CreateObject();
    cJSON_AddBoolToObject(timer_trigger, "enable", config.timer_trigger.enable);
    cJSON_AddStringToObject(timer_trigger, "capture_mode", get_capture_mode_string(config.timer_trigger.capture_mode));
    cJSON_AddNumberToObject(timer_trigger, "interval_sec", config.timer_trigger.interval_sec);
    cJSON_AddNumberToObject(timer_trigger, "time_node_count", config.timer_trigger.time_node_count);
    
    // Time nodes array
    cJSON* time_nodes = cJSON_CreateArray();
    for (int i = 0; i < config.timer_trigger.time_node_count && i < 10; i++) {
        char* time_node_str = get_time_node_string(config.timer_trigger.time_node[i]);
        cJSON_AddItemToArray(time_nodes, cJSON_CreateString(time_node_str));
        buffer_free(time_node_str);
    }
    cJSON_AddItemToObject(timer_trigger, "time_node", time_nodes);
    
    // Weekdays array
    cJSON* weekdays = cJSON_CreateArray();
    for (int i = 0; i < config.timer_trigger.time_node_count && i < 10; i++) {
        cJSON_AddItemToArray(weekdays, cJSON_CreateNumber(config.timer_trigger.weekdays[i]));
    }
    cJSON_AddItemToObject(timer_trigger, "weekdays", weekdays);

    // Interval mode and start time
    cJSON_AddStringToObject(timer_trigger, "interval_mode",
        config.timer_trigger.interval_mode == AICAM_TIMER_INTERVAL_MODE_SCHEDULED
            ? "scheduled" : "normal");
    char* start_time_str = get_time_node_string(config.timer_trigger.start_time);
    cJSON_AddStringToObject(timer_trigger, "start_time", start_time_str);
    buffer_free(start_time_str);

    // Next capture time (read-only)
    uint64_t next_capture_at = 0;
    system_controller_t* ctrl = get_system_controller();
    if (ctrl) {
        system_controller_get_next_capture_at(ctrl, &next_capture_at);
    }
    if (next_capture_at > 0) {
        cJSON_AddNumberToObject(timer_trigger, "next_capture_at", (double)next_capture_at);
    }

    cJSON_AddItemToObject(response, "timer_trigger", timer_trigger);

    // PIR trigger configuration
    cJSON* pir_trigger = cJSON_CreateObject();
    cJSON_AddBoolToObject(pir_trigger, "enable", config.pir_trigger.enable);
    //cJSON_AddNumberToObject(pir_trigger, "pin_number", config.pir_trigger.pin_number);
    cJSON_AddStringToObject(pir_trigger, "trigger_type", get_trigger_type_string(config.pir_trigger.trigger_type));
    // PIR sensor configuration parameters
    cJSON_AddNumberToObject(pir_trigger, "sensitivity_level", config.pir_trigger.sensitivity_level);
    cJSON_AddNumberToObject(pir_trigger, "ignore_time_s", config.pir_trigger.ignore_time_s);
    // Convert register value (0-3) to actual pulse count (1-4) for frontend
    cJSON_AddNumberToObject(pir_trigger, "pulse_count", config.pir_trigger.pulse_count + 1);
    cJSON_AddNumberToObject(pir_trigger, "window_time_s", config.pir_trigger.window_time_s);
    cJSON_AddBoolToObject(pir_trigger, "disable_in_preview", config.pir_trigger.disable_in_preview);
    cJSON_AddItemToObject(response, "pir_trigger", pir_trigger);

    // Remote trigger configuration
    cJSON* remote_trigger = cJSON_CreateObject();
    cJSON_AddBoolToObject(remote_trigger, "enable", config.remote_trigger.enable);
    cJSON_AddItemToObject(response, "remote_trigger", remote_trigger);
    
    // IO triggers array
    // cJSON* io_triggers = cJSON_CreateArray();
    // for (int i = 0; i < IO_TRIGGER_MAX; i++) {
    //     cJSON* io_trigger = cJSON_CreateObject();
    //     cJSON_AddNumberToObject(io_trigger, "id", i);
    //     cJSON_AddNumberToObject(io_trigger, "pin_number", config.io_trigger[i].pin_number);
    //     cJSON_AddBoolToObject(io_trigger, "enable", config.io_trigger[i].enable);
    //     cJSON_AddBoolToObject(io_trigger, "input_enable", config.io_trigger[i].input_enable);
    //     cJSON_AddBoolToObject(io_trigger, "output_enable", config.io_trigger[i].output_enable);
    //     cJSON_AddStringToObject(io_trigger, "input_trigger_type", get_trigger_type_string(config.io_trigger[i].input_trigger_type));
    //     cJSON_AddStringToObject(io_trigger, "output_trigger_type", get_trigger_type_string(config.io_trigger[i].output_trigger_type));
    //     cJSON_AddItemToArray(io_triggers, io_trigger);
    // }
    // cJSON_AddItemToObject(response, "io_trigger", io_triggers);
    //cJSON_AddItemToObject(response, "image_mode", image_mode);
    
    // Add video stream mode configuration
    // cJSON* video_stream_mode = cJSON_CreateObject();
    // cJSON_AddBoolToObject(video_stream_mode, "enable", config.video_stream_mode.enable);
    // cJSON_AddStringToObject(video_stream_mode, "rtsp_server_url", config.video_stream_mode.rtsp_server_url);
    // cJSON_AddItemToObject(response, "video_stream_mode", video_stream_mode);
    
    // Send response
    char* json_string = cJSON_Print(response);
    if (json_string) {
        api_response_success(ctx, json_string, "Work mode triggers retrieved successfully");
        //hal_mem_free(json_string);
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize JSON response");
    }
    
    cJSON_Delete(response);
    return AICAM_OK;
}

/**
 * @brief Configure image mode triggers handler
 */
static aicam_result_t work_mode_triggers_set_handler(http_handler_context_t* ctx) {
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
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        cJSON_Delete(request);
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    // Load current configuration
    work_mode_config_t config;
    aicam_result_t result = system_controller_get_work_config(controller, &config);
    if (result != AICAM_OK) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current configuration");
    }
    
    // Parse configuration from request
    // Parse timer trigger settings
    cJSON* timer_trigger = cJSON_GetObjectItem(request, "timer_trigger");
    if (timer_trigger && cJSON_IsObject(timer_trigger)) {
        cJSON* enable_item = cJSON_GetObjectItem(timer_trigger, "enable");
        config.timer_trigger.enable = (enable_item && cJSON_IsBool(enable_item)) ? cJSON_IsTrue(enable_item) : AICAM_FALSE;
        LOG_SVC_INFO("Timer trigger enable: %d", config.timer_trigger.enable);
        
        cJSON* capture_mode_item = cJSON_GetObjectItem(timer_trigger, "capture_mode");
        if (capture_mode_item && cJSON_IsString(capture_mode_item)) {
            config.timer_trigger.capture_mode = parse_capture_mode(cJSON_GetStringValue(capture_mode_item));
        }
        
        cJSON* interval_item = cJSON_GetObjectItem(timer_trigger, "interval_sec");
        if (interval_item && cJSON_IsNumber(interval_item)) {
            config.timer_trigger.interval_sec = (uint32_t)cJSON_GetNumberValue(interval_item);
        }
        
        cJSON* time_node_count_item = cJSON_GetObjectItem(timer_trigger, "time_node_count");
        if (time_node_count_item && cJSON_IsNumber(time_node_count_item)) {
            config.timer_trigger.time_node_count = (uint32_t)cJSON_GetNumberValue(time_node_count_item);
        }
        
        // Parse time nodes array
        cJSON* time_nodes = cJSON_GetObjectItem(timer_trigger, "time_node");
        if (time_nodes && cJSON_IsArray(time_nodes)) {
            int node_count = cJSON_GetArraySize(time_nodes);
            if (node_count > 10) node_count = 10; // Safety check
            config.timer_trigger.time_node_count = node_count;
            
            for (int i = 0; i < node_count; i++) {
                cJSON* node = cJSON_GetArrayItem(time_nodes, i);
                if (node && cJSON_IsString(node)) {
                    config.timer_trigger.time_node[i] = parse_time_node(cJSON_GetStringValue(node));
                }
            }
        }
        
        // Parse weekdays array
        cJSON* weekdays = cJSON_GetObjectItem(timer_trigger, "weekdays");
        if (weekdays && cJSON_IsArray(weekdays)) {
            int weekday_count = cJSON_GetArraySize(weekdays);
            if (weekday_count > 10) weekday_count = 10; // Safety check
            
            for (int i = 0; i < weekday_count; i++) {
                cJSON* weekday = cJSON_GetArrayItem(weekdays, i);
                if (weekday && cJSON_IsNumber(weekday)) {
                    uint8_t weekday_value = (uint8_t)cJSON_GetNumberValue(weekday);
                    if (weekday_value <= 7) { // 0-7 valid range
                        config.timer_trigger.weekdays[i] = weekday_value;
                    } else {
                        config.timer_trigger.weekdays[i] = 0; // Default to all days
                    }
                } else {
                    config.timer_trigger.weekdays[i] = 0; // Default to all days
                }
            }
        }

        // Parse interval_mode (new field)
        cJSON* interval_mode_item = cJSON_GetObjectItem(timer_trigger, "interval_mode");
        if (interval_mode_item && cJSON_IsString(interval_mode_item)) {
            const char* mode_str = cJSON_GetStringValue(interval_mode_item);
            if (strcmp(mode_str, "scheduled") == 0) {
                config.timer_trigger.interval_mode = AICAM_TIMER_INTERVAL_MODE_SCHEDULED;
            } else {
                config.timer_trigger.interval_mode = AICAM_TIMER_INTERVAL_MODE_NORMAL;
            }
        } else {
            config.timer_trigger.interval_mode = AICAM_TIMER_INTERVAL_MODE_NORMAL;
        }

        // Parse start_time (new field, "HH:MM" format)
        cJSON* start_time_item = cJSON_GetObjectItem(timer_trigger, "start_time");
        if (start_time_item && cJSON_IsString(start_time_item)) {
            config.timer_trigger.start_time = parse_time_node(cJSON_GetStringValue(start_time_item));
        } else if (start_time_item && cJSON_IsNumber(start_time_item)) {
            config.timer_trigger.start_time = (uint32_t)cJSON_GetNumberValue(start_time_item);
        } else {
            config.timer_trigger.start_time = 0;
        }
    }

    // Parse PIR trigger settings
    cJSON* pir_trigger = cJSON_GetObjectItem(request, "pir_trigger");
    if (pir_trigger && cJSON_IsObject(pir_trigger)) {
        cJSON* enable_item = cJSON_GetObjectItem(pir_trigger, "enable");
        config.pir_trigger.enable = (enable_item && cJSON_IsBool(enable_item)) ? cJSON_IsTrue(enable_item) : AICAM_FALSE;
        //config.pir_trigger.pin_number = web_api_get_int(pir_trigger, "pin_number");
        
        // Parse trigger type (wakeup mode)
        cJSON* trigger_type_item = cJSON_GetObjectItem(pir_trigger, "trigger_type");
        if (trigger_type_item) {
            if (cJSON_IsString(trigger_type_item)) {
                config.pir_trigger.trigger_type = parse_trigger_type(cJSON_GetStringValue(trigger_type_item));
            } else if (cJSON_IsNumber(trigger_type_item)) {
                uint32_t trigger_type = (uint32_t)cJSON_GetNumberValue(trigger_type_item);
                if (trigger_type < AICAM_TRIGGER_TYPE_MAX) {
                    config.pir_trigger.trigger_type = (aicam_trigger_type_t)trigger_type;
                }
            }
        }
        
        // Parse PIR sensor configuration parameters
        cJSON* sensitivity_item = cJSON_GetObjectItem(pir_trigger, "sensitivity_level");
        if (sensitivity_item && cJSON_IsNumber(sensitivity_item)) {
            uint8_t sensitivity = (uint8_t)cJSON_GetNumberValue(sensitivity_item);
            if (sensitivity >= 10 && sensitivity <= 255) {
                config.pir_trigger.sensitivity_level = sensitivity;
            }
        }
        
        cJSON* ignore_time_item = cJSON_GetObjectItem(pir_trigger, "ignore_time_s");
        if (ignore_time_item && cJSON_IsNumber(ignore_time_item)) {
            uint8_t ignore_time = (uint8_t)cJSON_GetNumberValue(ignore_time_item);
            if (ignore_time <= 15) {
                config.pir_trigger.ignore_time_s = ignore_time;
            }
        }
        
        cJSON* pulse_count_item = cJSON_GetObjectItem(pir_trigger, "pulse_count");
        if (pulse_count_item && cJSON_IsNumber(pulse_count_item)) {
            uint8_t pulse_count = (uint8_t)cJSON_GetNumberValue(pulse_count_item);
            // Frontend sends 1-4, we store as register value 0-3
            if (pulse_count >= 1 && pulse_count <= 4) {
                config.pir_trigger.pulse_count = pulse_count - 1;
            }
        }
        
        cJSON* window_time_item = cJSON_GetObjectItem(pir_trigger, "window_time_s");
        if (window_time_item && cJSON_IsNumber(window_time_item)) {
            uint8_t window_time = (uint8_t)cJSON_GetNumberValue(window_time_item);
            if (window_time <= 3) {
                config.pir_trigger.window_time_s = window_time;
            }
        }

        // Parse disable_in_preview (default: true)
        cJSON* disable_in_preview_item = cJSON_GetObjectItem(pir_trigger, "disable_in_preview");
        config.pir_trigger.disable_in_preview = (disable_in_preview_item && cJSON_IsBool(disable_in_preview_item))
            ? cJSON_IsTrue(disable_in_preview_item) : AICAM_TRUE;
    }

    // Parse Remote trigger settings
    cJSON* remote_trigger = cJSON_GetObjectItem(request, "remote_trigger");
    if (remote_trigger && cJSON_IsObject(remote_trigger)) {
        cJSON* enable_item = cJSON_GetObjectItem(remote_trigger, "enable");
        config.remote_trigger.enable = (enable_item && cJSON_IsBool(enable_item)) ? cJSON_IsTrue(enable_item) : AICAM_FALSE;
    }
    
    // Parse IO triggers
    cJSON* io_triggers = cJSON_GetObjectItem(request, "io_trigger");
    if (io_triggers && cJSON_IsArray(io_triggers)) {
        int trigger_count = cJSON_GetArraySize(io_triggers);
        for (int i = 0; i < trigger_count && i < IO_TRIGGER_MAX; i++) {
            cJSON* trigger = cJSON_GetArrayItem(io_triggers, i);
            if (trigger && cJSON_IsObject(trigger)) {
                cJSON* id_item = cJSON_GetObjectItem(trigger, "id");
                if (id_item && cJSON_IsNumber(id_item)) {
                    int id = (int)cJSON_GetNumberValue(id_item);
                    if (id >= 0 && id < IO_TRIGGER_MAX) {
                        cJSON* pin_item = cJSON_GetObjectItem(trigger, "pin_number");
                        if (pin_item && cJSON_IsNumber(pin_item)) {
                            config.io_trigger[id].pin_number = (uint32_t)cJSON_GetNumberValue(pin_item);
                        }
                        
                        cJSON* enable_item = cJSON_GetObjectItem(trigger, "enable");
                        config.io_trigger[id].enable = (enable_item && cJSON_IsBool(enable_item)) ? cJSON_IsTrue(enable_item) : AICAM_FALSE;
                        
                        cJSON* input_enable_item = cJSON_GetObjectItem(trigger, "input_enable");
                        config.io_trigger[id].input_enable = (input_enable_item && cJSON_IsBool(input_enable_item)) ? cJSON_IsTrue(input_enable_item) : AICAM_FALSE;
                        
                        cJSON* output_enable_item = cJSON_GetObjectItem(trigger, "output_enable");
                        config.io_trigger[id].output_enable = (output_enable_item && cJSON_IsBool(output_enable_item)) ? cJSON_IsTrue(output_enable_item) : AICAM_FALSE;
                        
                        cJSON* input_type_item = cJSON_GetObjectItem(trigger, "input_trigger_type");
                        if (input_type_item && cJSON_IsNumber(input_type_item)) {
                            config.io_trigger[id].input_trigger_type = (aicam_trigger_type_t)cJSON_GetNumberValue(input_type_item);
                        }
                        
                        cJSON* output_type_item = cJSON_GetObjectItem(trigger, "output_trigger_type");
                        if (output_type_item && cJSON_IsNumber(output_type_item)) {
                            config.io_trigger[id].output_trigger_type = (aicam_trigger_type_t)cJSON_GetNumberValue(output_type_item);
                        }
                    }
                }
            }
        }
    }
    
    
    cJSON_Delete(request);

    //log timer trigger configuration
    LOG_SVC_INFO("Timer trigger configuration: %s", config.timer_trigger.enable ? "enabled" : "disabled");
    LOG_SVC_INFO("Timer trigger capture mode: %d", config.timer_trigger.capture_mode);
    LOG_SVC_INFO("Timer trigger interval: %d", config.timer_trigger.interval_sec);
    LOG_SVC_INFO("Timer trigger time nodes: %d", config.timer_trigger.time_node_count);
    for (int i = 0; i < config.timer_trigger.time_node_count; i++) {
        LOG_SVC_INFO("Timer trigger time node %d: %d", i, config.timer_trigger.time_node[i]);
    }
    for (int i = 1; i < config.timer_trigger.time_node_count; i++) {
        LOG_SVC_INFO("Timer trigger weekdays %d: %d", i, config.timer_trigger.weekdays[i]);
    }
    
    // Log PIR trigger configuration
    LOG_SVC_INFO("PIR trigger configuration: %s", config.pir_trigger.enable ? "enabled" : "disabled");
    LOG_SVC_INFO("PIR sensitivity_level: %u", config.pir_trigger.sensitivity_level);
    LOG_SVC_INFO("PIR ignore_time_s: %u (%.1f seconds)", config.pir_trigger.ignore_time_s, 
                 0.5 + 0.5 * config.pir_trigger.ignore_time_s);
    LOG_SVC_INFO("PIR pulse_count: %u (actual: %u pulses)", config.pir_trigger.pulse_count, 
                 config.pir_trigger.pulse_count + 1);
    LOG_SVC_INFO("PIR window_time_s: %u (%.0f seconds)", config.pir_trigger.window_time_s, 
                 2.0 + 2.0 * config.pir_trigger.window_time_s);
 
    // Update configuration
    result = system_controller_set_work_config(controller, &config);
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "Image mode triggers configured successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to configure image mode triggers");
    }
    
    return AICAM_OK;
}

/**
 * @brief Configure video stream push handler
 */
static aicam_result_t work_mode_video_stream_config_handler(http_handler_context_t* ctx) {
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
    
    // Get and validate system service context
    system_service_context_t* service_ctx = system_service_get_context();
    aicam_result_t validation_result = validate_system_service_context(ctx, service_ctx);
    if (validation_result != AICAM_OK) {
        cJSON_Delete(request);
        return validation_result;
    }
    
    // Get system controller
    system_controller_t* controller = get_system_controller();
    if (!controller) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "System controller not available");
    }

    // Load current configuration
    work_mode_config_t config;
    aicam_result_t result = system_controller_get_work_config(controller, &config);
    if (result != AICAM_OK) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current configuration");
    }
    
    // Parse video stream configuration from request
    cJSON* video_config = cJSON_GetObjectItem(request, "video_stream_mode");
    if (video_config && cJSON_IsObject(video_config)) {
        // Parse enable flag
        cJSON* enable_item = cJSON_GetObjectItem(video_config, "enable");
        config.video_stream_mode.enable = (enable_item && cJSON_IsBool(enable_item)) ? cJSON_IsTrue(enable_item) : AICAM_FALSE;
        
        // Parse RTSP server URL
        cJSON* rtsp_url_item = cJSON_GetObjectItem(video_config, "rtsp_server_url");
        if (rtsp_url_item && cJSON_IsString(rtsp_url_item)) {
            const char* rtsp_server_url = cJSON_GetStringValue(rtsp_url_item);
            if (rtsp_server_url) {
                strncpy(config.video_stream_mode.rtsp_server_url, rtsp_server_url, 
                       sizeof(config.video_stream_mode.rtsp_server_url) - 1);
                config.video_stream_mode.rtsp_server_url[sizeof(config.video_stream_mode.rtsp_server_url) - 1] = '\0';
            }
        }
    }
    
    cJSON_Delete(request);
    
    // Update configuration
    result = system_controller_set_work_config(controller, &config);
    
    if (result == AICAM_OK) {
        api_response_success(ctx, NULL, "Video stream push configuration updated successfully");
    } else {
        api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to update video stream push configuration");
    }
    
    return AICAM_OK;
}

/* ==================== Module Definition ==================== */

// Work mode module routes
static const api_route_t work_mode_module_routes[] = {
    {
        .path = API_PATH_PREFIX "/work-mode/status",
        .method = "GET",
        .handler = work_mode_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/work-mode/switch",
        .method = "POST",
        .handler = work_mode_switch_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/work-mode/triggers",
        .method = "GET",
        .handler = work_mode_triggers_get_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/work-mode/triggers",
        .method = "POST",
        .handler = work_mode_triggers_set_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/work-mode/video-stream/config",
        .method = "POST",
        .handler = work_mode_video_stream_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/power-mode/status",
        .method = "GET",
        .handler = power_mode_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/power-mode/switch",
        .method = "POST",
        .handler = power_mode_switch_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/power-mode/config",
        .method = "GET",
        .handler = power_mode_config_get_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/power-mode/config",
        .method = "POST",
        .handler = power_mode_config_set_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};


/* ==================== Public API ==================== */

/**
 * @brief Register work mode and power mode module
 */
aicam_result_t web_api_register_work_mode_module(void) {
    LOG_CORE_INFO("Registering work mode and power mode module");
    
    for (int i = 0; i < sizeof(work_mode_module_routes) / sizeof(work_mode_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&work_mode_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to register work mode and power mode module: %d", result);
            return result;
        }
    }
    
    LOG_CORE_INFO("Work mode and power mode module registered successfully");
    return AICAM_OK;
}
