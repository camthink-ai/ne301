/**
 * @file api_rtmp_module.c
 * @brief RTMP API Module - Simplified configuration and streaming control
 */

#include "api_rtmp_module.h"
#include "rtmp_service.h"
#include "rtsp_service.h"
#include "web_api.h"
#include "web_server.h"
#include "cJSON.h"
#include "json_config_mgr.h"
#include <string.h>
#include "debug.h"

/* ==================== RTMP API Handlers ==================== */

/**
 * @brief Get RTMP configuration and status
 * GET /api/v1/apps/rtmp/config
 */
static aicam_result_t rtmp_config_get_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    // Get video stream mode config (includes RTMP)
    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);

    cJSON *response = cJSON_CreateObject();
    if (!response) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    // RTMP configuration
    cJSON *config = cJSON_CreateObject();
    cJSON_AddBoolToObject(config, "enable", vs_config.rtmp_enable);
    cJSON_AddStringToObject(config, "url", vs_config.rtmp_url[0] ? vs_config.rtmp_url : "");
    cJSON_AddStringToObject(config, "stream_key", vs_config.rtmp_stream_key[0] ? vs_config.rtmp_stream_key : "");
    cJSON_AddItemToObject(response, "config", config);

    // Service status
    cJSON *status = cJSON_CreateObject();
    cJSON_AddBoolToObject(status, "initialized", rtmp_service_is_initialized());
    cJSON_AddBoolToObject(status, "streaming", rtmp_service_is_streaming());
    cJSON_AddStringToObject(status, "state", rtmp_stream_state_to_string(rtmp_service_get_stream_state()));
    cJSON_AddItemToObject(response, "status", status);

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "RTMP config retrieved");
}

/**
 * @brief Set RTMP configuration
 * POST /api/v1/apps/rtmp/config
 * Body: { "enable": true, "url": "rtmp://...", "stream_key": "xxx" }
 */
static aicam_result_t rtmp_config_set_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON *request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    cJSON *url = cJSON_GetObjectItem(request, "url");
    cJSON *stream_key = cJSON_GetObjectItem(request, "stream_key");

    /* url/stream_key feed the running stream and cannot be changed while
     * streaming. The enable flag only arms the boot-time auto-start, so the
     * web toggle stays usable mid-stream. */
    if (rtmp_service_is_streaming()
        && ((url && cJSON_IsString(url)) || (stream_key && cJSON_IsString(stream_key)))) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Cannot change config while streaming");
    }

    // Get current config
    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);

    // Update fields if provided. The web UI posts the flag as "enabled"
    // (RTSP-API naming); the documented field is "enable" — accept both,
    // otherwise the enable bit is silently skipped and never reaches NVS.
    cJSON *enable = cJSON_GetObjectItem(request, "enable");
    if (!enable) {
        enable = cJSON_GetObjectItem(request, "enabled");
    }
    if (enable && cJSON_IsBool(enable)) {
        vs_config.rtmp_enable = cJSON_IsTrue(enable) ? AICAM_TRUE : AICAM_FALSE;
        /* Mutual exclusion: enabling RTMP disables RTSP */
        if (vs_config.rtmp_enable) {
            if (vs_config.rtsp_enable) {
                vs_config.rtsp_enable = AICAM_FALSE;
                rtsp_service_stop();
            }
        }
    }

    if (url && cJSON_IsString(url)) {
        strncpy(vs_config.rtmp_url, url->valuestring, sizeof(vs_config.rtmp_url) - 1);
        vs_config.rtmp_url[sizeof(vs_config.rtmp_url) - 1] = '\0';
    }

    if (stream_key && cJSON_IsString(stream_key)) {
        strncpy(vs_config.rtmp_stream_key, stream_key->valuestring, sizeof(vs_config.rtmp_stream_key) - 1);
        vs_config.rtmp_stream_key[sizeof(vs_config.rtmp_stream_key) - 1] = '\0';
    }

    cJSON_Delete(request);

    LOG_SVC_DEBUG("RTMP config updated: enable=%d, url=%s, stream_key=%s\r\n", vs_config.rtmp_enable, vs_config.rtmp_url, vs_config.rtmp_stream_key);
    

    // Save to NVS
    aicam_result_t result = json_config_set_video_stream_mode(&vs_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save config");
    }

    // Update RTMP service runtime config
    if (rtmp_service_is_initialized()) {
        rtmp_service_set_url(vs_config.rtmp_url);
        rtmp_service_set_stream_key(vs_config.rtmp_stream_key);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "RTMP config updated");

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    LOG_SVC_INFO("RTMP config updated via API");
    return api_response_success(ctx, json_str, "RTMP config updated");
}

