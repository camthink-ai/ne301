#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_control_module.h"

#include "web_api.h"
#include "web_server.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_host_control.h"

#define APP_CONTROL_MAX_B64_CHARS ((((uint32_t)APP_CONTROL_MSG_RAM_MAX + 2u) / 3u) * 4u)
#define APP_CONTROL_B64_DECODE_BUF_MAX (((APP_CONTROL_MAX_B64_CHARS / 4u) * 3u) + 4u)

typedef struct {
    uint32_t request_id;
    uint32_t msg_type;
    uint32_t expected_config_generation;
    uint32_t flags;
    uint8_t payload[APP_CONTROL_B64_DECODE_BUF_MAX];
    uint32_t payload_len;
    app_control_register_result_t reg_result;
} app_control_submit_t;

static aicam_result_t app_control_config_handler(http_handler_context_t *ctx);
static aicam_result_t app_control_reset_handler(http_handler_context_t *ctx);
static aicam_result_t app_control_status_handler(http_handler_context_t *ctx);
static aicam_result_t app_control_snapshot_handler(http_handler_context_t *ctx);

static const api_route_t g_app_control_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/control/config",
        .handler = app_control_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/control/reset",
        .handler = app_control_reset_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/control/status",
        .handler = app_control_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/business/snapshot",
        .handler = app_control_snapshot_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static aicam_result_t app_control_respond_json(http_handler_context_t *ctx, cJSON *root,
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
        free(response_text);
    }
    return rc;
}

