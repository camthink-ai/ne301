#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_session_module.h"
#include "api_app_install_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_installer.h"
#include "app_host_session.h"
#include "app_host_session_device.h"

static aicam_result_t app_session_start_handler(http_handler_context_t *ctx);
static aicam_result_t app_session_stop_handler(http_handler_context_t *ctx);
static aicam_result_t app_session_status_handler(http_handler_context_t *ctx);
static void app_session_add_identity(cJSON *root, const app_install_record_t *record);

static const api_route_t g_app_session_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/session/start",
        .handler = app_session_start_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/session/stop",
        .handler = app_session_stop_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/session/status",
        .handler = app_session_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static void app_session_add_running(cJSON *root)
{
    cJSON_AddBoolToObject(root, "app_running",
                          (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
}

static void app_session_add_identity(cJSON *root, const app_install_record_t *record)
{
    cJSON_AddStringToObject(root, "publisher_id", record->publisher_id);
    cJSON_AddStringToObject(root, "app_id", record->app_id);
    cJSON_AddNumberToObject(root, "version_major", (double)record->version_major);
    cJSON_AddNumberToObject(root, "version_minor", (double)record->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", (double)record->version_patch);
}

aicam_result_t app_session_start_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *root = NULL;
    app_host_session_start_out_t out;
    app_session_result_t result;
    char caps_text[16];

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app session: start refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&out, 0, sizeof(out));
    result = app_host_session_start(io, &out);

    if (result == APP_SESSION_OK) {
        LOG_SVC_INFO("app session: started publisher=%s app=%s v%u.%u.%u abi=0x%08x caps=0x%08x event_max=%u",
                     out.record.publisher_id, out.record.app_id,
                     (unsigned int)out.record.version_major,
                     (unsigned int)out.record.version_minor,
                     (unsigned int)out.record.version_patch,
                     (unsigned int)out.abi_version,
                     (unsigned int)out.granted_caps,
                     (unsigned int)out.event_max);
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "started");
        app_session_add_identity(root, &out.record);
        app_install_response_add_product_version(root, app_installer_device_io(),
                                                  &out.record);
        cJSON_AddNumberToObject(root, "abi_version", (double)out.abi_version);
        snprintf(caps_text, sizeof(caps_text), "0x%08x", (unsigned int)out.granted_caps);
        cJSON_AddStringToObject(root, "granted_caps", caps_text);
        cJSON_AddNumberToObject(root, "event_max", (double)out.event_max);
        app_session_add_running(root);
    } else {
        if (result == APP_SESSION_ERR_STORAGE_STATE_UNKNOWN) {
            LOG_SVC_ERROR("app session: start storage state UNKNOWN, fail-closed, no execution");
        } else {
            LOG_SVC_WARN("app session: start refused result=%s reason=%s",
                         app_session_result_str(result), out.reason);
        }
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddStringToObject(root, "reason", app_session_result_str(result));
        if (out.reason[0] != '\0' &&
            strcmp(out.reason, app_session_result_str(result)) != 0) {
            cJSON_AddStringToObject(root, "detail", out.reason);
        }
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_session_add_running(root);
    }

    return api_response_json(ctx, root, "App session start completed with result");
}

