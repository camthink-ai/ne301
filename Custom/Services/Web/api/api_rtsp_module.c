/**
 * @file api_rtsp_module.c
 * @brief RTSP API Module - Configuration and status for RTSP server
 */

#include "api_rtsp_module.h"
#include "web_api.h"
#include "web_server.h"
#include "cJSON.h"
#include "json_config_mgr.h"
#include "rtsp_service.h"
#include "cmsis_os2.h"
#include <string.h>
#include "lwip/inet.h"
#include "debug.h"

/* ==================== RTSP API Handlers ==================== */

/**
 * @brief Get RTSP configuration
 * GET /api/v1/apps/rtsp/config
 */
static aicam_result_t rtsp_config_get_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);

    cJSON *response = cJSON_CreateObject();
    if (!response) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    cJSON_AddBoolToObject(response, "enabled", vs_config.rtsp_enable);
    cJSON_AddNumberToObject(response, "port", vs_config.rtsp_port);
    cJSON_AddStringToObject(response, "auth_mode", vs_config.rtsp_auth_mode[0] ? vs_config.rtsp_auth_mode : "none");
    cJSON_AddStringToObject(response, "username", vs_config.rtsp_username[0] ? vs_config.rtsp_username : "");
    cJSON_AddStringToObject(response, "password", vs_config.rtsp_password[0] ? vs_config.rtsp_password : "");

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "RTSP config retrieved");
}

/**
 * @brief Set RTSP configuration
 * PUT /api/v1/apps/rtsp/config
 * Body: { "enabled": true, "port": 554, "auth_mode": "none", "username": "", "password": "" }
 */
static aicam_result_t rtsp_config_set_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "PUT")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON *request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    /* Get current config */
    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);

    aicam_bool_t was_enabled = vs_config.rtsp_enable;

    /* Snapshot old values before modification */
    uint16_t old_port = vs_config.rtsp_port;
    char old_auth_mode[sizeof(vs_config.rtsp_auth_mode)];
    char old_username[sizeof(vs_config.rtsp_username)];
    char old_password[sizeof(vs_config.rtsp_password)];
    strncpy(old_auth_mode, vs_config.rtsp_auth_mode, sizeof(old_auth_mode));
    strncpy(old_username, vs_config.rtsp_username, sizeof(old_username));
    strncpy(old_password, vs_config.rtsp_password, sizeof(old_password));

    /* Update fields if provided */
    cJSON *enabled = cJSON_GetObjectItem(request, "enabled");
    if (enabled && cJSON_IsBool(enabled)) {
        vs_config.rtsp_enable = cJSON_IsTrue(enabled) ? AICAM_TRUE : AICAM_FALSE;
        /* Mutual exclusion: enabling RTSP disables RTMP */
        if (vs_config.rtsp_enable) {
            vs_config.rtmp_enable = AICAM_FALSE;
        }
    }

    cJSON *port = cJSON_GetObjectItem(request, "port");
    if (port && cJSON_IsNumber(port)) {
        int p = port->valueint;
        if (p < 1 || p > 65535) {
            cJSON_Delete(request);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Port must be 1-65535");
        }
        vs_config.rtsp_port = (uint16_t)p;
    }

    cJSON *auth_mode = cJSON_GetObjectItem(request, "auth_mode");
    if (auth_mode && cJSON_IsString(auth_mode)) {
        strncpy(vs_config.rtsp_auth_mode, auth_mode->valuestring, sizeof(vs_config.rtsp_auth_mode) - 1);
        vs_config.rtsp_auth_mode[sizeof(vs_config.rtsp_auth_mode) - 1] = '\0';
    }

    cJSON *username = cJSON_GetObjectItem(request, "username");
    if (username && cJSON_IsString(username)) {
        strncpy(vs_config.rtsp_username, username->valuestring, sizeof(vs_config.rtsp_username) - 1);
        vs_config.rtsp_username[sizeof(vs_config.rtsp_username) - 1] = '\0';
    }

    cJSON *password = cJSON_GetObjectItem(request, "password");
    if (password && cJSON_IsString(password)) {
        /* Skip if masked password sent back */
        if (strcmp(password->valuestring, "******") != 0) {
            strncpy(vs_config.rtsp_password, password->valuestring, sizeof(vs_config.rtsp_password) - 1);
            vs_config.rtsp_password[sizeof(vs_config.rtsp_password) - 1] = '\0';
        }
    }

    cJSON_Delete(request);

    /* Save to NVS */
    aicam_result_t result = json_config_set_video_stream_mode(&vs_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save config");
    }

    /* Start/stop RTSP service based on enabled state */
    if (vs_config.rtsp_enable && !was_enabled) {
        rtsp_service_start();
    } else if (!vs_config.rtsp_enable && was_enabled) {
        rtsp_service_stop();
    } else if (vs_config.rtsp_enable && was_enabled) {
        /* Already running - check if runtime config changed (port, auth) */
        aicam_bool_t config_changed =
            (vs_config.rtsp_port != old_port) ||
            (strncmp(vs_config.rtsp_auth_mode, old_auth_mode, sizeof(vs_config.rtsp_auth_mode)) != 0) ||
            (strncmp(vs_config.rtsp_username, old_username, sizeof(vs_config.rtsp_username)) != 0) ||
            (strncmp(vs_config.rtsp_password, old_password, sizeof(vs_config.rtsp_password)) != 0);

        if (config_changed) {
            LOG_SVC_INFO("RTSP: runtime config changed, restarting service");
            rtsp_service_stop();
            rtsp_service_start();
        }
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "enabled", vs_config.rtsp_enable);
    cJSON_AddNumberToObject(response, "port", vs_config.rtsp_port);
    cJSON_AddStringToObject(response, "auth_mode", vs_config.rtsp_auth_mode);
    cJSON_AddStringToObject(response, "message", "RTSP config updated");

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    LOG_SVC_INFO("RTSP config updated via API: enabled=%d, port=%lu, auth=%s",
                 vs_config.rtsp_enable, (unsigned long)vs_config.rtsp_port, vs_config.rtsp_auth_mode);
    return api_response_success(ctx, json_str, "RTSP config updated");
}

