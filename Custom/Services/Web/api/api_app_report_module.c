#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_report_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_report_service.h"
#include "app_report_service_device.h"

static aicam_result_t app_report_service_handler(http_handler_context_t *ctx);

static const api_route_t g_app_report_routes[] = {
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/report/service",
        .handler = app_report_service_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

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

    return api_response_json(ctx, root, "App report service status");
}

aicam_result_t web_api_register_app_report_module(void)
{
    size_t count = sizeof(g_app_report_routes) / sizeof(g_app_report_routes[0]);
    size_t i;
    aicam_result_t result;

    LOG_SVC_INFO("Registering App Report API module...");

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_report_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_report_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("App Report API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
