#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_session_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "mm_hal_common.h"
#include "mmhal_core.h"
#include "aicam_types.h"

#include "app_installer.h"
#include "app_host.h"
#include "app_host_session.h"
#include "app_host_session_device.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "app_admission.h"
#include "app_sidecar_lifecycle.h"
#include "app_pair_installer.h"
#include "app_report_policy.h"
#include "app_report_service.h"
#include "app_report_service_device.h"
#include "web_assets.h"
#include "websocket_stream_server.h"
#include "cmsis_os2.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "ai_service.h"

#include <stdlib.h>
#include <time.h>

#if defined(STM32N657xx)
#include "stm32n6xx_hal.h"
#endif

static aicam_result_t app_session_start_handler(http_handler_context_t *ctx);
static aicam_result_t app_session_stop_handler(http_handler_context_t *ctx);
static aicam_result_t app_session_status_handler(http_handler_context_t *ctx);
static aicam_result_t app_report_service_handler(http_handler_context_t *ctx);
static void app_session_add_identity(cJSON *root, const app_install_record_t *record);
static aicam_result_t app_sidecar_ui_handler(http_handler_context_t *ctx);
static aicam_result_t app_bridge_grant_handler(http_handler_context_t *ctx);
static aicam_result_t app_bridge_call_handler(http_handler_context_t *ctx);
static aicam_result_t app_bridge_confirm_handler(http_handler_context_t *ctx);
static aicam_result_t app_bridge_revoke_handler(http_handler_context_t *ctx);
static cJSON *app_bridge_dispatch_enable_read(uint8_t slot, cJSON *body);
static cJSON *app_bridge_dispatch_enable_apply(uint8_t slot, cJSON *body, uint32_t request_id);
static cJSON *app_bridge_dispatch_report_policy_read(uint8_t slot, cJSON *body);
static cJSON *app_bridge_dispatch_report_policy_apply(uint8_t slot, cJSON *body);
static cJSON *app_bridge_dispatch_live_subscribe(uint8_t slot, cJSON *body);
static cJSON *app_bridge_dispatch_live_unsubscribe(uint8_t slot, cJSON *body);

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
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/report/service",
        .handler = app_report_service_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/sidecar/ui",
        .handler = app_sidecar_ui_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/sidecar/bridge/grant",
        .handler = app_bridge_grant_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/sidecar/bridge",
        .handler = app_bridge_call_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/sidecar/bridge/confirm",
        .handler = app_bridge_confirm_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/sidecar/bridge/revoke",
        .handler = app_bridge_revoke_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static aicam_result_t app_session_respond_json(http_handler_context_t *ctx, cJSON *root,
                                               const char *message)
{
    char *response_text;
    aicam_result_t rc;

    response_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response_text == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    rc = api_response_success(ctx, response_text, message);
    if (rc != AICAM_OK) {
        cJSON_free(response_text);
    }
    return rc;
}

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

    return app_session_respond_json(ctx, root, "App session start completed with result");
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

    return app_session_respond_json(ctx, root, "App session stop completed with result");
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

    return app_session_respond_json(ctx, root, "App session status");
}

static const char *app_report_state_str(uint32_t state)
{
    switch (state) {
    case APP_REPORT_MQTT_NOT_CONFIGURED: return "not_configured";
    case APP_REPORT_MQTT_PENDING: return "pending";
    case APP_REPORT_MQTT_TRANSPORT_SUCCEEDED: return "transport_succeeded";
    case APP_REPORT_MQTT_TRANSPORT_FAILED: return "transport_failed";
    case APP_REPORT_MQTT_UNKNOWN: return "unknown";
    case APP_REPORT_MQTT_CANCELLED: return "cancelled";
    case APP_REPORT_MQTT_RETRY_SUPPRESSED: return "retry_suppressed";
    case APP_REPORT_MQTT_DISABLED: return "disabled";
    case APP_REPORT_MQTT_POLICY_UNKNOWN: return "policy_unknown";
    default: return "invalid";
    }
}

aicam_result_t app_report_service_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;
    cJSON *stats_json = NULL;
    cJSON *queue_json = NULL;
    app_report_stats_t stats;
    uint32_t i;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app report: service status refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    app_report_service_get_stats(&stats);

    root = cJSON_CreateObject();
    stats_json = cJSON_CreateObject();
    queue_json = cJSON_CreateArray();
    if (root == NULL || stats_json == NULL || queue_json == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(stats_json);
        cJSON_Delete(queue_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(root, "running",
                          app_report_service_device_running() == AICAM_TRUE);
    cJSON_AddNumberToObject(root, "boot_id",
                            (double)app_report_service_boot_id());
    cJSON_AddNumberToObject(root, "queue_count",
                            (double)app_report_service_queue_count());
    cJSON_AddNumberToObject(stats_json, "accepted", (double)stats.accepted);
    cJSON_AddNumberToObject(stats_json, "dedup_hits", (double)stats.dedup_hits);
    cJSON_AddNumberToObject(stats_json, "rejected_param", (double)stats.rejected_param);
    cJSON_AddNumberToObject(stats_json, "rejected_schema", (double)stats.rejected_schema);
    cJSON_AddNumberToObject(stats_json, "rejected_quota", (double)stats.rejected_quota);
    cJSON_AddNumberToObject(stats_json, "rejected_busy", (double)stats.rejected_busy);
    cJSON_AddNumberToObject(stats_json, "rejected_unauthorized", (double)stats.rejected_unauthorized);
    cJSON_AddNumberToObject(stats_json, "rejected_identity", (double)stats.rejected_identity);
    cJSON_AddNumberToObject(stats_json, "tx_batches", (double)stats.tx_batches);
    cJSON_AddNumberToObject(stats_json, "tx_skipped", (double)stats.tx_skipped);
    cJSON_AddItemToObject(root, "stats", stats_json);

    for (i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        app_report_entry_info_t info;
        cJSON *entry_json;

        if (app_report_service_entry(i, &info) != AICAM_TRUE) {
            continue;
        }
        entry_json = cJSON_CreateObject();
        if (entry_json == NULL) {
            continue;
        }
        cJSON_AddNumberToObject(entry_json, "slot", (double)i);
        cJSON_AddNumberToObject(entry_json, "report_seq", (double)info.report_seq);
        cJSON_AddNumberToObject(entry_json, "payload_len", (double)info.payload_len);
        cJSON_AddStringToObject(entry_json, "mqtt_state", app_report_state_str(info.mqtt_state));
        cJSON_AddNumberToObject(entry_json, "mqtt_attempts", (double)info.mqtt_attempts);
        cJSON_AddStringToObject(entry_json, "webhook_state", app_report_state_str(info.webhook_state));
        cJSON_AddNumberToObject(entry_json, "webhook_attempts", (double)info.webhook_attempts);
        cJSON_AddItemToArray(queue_json, entry_json);
    }
    cJSON_AddItemToObject(root, "queue", queue_json);

    {
        aicam_bool_t pending = AICAM_FALSE;
        aicam_bool_t bound = AICAM_FALSE;
        int32_t last_result = 0;
        uint8_t active_tbs[APP_REPORT_SHA256_LEN];
        char tbs_hex[APP_REPORT_SHA256_LEN * 2u + 1u];
        uint32_t k;
        app_report_service_policy_debug(&pending, &bound, &last_result, active_tbs);
        for (k = 0u; k < APP_REPORT_SHA256_LEN; k++) {
            (void)snprintf(&tbs_hex[k * 2u], 3u, "%02x", active_tbs[k]);
        }
        cJSON_AddBoolToObject(root, "policy_pending",
                              pending == AICAM_TRUE);
        cJSON_AddBoolToObject(root, "policy_bound", bound == AICAM_TRUE);
        cJSON_AddNumberToObject(root, "policy_last_result", (double)last_result);
        cJSON_AddStringToObject(root, "policy_active_tbs", tbs_hex);
    }

    return app_session_respond_json(ctx, root, "App report service status");
}

#define APP_BRIDGE_PROTOCOL_V1 "ne301.sidecar.bridge.v1"
#define APP_BRIDGE_GRANT_SLOTS 4u
#define APP_BRIDGE_GRANT_ID_BYTES 32u
#define APP_BRIDGE_GRANT_ID_HEX (APP_BRIDGE_GRANT_ID_BYTES * 2u)
#define APP_BRIDGE_GRANT_TTL_MS 300000u
#define APP_BRIDGE_GRANT_INACTIVE_MS 120000u
#define APP_BRIDGE_CONFIRM_SLOTS 4u
#define APP_BRIDGE_CONFIRM_TTL_MS 120000u
#define APP_BRIDGE_PAYLOAD_MAX_BYTES 16384u
#define APP_BRIDGE_BODY_MAX_BYTES 20480u
#define APP_BRIDGE_REPLAY_RING 8u
#define APP_BRIDGE_CONFIG_WAIT_MS 2000u
#define APP_BRIDGE_POLL_MS 10u
#define APP_BRIDGE_SNAPSHOT_POLL_MAX 100u

#define APP_BRIDGE_VIDEO_WS_PORT 8081u
#define APP_BRIDGE_VIDEO_WS_PATH "/stream"
#define APP_BRIDGE_VIDEO_LAN_HOST_MAX 64u
#define APP_BRIDGE_VIDEO_URL_MAX 128u
#define APP_BRIDGE_VIDEO_HEADER_VALUE_MAX 96u

#define APP_BRIDGE_LIVE_STREAM_NAME "live"
#define APP_BRIDGE_LIVE_EVENTS_MAX 32u
#define APP_BRIDGE_LIVE_TRACKS_MAX 16u
#define APP_BRIDGE_LIVE_TARGET_CLASS_MAX 64u

#define APP_BRIDGE_CAP_SNAPSHOT_READ (1u << 0)
#define APP_BRIDGE_CAP_CONFIG_READ (1u << 1)
#define APP_BRIDGE_CAP_CONFIG_APPLY (1u << 2)
#define APP_BRIDGE_CAP_COUNTERS_RESET (1u << 3)
#define APP_BRIDGE_CAP_REPORT_POLICY_READ (1u << 4)
#define APP_BRIDGE_CAP_REPORT_POLICY_APPLY (1u << 5)
#define APP_BRIDGE_CAP_VIDEO_OPEN (1u << 6)
#define APP_BRIDGE_CAP_VIDEO_CLOSE (1u << 7)
#define APP_BRIDGE_CAP_ENABLE_READ (1u << 8)
#define APP_BRIDGE_CAP_ENABLE_APPLY (1u << 9)
#define APP_BRIDGE_CAP_LIVE_SUBSCRIBE (1u << 10)
#define APP_BRIDGE_CAP_LIVE_UNSUBSCRIBE (1u << 11)
#define APP_BRIDGE_CAP_KNOWN_MASK (APP_BRIDGE_CAP_SNAPSHOT_READ | \
                                    APP_BRIDGE_CAP_CONFIG_READ | \
                                    APP_BRIDGE_CAP_CONFIG_APPLY | \
                                    APP_BRIDGE_CAP_COUNTERS_RESET | \
                                    APP_BRIDGE_CAP_REPORT_POLICY_READ | \
                                    APP_BRIDGE_CAP_REPORT_POLICY_APPLY | \
                                    APP_BRIDGE_CAP_VIDEO_OPEN | \
                                    APP_BRIDGE_CAP_VIDEO_CLOSE | \
                                    APP_BRIDGE_CAP_ENABLE_READ | \
                                    APP_BRIDGE_CAP_ENABLE_APPLY | \
                                    APP_BRIDGE_CAP_LIVE_SUBSCRIBE | \
                                    APP_BRIDGE_CAP_LIVE_UNSUBSCRIBE)

typedef struct {
    const char *op;
    uint32_t bit;
} app_bridge_op_map_t;

static const app_bridge_op_map_t g_app_bridge_ops[] = {
    { "snapshot.read", APP_BRIDGE_CAP_SNAPSHOT_READ },
    { "config.read", APP_BRIDGE_CAP_CONFIG_READ },
    { "config.apply", APP_BRIDGE_CAP_CONFIG_APPLY },
    { "counters.reset", APP_BRIDGE_CAP_COUNTERS_RESET },
    { "report-policy.read", APP_BRIDGE_CAP_REPORT_POLICY_READ },
    { "report-policy.apply", APP_BRIDGE_CAP_REPORT_POLICY_APPLY },
    { "video.open", APP_BRIDGE_CAP_VIDEO_OPEN },
    { "video.close", APP_BRIDGE_CAP_VIDEO_CLOSE },
    { "enable.read", APP_BRIDGE_CAP_ENABLE_READ },
    { "enable.apply", APP_BRIDGE_CAP_ENABLE_APPLY },
    { "live.subscribe", APP_BRIDGE_CAP_LIVE_SUBSCRIBE },
    { "live.unsubscribe", APP_BRIDGE_CAP_LIVE_UNSUBSCRIBE }
};

typedef struct {
    aicam_bool_t active;
    aicam_bool_t revoked;
    char grant_id[APP_BRIDGE_GRANT_ID_HEX + 1u];
    uint32_t capabilities;
    uint64_t issued_ms;
    uint64_t expires_ms;
    uint64_t last_used_ms;
    uint64_t sidecar_service_generation;
    uint8_t publisher_key_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t native_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t ui_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    char publisher_id[APP_SIDECAR_VERDICT_ID_SLOT];
    char app_id[APP_SIDECAR_VERDICT_APP_SLOT];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t host_boot_generation;
    uint32_t native_session_generation;
    uint32_t ui_generation;
    aicam_bool_t video_session_open;
    aicam_bool_t live_active;
    uint64_t live_after_cursor;
    uint32_t live_view_epoch;
    struct {
        uint32_t op_hash;
        uint32_t request_id;
    } replay[APP_BRIDGE_REPLAY_RING];
    uint8_t replay_next;
} app_bridge_grant_t;