static aicam_bool_t app_control_get_u32(cJSON *item, uint32_t *out)
{
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

static void app_control_add_command(cJSON *root)
{
    app_control_status_t st;
    cJSON *cmd;

    app_host_control_status(&st);
    cJSON_AddBoolToObject(root, "session_active",
                          (st.session_active == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddNumberToObject(root, "session_generation", (double)st.session_generation);
    cJSON_AddNumberToObject(root, "control_max", (double)st.control_max);
    cJSON_AddNumberToObject(root, "snapshot_max", (double)st.snapshot_max);
    if (st.has_known_config_generation == AICAM_TRUE) {
        cJSON_AddNumberToObject(root, "known_config_generation",
                                (double)st.known_config_generation);
    }
    cmd = cJSON_CreateObject();
    if (cmd == NULL) {
        return;
    }
    if (st.has_command != AICAM_TRUE) {
        cJSON_AddStringToObject(cmd, "state", app_host_control_cmd_state_str(APP_CONTROL_CMD_NONE));
        cJSON_AddItemToObject(root, "command", cmd);
    } else {
        cJSON_AddStringToObject(cmd, "state", app_host_control_cmd_state_str(st.command_state));
        cJSON_AddNumberToObject(cmd, "request_id", (double)st.command_request_id);
        cJSON_AddStringToObject(cmd, "msg_type",
                                app_host_control_msg_type_str(st.command_msg_type));
        cJSON_AddNumberToObject(cmd, "expected_config_generation",
                                (double)st.command_expected_config_generation);
        cJSON_AddNumberToObject(cmd, "flags", (double)st.command_flags);
        cJSON_AddNumberToObject(cmd, "payload_len", (double)st.command_payload_len);
        if (st.command_state != APP_CONTROL_CMD_PENDING &&
            st.command_state != APP_CONTROL_CMD_DELIVERED) {
            cJSON_AddNumberToObject(cmd, "result_config_generation",
                                    (double)st.result_config_generation);
            cJSON_AddNumberToObject(cmd, "result_state_revision",
                                    (double)st.result_state_revision);
        }
        cJSON_AddItemToObject(root, "command", cmd);
    }
    if (st.unknown_count > 0u) {
        cJSON *unknown = cJSON_CreateObject();
        if (unknown == NULL) {
            return;
        }
        cJSON_AddNumberToObject(unknown, "count", (double)st.unknown_count);
        cJSON_AddNumberToObject(unknown, "session_generation",
                                (double)st.unknown_session_generation);
        cJSON_AddNumberToObject(unknown, "request_id", (double)st.unknown_request_id);
        cJSON_AddStringToObject(unknown, "msg_type",
                                app_host_control_msg_type_str(st.unknown_msg_type));
        cJSON_AddItemToObject(root, "last_unknown", unknown);
    }
}

static aicam_bool_t app_control_parse_common(cJSON *body, app_control_submit_t *sub,
                                             uint32_t msg_type)
{
    cJSON *id_item;
    cJSON *gen_item;
    cJSON *confirm_item;

    memset(sub, 0, sizeof(*sub));
    sub->msg_type = msg_type;
    id_item = cJSON_GetObjectItem(body, "request_id");
    gen_item = cJSON_GetObjectItem(body, "expected_config_generation");
    if (app_control_get_u32(id_item, &sub->request_id) != AICAM_TRUE ||
        sub->request_id == 0u) {
        return AICAM_FALSE;
    }
    if (app_control_get_u32(gen_item, &sub->expected_config_generation) != AICAM_TRUE ||
        sub->expected_config_generation == 0u) {
        return AICAM_FALSE;
    }
    confirm_item = cJSON_GetObjectItem(body, "destructive_confirmed");
    if (confirm_item != NULL && cJSON_IsBool(confirm_item) &&
        cJSON_IsTrue(confirm_item)) {
        sub->flags |= APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
    }
    return AICAM_TRUE;
}

static aicam_bool_t app_control_b64_char_value(uint8_t c, uint32_t *out)
{
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') {
        *out = (uint32_t)(c - (uint8_t)'A');
        return AICAM_TRUE;
    }
    if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
        *out = (uint32_t)(c - (uint8_t)'a') + 26u;
        return AICAM_TRUE;
    }
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
        *out = (uint32_t)(c - (uint8_t)'0') + 52u;
        return AICAM_TRUE;
    }
    if (c == (uint8_t)'+') {
        *out = 62u;
        return AICAM_TRUE;
    }
    if (c == (uint8_t)'/') {
        *out = 63u;
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static aicam_bool_t app_control_b64_plan(const char *s, size_t len, uint32_t *out_len)
{
    size_t i;
    size_t pad;
    uint32_t v;

    if (s == NULL || out_len == NULL) {
        return AICAM_FALSE;
    }
    if (len == 0u || (len % 4u) != 0u) {
        return AICAM_FALSE;
    }
    pad = 0u;
    while (pad < 2u && (uint8_t)s[len - 1u - pad] == (uint8_t)'=') {
        pad++;
    }
    i = 0u;
    while (i < (len - pad)) {
        if (app_control_b64_char_value((uint8_t)s[i], &v) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        i++;
    }
    *out_len = (uint32_t)(((len / 4u) * 3u) - pad);
    return AICAM_TRUE;
}

static aicam_result_t app_control_submit_handler(http_handler_context_t *ctx,
                                                 uint32_t msg_type)
{
    cJSON *request_json = NULL;
    cJSON *response_json = NULL;
    cJSON *b64_item = NULL;
    app_control_submit_t sub;
    app_control_request_t req;
    const char *status_text;
    aicam_bool_t parse_ok;

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app control: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    parse_ok = app_control_parse_common(request_json, &sub, msg_type);
    if (parse_ok == AICAM_TRUE && msg_type == APP_CONTROL_MSG_TYPE_CONFIG_APPLY) {
        size_t b64_len;
        size_t decoded_cap;
        uint32_t decoded_len = 0u;

        b64_item = cJSON_GetObjectItem(request_json, "payload_base64");
        if (b64_item == NULL || !cJSON_IsString(b64_item) ||
            b64_item->valuestring == NULL) {
            parse_ok = AICAM_FALSE;
        } else {
            b64_len = strlen(b64_item->valuestring);
            if (b64_len == 0u || b64_len > (size_t)APP_CONTROL_MAX_B64_CHARS ||
                app_control_b64_plan(b64_item->valuestring, b64_len,
                                     &decoded_len) != AICAM_TRUE ||
                decoded_len > APP_CONTROL_MSG_RAM_MAX) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "payload_base64 invalid or out of bounds");
            }
            decoded_cap = ((b64_len / 4u) * 3u) + 3u;
            if (decoded_cap > sizeof(sub.payload)) {
                decoded_cap = sizeof(sub.payload);
            }
            sub.payload_len = (uint32_t)web_api_base64_decode(b64_item->valuestring,
                                                              sub.payload, decoded_cap);
            if (sub.payload_len == 0u || (uint32_t)decoded_len != sub.payload_len ||
                sub.payload_len > APP_CONTROL_MSG_RAM_MAX) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "payload_base64 decode failed");
            }
        }
    }
    cJSON_Delete(request_json);
    request_json = NULL;

    if (parse_ok != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid control request fields");
    }

    memset(&req, 0, sizeof(req));
    req.request_id = sub.request_id;
    req.msg_type = sub.msg_type;
    req.payload = (sub.payload_len > 0u) ? sub.payload : NULL;
    req.payload_len = sub.payload_len;
    req.expected_config_generation = sub.expected_config_generation;
    req.flags = sub.flags;
    sub.reg_result = app_host_control_register(&req);

    if (sub.reg_result == APP_CONTROL_REG_OK) {
        LOG_SVC_INFO("app control: registered request_id=%u type=%s expected_gen=%u",
                     sub.request_id, app_host_control_msg_type_str(msg_type),
                     sub.expected_config_generation);
        status_text = "registered";
    } else if (sub.reg_result == APP_CONTROL_REG_CACHED) {
        LOG_SVC_INFO("app control: idempotent replay request_id=%u type=%s",
                     sub.request_id, app_host_control_msg_type_str(msg_type));
        status_text = "cached";
    } else if (sub.reg_result == APP_CONTROL_REG_ALREADY) {
        LOG_SVC_INFO("app control: duplicate in-flight request_id=%u type=%s",
                     sub.request_id, app_host_control_msg_type_str(msg_type));
        status_text = "already_registered";
    } else {
        if (sub.reg_result == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT) {
            LOG_SVC_WARN("app control: duplicate id conflict request_id=%u", sub.request_id);
        } else {
            LOG_SVC_WARN("app control: register refused request_id=%u result=%s",
                         sub.request_id,
                         app_host_control_register_result_str(sub.reg_result));
        }
        status_text = "refused";
    }

    response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(response_json, "status", status_text);
    if (sub.reg_result < APP_CONTROL_REG_OK) {
        cJSON_AddStringToObject(response_json, "reason",
                                app_host_control_register_result_str(sub.reg_result));
    }
    app_control_add_command(response_json);
    return app_control_respond_json(ctx, response_json,
                                    "App control submit completed with result");
}