/**
 * @brief Start RTMP streaming
 * POST /api/v1/apps/rtmp/start
 * Optional body: { "url": "rtmp://...", "stream_key": "xxx" }
 */
static aicam_result_t rtmp_start_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!rtmp_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "RTMP service not initialized");
    }

    if (rtmp_service_is_streaming()) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "already_streaming");
        char *json_str = cJSON_Print(response);
        cJSON_Delete(response);
        return api_response_success(ctx, json_str, "Already streaming");
    }

    // Optional: override URL/stream_key from request body
    if (ctx->request.content_length > 0 && ctx->request.body) {
        cJSON *request = web_api_parse_body(ctx);
        if (request) {
            cJSON *url = cJSON_GetObjectItem(request, "url");
            if (url && cJSON_IsString(url)) {
                rtmp_service_set_url(url->valuestring);
            }
            cJSON *key = cJSON_GetObjectItem(request, "stream_key");
            if (key && cJSON_IsString(key)) {
                rtmp_service_set_stream_key(key->valuestring);
            }
            cJSON_Delete(request);
        }
    }

    aicam_result_t result = rtmp_service_start_stream();

    cJSON *response = cJSON_CreateObject();
    if (result == AICAM_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "started");
        LOG_SVC_INFO("RTMP stream started via API");
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "failed");
        cJSON_AddNumberToObject(response, "error_code", result);
    }
    cJSON_AddStringToObject(response, "state", rtmp_stream_state_to_string(rtmp_service_get_stream_state()));

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    return api_response_success(ctx, json_str, result == AICAM_OK ? "Stream started" : "Start failed");
}

/**
 * @brief Stop RTMP streaming
 * POST /api/v1/apps/rtmp/stop
 */
static aicam_result_t rtmp_stop_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!rtmp_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "RTMP service not initialized");
    }

    if (!rtmp_service_is_streaming()) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "already_stopped");
        char *json_str = cJSON_Print(response);
        cJSON_Delete(response);
        return api_response_success(ctx, json_str, "Already stopped");
    }

    aicam_result_t result = rtmp_service_stop_stream();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response, "status", result == AICAM_OK ? "stopped" : "failed");
    cJSON_AddStringToObject(response, "state", rtmp_stream_state_to_string(rtmp_service_get_stream_state()));

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    LOG_SVC_INFO("RTMP stream stopped via API");
    return api_response_success(ctx, json_str, result == AICAM_OK ? "Stream stopped" : "Stop failed");
}

/**
 * @brief Get RTMP status and statistics
 * GET /api/v1/apps/rtmp/status
 */
static aicam_result_t rtmp_status_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!rtmp_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "RTMP service not initialized");
    }

    rtmp_service_stats_t stats;
    rtmp_service_get_stats(&stats);

    cJSON *response = cJSON_CreateObject();

    // Status
    cJSON *status = cJSON_CreateObject();
    cJSON_AddBoolToObject(status, "streaming", rtmp_service_is_streaming());
    cJSON_AddStringToObject(status, "state", rtmp_stream_state_to_string(rtmp_service_get_stream_state()));
    cJSON_AddItemToObject(response, "status", status);

    // Statistics
    cJSON *statistics = cJSON_CreateObject();
    cJSON_AddNumberToObject(statistics, "frames_sent", (double)stats.frames_sent);
    cJSON_AddNumberToObject(statistics, "bytes_sent", (double)stats.bytes_sent);
    cJSON_AddNumberToObject(statistics, "keyframes_sent", (double)stats.keyframes_sent);
    cJSON_AddNumberToObject(statistics, "dropped_frames", (double)stats.dropped_frames);
    cJSON_AddNumberToObject(statistics, "reconnect_count", stats.reconnect_count);
    cJSON_AddNumberToObject(statistics, "stream_duration_sec", stats.stream_duration_sec);
    cJSON_AddItemToObject(response, "statistics", statistics);

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    return api_response_success(ctx, json_str, "RTMP status retrieved");
}

/* ==================== API Module Registration ==================== */

aicam_result_t web_api_register_rtmp_module(void)
{
    api_route_t routes[] = {
        { .path = API_PATH_PREFIX"/apps/rtmp/config", .method = "GET",  .handler = rtmp_config_get_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtmp/config", .method = "POST", .handler = rtmp_config_set_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtmp/start",  .method = "POST", .handler = rtmp_start_handler,      .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtmp/stop",   .method = "POST", .handler = rtmp_stop_handler,       .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtmp/status", .method = "GET",  .handler = rtmp_status_handler,     .require_auth = AICAM_TRUE }
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register RTMP route: %s", routes[i].path);
            return result;
        }
    }

    LOG_SVC_INFO("RTMP API module registered");
    return AICAM_OK;
}