typedef struct {
    aicam_bool_t used;
    uint8_t grant_slot;
    char grant_id[APP_BRIDGE_GRANT_ID_HEX + 1u];
    uint32_t op_bit;
    uint64_t expires_ms;
    char token[APP_BRIDGE_GRANT_ID_HEX + 1u];
} app_bridge_confirm_token_t;

static app_bridge_grant_t g_app_bridge_grants[APP_BRIDGE_GRANT_SLOTS];
static app_bridge_confirm_token_t g_app_bridge_confirms[APP_BRIDGE_CONFIRM_SLOTS];
static char g_app_bridge_video_lan_host[APP_BRIDGE_VIDEO_LAN_HOST_MAX];
static uint32_t g_app_bridge_boot_generation = 0u;
static uint32_t g_app_bridge_ui_generation = 0u;
static aicam_bool_t g_app_bridge_drbg_ready = AICAM_FALSE;
static aicam_bool_t g_app_bridge_drbg_failed = AICAM_FALSE;
static uint32_t g_app_bridge_rng_failures = 0u;
static aicam_bool_t g_app_bridge_rng_seed_fault = AICAM_FALSE;
static aicam_bool_t g_app_bridge_rng_generate_fault = AICAM_FALSE;
static mbedtls_ctr_drbg_context g_app_bridge_drbg;
static app_bridge_backend_t g_app_bridge_backend = { NULL, NULL, NULL };

void app_bridge_backend_set(const app_bridge_backend_t *backend) {
    if (backend == NULL) {
        g_app_bridge_backend.now_ms = NULL;
        g_app_bridge_backend.sleep_ms = NULL;
        g_app_bridge_backend.epoch_ms = NULL;
        return;
    }
    g_app_bridge_backend = *backend;
}

static uint32_t app_bridge_now_ms(void) {
    if (g_app_bridge_backend.now_ms != NULL) {
        return g_app_bridge_backend.now_ms();
    }
    return (uint32_t)osKernelGetTickCount();
}

static uint64_t app_bridge_epoch_ms(void) {
    if (g_app_bridge_backend.epoch_ms != NULL) {
        return g_app_bridge_backend.epoch_ms();
    }
    return (uint64_t)time(NULL) * 1000u;
}

static void app_bridge_sleep_ms(uint32_t ms) {
    if (g_app_bridge_backend.sleep_ms != NULL) {
        g_app_bridge_backend.sleep_ms(ms);
        return;
    }
    (void)osDelay(ms);
}

void app_bridge_video_lan_host_set(const char *host) {
    size_t len;
    size_t i;

    if (host == NULL) {
        g_app_bridge_video_lan_host[0] = '\0';
        return;
    }
    len = strlen(host);
    if (len == 0u || len >= sizeof(g_app_bridge_video_lan_host)) {
        g_app_bridge_video_lan_host[0] = '\0';
        return;
    }
    if (strstr(host, "://") != NULL || strchr(host, '/') != NULL ||
        strchr(host, ' ') != NULL || strchr(host, '"') != NULL ||
        strchr(host, '?') != NULL || strchr(host, '%') != NULL) {
        g_app_bridge_video_lan_host[0] = '\0';
        return;
    }
    for (i = 0u; i < len; i++) {
        if ((uint8_t)host[i] < 0x21u || (uint8_t)host[i] > 0x7eu) {
            g_app_bridge_video_lan_host[0] = '\0';
            return;
        }
    }
    snprintf(g_app_bridge_video_lan_host, sizeof(g_app_bridge_video_lan_host), "%s", host);
}