/**
 * @brief Get RTSP service status
 * GET /api/v1/apps/rtsp/status
 */
static aicam_result_t rtsp_status_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);

    aicam_bool_t running = rtsp_service_is_running();

    /* Build stream URL using the device's own IP */
    char stream_url[256] = {0};
    if (running) {
        char device_ip[64];
        rtsp_service_get_device_ip(device_ip, sizeof(device_ip));
        if (device_ip[0]) {
            snprintf(stream_url, sizeof(stream_url),
                     "rtsp://%s:%lu/live/stream",
                     device_ip, (unsigned long)vs_config.rtsp_port);
        } else {
            snprintf(stream_url, sizeof(stream_url),
                     "rtsp://device:%lu/live/stream",
                     (unsigned long)vs_config.rtsp_port);
        }
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "running", running);
    cJSON_AddStringToObject(response, "stream_url", stream_url);

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    return api_response_success(ctx, json_str, "RTSP status retrieved");
}

/**
 * @brief Get active RTSP clients
 * GET /api/v1/apps/rtsp/clients
 */
static aicam_result_t rtsp_clients_get_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    uint32_t count = 0;
    rtsp_service_get_clients(clients, &count);

    cJSON *response = cJSON_CreateArray();
    for (uint32_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", clients[i].session_id);
        cJSON_AddStringToObject(item, "ip", inet_ntoa(clients[i].client_addr.sin_addr));
        cJSON_AddNumberToObject(item, "port", ntohs(clients[i].client_addr.sin_port));

        uint32_t elapsed_ms = (osKernelGetTickCount() - clients[i].connected_time) * 10 / 1000;
        char duration[32];
        snprintf(duration, sizeof(duration), "%lus", (unsigned long)elapsed_ms);
        cJSON_AddStringToObject(item, "connected_duration", duration);

        cJSON_AddItemToArray(response, item);
    }

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    return api_response_success(ctx, json_str, "RTSP clients retrieved");
}

/**
 * @brief Kick an RTSP client
 * DELETE /api/v1/apps/rtsp/clients/:id
 */
static aicam_result_t rtsp_client_kick_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "DELETE")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    /* Extract session ID from URL path */
    const char *uri = ctx->request.uri;
    const char *prefix = "/api/v1/apps/rtsp/clients/";
    const char *id_start = strstr(uri, prefix);
    if (id_start) {
        id_start += strlen(prefix);
        char session_id[64] = {0};
        strncpy(session_id, id_start, sizeof(session_id) - 1);
        rtsp_service_kick_client(session_id);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", 1);
    cJSON_AddStringToObject(response, "message", "Client kicked");

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);

    return api_response_success(ctx, json_str, "Client kicked");
}

/* ==================== API Module Registration ==================== */

aicam_result_t web_api_register_rtsp_module(void)
{
    api_route_t routes[] = {
        { .path = API_PATH_PREFIX"/apps/rtsp/config",  .method = "GET",    .handler = rtsp_config_get_handler,   .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtsp/config",  .method = "PUT",    .handler = rtsp_config_set_handler,   .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtsp/status",  .method = "GET",    .handler = rtsp_status_handler,       .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtsp/clients", .method = "GET",    .handler = rtsp_clients_get_handler,  .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/apps/rtsp/clients/*", .method = "DELETE", .handler = rtsp_client_kick_handler, .require_auth = AICAM_TRUE }
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register RTSP route: %s", routes[i].path);
            return result;
        }
    }

    LOG_SVC_INFO("RTSP API module registered");
    return AICAM_OK;
}
