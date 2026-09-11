/**
 * @file api_preview_module.c
 * @brief Preview API Module Implementation
 * @details Video preview service API module for WebSocket streaming control
 */

#include "api_preview_module.h"
#include "websocket_stream_server.h"
#include "Services/Video/video_stream_hub.h"
#include "Services/AI/ai_service.h"
#include "web_api.h"
#include "web_server.h"
#include "cJSON.h"
#include <string.h>
#include "debug.h"

/* ==================== Preview API Handlers ==================== */

/**
 * @brief Get preview status handler
 * GET /api/v1/preview/status
 */
static aicam_result_t preview_status_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    cJSON *response = cJSON_CreateObject();
    if (!response) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Preview status
    cJSON *preview = cJSON_CreateObject();
    websocket_stream_stats_t ws_stats;
    aicam_bool_t ws_active = AICAM_FALSE;
    
    if (websocket_stream_server_get_stats(&ws_stats) == AICAM_OK) {
        ws_active = ws_stats.stream_active;
    }
    
    cJSON_AddBoolToObject(preview, "active", ws_active);
    cJSON_AddNumberToObject(preview, "connected_clients", ws_stats.active_clients);
    cJSON_AddNumberToObject(preview, "fps", ws_stats.stream_fps);
    cJSON_AddNumberToObject(preview, "total_frames_sent", (double)ws_stats.total_frames_sent);
    cJSON_AddNumberToObject(preview, "total_bytes_sent", (double)ws_stats.total_bytes_sent);
    cJSON_AddItemToObject(response, "preview", preview);
    
    // Pipeline status
    cJSON *pipeline = cJSON_CreateObject();
    cJSON_AddBoolToObject(pipeline, "initialized", ai_pipeline_is_initialized());
    cJSON_AddBoolToObject(pipeline, "running", ai_pipeline_is_running());
    cJSON_AddItemToObject(response, "pipeline", pipeline);
    
    // Hub status
    cJSON *hub = cJSON_CreateObject();
    cJSON_AddBoolToObject(hub, "initialized", video_hub_is_initialized());
    cJSON_AddNumberToObject(hub, "subscriber_count", video_hub_get_subscriber_count());
    cJSON_AddItemToObject(response, "hub", hub);
    
    json_string = cJSON_Print(response);
    cJSON_Delete(response);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t result = api_response_success(ctx, json_string, "Preview status retrieved");
    return result;
}

/**
 * @brief Start preview handler
 * POST /api/v1/preview/start
 */
static aicam_result_t preview_start_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Check if WebSocket server is initialized
    if (!websocket_stream_server_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "WebSocket server not initialized");
    }
    
    // Check if already active
    websocket_stream_stats_t stats;
    if (websocket_stream_server_get_stats(&stats) == AICAM_OK && stats.stream_active) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", 1);
        cJSON_AddStringToObject(response, "status", "already_active");
        cJSON_AddBoolToObject(response, "pipeline_running", ai_pipeline_is_running());
        
        json_string = cJSON_Print(response);
        cJSON_Delete(response);
        
        return api_response_success(ctx, json_string, "Preview already active");
    }
    
    // Start stream (this will subscribe to Hub, which auto-starts pipeline)
    aicam_result_t result = websocket_stream_server_start_stream(1);
    
    cJSON *response = cJSON_CreateObject();
    
    if (result == AICAM_OK) {
        cJSON_AddBoolToObject(response, "success", 1);
        cJSON_AddStringToObject(response, "status", "started");
        cJSON_AddBoolToObject(response, "pipeline_running", ai_pipeline_is_running());
        cJSON_AddNumberToObject(response, "hub_subscribers", video_hub_get_subscriber_count());
        
        json_string = cJSON_Print(response);
        cJSON_Delete(response);
        
        LOG_SVC_INFO("Preview started via API");
        return api_response_success(ctx, json_string, "Preview started successfully");
    } else {
        cJSON_Delete(response);
        LOG_SVC_ERROR("Failed to start preview: %d", result);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to start preview");
    }
}

/**
 * @brief Stop preview handler
 * POST /api/v1/preview/stop
 */
static aicam_result_t preview_stop_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Check if WebSocket server is initialized
    if (!websocket_stream_server_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "WebSocket server not initialized");
    }
    
    // Check if not active
    websocket_stream_stats_t stats;
    if (websocket_stream_server_get_stats(&stats) == AICAM_OK && !stats.stream_active) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", 1);
        cJSON_AddStringToObject(response, "status", "already_stopped");
        cJSON_AddBoolToObject(response, "pipeline_running", ai_pipeline_is_running());
        
        json_string = cJSON_Print(response);
        cJSON_Delete(response);
        
        return api_response_success(ctx, json_string, "Preview already stopped");
    }
    
    // Stop stream (this will unsubscribe from Hub, which may stop pipeline if no other subscribers)
    aicam_result_t result = websocket_stream_server_stop_stream();
    
    cJSON *response = cJSON_CreateObject();
    
    if (result == AICAM_OK) {
        cJSON_AddBoolToObject(response, "success", 1);
        cJSON_AddStringToObject(response, "status", "stopped");
        cJSON_AddBoolToObject(response, "pipeline_running", ai_pipeline_is_running());
        cJSON_AddNumberToObject(response, "hub_subscribers", video_hub_get_subscriber_count());
        
        json_string = cJSON_Print(response);
        cJSON_Delete(response);
        
        LOG_SVC_INFO("Preview stopped via API");
        return api_response_success(ctx, json_string, "Preview stopped successfully");
    } else {
        cJSON_Delete(response);
        LOG_SVC_ERROR("Failed to stop preview: %d", result);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to stop preview");
    }
}

/* ==================== API Route Registration ==================== */

static const api_route_t preview_routes[] = {
    {
        .path = API_PATH_PREFIX "/preview/status",
        .method = "GET",
        .handler = preview_status_handler,
        .require_auth = AICAM_FALSE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/preview/start",
        .method = "POST",
        .handler = preview_start_handler,
        .require_auth = AICAM_FALSE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/preview/stop",
        .method = "POST",
        .handler = preview_stop_handler,
        .require_auth = AICAM_FALSE,
        .user_data = NULL
    }
};

aicam_result_t web_api_register_preview_module(void)
{
    LOG_SVC_INFO("Registering Preview API module...");
    
    size_t route_count = sizeof(preview_routes) / sizeof(preview_routes[0]);
    for (size_t i = 0; i < route_count; i++) {
        aicam_result_t result = http_server_register_route(&preview_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register preview route: %s", preview_routes[i].path);
            return result;
        }
    }
    
    LOG_SVC_INFO("Preview API module registered (%zu routes)", route_count);
    return AICAM_OK;
}