static int app_bridge_entropy_source(void *data, unsigned char *output, size_t len,
                                     size_t *out_len) {
    size_t filled = 0u;

    (void)data;
    if (output == NULL || out_len == NULL) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
#if defined(STM32N657xx)
    {
        uint32_t word = 0u;
        while (filled < len) {
            if (mmhal_random_get_u32(&word) != 1) {
                return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
            }
            {
                size_t chunk = (len - filled < 4u) ? (len - filled) : 4u;
                memcpy(output + filled, &word, chunk);
                filled += chunk;
            }
        }
    }
#else
    {
        static FILE *g_app_bridge_urandom;
        if (g_app_bridge_urandom == NULL) {
            g_app_bridge_urandom = fopen("/dev/urandom", "rb");
        }
        if (g_app_bridge_urandom == NULL) {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        if (fread(output, 1u, len, g_app_bridge_urandom) != len) {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        filled = len;
    }
#endif
    *out_len = filled;
    return 0;
}

static aicam_bool_t rng_failure_latched(void) {
    g_app_bridge_rng_failures++;
    return (g_app_bridge_rng_failures >= 3u) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t app_bridge_random(uint8_t *out, size_t len) {
    static mbedtls_entropy_context g_app_bridge_entropy;
    static aicam_bool_t g_app_bridge_entropy_ready = AICAM_FALSE;

    if (out == NULL || len == 0u) {
        return AICAM_FALSE;
    }
    if (g_app_bridge_drbg_failed) {
        memset(out, 0, len);
        return AICAM_FALSE;
    }
    if (!g_app_bridge_drbg_ready) {
        mbedtls_ctr_drbg_init(&g_app_bridge_drbg);
        if (!g_app_bridge_entropy_ready) {
#if defined(STM32N657xx)
            mmhal_random_init();
#endif
            mbedtls_entropy_init(&g_app_bridge_entropy);
            if (mbedtls_entropy_add_source(&g_app_bridge_entropy,
                                           app_bridge_entropy_source, NULL, 32u,
                                           MBEDTLS_ENTROPY_SOURCE_STRONG) != 0) {
                if (rng_failure_latched()) {
                    g_app_bridge_drbg_failed = AICAM_TRUE;
                }
                memset(out, 0, len);
                return AICAM_FALSE;
            }
            g_app_bridge_entropy_ready = AICAM_TRUE;
        }
        if (g_app_bridge_rng_seed_fault ||
            mbedtls_ctr_drbg_seed(&g_app_bridge_drbg, mbedtls_entropy_func,
                                  &g_app_bridge_entropy,
                                  (const unsigned char *)APP_BRIDGE_PROTOCOL_V1,
                                  strlen(APP_BRIDGE_PROTOCOL_V1)) != 0) {
            if (rng_failure_latched()) {
                g_app_bridge_drbg_failed = AICAM_TRUE;
            }
            memset(out, 0, len);
            return AICAM_FALSE;
        }
        g_app_bridge_drbg_ready = AICAM_TRUE;
    }
    if (g_app_bridge_rng_generate_fault ||
        mbedtls_ctr_drbg_random(&g_app_bridge_drbg, out, len) != 0) {
        g_app_bridge_drbg_ready = AICAM_FALSE;
        if (rng_failure_latched()) {
            g_app_bridge_drbg_failed = AICAM_TRUE;
        }
        memset(out, 0, len);
        return AICAM_FALSE;
    }
    g_app_bridge_rng_failures = 0u;
    return AICAM_TRUE;
}

void app_bridge_rng_fault_set(aicam_bool_t seed_fail, aicam_bool_t generate_fail) {
    g_app_bridge_rng_seed_fault = seed_fail;
    g_app_bridge_rng_generate_fault = generate_fail;
}

static void app_bridge_hex(const uint8_t *data, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0u; i < len; i++) {
        out[i * 2u] = digits[data[i] >> 4];
        out[i * 2u + 1u] = digits[data[i] & 0x0fu];
    }
    out[len * 2u] = '\0';
}

static uint32_t app_bridge_op_hash(const char *op) {
    uint32_t hash = 2166136261u;
    while (*op != '\0') {
        hash ^= (uint8_t)*op++;
        hash *= 16777619u;
    }
    return hash;
}

static const app_bridge_op_map_t *app_bridge_op_lookup(const char *op) {
    size_t i;
    if (op == NULL) {
        return NULL;
    }
    for (i = 0u; i < sizeof(g_app_bridge_ops) / sizeof(g_app_bridge_ops[0]); i++) {
        if (strcmp(g_app_bridge_ops[i].op, op) == 0) {
            return &g_app_bridge_ops[i];
        }
    }
    return NULL;
}

void app_bridge_grants_reset(void) {
    memset(g_app_bridge_grants, 0, sizeof(g_app_bridge_grants));
    memset(g_app_bridge_confirms, 0, sizeof(g_app_bridge_confirms));
    memset(&g_app_bridge_drbg, 0, sizeof(g_app_bridge_drbg));
    g_app_bridge_drbg_ready = AICAM_FALSE;
    g_app_bridge_drbg_failed = AICAM_FALSE;
    g_app_bridge_rng_failures = 0u;
    g_app_bridge_rng_seed_fault = AICAM_FALSE;
    g_app_bridge_rng_generate_fault = AICAM_FALSE;
    g_app_bridge_ui_generation = g_app_bridge_boot_generation;
}

static aicam_bool_t app_bridge_video_host_allowed_char(char ch) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
        return AICAM_TRUE;
    }
    return (ch == '.' || ch == '-' || ch == ':' || ch == '[' || ch == ']')
               ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t app_bridge_video_host_from_value(const char *value, char *out, size_t cap) {
    size_t len;
    size_t i;
    const char *end;

    if (value == NULL || out == NULL || cap < 2u) {
        return AICAM_FALSE;
    }
    len = strlen(value);
    if (len == 0u || len > APP_BRIDGE_VIDEO_HEADER_VALUE_MAX) {
        return AICAM_FALSE;
    }
    if (value[0] == '[') {
        end = strchr(value, ']');
        if (end == NULL) {
            return AICAM_FALSE;
        }
        end++;
    } else {
        end = strchr(value, ':');
        if (end == NULL) {
            end = value + len;
        }
    }
    len = (size_t)(end - value);
    if (len == 0u || len >= cap) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < len; i++) {
        if (!app_bridge_video_host_allowed_char(value[i])) {
            return AICAM_FALSE;
        }
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_video_host_from_request(http_handler_context_t *ctx,
                                                       char *out, size_t cap) {
    struct mg_str *host;
    char value[APP_BRIDGE_VIDEO_HEADER_VALUE_MAX + 1u];
    size_t i;

    if (ctx == NULL || ctx->msg == NULL) {
        return AICAM_FALSE;
    }
    host = mg_http_get_header(ctx->msg, "Host");
    if (host == NULL || host->buf == NULL || host->len == 0u ||
        host->len >= sizeof(value)) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < host->len; i++) {
        if (host->buf[i] == '\0') {
            return AICAM_FALSE;
        }
        value[i] = host->buf[i];
    }
    value[host->len] = '\0';
    return app_bridge_video_host_from_value(value, out, cap);
}

static aicam_bool_t app_bridge_payload_u32(cJSON *payload, const char *key, uint32_t *out);

static aicam_bool_t app_bridge_consume_confirm(uint8_t grant_slot, const char *grant_id,
                                               uint32_t op_bit, const char *token);

static cJSON *app_bridge_dispatch_video_open(uint8_t slot, http_handler_context_t *ctx,
                                             cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_item;
    cJSON *payload_json;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    websocket_stream_stats_t stats;
    char host[APP_BRIDGE_VIDEO_LAN_HOST_MAX];
    char url[APP_BRIDGE_VIDEO_URL_MAX];
    int url_len;
    uint32_t session_generation = 0u;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "session_generation", &session_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "SESSION_GENERATION_REQUIRED");
        }
        return result;
    }
    if (session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STALE_SESSION_GENERATION");
        }
        return result;
    }
    if (websocket_stream_server_is_running() != AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "VIDEO_SERVICE_UNAVAILABLE");
        }
        return result;
    }
    memset(&stats, 0, sizeof(stats));
    if (websocket_stream_server_get_stats(&stats) != AICAM_OK ||
        stats.stream_active != AICAM_TRUE) {
        if (websocket_stream_server_start_stream(1u) != AICAM_OK ||
            websocket_stream_server_get_stats(&stats) != AICAM_OK ||
            stats.stream_active != AICAM_TRUE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
                cJSON_AddStringToObject(result, "reason", "VIDEO_SOURCE_UNAVAILABLE");
            }
            return result;
        }
    }
    if (g_app_bridge_video_lan_host[0] != '\0') {
        snprintf(host, sizeof(host), "%s", g_app_bridge_video_lan_host);
    } else if (!app_bridge_video_host_from_request(ctx, host, sizeof(host))) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "VIDEO_ENDPOINT_UNAVAILABLE");
        }
        return result;
    }
    url_len = snprintf(url, sizeof(url), "ws://%s:%u%s", host,
                       (unsigned int)APP_BRIDGE_VIDEO_WS_PORT, APP_BRIDGE_VIDEO_WS_PATH);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "VIDEO_ENDPOINT_UNAVAILABLE");
        }
        return result;
    }
    grant->video_session_open = AICAM_TRUE;
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    if (result == NULL || payload_json == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(payload_json);
        return NULL;
    }
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddStringToObject(payload_json, "url", url);
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddNumberToObject(payload_json, "expires_at_ms", (double)grant->expires_ms);
    cJSON_AddStringToObject(payload_json, "grant_id", grant->grant_id);
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static cJSON *app_bridge_dispatch_video_close(uint8_t slot, cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_item;
    cJSON *payload_json;
    cJSON *grant_item;
    const char *payload_grant_id;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    uint32_t session_generation = 0u;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "session_generation", &session_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "SESSION_GENERATION_REQUIRED");
        }
        return result;
    }
    if (session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STALE_SESSION_GENERATION");
        }
        return result;
    }
    grant_item = cJSON_GetObjectItem(payload_item, "grant_id");
    payload_grant_id = (grant_item != NULL && cJSON_IsString(grant_item))
                           ? grant_item->valuestring : NULL;
    if (payload_grant_id == NULL || strcmp(payload_grant_id, grant->grant_id) != 0) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "GRANT_MISMATCH");
        }
        return result;
    }
    if (grant->video_session_open != AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "VIDEO_SESSION_NOT_OPEN");
        }
        return result;
    }
    grant->video_session_open = AICAM_FALSE;
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    if (result == NULL || payload_json == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(payload_json);
        return NULL;
    }
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static void app_bridge_put_u32le(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value & 0xFFu);
    buf[1] = (uint8_t)((value >> 8) & 0xFFu);
    buf[2] = (uint8_t)((value >> 16) & 0xFFu);
    buf[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static aicam_bool_t app_bridge_snapshot_enable_state(const char *json, aicam_bool_t *enabled) {
    cJSON *root;
    cJSON *status_item;
    cJSON *state_item;
    aicam_bool_t resolved = AICAM_FALSE;

    if (json == NULL || enabled == NULL) {
        return AICAM_FALSE;
    }
    root = cJSON_Parse(json);
    if (root == NULL) {
        return AICAM_FALSE;
    }
    status_item = cJSON_GetObjectItem(root, "status");
    state_item = (status_item != NULL && cJSON_IsObject(status_item))
                     ? cJSON_GetObjectItem(status_item, "state") : NULL;
    if (state_item != NULL && cJSON_IsString(state_item) && state_item->valuestring != NULL) {
        if (strcmp(state_item->valuestring, "running") == 0) {
            *enabled = AICAM_TRUE;
            resolved = AICAM_TRUE;
        } else if (strcmp(state_item->valuestring, "stopped") == 0) {
            *enabled = AICAM_FALSE;
            resolved = AICAM_TRUE;
        }
    }
    cJSON_Delete(root);
    return resolved;
}

static void app_bridge_snapshot_target_class(const char *json, char *out, size_t cap) {
    cJSON *root;
    cJSON *config_item;
    cJSON *class_item;
    size_t len;

    out[0] = '\0';
    if (json == NULL) {
        return;
    }
    root = cJSON_Parse(json);
    if (root == NULL) {
        return;
    }
    config_item = cJSON_GetObjectItem(root, "active_config");
    class_item = (config_item != NULL && cJSON_IsObject(config_item))
                     ? cJSON_GetObjectItem(config_item, "target_class_name") : NULL;
    if (class_item == NULL || !cJSON_IsString(class_item)) {
        config_item = cJSON_GetObjectItem(root, "config");
        class_item = (config_item != NULL && cJSON_IsObject(config_item))
                         ? cJSON_GetObjectItem(config_item, "target_class_name") : NULL;
    }
    if (class_item != NULL && cJSON_IsString(class_item) && class_item->valuestring != NULL) {
        len = strlen(class_item->valuestring);
        if (len >= 1u && len <= APP_BRIDGE_LIVE_TARGET_CLASS_MAX && len < cap) {
            memcpy(out, class_item->valuestring, len + 1u);
        }
    }
    cJSON_Delete(root);
}

static aicam_bool_t app_bridge_enable_session_ready(app_bridge_grant_t *grant,
                                                    app_control_status_t *control) {
    memset(control, 0, sizeof(*control));
    app_host_control_status(control);
    if (control->session_active != AICAM_TRUE ||
        control->session_generation != grant->native_session_generation ||
        (control->caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) == 0u) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static cJSON *app_bridge_enable_payload_value(app_bridge_grant_t *grant, aicam_bool_t value) {
    cJSON *payload_json = cJSON_CreateObject();

    if (payload_json == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddBoolToObject(payload_json, "value", value);
    return payload_json;
}

static cJSON *app_bridge_dispatch_enable_read(uint8_t slot, cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_json;
    app_control_status_t control;
    app_control_snapshot_view_t view;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    uint8_t *payload_buf;
    uint32_t payload_len = 0u;
    int32_t rc;
    aicam_bool_t enabled = AICAM_FALSE;

    (void)body;
    if (app_bridge_enable_session_ready(grant, &control) != AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "ENABLE_UNAVAILABLE");
        }
        return result;
    }
    payload_buf = (uint8_t *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
    result = cJSON_CreateObject();
    if (result == NULL || payload_buf == NULL) {
        free(payload_buf);
        cJSON_Delete(result);
        return NULL;
    }
    memset(&view, 0, sizeof(view));
    rc = app_host_control_snapshot_read(&view, payload_buf,
                                        APP_CONTROL_SNAPSHOT_RAM_MAX, &payload_len);
    if (rc == APP_CONTROL_NOT_FOUND) {
        cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
        cJSON_AddStringToObject(result, "reason", "ENABLE_STATE_UNKNOWN");
        free(payload_buf);
        return result;
    }
    if (rc != APP_CONTROL_OK || payload_len != view.payload_len) {
        cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
        cJSON_AddStringToObject(result, "reason", "SNAPSHOT_READ_FAILED");
        free(payload_buf);
        return result;
    }
    if (view.session_generation != grant->native_session_generation) {
        cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
        cJSON_AddStringToObject(result, "reason", "SNAPSHOT_SESSION_MISMATCH");
        free(payload_buf);
        return result;
    }
    payload_buf[view.payload_len] = '\0';
    if (app_bridge_snapshot_enable_state((const char *)payload_buf, &enabled) != AICAM_TRUE) {
        cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
        cJSON_AddStringToObject(result, "reason", "ENABLE_STATE_UNKNOWN");
        free(payload_buf);
        return result;
    }
    free(payload_buf);
    payload_json = app_bridge_enable_payload_value(grant, enabled);
    if (payload_json == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static cJSON *app_bridge_dispatch_enable_apply(uint8_t slot, cJSON *body, uint32_t request_id) {
    cJSON *result = NULL;
    cJSON *payload_item;
    cJSON *value_item;
    cJSON *payload_json;
    app_control_request_t req;
    app_control_status_t st;
    app_control_snapshot_view_t view;
    app_control_register_result_t reg;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    uint8_t enable_payload[APP_CONTROL_ENABLE_PAYLOAD_SIZE];
    uint8_t *snapshot_buf;
    uint32_t snapshot_len = 0u;
    uint32_t session_generation = 0u;
    uint32_t desired_state;
    uint32_t expected_revision;
    uint32_t deadline;
    int32_t rc;
    aicam_bool_t requested_value;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "session_generation", &session_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "SESSION_GENERATION_REQUIRED");
        }
        return result;
    }
    if (session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STALE_SESSION_GENERATION");
        }
        return result;
    }
    value_item = cJSON_GetObjectItem(payload_item, "value");
    if (value_item == NULL || !cJSON_IsBool(value_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "ENABLE_VALUE_INVALID");
        }
        return result;
    }
    requested_value = cJSON_IsTrue(value_item) ? AICAM_TRUE : AICAM_FALSE;
    desired_state = (requested_value == AICAM_TRUE)
                        ? APP_CONTROL_ENABLE_STATE_ENABLED
                        : APP_CONTROL_ENABLE_STATE_PAUSED;
    if (app_bridge_enable_session_ready(grant, &st) != AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "ENABLE_UNAVAILABLE");
        }
        return result;
    }
    if (st.has_known_config_generation != AICAM_TRUE || st.known_config_generation == 0u) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CONFIG_GENERATION_UNKNOWN");
        }
        return result;
    }
    snapshot_buf = (uint8_t *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
    if (snapshot_buf == NULL) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
            cJSON_AddStringToObject(result, "reason", "SNAPSHOT_READ_FAILED");
        }
        return result;
    }
    memset(&view, 0, sizeof(view));
    rc = app_host_control_snapshot_read(&view, snapshot_buf,
                                        APP_CONTROL_SNAPSHOT_RAM_MAX, &snapshot_len);
    free(snapshot_buf);
    if (rc == APP_CONTROL_NOT_FOUND) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STATE_REVISION_UNKNOWN");
        }
        return result;
    }
    if (rc != APP_CONTROL_OK || snapshot_len != view.payload_len ||
        view.session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
            cJSON_AddStringToObject(result, "reason", "SNAPSHOT_READ_FAILED");
        }
        return result;
    }
    expected_revision = view.state_revision;
    {
        const char *confirm_token = cJSON_GetObjectItem(body, "confirm_token") != NULL
                                        ? cJSON_GetObjectItem(body, "confirm_token")->valuestring
                                        : NULL;

        if (app_bridge_consume_confirm(slot, grant->grant_id,
                                       APP_BRIDGE_CAP_ENABLE_APPLY,
                                       confirm_token) != AICAM_TRUE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "ADMIN_CONFIRM_REQUIRED");
            }
            return result;
        }
    }
    enable_payload[0] = 0x45u;
    enable_payload[1] = 0x4Eu;
    enable_payload[2] = 0x42u;
    enable_payload[3] = 0x31u;
    app_bridge_put_u32le(enable_payload + 4u, desired_state);
    app_bridge_put_u32le(enable_payload + 8u, expected_revision);
    app_bridge_put_u32le(enable_payload + 12u, 0u);
    memset(&req, 0, sizeof(req));
    req.request_id = request_id;
    req.msg_type = APP_CONTROL_MSG_TYPE_ENABLE_SET;
    req.payload = enable_payload;
    req.payload_len = (uint32_t)sizeof(enable_payload);
    req.expected_config_generation = st.known_config_generation;
    req.flags = 0u;
    reg = app_host_control_register(&req);
    if (reg != APP_CONTROL_REG_OK && reg != APP_CONTROL_REG_CACHED) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason",
                                    app_host_control_register_result_str(reg));
        }
        return result;
    }
    deadline = app_bridge_now_ms() + APP_BRIDGE_CONFIG_WAIT_MS;
    for (;;) {
        memset(&st, 0, sizeof(st));
        app_host_control_status(&st);
        if (st.has_command != AICAM_TRUE ||
            st.command_request_id != request_id) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason", "COMMAND_SLOT_OVERRUN");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_APPLIED) {
            payload_json = app_bridge_enable_payload_value(grant, requested_value);
            result = cJSON_CreateObject();
            if (result == NULL || payload_json == NULL) {
                cJSON_Delete(payload_json);
                cJSON_Delete(result);
                return NULL;
            }
            cJSON_AddStringToObject(result, "outcome", "APPLIED");
            cJSON_AddNumberToObject(result, "config_generation",
                                    (double)st.result_config_generation);
            cJSON_AddNumberToObject(result, "state_revision",
                                    (double)st.result_state_revision);
            cJSON_AddItemToObject(result, "payload", payload_json);
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_STALE_CONFIG) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "CONFLICT");
                cJSON_AddStringToObject(result, "reason", "STALE_CONFIG_GENERATION");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_FAILED_STATE_CONFLICT) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "CONFLICT");
                cJSON_AddStringToObject(result, "reason", "STATE_CONFLICT");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_UNCONFIRMED) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "ENABLE_SET_UNCONFIRMED");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_INVALID ||
            st.command_state == APP_CONTROL_CMD_REJECTED_RESOURCE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason",
                                        app_host_control_cmd_state_str(st.command_state));
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_FAILED_STORAGE_IO ||
            st.command_state == APP_CONTROL_CMD_FAILED_STORAGE_UNKNOWN) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason",
                                        app_host_control_cmd_state_str(st.command_state));
            }
            return result;
        }
        if (st.command_state >= APP_CONTROL_CMD_APPLIED) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason",
                                        app_host_control_cmd_state_str(st.command_state));
            }
            return result;
        }
        if (app_bridge_now_ms() >= deadline) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason", "ACK_TIMEOUT");
            }
            return result;
        }
        app_bridge_sleep_ms(APP_BRIDGE_POLL_MS);
    }
}

static int app_bridge_live_stream_check(cJSON *body) {
    cJSON *payload_item;
    cJSON *stream_item;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        return 0;
    }
    stream_item = cJSON_GetObjectItem(payload_item, "stream");
    if (stream_item == NULL || !cJSON_IsString(stream_item) ||
        stream_item->valuestring == NULL ||
        strcmp(stream_item->valuestring, APP_BRIDGE_LIVE_STREAM_NAME) != 0) {
        return 2;
    }
    return 1;
}