aicam_result_t app_session_stop_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;
    app_host_session_stop_out_t out;
    app_session_result_t result;
    char *log_text;
    size_t log_len;
    aicam_bool_t log_truncated;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app session: stop refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    memset(&out, 0, sizeof(out));
    result = app_host_session_stop(0u, &out);
    log_truncated = app_host_session_log_truncated();

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    if (result == APP_SESSION_OK) {
        LOG_SVC_INFO("app session: stopped cooperatively, entry returned");
        cJSON_AddStringToObject(root, "status", "stopped");
        cJSON_AddBoolToObject(root, "entry_returned", AICAM_TRUE);
    } else if (result == APP_SESSION_ERR_BUSY) {
        LOG_SVC_WARN("app session: stop requested but entry not returned within bound, BUSY, image preserved");
        cJSON_AddStringToObject(root, "status", "stopping");
        cJSON_AddBoolToObject(root, "entry_returned", AICAM_FALSE);
        cJSON_AddBoolToObject(root, "busy", AICAM_TRUE);
    } else {
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddBoolToObject(root, "entry_returned", AICAM_FALSE);
    }
    if (out.reason[0] != '\0') {
        cJSON_AddStringToObject(root, "reason", out.reason);
    }
    app_install_response_add_current_product_version(root, app_installer_device_io());
    log_text = (char *)malloc(APP_SESSION_LOG_CAP + 1u);
    if (log_text != NULL) {
        log_len = app_host_session_take_log(log_text, APP_SESSION_LOG_CAP + 1u);
        log_text[log_len] = '\0';
        cJSON_AddStringToObject(root, "log", log_text);
        free(log_text);
    }
    cJSON_AddBoolToObject(root, "log_truncated",
                          (log_truncated == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    app_session_add_running(root);

    return api_response_json(ctx, root, "App session stop completed with result");
}

aicam_result_t app_session_status_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;
    cJSON *model_json = NULL;
    cJSON *counters_json = NULL;
    app_host_session_status_t status;
    char caps_text[16];

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app session: status refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    memset(&status, 0, sizeof(status));
    app_host_session_status(&status);

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(root, "state", app_session_state_str(status.state));
    app_install_response_add_current_product_version(root, app_installer_device_io());
    if (status.publisher_id[0] != '\0' || status.app_id[0] != '\0') {
        cJSON_AddStringToObject(root, "publisher_id", status.publisher_id);
        cJSON_AddStringToObject(root, "app_id", status.app_id);
        cJSON_AddNumberToObject(root, "version_major", (double)status.version_major);
        cJSON_AddNumberToObject(root, "version_minor", (double)status.version_minor);
        cJSON_AddNumberToObject(root, "version_patch", (double)status.version_patch);
    }
    app_session_add_running(root);
    cJSON_AddNumberToObject(root, "abi_version", (double)status.abi_version);
    snprintf(caps_text, sizeof(caps_text), "0x%08x", (unsigned int)status.granted_caps);
    cJSON_AddStringToObject(root, "granted_caps", caps_text);
    cJSON_AddNumberToObject(root, "event_max", (double)status.event_max);

    model_json = cJSON_CreateObject();
    if (model_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(model_json, "model_name", status.model_name);
    cJSON_AddStringToObject(model_json, "model_version", status.model_version);
    cJSON_AddNumberToObject(model_json, "model_generation", (double)status.model_generation);
    cJSON_AddNumberToObject(model_json, "class_generation", (double)status.class_generation);
    cJSON_AddNumberToObject(model_json, "class_count", (double)status.class_count);
    cJSON_AddItemToObject(root, "model", model_json);

    counters_json = cJSON_CreateObject();
    if (counters_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddNumberToObject(counters_json, "events_delivered", (double)status.events_delivered);
    cJSON_AddNumberToObject(counters_json, "frames_dropped", (double)status.frames_dropped);
    cJSON_AddNumberToObject(counters_json, "gap_events", (double)status.gap_events);
    cJSON_AddNumberToObject(counters_json, "model_changes", (double)status.model_changes);
    cJSON_AddItemToObject(root, "counters", counters_json);

    cJSON_AddStringToObject(root, "last_result", app_session_result_str(status.last_result));
    if (status.last_reason[0] != '\0') {
        cJSON_AddStringToObject(root, "last_reason", status.last_reason);
    }

    return api_response_json(ctx, root, "App session status");
}

aicam_result_t web_api_register_app_session_module(void)
{
    size_t count = sizeof(g_app_session_routes) / sizeof(g_app_session_routes[0]);
    size_t i;
    aicam_result_t result;

    LOG_SVC_INFO("Registering App Session API module...");

    app_host_session_init(app_host_session_device_backend());

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_session_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_session_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("App Session API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