static aicam_result_t app_control_config_handler(http_handler_context_t *ctx)
{
    return app_control_submit_handler(ctx, APP_CONTROL_MSG_TYPE_CONFIG_APPLY);
}

static aicam_result_t app_control_reset_handler(http_handler_context_t *ctx)
{
    return app_control_submit_handler(ctx, APP_CONTROL_MSG_TYPE_COUNTERS_RESET);
}

static aicam_result_t app_control_status_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app control: status refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    app_control_add_command(root);
    return app_control_respond_json(ctx, root, "App control status");
}

static aicam_result_t app_control_snapshot_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;
    app_control_snapshot_view_t view;
    uint8_t *payload_buf = NULL;
    uint32_t payload_len = 0u;
    int32_t rc;

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app snapshot: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    memset(&view, 0, sizeof(view));
    payload_buf = (uint8_t *)malloc((size_t)APP_CONTROL_SNAPSHOT_RAM_MAX + 1u);
    if (payload_buf == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    rc = app_host_control_snapshot_read(&view, payload_buf,
                                        APP_CONTROL_SNAPSHOT_RAM_MAX, &payload_len);

    root = cJSON_CreateObject();
    if (root == NULL) {
        free(payload_buf);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    if (rc == APP_CONTROL_NOT_FOUND) {
        free(payload_buf);
        cJSON_AddStringToObject(root, "availability", "unavailable");
        return app_control_respond_json(ctx, root, "App business snapshot");
    }
    if (rc != APP_CONTROL_OK || payload_len != view.payload_len) {
        free(payload_buf);
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Snapshot query failed");
    }
    payload_buf[payload_len] = '\0';

    cJSON_AddStringToObject(root, "availability",
                            app_host_control_snapshot_freshness_str(view.freshness));
    cJSON_AddNumberToObject(root, "session_generation", (double)view.session_generation);
    cJSON_AddNumberToObject(root, "visible_version", (double)view.visible_version);
    cJSON_AddNumberToObject(root, "config_generation", (double)view.config_generation);
    cJSON_AddNumberToObject(root, "state_revision", (double)view.state_revision);
    cJSON_AddNumberToObject(root, "age_ms", (double)view.age_ms);
    cJSON_AddNumberToObject(root, "payload_len", (double)view.payload_len);
    if (view.publisher_id[0] != '\0' || view.app_id[0] != '\0') {
        cJSON_AddStringToObject(root, "publisher_id", view.publisher_id);
        cJSON_AddStringToObject(root, "app_id", view.app_id);
    }
    if (cJSON_AddRawToObject(root, "payload", (const char *)payload_buf) == NULL) {
        free(payload_buf);
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    free(payload_buf);
    return app_control_respond_json(ctx, root, "App business snapshot");
}

aicam_result_t web_api_register_app_control_module(void)
{
    size_t count = sizeof(g_app_control_routes) / sizeof(g_app_control_routes[0]);
    size_t i;
    aicam_result_t result;

    LOG_SVC_INFO("Registering App Control API module...");

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_control_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_control_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("App Control API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