static aicam_bool_t app_bridge_live_identity_bound(app_bridge_grant_t *grant,
                                                   const app_host_live_status_t *live) {
    if (live->bound != AICAM_TRUE || live->closed == AICAM_TRUE ||
        live->session_generation != grant->native_session_generation ||
        strcmp(live->publisher_id, grant->publisher_id) != 0 ||
        strcmp(live->app_id, grant->app_id) != 0 ||
        memcmp(live->spki_sha256, grant->publisher_key_sha256,
               APP_SIDECAR_SHA256_LEN) != 0 ||
        memcmp(live->tbs_sha256, grant->native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static cJSON *app_bridge_live_unavailable(void) {
    cJSON *result = cJSON_CreateObject();

    if (result != NULL) {
        cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
        cJSON_AddStringToObject(result, "reason", "LIVE_UNAVAILABLE");
    }
    return result;
}

static cJSON *app_bridge_dispatch_live_subscribe(uint8_t slot, cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_json;
    cJSON *tracks_json;
    cJSON *events_json;
    cJSON *track_json;
    cJSON *points_json;
    cJSON *point_json;
    cJSON *event_json;
    app_control_status_t control;
    app_host_live_status_t live;
    app_host_live_read_out_t read_info;
    app_host_live_snapshot_out_t snap_info;
    app_host_live_event_t *events;
    app_host_live_track_t *tracks;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    char *snapshot_buf;
    char target_class[APP_BRIDGE_LIVE_TARGET_CLASS_MAX + 1u];
    uint32_t i;
    uint32_t j;
    uint32_t event_count;
    uint32_t track_count_out;
    aicam_bool_t truncated;
    int32_t rc;

    if (app_bridge_live_stream_check(body) == 0) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_live_stream_check(body) != 1) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STREAM_UNKNOWN");
        }
        return result;
    }
    memset(&control, 0, sizeof(control));
    app_host_control_status(&control);
    if (control.session_active != AICAM_TRUE ||
        control.session_generation != grant->native_session_generation ||
        (control.caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) == 0u) {
        return app_bridge_live_unavailable();
    }
    memset(&live, 0, sizeof(live));
    app_host_live_status(&live);
    if (app_bridge_live_identity_bound(grant, &live) != AICAM_TRUE) {
        return app_bridge_live_unavailable();
    }
    events = (app_host_live_event_t *)malloc(sizeof(*events) * APP_BRIDGE_LIVE_EVENTS_MAX);
    tracks = (app_host_live_track_t *)malloc(sizeof(*tracks) * APP_HOST_LIVE_TRACK_MAX);
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    if (events == NULL || tracks == NULL || result == NULL || payload_json == NULL) {
        free(events);
        free(tracks);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    if (grant->live_active != AICAM_TRUE) {
        rc = app_host_live_read(live.session_generation, 0xFFFFFFFFFFFFFFFFu,
                                events, 1u, &read_info);
        if (rc != APP_HOST_LIVE_OK) {
            free(events);
            free(tracks);
            cJSON_Delete(payload_json);
            cJSON_Delete(result);
            return app_bridge_live_unavailable();
        }
        grant->live_after_cursor = read_info.newest_cursor;
        grant->live_view_epoch = read_info.view_epoch;
        grant->live_active = AICAM_TRUE;
    }
    rc = app_host_live_read(live.session_generation, grant->live_after_cursor,
                            events, APP_BRIDGE_LIVE_EVENTS_MAX, &read_info);
    if (rc != APP_HOST_LIVE_OK) {
        grant->live_active = AICAM_FALSE;
        grant->live_after_cursor = 0u;
        free(events);
        free(tracks);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return app_bridge_live_unavailable();
    }
    event_count = read_info.returned;
    if (event_count > 0u) {
        grant->live_after_cursor = events[event_count - 1u].cursor;
    }
    memset(&snap_info, 0, sizeof(snap_info));
    rc = app_host_live_snapshot_read(live.session_generation, tracks,
                                     APP_HOST_LIVE_TRACK_MAX, &snap_info);
    if (rc != APP_HOST_LIVE_OK) {
        grant->live_active = AICAM_FALSE;
        grant->live_after_cursor = 0u;
        free(events);
        free(tracks);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return app_bridge_live_unavailable();
    }
    truncated = (snap_info.track_count > APP_BRIDGE_LIVE_TRACKS_MAX)
                    ? AICAM_TRUE : AICAM_FALSE;
    track_count_out = truncated ? APP_BRIDGE_LIVE_TRACKS_MAX : snap_info.track_count;
    snapshot_buf = (char *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
    target_class[0] = '\0';
    if (snapshot_buf != NULL) {
        app_control_snapshot_view_t view;
        uint32_t payload_len = 0u;

        memset(&view, 0, sizeof(view));
        if (app_host_control_snapshot_read(&view, snapshot_buf,
                                           APP_CONTROL_SNAPSHOT_RAM_MAX,
                                           &payload_len) == APP_CONTROL_OK &&
            payload_len == view.payload_len &&
            view.session_generation == grant->native_session_generation) {
            snapshot_buf[payload_len] = '\0';
            app_bridge_snapshot_target_class(snapshot_buf, target_class,
                                             sizeof(target_class));
        }
        free(snapshot_buf);
    }
    if (target_class[0] == '\0') {
        snprintf(target_class, sizeof(target_class), "unknown");
    }
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)live.session_generation);
    cJSON_AddNumberToObject(payload_json, "view_epoch", (double)read_info.view_epoch);
    cJSON_AddStringToObject(payload_json, "freshness",
                            app_host_live_freshness_str(read_info.freshness));
    cJSON_AddNumberToObject(payload_json, "age_ms", (double)read_info.age_ms);
    cJSON_AddStringToObject(payload_json, "state",
                            app_host_live_read_state_str(read_info.state));
    cJSON_AddNumberToObject(payload_json, "through_seq", (double)read_info.through_seq);
    cJSON_AddNumberToObject(payload_json, "source_total", (double)read_info.source_total);
    cJSON_AddNumberToObject(payload_json, "source_gap", (double)read_info.source_gap);
    cJSON_AddNumberToObject(payload_json, "host_dropped_events",
                            (double)read_info.host_dropped_events);
    cJSON_AddNumberToObject(payload_json, "packet_count", (double)read_info.packet_count);
    cJSON_AddNumberToObject(payload_json, "last_packet_seq",
                            (double)read_info.last_packet_seq);
    cJSON_AddBoolToObject(payload_json, "loss_unknown",
                          (read_info.loss_unknown == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddBoolToObject(payload_json, "resync_required",
                          (read_info.resync_required == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddBoolToObject(payload_json, "tracks_truncated", truncated);
    cJSON_AddNumberToObject(payload_json, "next_cursor", (double)read_info.next_cursor);
    cJSON_AddNumberToObject(payload_json, "server_now_ms", (double)app_host_live_epoch_ms());
    cJSON_AddStringToObject(payload_json, "target_class", target_class);
    tracks_json = cJSON_AddArrayToObject(payload_json, "tracks");
    if (tracks_json == NULL) {
        free(events);
        free(tracks);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    for (i = 0u; i < track_count_out; i++) {
        const app_host_live_track_t *track = &tracks[i];

        track_json = cJSON_CreateObject();
        if (track_json == NULL) {
            break;
        }
        cJSON_AddNumberToObject(track_json, "track_id", (double)track->track_id);
        cJSON_AddNumberToObject(track_json, "point_count", (double)track->point_count);
        cJSON_AddNumberToObject(track_json, "class_index", (double)track->class_index);
        cJSON_AddNumberToObject(track_json, "state", (double)track->state);
        cJSON_AddNumberToObject(track_json, "flags", (double)track->flags);
        cJSON_AddNumberToObject(track_json, "confidence_q16",
                                (double)track->confidence_q16);
        points_json = cJSON_AddArrayToObject(track_json, "points");
        if (points_json == NULL) {
            cJSON_Delete(track_json);
            break;
        }
        for (j = 0u; j < track->point_count; j++) {
            point_json = cJSON_CreateObject();
            if (point_json == NULL) {
                break;
            }
            cJSON_AddNumberToObject(point_json, "ts_ms", (double)track->points[j].ts_ms);
            cJSON_AddNumberToObject(point_json, "x_q16", (double)track->points[j].x_q16);
            cJSON_AddNumberToObject(point_json, "y_q16", (double)track->points[j].y_q16);
            cJSON_AddItemToArray(points_json, point_json);
        }
        cJSON_AddItemToArray(tracks_json, track_json);
    }
    events_json = cJSON_AddArrayToObject(payload_json, "events");
    if (events_json == NULL) {
        free(events);
        free(tracks);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    for (i = 0u; i < event_count; i++) {
        event_json = cJSON_CreateObject();
        if (event_json == NULL) {
            break;
        }
        cJSON_AddNumberToObject(event_json, "cursor", (double)events[i].cursor);
        cJSON_AddNumberToObject(event_json, "event_seq", (double)events[i].event_seq);
        cJSON_AddNumberToObject(event_json, "track_id", (double)events[i].track_id);
        cJSON_AddNumberToObject(event_json, "event_ts_ms", (double)events[i].event_ts_ms);
        cJSON_AddNumberToObject(event_json, "direction", (double)events[i].direction);
        cJSON_AddNumberToObject(event_json, "flags", (double)events[i].flags);
        cJSON_AddNumberToObject(event_json, "x_q8", (double)events[i].x_q8);
        cJSON_AddNumberToObject(event_json, "y_q8", (double)events[i].y_q8);
        cJSON_AddItemToArray(events_json, event_json);
    }
    free(events);
    free(tracks);
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static cJSON *app_bridge_dispatch_live_unsubscribe(uint8_t slot, cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_json;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];

    if (app_bridge_live_stream_check(body) == 0) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_live_stream_check(body) != 1) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STREAM_UNKNOWN");
        }
        return result;
    }
    grant->live_active = AICAM_FALSE;
    grant->live_after_cursor = 0u;
    grant->live_view_epoch = 0u;
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    if (result == NULL || payload_json == NULL) {
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddBoolToObject(payload_json, "active", AICAM_FALSE);
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static void app_bridge_revoke_slot(uint8_t slot) {
    if (slot >= APP_BRIDGE_GRANT_SLOTS) {
        return;
    }
    if (g_app_bridge_grants[slot].active != AICAM_TRUE ||
        g_app_bridge_grants[slot].revoked != AICAM_TRUE) {
        g_app_bridge_ui_generation++;
    }
    g_app_bridge_grants[slot].revoked = AICAM_TRUE;
    g_app_bridge_grants[slot].active = AICAM_FALSE;
    g_app_bridge_grants[slot].live_active = AICAM_FALSE;
    g_app_bridge_grants[slot].live_after_cursor = 0u;
    g_app_bridge_grants[slot].live_view_epoch = 0u;
}

static aicam_bool_t app_bridge_sync_descriptor(app_sidecar_descriptor_t *out) {
    const app_install_io_t *io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    app_sidecar_lifecycle_result_t result;
    aicam_bool_t present = AICAM_FALSE;

    io = app_installer_device_io();
    if (io == NULL) {
        return AICAM_FALSE;
    }
    if (app_install_query_committed(io, &native, &present) != APP_INSTALL_OK ||
        present != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    memset(&trust, 0, sizeof(trust));
    if (strcmp(native.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        memcmp(native.publisher_key_sha256,
               (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
               APP_INSTALL_SHA256_LEN) != 0) {
        return AICAM_FALSE;
    }
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    memset(out, 0, sizeof(*out));
    result = app_sidecar_lifecycle_query(io, &trust, &native, out, &state);
    if (result != APP_SIDECAR_LIFECYCLE_OK ||
        state != APP_SIDECAR_LIFECYCLE_ACTIVE ||
        app_pair_is_authorized(io, &native, out) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_grant_matches(const app_bridge_grant_t *grant,
                                             const app_sidecar_descriptor_t *desc,
                                             uint32_t session_generation) {
    if (memcmp(grant->publisher_key_sha256, desc->publisher_key_sha256,
               APP_SIDECAR_SHA256_LEN) != 0 ||
        memcmp(grant->native_tbs_sha256, desc->native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) != 0 ||
        memcmp(grant->ui_tbs_sha256, desc->ui_tbs_sha256, APP_SIDECAR_SHA256_LEN) != 0 ||
        grant->sidecar_service_generation != desc->service_generation ||
        strcmp(grant->publisher_id, desc->publisher_id) != 0 ||
        strcmp(grant->app_id, desc->app_id) != 0 ||
        grant->native_session_generation != session_generation) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_get_u32(cJSON *item, uint32_t *out) {
    double v;

    if (item == NULL || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
        return AICAM_FALSE;
    }
    v = item->valuedouble;
    if (v > 4294967295.0 || v != (double)((uint32_t)v)) {
        return AICAM_FALSE;
    }
    *out = (uint32_t)v;
    return AICAM_TRUE;
}

static aicam_result_t app_bridge_respond_json(http_handler_context_t *ctx, cJSON *root,
                                              const char *message) {
    char *response_text;
    aicam_result_t rc;

    response_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response_text == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    rc = api_response_success(ctx, response_text, message);
    if (rc != AICAM_OK) {
        cJSON_free(response_text);
    }
    return rc;
}

static void app_bridge_add_projection_identity(cJSON *root, const app_sidecar_descriptor_t *desc,
                                               uint32_t host_boot_generation,
                                               uint32_t native_session_generation,
                                               uint32_t ui_generation) {
    char hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char generation[24];

    cJSON_AddStringToObject(root, "publisher_id", desc->publisher_id);
    cJSON_AddStringToObject(root, "app_id", desc->app_id);
    app_bridge_hex(desc->publisher_key_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "publisher_key_sha256", hex);
    app_bridge_hex(desc->native_tbs_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "native_tbs_sha256", hex);
    app_bridge_hex(desc->ui_tbs_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "ui_tbs_sha256", hex);
    cJSON_AddNumberToObject(root, "version_major", (double)desc->version_major);
    cJSON_AddNumberToObject(root, "version_minor", (double)desc->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", (double)desc->version_patch);
    cJSON_AddNumberToObject(root, "host_ui_abi", (double)desc->host_ui_abi);
    snprintf(generation, sizeof(generation), "%lu",
             (unsigned long)desc->service_generation);
    cJSON_AddStringToObject(root, "sidecar_service_generation", generation);
    cJSON_AddNumberToObject(root, "host_boot_generation", (double)host_boot_generation);
    cJSON_AddNumberToObject(root, "native_session_generation",
                            (double)native_session_generation);
    cJSON_AddNumberToObject(root, "ui_generation", (double)ui_generation);
}

static void app_bridge_add_bridge_identity(cJSON *root, const app_sidecar_descriptor_t *desc,
                                           uint32_t host_boot_generation,
                                           uint32_t native_session_generation,
                                           uint32_t ui_generation) {
    char hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char build_id[32];

    cJSON_AddStringToObject(root, "publisher_id", desc->publisher_id);
    cJSON_AddStringToObject(root, "app_id", desc->app_id);
    app_bridge_hex(desc->publisher_key_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "publisher_key_sha256", hex);
    app_bridge_hex(desc->native_tbs_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "native_tbs_sha256", hex);
    app_bridge_hex(desc->ui_tbs_sha256, APP_SIDECAR_SHA256_LEN, hex);
    cJSON_AddStringToObject(root, "ui_tbs_sha256", hex);
    snprintf(build_id, sizeof(build_id), "%u.%u.%u",
             (unsigned int)desc->version_major,
             (unsigned int)desc->version_minor,
             (unsigned int)desc->version_patch);
    cJSON_AddStringToObject(root, "build_id", build_id);
    cJSON_AddNumberToObject(root, "host_boot_generation", (double)host_boot_generation);
    cJSON_AddNumberToObject(root, "native_session_generation",
                            (double)native_session_generation);
    cJSON_AddNumberToObject(root, "ui_generation", (double)ui_generation);
    cJSON_AddNumberToObject(root, "sidecar_service_generation",
                            (double)desc->service_generation);
}

static aicam_result_t app_sidecar_ui_handler(http_handler_context_t *ctx) {
    cJSON *root;
    app_sidecar_descriptor_t desc;
    app_control_status_t control;
    char base_path[64];

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("sidecar ui: refused, high-privilege administrator required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    if (!app_bridge_sync_descriptor(&desc) || !web_sidecar_projection_refresh()) {
        cJSON_AddStringToObject(root, "state", "unavailable");
        cJSON_AddStringToObject(root, "reason", "SIDECAR_NOT_ACTIVE");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        return app_bridge_respond_json(ctx, root, "Sidecar UI projection");
    }
    memset(&control, 0, sizeof(control));
    app_host_control_status(&control);
    cJSON_AddStringToObject(root, "state", "active");
    app_bridge_add_projection_identity(root, &desc, g_app_bridge_boot_generation,
                                       control.session_generation, g_app_bridge_ui_generation);
    snprintf(base_path, sizeof(base_path), "/appsite/g%lu/",
             (unsigned long)desc.service_generation);
    cJSON_AddStringToObject(root, "ui_base_path", base_path);
    cJSON_AddStringToObject(root, "entry_path", "index.html");
    cJSON_AddNullToObject(root, "app_site_origin");
    cJSON_AddNumberToObject(root, "file_count", (double)web_sidecar_projection_file_count());
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    return app_bridge_respond_json(ctx, root, "Sidecar UI projection");
}

static aicam_result_t app_bridge_grant_handler(http_handler_context_t *ctx) {
    cJSON *request_json;
    cJSON *root;
    cJSON *caps_item;
    cJSON *cap_item;
    cJSON *grant_json;
    app_sidecar_descriptor_t desc;
    app_control_status_t control;
    app_bridge_grant_t *grant = NULL;
    uint8_t raw_id[APP_BRIDGE_GRANT_ID_BYTES];
    uint32_t capabilities = 0u;
    uint32_t boot_generation = 0u;
    aicam_bool_t evict_pending = AICAM_FALSE;
    size_t i;
    int slot;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("sidecar bridge: grant refused, high-privilege administrator required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    if (ctx->request.content_length > APP_BRIDGE_BODY_MAX_BYTES) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Request body too large");
    }
    if (!app_bridge_sync_descriptor(&desc)) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddStringToObject(root, "reason", "SIDECAR_NOT_ACTIVE");
        return app_bridge_respond_json(ctx, root, "Sidecar bridge grant result");
    }
    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    caps_item = cJSON_GetObjectItem(request_json, "capabilities");
    if (caps_item == NULL || !cJSON_IsArray(caps_item) ||
        cJSON_GetArraySize(caps_item) < 1 ||
        cJSON_GetArraySize(caps_item) > (int)(sizeof(g_app_bridge_ops) /
                                              sizeof(g_app_bridge_ops[0]))) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid capabilities");
    }
    {
        uint64_t now_epoch = app_bridge_epoch_ms();

        for (slot = 0; slot < (int)APP_BRIDGE_GRANT_SLOTS; slot++) {
            app_bridge_grant_t *entry = &g_app_bridge_grants[slot];

            if (entry->active == AICAM_TRUE && now_epoch > entry->expires_ms) {
                entry->active = AICAM_FALSE;
                entry->live_active = AICAM_FALSE;
            }
            if (entry->active != AICAM_TRUE) {
                grant = entry;
                break;
            }
        }
    }
    if (grant == NULL) {
        int evict_slot = -1;
        uint64_t oldest_used_ms = 0u;
        uint64_t now_epoch = app_bridge_epoch_ms();

        for (slot = 0; slot < (int)APP_BRIDGE_GRANT_SLOTS; slot++) {
            if (g_app_bridge_grants[slot].active != AICAM_TRUE) {
                continue;
            }
            if (now_epoch - g_app_bridge_grants[slot].last_used_ms <=
                APP_BRIDGE_GRANT_INACTIVE_MS) {
                continue;
            }
            if (evict_slot < 0 ||
                g_app_bridge_grants[slot].last_used_ms < oldest_used_ms) {
                evict_slot = slot;
                oldest_used_ms = g_app_bridge_grants[slot].last_used_ms;
            }
        }
        if (evict_slot >= 0) {
            slot = evict_slot;
            evict_pending = AICAM_TRUE;
        }
    }
    if (grant == NULL && evict_pending != AICAM_TRUE) {
        cJSON_Delete(request_json);
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddStringToObject(root, "reason", "GRANT_LIMIT_REACHED");
        return app_bridge_respond_json(ctx, root, "Sidecar bridge grant result");
    }
    caps_item = cJSON_GetObjectItem(request_json, "capabilities");
    {
        cJSON_ArrayForEach(cap_item, caps_item) {
            const app_bridge_op_map_t *op = app_bridge_op_lookup(
                (cap_item != NULL && cJSON_IsString(cap_item)) ? cap_item->valuestring : NULL);
            if (op == NULL) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "Unknown capability requested");
            }
            capabilities |= op->bit;
        }
    }
    cJSON_Delete(request_json);
    if (!app_bridge_random(raw_id, sizeof(raw_id))) {
        LOG_SVC_ERROR("sidecar bridge: grant refused, secure RNG unavailable");
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                  "Bridge grant RNG unavailable");
    }
    do {
        uint8_t boot_raw[sizeof(boot_generation)];
        if (!app_bridge_random(boot_raw, sizeof(boot_raw))) {
            LOG_SVC_ERROR("sidecar bridge: grant refused, secure RNG unavailable");
            return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                      "Bridge grant RNG unavailable");
        }
        boot_generation = ((uint32_t)boot_raw[0]) |
                          ((uint32_t)boot_raw[1] << 8) |
                          ((uint32_t)boot_raw[2] << 16) |
                          ((uint32_t)boot_raw[3] << 24);
    } while (boot_generation == 0u);
    if (evict_pending == AICAM_TRUE) {
        LOG_SVC_WARN("sidecar bridge: reclaiming inactive grant slot=%d app=%s",
                     slot, g_app_bridge_grants[slot].app_id);
        app_bridge_revoke_slot((uint8_t)slot);
        grant = &g_app_bridge_grants[slot];
    }
    memset(grant, 0, sizeof(*grant));
    grant->capabilities = capabilities;
    app_bridge_hex(raw_id, sizeof(raw_id), grant->grant_id);
    grant->host_boot_generation = boot_generation;
    g_app_bridge_ui_generation++;
    grant->ui_generation = g_app_bridge_ui_generation;
    grant->sidecar_service_generation = desc.service_generation;
    memcpy(grant->publisher_key_sha256, desc.publisher_key_sha256, APP_SIDECAR_SHA256_LEN);
    memcpy(grant->native_tbs_sha256, desc.native_tbs_sha256, APP_SIDECAR_SHA256_LEN);
    memcpy(grant->ui_tbs_sha256, desc.ui_tbs_sha256, APP_SIDECAR_SHA256_LEN);
    snprintf(grant->publisher_id, sizeof(grant->publisher_id), "%s", desc.publisher_id);
    snprintf(grant->app_id, sizeof(grant->app_id), "%s", desc.app_id);
    grant->version_major = desc.version_major;
    grant->version_minor = desc.version_minor;
    grant->version_patch = desc.version_patch;
    memset(&control, 0, sizeof(control));
    app_host_control_status(&control);
    grant->native_session_generation = control.session_generation;
    grant->issued_ms = app_bridge_epoch_ms();
    grant->expires_ms = grant->issued_ms + APP_BRIDGE_GRANT_TTL_MS;
    grant->last_used_ms = grant->issued_ms;
    grant->active = AICAM_TRUE;
    grant->revoked = AICAM_FALSE;
    for (i = 0u; i < APP_BRIDGE_REPLAY_RING; i++) {
        grant->replay[i].op_hash = 0u;
        grant->replay[i].request_id = 0u;
    }
    grant->replay_next = 0u;
    LOG_SVC_INFO("sidecar bridge: grant issued app=%s slot=%d caps=0x%08x",
                 grant->app_id, slot, (unsigned int)grant->capabilities);

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(root, "status", "granted");
    grant_json = cJSON_CreateObject();
    if (grant_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(grant_json, "grant_id", grant->grant_id);
    cJSON_AddNumberToObject(grant_json, "expires_at_ms", (double)grant->expires_ms);
    cJSON_AddNumberToObject(grant_json, "issued_at_ms", (double)grant->issued_ms);
    {
        cJSON *caps_out = cJSON_AddArrayToObject(root, "capabilities");
        if (caps_out == NULL) {
            cJSON_Delete(grant_json);
            cJSON_Delete(root);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        for (i = 0u; i < sizeof(g_app_bridge_ops) / sizeof(g_app_bridge_ops[0]); i++) {
            if ((grant->capabilities & g_app_bridge_ops[i].bit) != 0u) {
                cJSON_AddItemToArray(caps_out, cJSON_CreateString(g_app_bridge_ops[i].op));
            }
        }
    }
    {
        cJSON *identity = cJSON_CreateObject();
        if (identity == NULL) {
            cJSON_Delete(grant_json);
            cJSON_Delete(root);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        app_bridge_add_bridge_identity(identity, &desc, grant->host_boot_generation,
                                       grant->native_session_generation, grant->ui_generation);
        cJSON_AddItemToObject(grant_json, "identity", identity);
    }
    cJSON_AddItemToObject(root, "grant", grant_json);
    return app_bridge_respond_json(ctx, root, "Sidecar bridge grant result");
}

static aicam_bool_t app_bridge_seen_request(app_bridge_grant_t *grant, uint32_t op_hash,
                                            uint32_t request_id) {
    uint8_t i;

    for (i = 0u; i < APP_BRIDGE_REPLAY_RING; i++) {
        if (grant->replay[i].request_id == request_id &&
            grant->replay[i].op_hash == op_hash) {
            return AICAM_TRUE;
        }
    }
    grant->replay[grant->replay_next].op_hash = op_hash;
    grant->replay[grant->replay_next].request_id = request_id;
    grant->replay_next = (uint8_t)((grant->replay_next + 1u) % APP_BRIDGE_REPLAY_RING);
    return AICAM_FALSE;
}

static aicam_bool_t app_bridge_consume_confirm(uint8_t grant_slot, const char *grant_id,
                                               uint32_t op_bit, const char *token) {
    int i;

    if (token == NULL || grant_id == NULL ||
        strlen(token) != APP_BRIDGE_GRANT_ID_HEX) {
        return AICAM_FALSE;
    }
    for (i = 0; i < (int)APP_BRIDGE_CONFIRM_SLOTS; i++) {
        app_bridge_confirm_token_t *entry = &g_app_bridge_confirms[i];
        if (entry->used != AICAM_TRUE || entry->grant_slot != grant_slot ||
            strcmp(entry->grant_id, grant_id) != 0 ||
            entry->op_bit != op_bit) {
            continue;
        }
        if (app_bridge_epoch_ms() > entry->expires_ms) {
            entry->used = AICAM_FALSE;
            continue;
        }
        {
            volatile const char *a = entry->token;
            volatile const char *b = token;
            uint8_t diff = 0u;
            size_t k;
            for (k = 0u; k <= APP_BRIDGE_GRANT_ID_HEX; k++) {
                diff |= (uint8_t)(a[k] ^ b[k]);
            }
            if (diff != 0u) {
                continue;
            }
        }
        entry->used = AICAM_FALSE;
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static aicam_bool_t app_bridge_payload_u32(cJSON *payload, const char *key, uint32_t *out) {
    return app_bridge_get_u32(cJSON_GetObjectItem(payload, key), out);
}


#define APP_BRIDGE_LC_CFG_SIZE 144u

static void app_bridge_cfg_put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void app_bridge_cfg_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static aicam_bool_t app_bridge_cfg_name(const cJSON *obj, const char *key,
                                        uint8_t *dst, uint32_t slot,
                                        uint8_t *len_out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    const char *text;
    uint32_t len;
    uint32_t i;

    if (item == NULL || !cJSON_IsString(item)) {
        return AICAM_FALSE;
    }
    text = item->valuestring;
    len = (uint32_t)strlen(text);
    if (len == 0u || len >= slot) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '"' || c == '\\' || c == 0x7Fu) {
            return AICAM_FALSE;
        }
    }
    memset(dst, 0, slot);
    memcpy(dst, text, len);
    *len_out = (uint8_t)len;
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_cfg_u32(const cJSON *obj, const char *key,
                                       uint32_t lo, uint32_t hi, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);

    if (item == NULL || !cJSON_IsNumber(item)) {
        return AICAM_FALSE;
    }
    if (item->valuedouble < (double)lo || item->valuedouble > (double)hi ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return AICAM_FALSE;
    }
    *out = (uint32_t)item->valuedouble;
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_cfg_encode(const cJSON *candidate, uint8_t out[APP_BRIDGE_LC_CFG_SIZE])
{
    uint32_t schema;
    uint32_t x1, y1, x2, y2, ox, oy;
    uint32_t conf, dist, track_k, max_miss, k_confirm, window_min;
    const cJSON *line_item;
    const cJSON *flag;
    uint8_t target_len = 0u;
    uint8_t counter_len = 0u;
    uint8_t toggles = 0u;

    if (!app_bridge_cfg_u32(candidate, "schema", 1u, 1u, &schema)) {
        return AICAM_FALSE;
    }
    line_item = cJSON_GetObjectItem(candidate, "line");
    if (line_item == NULL || !cJSON_IsObject(line_item)) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_u32(line_item, "x1", 0u, 1000u, &x1) ||
        !app_bridge_cfg_u32(line_item, "y1", 0u, 1000u, &y1) ||
        !app_bridge_cfg_u32(line_item, "x2", 0u, 1000u, &x2) ||
        !app_bridge_cfg_u32(line_item, "y2", 0u, 1000u, &y2) ||
        !app_bridge_cfg_u32(line_item, "outside_x", 0u, 1000u, &ox) ||
        !app_bridge_cfg_u32(line_item, "outside_y", 0u, 1000u, &oy)) {
        return AICAM_FALSE;
    }
    if (x1 == x2 && y1 == y2) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_u32(candidate, "conf_threshold_permille", 0u, 1000u, &conf) ||
        !app_bridge_cfg_u32(candidate, "max_dist_permille", 1u, 1000u, &dist) ||
        !app_bridge_cfg_u32(candidate, "track_history_k", 4u, 16u, &track_k) ||
        !app_bridge_cfg_u32(candidate, "max_miss", 1u, 255u, &max_miss) ||
        !app_bridge_cfg_u32(candidate, "k_confirm", 1u, 255u, &k_confirm) ||
        !app_bridge_cfg_u32(candidate, "window_minutes", 1u, 1440u, &window_min)) {
        return AICAM_FALSE;
    }
    flag = cJSON_GetObjectItem(candidate, "tracks_report_enable");
    if (flag == NULL || !cJSON_IsBool(flag)) {
        return AICAM_FALSE;
    }
    if (cJSON_IsTrue(flag)) {
        toggles |= 0x01u;
    }
    flag = cJSON_GetObjectItem(candidate, "heat_grid_enable");
    if (flag == NULL || !cJSON_IsBool(flag)) {
        return AICAM_FALSE;
    }
    if (cJSON_IsTrue(flag)) {
        toggles |= 0x02u;
    }

    memset(out, 0, APP_BRIDGE_LC_CFG_SIZE);
    if (!app_bridge_cfg_name(candidate, "target_class_name",
                             out + 17u, 32u, &target_len)) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_name(candidate, "counter_name",
                             out + 50u, 64u, &counter_len)) {
        return AICAM_FALSE;
    }
    app_bridge_cfg_put_u32(out + 0u, 0x3343434Cu);
    app_bridge_cfg_put_u32(out + 4u, 1u);
    app_bridge_cfg_put_u32(out + 8u, APP_BRIDGE_LC_CFG_SIZE);
    app_bridge_cfg_put_u32(out + 12u, 0u);
    out[16u] = target_len;
    out[49u] = counter_len;
    app_bridge_cfg_put_u16(out + 114u, x1);
    app_bridge_cfg_put_u16(out + 116u, y1);
    app_bridge_cfg_put_u16(out + 118u, x2);
    app_bridge_cfg_put_u16(out + 120u, y2);
    app_bridge_cfg_put_u16(out + 122u, ox);
    app_bridge_cfg_put_u16(out + 124u, oy);
    app_bridge_cfg_put_u16(out + 126u, conf);
    app_bridge_cfg_put_u16(out + 128u, dist);
    out[130u] = (uint8_t)track_k;
    out[131u] = (uint8_t)max_miss;
    out[132u] = (uint8_t)k_confirm;
    out[133u] = toggles;
    app_bridge_cfg_put_u16(out + 134u, window_min);
    return AICAM_TRUE;
}

#define APP_BRIDGE_CONF_MIRROR_RID_BASE 0x88000000u

static uint32_t g_app_bridge_conf_mirror_rid = APP_BRIDGE_CONF_MIRROR_RID_BASE;

static uint32_t app_bridge_conf_host_permille(void)
{
    uint32_t percent = ai_get_confidence_threshold();

    if (percent > 100u) {
        percent = 100u;
    }
    return percent * 10u;
}

static aicam_bool_t app_bridge_cfg_move_num(const cJSON *src, const char *src_key,
                                            cJSON *dst, const char *dst_key)
{
    const cJSON *item = cJSON_GetObjectItem(src, src_key);

    if (item == NULL || !cJSON_IsNumber(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddNumberToObject(dst, dst_key, item->valuedouble) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static aicam_bool_t app_bridge_cfg_move_str(const cJSON *src, const char *key,
                                            cJSON *dst)
{
    const cJSON *item = cJSON_GetObjectItem(src, key);

    if (item == NULL || !cJSON_IsString(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddStringToObject(dst, key, item->valuestring) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static aicam_bool_t app_bridge_cfg_move_bool(const cJSON *src, const char *key,
                                             cJSON *dst)
{
    const cJSON *item = cJSON_GetObjectItem(src, key);

    if (item == NULL || !cJSON_IsBool(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddBoolToObject(dst, key, cJSON_IsTrue(item) ? 1 : 0) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static void app_bridge_conf_mirror_register(const cJSON *active,
                                            uint32_t expected_gen)
{
    const cJSON *line = cJSON_GetObjectItem(active, "line");
    const cJSON *outside = cJSON_GetObjectItem(active, "outside");
    cJSON *candidate;
    cJSON *cand_line;
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];
    app_control_request_t req;
    aicam_bool_t ok;

    if (line == NULL || !cJSON_IsObject(line) ||
        outside == NULL || !cJSON_IsObject(outside)) {
        return;
    }
    candidate = cJSON_CreateObject();
    cand_line = cJSON_CreateObject();
    if (candidate == NULL || cand_line == NULL) {
        cJSON_Delete(candidate);
        cJSON_Delete(cand_line);
        return;
    }
    ok = app_bridge_cfg_move_num(line, "x1_permille", cand_line, "x1");
    ok = ok && app_bridge_cfg_move_num(line, "y1_permille", cand_line, "y1");
    ok = ok && app_bridge_cfg_move_num(line, "x2_permille", cand_line, "x2");
    ok = ok && app_bridge_cfg_move_num(line, "y2_permille", cand_line, "y2");
    ok = ok && app_bridge_cfg_move_num(outside, "x_permille", cand_line, "outside_x");
    ok = ok && app_bridge_cfg_move_num(outside, "y_permille", cand_line, "outside_y");
    ok = ok && (cJSON_AddNumberToObject(candidate, "schema", 1.0) != NULL);
    ok = ok && app_bridge_cfg_move_str(active, "counter_name", candidate);
    ok = ok && app_bridge_cfg_move_str(active, "target_class_name", candidate);
    ok = ok && app_bridge_cfg_move_num(active, "conf_threshold_permille", candidate,
                                       "conf_threshold_permille");
    ok = ok && app_bridge_cfg_move_num(active, "max_dist_permille", candidate,
                                       "max_dist_permille");
    ok = ok && app_bridge_cfg_move_num(active, "track_history_k", candidate,
                                       "track_history_k");
    ok = ok && app_bridge_cfg_move_num(active, "max_miss", candidate, "max_miss");
    ok = ok && app_bridge_cfg_move_num(active, "k_confirm", candidate, "k_confirm");
    ok = ok && app_bridge_cfg_move_num(active, "window_minutes", candidate,
                                       "window_minutes");
    ok = ok && app_bridge_cfg_move_bool(active, "tracks_report_enable", candidate);
    ok = ok && app_bridge_cfg_move_bool(active, "heat_grid_enable", candidate);
    if (ok == AICAM_TRUE) {
        if (cJSON_AddItemToObject(candidate, "line", cand_line) != 0) {
            cand_line = NULL;
        } else {
            ok = AICAM_FALSE;
        }
    }
    if (ok == AICAM_TRUE &&
        app_bridge_cfg_encode(candidate, blob) == AICAM_TRUE) {
        memset(&req, 0, sizeof(req));
        g_app_bridge_conf_mirror_rid++;
        req.request_id = g_app_bridge_conf_mirror_rid;
        req.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
        req.payload = blob;
        req.payload_len = APP_BRIDGE_LC_CFG_SIZE;
        req.expected_config_generation = expected_gen;
        (void)app_host_control_register(&req);
    }
    cJSON_Delete(candidate);
    cJSON_Delete(cand_line);
}

static char *app_bridge_conf_readthrough_payload(uint8_t *payload_buf,
                                                 uint32_t payload_len,
                                                 uint32_t config_generation)
{
    cJSON *parsed;
    cJSON *active;
    cJSON *conf;
    char *text = NULL;
    uint32_t host_permille;

    (void)payload_len;
    parsed = cJSON_Parse((const char *)payload_buf);
    if (parsed == NULL) {
        return NULL;
    }
    active = cJSON_GetObjectItem(parsed, "active_config");
    conf = (active != NULL && cJSON_IsObject(active))
               ? cJSON_GetObjectItem(active, "conf_threshold_permille")
               : NULL;
    if (conf != NULL && cJSON_IsNumber(conf)) {
        host_permille = app_bridge_conf_host_permille();
        if ((uint32_t)conf->valueint != host_permille) {
            if (cJSON_ReplaceItemInObject(
                    active, "conf_threshold_permille",
                    cJSON_CreateNumber((double)host_permille)) == 1) {
                app_bridge_conf_mirror_register(active, config_generation);
                text = cJSON_PrintUnformatted(parsed);
            }
        }
    }
    cJSON_Delete(parsed);
    return text;
}

static void app_bridge_attach_apply_echo(const app_bridge_grant_t *grant,
                                         const app_control_status_t *st,
                                         uint32_t deadline, cJSON *result) {
    cJSON *payload_json;
    uint8_t *payload_buf;
    app_control_snapshot_view_t view;
    uint32_t payload_len = 0u;
    uint32_t attempts = 0u;
    int32_t rc = APP_CONTROL_NOT_FOUND;
    aicam_bool_t reconciled = AICAM_FALSE;

    payload_json = cJSON_CreateObject();
    payload_buf = (uint8_t *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
    if (payload_json == NULL || payload_buf == NULL) {
        cJSON_Delete(payload_json);
        free(payload_buf);
        return;
    }
    memset(&view, 0, sizeof(view));
    while (attempts < APP_BRIDGE_SNAPSHOT_POLL_MAX &&
           app_bridge_now_ms() < deadline) {
        rc = app_host_control_snapshot_read(&view, payload_buf,
                                            APP_CONTROL_SNAPSHOT_RAM_MAX,
                                            &payload_len);
        if (rc == APP_CONTROL_OK &&
            view.session_generation == grant->native_session_generation &&
            view.config_generation == st->result_config_generation) {
            reconciled = AICAM_TRUE;
            break;
        }
        attempts++;
        app_bridge_sleep_ms(APP_BRIDGE_POLL_MS);
    }
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddNumberToObject(payload_json, "config_generation",
                            (double)st->result_config_generation);
    if (reconciled == AICAM_TRUE && payload_len > 0u &&
        payload_len <= APP_CONTROL_SNAPSHOT_RAM_MAX) {
        char *conf_text;

        payload_buf[payload_len] = '\0';
        conf_text = app_bridge_conf_readthrough_payload(payload_buf, payload_len,
                                                        view.config_generation);
        if (cJSON_AddRawToObject(payload_json, "snapshot",
                                 (conf_text != NULL) ? conf_text
                                                     : (const char *)payload_buf) == NULL) {
            cJSON_free(conf_text);
            cJSON_Delete(payload_json);
            free(payload_buf);
            return;
        }
        cJSON_free(conf_text);
    }
    free(payload_buf);
    cJSON_AddItemToObject(result, "payload", payload_json);
}

static cJSON *app_bridge_dispatch_config_apply(uint32_t slot,
                                               cJSON *body, uint32_t request_id,
                                               const char *op) {
    cJSON *result;
    cJSON *payload_item;
    cJSON *content_item;
    cJSON *gen_item;
    char *payload_text;
    uint8_t cfg_blob[APP_BRIDGE_LC_CFG_SIZE];
    app_control_request_t req;
    app_control_status_t st;
    app_control_register_result_t reg = APP_CONTROL_REG_OK;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    uint32_t expected_gen = 0u;
    uint32_t session_generation = 0u;
    uint32_t deadline;
    size_t compact_len;
    aicam_bool_t confirmed = AICAM_FALSE;
    aicam_bool_t is_reset = (strcmp(op, "counters.reset") == 0) ? AICAM_TRUE : AICAM_FALSE;
    const char *confirm_token = NULL;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "session_generation", &session_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "SESSION_GENERATION_REQUIRED");
        }
        return result;
    }
    if (session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STALE_SESSION_GENERATION");
        }
        return result;
    }
    gen_item = cJSON_GetObjectItem(body, "expected_config_generation");
    if (gen_item != NULL) {
        if (app_bridge_get_u32(gen_item, &expected_gen) != AICAM_TRUE || expected_gen == 0u) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "CONFIG_GENERATION_INVALID");
            }
            return result;
        }
    } else if (is_reset) {
        memset(&st, 0, sizeof(st));
        app_host_control_status(&st);
        expected_gen = (st.has_known_config_generation == AICAM_TRUE)
                           ? st.known_config_generation
                           : grant->native_session_generation;
        if (expected_gen == 0u) {
            expected_gen = 1u;
        }
    } else {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CONFIG_GENERATION_REQUIRED");
        }
        return result;
    }
    if (is_reset) {
        confirm_token = cJSON_GetObjectItem(body, "confirm_token") != NULL
                            ? cJSON_GetObjectItem(body, "confirm_token")->valuestring
                            : NULL;
        confirmed = app_bridge_consume_confirm((uint8_t)slot, grant->grant_id,
                                               APP_BRIDGE_CAP_COUNTERS_RESET, confirm_token);
        if (confirmed != AICAM_TRUE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "ADMIN_CONFIRM_REQUIRED");
            }
            return result;
        }
        payload_text = NULL;
        compact_len = 0u;
    } else {
        content_item = cJSON_GetObjectItem(payload_item, "candidate");
        if (content_item == NULL || !cJSON_IsObject(content_item)) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "CANDIDATE_REQUIRED");
            }
            return result;
        }
        if (app_bridge_cfg_encode(content_item, cfg_blob) != AICAM_TRUE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "CANDIDATE_INVALID");
            }
            return result;
        }
        payload_text = NULL;
        compact_len = APP_BRIDGE_LC_CFG_SIZE;
    }
    memset(&req, 0, sizeof(req));
    req.request_id = request_id;
    req.msg_type = is_reset ? APP_CONTROL_MSG_TYPE_COUNTERS_RESET
                            : APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
    req.payload = (const uint8_t *)((payload_text != NULL && compact_len > 0u)
                                      ? (const void *)payload_text
                                      : (compact_len > 0u ? (const void *)cfg_blob : NULL));
    req.payload_len = (uint32_t)compact_len;
    req.expected_config_generation = expected_gen;
    if (confirmed) {
        req.flags |= APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
    }
    reg = app_host_control_register(&req);
    free(payload_text);
    if (reg != APP_CONTROL_REG_OK && reg != APP_CONTROL_REG_CACHED) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason",
                                    app_host_control_register_result_str(reg));
        }
        return result;
    }
    deadline = app_bridge_now_ms() + APP_BRIDGE_CONFIG_WAIT_MS;
    for (;;) {
        memset(&st, 0, sizeof(st));
        app_host_control_status(&st);
        if (st.has_command != AICAM_TRUE ||
            st.command_request_id != request_id) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason", "COMMAND_SLOT_OVERRUN");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_APPLIED) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "APPLIED");
                cJSON_AddNumberToObject(result, "config_generation",
                                        (double)st.result_config_generation);
                cJSON_AddNumberToObject(result, "state_revision",
                                        (double)st.result_state_revision);
                if (is_reset == AICAM_FALSE && reg == APP_CONTROL_REG_OK) {
                    uint32_t conf_permille = (uint32_t)cfg_blob[126u] |
                                             ((uint32_t)cfg_blob[127u] << 8);
                    uint32_t percent = (conf_permille + 5u) / 10u;
                    aicam_bool_t host_synced;

                    if (percent < 1u) {
                        percent = 1u;
                    }
                    if (percent > 100u) {
                        percent = 100u;
                    }
                    host_synced = (ai_set_confidence_threshold(percent) == AICAM_OK)
                                      ? AICAM_TRUE
                                      : AICAM_FALSE;
                    cJSON_AddBoolToObject(result, "host_threshold_synced",
                                          (host_synced == AICAM_TRUE) ? 1 : 0);
                }
                app_bridge_attach_apply_echo(grant, &st, deadline, result);
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_STALE_CONFIG) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "CONFLICT");
                cJSON_AddStringToObject(result, "reason", "STALE_CONFIG_GENERATION");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_UNCONFIRMED) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason", "DESTRUCTIVE_UNCONFIRMED");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_FAILED_STATE_CONFLICT) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "CONFLICT");
                cJSON_AddStringToObject(result, "reason", "STATE_CONFLICT");
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_REJECTED_INVALID ||
            st.command_state == APP_CONTROL_CMD_REJECTED_RESOURCE) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "REJECTED");
                cJSON_AddStringToObject(result, "reason",
                                        app_host_control_cmd_state_str(st.command_state));
            }
            return result;
        }
        if (st.command_state == APP_CONTROL_CMD_FAILED_STORAGE_IO ||
            st.command_state == APP_CONTROL_CMD_FAILED_STORAGE_UNKNOWN) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason",
                                        app_host_control_cmd_state_str(st.command_state));
            }
            return result;
        }
        if (app_bridge_now_ms() >= deadline) {
            result = cJSON_CreateObject();
            if (result != NULL) {
                cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
                cJSON_AddStringToObject(result, "reason", "ACK_TIMEOUT");
            }
            return result;
        }
        app_bridge_sleep_ms(APP_BRIDGE_POLL_MS);
    }
}

static void app_bridge_report_policy_defaults(app_report_policy_values_t *values) {
    values->mqtt_enabled = AICAM_TRUE;
    values->webhook_enabled = AICAM_TRUE;
    values->backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
}

static void app_bridge_report_policy_identity(const app_bridge_grant_t *grant,
                                              app_report_policy_identity_t *identity) {
    memset(identity, 0, sizeof(*identity));
    snprintf(identity->publisher_id, sizeof(identity->publisher_id), "%s", grant->publisher_id);
    snprintf(identity->app_id, sizeof(identity->app_id), "%s", grant->app_id);
    memcpy(identity->publisher_key_sha256, grant->publisher_key_sha256, APP_SIDECAR_SHA256_LEN);
    memcpy(identity->native_tbs_sha256, grant->native_tbs_sha256, APP_SIDECAR_SHA256_LEN);
}

static cJSON *app_bridge_report_policy_config(const app_report_policy_values_t *values) {
    cJSON *config_json = cJSON_CreateObject();

    if (config_json == NULL) {
        return NULL;
    }
    cJSON_AddBoolToObject(config_json, "mqtt_enabled",
                          (values->mqtt_enabled == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddBoolToObject(config_json, "webhook_enabled",
                          (values->webhook_enabled == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddNumberToObject(config_json, "backlog_capacity", (double)values->backlog_capacity);
    return config_json;
}

static cJSON *app_bridge_dispatch_report_policy_read(uint8_t slot, cJSON *body) {
    cJSON *result;
    cJSON *payload_json;
    cJSON *config_json;
    app_report_policy_identity_t identity;
    app_report_policy_values_t values;
    uint32_t revision = 0u;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    int32_t rc;

    (void)body;
    app_bridge_report_policy_defaults(&values);
    app_bridge_report_policy_identity(grant, &identity);
    rc = app_report_policy_store_get(&identity, &values, &revision);
    if (rc == APP_REPORT_ABI_NOT_FOUND) {
        app_bridge_report_policy_defaults(&values);
        revision = 0u;
        rc = APP_REPORT_ABI_OK;
    }
    if (rc != APP_REPORT_ABI_OK) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "REPORT_POLICY_STORAGE_UNAVAILABLE");
        }
        return result;
    }
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    config_json = app_bridge_report_policy_config(&values);
    if (result == NULL || payload_json == NULL || config_json == NULL) {
        cJSON_Delete(config_json);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddNumberToObject(payload_json, "generation", (double)revision);
    cJSON_AddItemToObject(payload_json, "config", config_json);
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static cJSON *app_bridge_dispatch_report_policy_apply(uint8_t slot, cJSON *body) {
    cJSON *result = NULL;
    cJSON *payload_item;
    cJSON *candidate_item;
    cJSON *flag_item;
    cJSON *payload_json;
    cJSON *snapshot_json;
    cJSON *config_json;
    app_report_policy_identity_t identity;
    app_report_policy_values_t values;
    app_bridge_grant_t *grant = &g_app_bridge_grants[slot];
    uint32_t session_generation = 0u;
    uint32_t expected_generation = 0u;
    uint32_t committed_revision = 0u;
    int32_t rc;

    payload_item = cJSON_GetObjectItem(body, "payload");
    if (payload_item == NULL || !cJSON_IsObject(payload_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "PAYLOAD_REQUIRED");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "session_generation", &session_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "SESSION_GENERATION_REQUIRED");
        }
        return result;
    }
    if (session_generation != grant->native_session_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "STALE_SESSION_GENERATION");
        }
        return result;
    }
    if (app_bridge_payload_u32(payload_item, "expected_generation", &expected_generation) !=
        AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "POLICY_GENERATION_REQUIRED");
        }
        return result;
    }
    candidate_item = cJSON_GetObjectItem(payload_item, "candidate");
    if (candidate_item == NULL || !cJSON_IsObject(candidate_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CANDIDATE_REQUIRED");
        }
        return result;
    }
    memset(&values, 0, sizeof(values));
    flag_item = cJSON_GetObjectItem(candidate_item, "mqtt_enabled");
    if (flag_item == NULL || !cJSON_IsBool(flag_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CANDIDATE_INVALID");
        }
        return result;
    }
    values.mqtt_enabled = cJSON_IsTrue(flag_item) ? AICAM_TRUE : AICAM_FALSE;
    flag_item = cJSON_GetObjectItem(candidate_item, "webhook_enabled");
    if (flag_item == NULL || !cJSON_IsBool(flag_item)) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CANDIDATE_INVALID");
        }
        return result;
    }
    values.webhook_enabled = cJSON_IsTrue(flag_item) ? AICAM_TRUE : AICAM_FALSE;
    if (app_bridge_cfg_u32(candidate_item, "backlog_capacity",
                           APP_REPORT_POLICY_MIN_CAPACITY, APP_REPORT_POLICY_MAX_CAPACITY,
                           &values.backlog_capacity) != AICAM_TRUE) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "REJECTED");
            cJSON_AddStringToObject(result, "reason", "CANDIDATE_INVALID");
        }
        return result;
    }
    app_bridge_report_policy_identity(grant, &identity);
    rc = app_report_policy_store_commit(&identity, expected_generation, &values,
                                        &committed_revision);
    if (rc == APP_REPORT_ABI_CONFLICT) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "CONFLICT");
            cJSON_AddStringToObject(result, "reason", "POLICY_REVISION_MISMATCH");
        }
        return result;
    }
    if (rc != APP_REPORT_ABI_OK || committed_revision <= expected_generation) {
        result = cJSON_CreateObject();
        if (result != NULL) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "REPORT_POLICY_STORAGE_UNAVAILABLE");
        }
        return result;
    }
    result = cJSON_CreateObject();
    payload_json = cJSON_CreateObject();
    snapshot_json = cJSON_CreateObject();
    config_json = app_bridge_report_policy_config(&values);
    if (result == NULL || payload_json == NULL || snapshot_json == NULL || config_json == NULL) {
        cJSON_Delete(config_json);
        cJSON_Delete(snapshot_json);
        cJSON_Delete(payload_json);
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddStringToObject(result, "outcome", "APPLIED");
    cJSON_AddNumberToObject(payload_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddNumberToObject(payload_json, "generation", (double)committed_revision);
    cJSON_AddNumberToObject(snapshot_json, "session_generation",
                            (double)grant->native_session_generation);
    cJSON_AddNumberToObject(snapshot_json, "generation", (double)committed_revision);
    cJSON_AddItemToObject(snapshot_json, "config", config_json);
    cJSON_AddItemToObject(payload_json, "snapshot", snapshot_json);
    cJSON_AddItemToObject(result, "payload", payload_json);
    return result;
}

static aicam_result_t app_bridge_call_handler(http_handler_context_t *ctx) {
    cJSON *request_json;
    cJSON *root;
    cJSON *result = NULL;
    cJSON *grant_item;
    cJSON *op_item;
    cJSON *rid_item;
    const char *grant_id;
    const char *op;
    const app_bridge_op_map_t *op_map;
    app_bridge_grant_t *grant = NULL;
    app_sidecar_descriptor_t desc;
    app_control_status_t control;
    uint32_t request_id = 0u;
    uint64_t now_epoch;
    int slot;
    int found = -1;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("sidecar bridge: call refused, high-privilege administrator required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    if (ctx->request.content_length > APP_BRIDGE_BODY_MAX_BYTES ||
        ctx->request.content_length > APP_BRIDGE_PAYLOAD_MAX_BYTES) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Request body too large");
    }
    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    grant_item = cJSON_GetObjectItem(request_json, "grant_id");
    op_item = cJSON_GetObjectItem(request_json, "op");
    rid_item = cJSON_GetObjectItem(request_json, "request_id");
    grant_id = (grant_item != NULL && cJSON_IsString(grant_item)) ? grant_item->valuestring : NULL;
    op = (op_item != NULL && cJSON_IsString(op_item)) ? op_item->valuestring : NULL;
    op_map = app_bridge_op_lookup(op);
    if (grant_id == NULL || strlen(grant_id) != APP_BRIDGE_GRANT_ID_HEX ||
        op_map == NULL ||
        app_bridge_get_u32(rid_item, &request_id) != AICAM_TRUE || request_id == 0u) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid bridge request fields");
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(root, "protocol", APP_BRIDGE_PROTOCOL_V1);
    cJSON_AddNumberToObject(root, "request_id", (double)request_id);
    cJSON_AddStringToObject(root, "op", op);
    for (slot = 0; slot < (int)APP_BRIDGE_GRANT_SLOTS; slot++) {
        if (g_app_bridge_grants[slot].active == AICAM_TRUE &&
            strcmp(g_app_bridge_grants[slot].grant_id, grant_id) == 0) {
            grant = &g_app_bridge_grants[slot];
            found = slot;
            break;
        }
    }
    now_epoch = app_bridge_epoch_ms();
    if (grant == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Unknown bridge grant");
    }
    if (grant->revoked == AICAM_TRUE || now_epoch > grant->expires_ms) {
        app_bridge_revoke_slot((uint8_t)found);
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "REJECTED");
        cJSON_AddStringToObject(result, "reason",
                                grant->revoked && now_epoch <= grant->expires_ms
                                    ? "GRANT_REVOKED" : "GRANT_EXPIRED");
        cJSON_AddBoolToObject(root, "revoked", AICAM_TRUE);
        cJSON_AddItemToObject(root, "result", result);
        cJSON_Delete(request_json);
        return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
    }
    if (!app_bridge_sync_descriptor(&desc)) {
        app_bridge_revoke_slot((uint8_t)found);
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "REJECTED");
        cJSON_AddStringToObject(result, "reason", "SIDECAR_NOT_ACTIVE");
        cJSON_AddBoolToObject(root, "revoked", AICAM_TRUE);
        cJSON_AddItemToObject(root, "result", result);
        cJSON_Delete(request_json);
        return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
    }
    grant->expires_ms = now_epoch + APP_BRIDGE_GRANT_TTL_MS;
    memset(&control, 0, sizeof(control));
    app_host_control_status(&control);
    if (!app_bridge_grant_matches(grant, &desc, control.session_generation)) {
        app_bridge_revoke_slot((uint8_t)found);
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "REJECTED");
        cJSON_AddStringToObject(result, "reason", "IDENTITY_OR_GENERATION_CHANGED");
        cJSON_AddBoolToObject(root, "revoked", AICAM_TRUE);
        cJSON_AddItemToObject(root, "result", result);
        cJSON_Delete(request_json);
        return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
    }
    grant->last_used_ms = app_bridge_epoch_ms();
    if ((grant->capabilities & op_map->bit) == 0u) {
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "REJECTED");
        cJSON_AddStringToObject(result, "reason", "OP_NOT_IN_GRANT");
        cJSON_AddItemToObject(root, "result", result);
        cJSON_Delete(request_json);
        return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
    }
    if (app_bridge_seen_request(grant, app_bridge_op_hash(op), request_id)) {
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "REJECTED");
        cJSON_AddStringToObject(result, "reason", "REPLAY");
        cJSON_AddItemToObject(root, "result", result);
        cJSON_Delete(request_json);
        return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
    }
    if (strcmp(op, "snapshot.read") == 0) {
        app_control_snapshot_view_t view;
        uint8_t *payload_buf = (uint8_t *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
        uint32_t payload_len = 0u;
        int32_t rc;

        result = cJSON_CreateObject();
        if (result == NULL || payload_buf == NULL) {
            free(payload_buf);
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        memset(&view, 0, sizeof(view));
        rc = app_host_control_snapshot_read(&view, payload_buf,
                                            APP_CONTROL_SNAPSHOT_RAM_MAX, &payload_len);
        if (rc == APP_CONTROL_NOT_FOUND) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "SNAPSHOT_UNAVAILABLE");
        } else if (rc != APP_CONTROL_OK || payload_len != view.payload_len) {
            cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
            cJSON_AddStringToObject(result, "reason", "SNAPSHOT_READ_FAILED");
        } else if (view.session_generation != grant->native_session_generation) {
            cJSON_AddStringToObject(result, "outcome", "UNKNOWN");
            cJSON_AddStringToObject(result, "reason", "SNAPSHOT_SESSION_MISMATCH");
        } else {
            char *conf_text;

            payload_buf[view.payload_len] = '\0';
            cJSON_AddStringToObject(result, "outcome",
                                    view.freshness == APP_CONTROL_SNAPSHOT_FRESH
                                        ? "APPLIED" : "UNKNOWN");
            cJSON_AddStringToObject(result, "freshness",
                                    app_host_control_snapshot_freshness_str(view.freshness));
            cJSON_AddNumberToObject(result, "config_generation",
                                    (double)view.config_generation);
            cJSON_AddNumberToObject(result, "state_revision", (double)view.state_revision);
            cJSON_AddNumberToObject(result, "age_ms", (double)view.age_ms);
            conf_text = app_bridge_conf_readthrough_payload(payload_buf,
                                                            view.payload_len,
                                                            view.config_generation);
            if (cJSON_AddRawToObject(result, "payload",
                                     (conf_text != NULL) ? conf_text
                                                         : (const char *)payload_buf) == NULL) {
                cJSON_free(conf_text);
                free(payload_buf);
                cJSON_Delete(result);
                cJSON_Delete(root);
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_free(conf_text);
        }
        free(payload_buf);
    } else if (strcmp(op, "config.read") == 0) {
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        if (control.session_active != AICAM_TRUE ||
            control.has_known_config_generation != AICAM_TRUE) {
            cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
            cJSON_AddStringToObject(result, "reason", "SESSION_NOT_ACTIVE");
        } else {
            cJSON_AddStringToObject(result, "outcome", "APPLIED");
            cJSON_AddNumberToObject(result, "config_generation",
                                    (double)control.known_config_generation);
            cJSON_AddNumberToObject(result, "session_generation",
                                    (double)control.session_generation);
        }
    } else if (strcmp(op, "config.apply") == 0 || strcmp(op, "counters.reset") == 0) {
        result = app_bridge_dispatch_config_apply((uint8_t)found, request_json,
                                                  request_id, op);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "report-policy.read") == 0) {
        result = app_bridge_dispatch_report_policy_read((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "report-policy.apply") == 0) {
        result = app_bridge_dispatch_report_policy_apply((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "video.open") == 0) {
        result = app_bridge_dispatch_video_open((uint8_t)found, ctx, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "video.close") == 0) {
        result = app_bridge_dispatch_video_close((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "enable.read") == 0) {
        result = app_bridge_dispatch_enable_read((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "enable.apply") == 0) {
        result = app_bridge_dispatch_enable_apply((uint8_t)found, request_json, request_id);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "live.subscribe") == 0) {
        result = app_bridge_dispatch_live_subscribe((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else if (strcmp(op, "live.unsubscribe") == 0) {
        result = app_bridge_dispatch_live_unsubscribe((uint8_t)found, request_json);
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
    } else {
        result = cJSON_CreateObject();
        if (result == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(result, "outcome", "UNAVAILABLE");
        cJSON_AddStringToObject(result, "reason", "CAPABILITY_NOT_REVIEWED");
    }
    cJSON_AddItemToObject(root, "result", result);
    cJSON_Delete(request_json);
    return app_bridge_respond_json(ctx, root, "Sidecar bridge call result");
}

static aicam_result_t app_bridge_confirm_handler(http_handler_context_t *ctx) {
    cJSON *request_json;
    cJSON *root;
    cJSON *grant_item;
    cJSON *op_item;
    const char *grant_id;
    const char *op;
    const app_bridge_op_map_t *op_map;
    app_bridge_grant_t *grant = NULL;
    app_sidecar_descriptor_t desc;
    uint8_t raw[APP_BRIDGE_GRANT_ID_BYTES];
    int slot;
    int found = -1;
    int token_slot = -1;
    uint64_t now_epoch;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("sidecar bridge: confirm refused, high-privilege administrator required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    grant_item = cJSON_GetObjectItem(request_json, "grant_id");
    op_item = cJSON_GetObjectItem(request_json, "op");
    grant_id = (grant_item != NULL && cJSON_IsString(grant_item)) ? grant_item->valuestring : NULL;
    op = (op_item != NULL && cJSON_IsString(op_item)) ? op_item->valuestring : NULL;
    op_map = app_bridge_op_lookup(op);
    if (grant_id == NULL || strlen(grant_id) != APP_BRIDGE_GRANT_ID_HEX ||
        op_map == NULL || (op_map->bit != APP_BRIDGE_CAP_COUNTERS_RESET &&
                           op_map->bit != APP_BRIDGE_CAP_ENABLE_APPLY)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Invalid confirm request fields");
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    for (slot = 0; slot < (int)APP_BRIDGE_GRANT_SLOTS; slot++) {
        if (g_app_bridge_grants[slot].active == AICAM_TRUE &&
            strcmp(g_app_bridge_grants[slot].grant_id, grant_id) == 0) {
            grant = &g_app_bridge_grants[slot];
            found = slot;
            break;
        }
    }
    now_epoch = app_bridge_epoch_ms();
    if (grant == NULL || grant->revoked == AICAM_TRUE || now_epoch > grant->expires_ms) {
        cJSON_Delete(request_json);
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Unknown bridge grant");
    }
    if (!app_bridge_sync_descriptor(&desc)) {
        cJSON_Delete(request_json);
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Sidecar not active");
    }
    for (slot = 0; slot < (int)APP_BRIDGE_CONFIRM_SLOTS; slot++) {
        if (g_app_bridge_confirms[slot].used != AICAM_TRUE) {
            token_slot = slot;
            break;
        }
    }
    if (token_slot < 0) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                  "Confirm token slots exhausted");
    }
    if (!app_bridge_random(raw, sizeof(raw))) {
        cJSON_Delete(root);
        LOG_SVC_ERROR("sidecar bridge: confirm refused, secure RNG unavailable");
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                  "Bridge confirm RNG unavailable");
    }
    memset(&g_app_bridge_confirms[token_slot], 0, sizeof(app_bridge_confirm_token_t));
    g_app_bridge_confirms[token_slot].used = AICAM_TRUE;
    g_app_bridge_confirms[token_slot].grant_slot = (uint8_t)found;
    snprintf(g_app_bridge_confirms[token_slot].grant_id,
             sizeof(g_app_bridge_confirms[token_slot].grant_id), "%s", grant->grant_id);
    g_app_bridge_confirms[token_slot].op_bit = op_map->bit;
    g_app_bridge_confirms[token_slot].expires_ms = now_epoch + APP_BRIDGE_CONFIRM_TTL_MS;
    app_bridge_hex(raw, sizeof(raw), g_app_bridge_confirms[token_slot].token);
    LOG_SVC_WARN("sidecar bridge: destructive confirm issued app=%s op=%s",
                 grant->app_id, op);
    cJSON_AddStringToObject(root, "status", "confirmed");
    cJSON_AddStringToObject(root, "confirm_token", g_app_bridge_confirms[token_slot].token);
    cJSON_AddNumberToObject(root, "expires_at_ms", (double)g_app_bridge_confirms[token_slot].expires_ms);
    cJSON_Delete(request_json);
    return app_bridge_respond_json(ctx, root, "Destructive operation confirm issued");
}

static aicam_result_t app_bridge_revoke_handler(http_handler_context_t *ctx) {
    cJSON *request_json;
    cJSON *root;
    cJSON *grant_item;
    cJSON *all_item;
    const char *grant_id;
    int slot;
    int count = 0;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (ctx->authenticated != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    request_json = web_api_parse_body(ctx);
    grant_id = NULL;
    if (request_json != NULL) {
        grant_item = cJSON_GetObjectItem(request_json, "grant_id");
        grant_id = (grant_item != NULL && cJSON_IsString(grant_item))
                       ? grant_item->valuestring : NULL;
        all_item = cJSON_GetObjectItem(request_json, "all");
        if (all_item != NULL && cJSON_IsTrue(all_item)) {
            grant_id = NULL;
        }
    }
    for (slot = 0; slot < (int)APP_BRIDGE_GRANT_SLOTS; slot++) {
        if (g_app_bridge_grants[slot].active != AICAM_TRUE) {
            continue;
        }
        if (grant_id != NULL && strcmp(g_app_bridge_grants[slot].grant_id, grant_id) != 0) {
            continue;
        }
        app_bridge_revoke_slot((uint8_t)slot);
        count++;
    }
    for (slot = 0; slot < (int)APP_BRIDGE_CONFIRM_SLOTS; slot++) {
        g_app_bridge_confirms[slot].used = AICAM_FALSE;
    }
    cJSON_Delete(request_json);
    LOG_SVC_INFO("sidecar bridge: revoked %d grant(s)", count);
    cJSON_AddStringToObject(root, "status", "revoked");
    cJSON_AddNumberToObject(root, "count", (double)count);
    return app_bridge_respond_json(ctx, root, "Sidecar bridge grants revoked");
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
