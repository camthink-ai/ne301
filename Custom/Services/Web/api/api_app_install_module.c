#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_install_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_installer.h"
#include "app_host.h"
#include "app_host_run.h"
#include "app_host_session.h"
#include "app_admission.h"
#include "app_host_abi.h"
#include "app_host_live.h"
#include "app_host_control.h"
#include "app_sidecar_lifecycle.h"
#include "app_pair_installer.h"
aicam_bool_t ai_pipeline_is_running(void);
aicam_result_t ai_pipeline_stop(void);
aicam_result_t ai_pipeline_start(void);
aicam_result_t device_service_camera_stop(void);
aicam_result_t device_service_camera_start(void);
#include "app_report_service_device.h"
#include "app_report_service.h"

#define APP_INSTALL_HEX_PER_BYTE 2

typedef struct {
    app_admission_policy_t policy;
} api_app_install_verify_ctx_t;

static void api_sidecar_hex(const uint8_t *data, size_t len, char *out);
static aicam_bool_t api_sidecar_fill_trust(const app_install_record_t *native,
                                           app_sidecar_trust_t *trust);
static cJSON *api_sidecar_descriptor_json(const app_sidecar_descriptor_t *descriptor,
                                          const char *status);

static const api_route_t g_app_install_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/install",
        .handler = app_install_package_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/installed",
        .handler = app_install_state_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "DELETE",
        .path = API_PATH_PREFIX "/app/installed",
        .handler = app_install_remove_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/run",
        .handler = app_install_run_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static app_admission_result_t api_app_install_verify(const uint8_t *data, size_t len,
                                                     app_admission_verdict_t *out,
                                                     void *ctx)
{
    api_app_install_verify_ctx_t *vctx = (api_app_install_verify_ctx_t *)ctx;
    uint32_t container = app_admission_container_version(data, len);

    if (container >= 1u && container <= 4u) {
        return APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT;
    }
    if (container != 5u) {
        return APP_ADMISSION_ERR_UNKNOWN_FORMAT;
    }
    return app_admission_verify_package(data, len, &vctx->policy, NULL, out);
}

static aicam_bool_t api_app_install_fill_ctx(api_app_install_verify_ctx_t *vctx)
{
    uint32_t base = 0;
    uint32_t size = 0;

    memset(vctx, 0, sizeof(*vctx));

    vctx->policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    vctx->policy.psram_mib = 64u;
    vctx->policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    vctx->policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                       APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                       APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    vctx->policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    vctx->policy.event_capacity = 16384u;
    vctx->policy.report_capacity = 16384u;
    vctx->policy.state_quota_capacity = 8192u;
    vctx->policy.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    vctx->policy.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    vctx->policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    vctx->policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    vctx->policy.live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    vctx->policy.live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;

    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        vctx->policy.exec_region_base = base;
        vctx->policy.exec_region_size = size;
    }
    return AICAM_TRUE;
}

cJSON *app_install_record_json(const app_install_record_t *record,
                               const char *status)
{
    cJSON *root = cJSON_CreateObject();
    char pkg_hex[APP_INSTALL_SHA256_LEN * APP_INSTALL_HEX_PER_BYTE + 1];
    char tbs_hex[APP_INSTALL_SHA256_LEN * APP_INSTALL_HEX_PER_BYTE + 1];
    size_t i;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "publisher_id", record->publisher_id);
    cJSON_AddStringToObject(root, "app_id", record->app_id);
    cJSON_AddNumberToObject(root, "version_major", (double)record->version_major);
    cJSON_AddNumberToObject(root, "version_minor", (double)record->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", (double)record->version_patch);
    cJSON_AddNumberToObject(root, "package_len", (double)record->package_len);

    for (i = 0; i < APP_INSTALL_SHA256_LEN; i++) {
        snprintf(&pkg_hex[i * 2], 3, "%02x", record->package_sha256[i]);
        snprintf(&tbs_hex[i * 2], 3, "%02x", record->tbs_sha256[i]);
    }
    cJSON_AddStringToObject(root, "package_sha256", pkg_hex);
    cJSON_AddStringToObject(root, "tbs_sha256", tbs_hex);
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    return root;
}

static aicam_result_t app_install_respond_json(http_handler_context_t *ctx, cJSON *root,
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

static void app_install_state_add_running(cJSON *root)
{
    if (root == NULL) {
        return;
    }
    cJSON_AddBoolToObject(root, "app_running",
                          (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
}

aicam_result_t app_install_package_handler(http_handler_context_t *ctx)
{
    api_app_install_verify_ctx_t vctx;
    const app_install_io_t *io;
    cJSON *response_json = NULL;
    app_install_record_t record;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t pair_state;
    app_pair_history_t history;
    aicam_bool_t has_history = AICAM_FALSE;
    app_pair_install_result_t result;
    cJSON *ui_json;
    static const uint8_t unsigned_magic[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
        'N', 'E', 'A', 'P', 'P', 5u, 0u, 0u
    };

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app install: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    if (ctx->request.body == NULL || ctx->request.content_length == 0u ||
        ctx->request.content_length > API_APP_INSTALL_MAX_PACKAGE_BYTES) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "NEAPP05 package length out of bounds");
    }
    if (ctx->request.content_length < sizeof(unsigned_magic) ||
        memcmp(ctx->request.body, unsigned_magic, sizeof(unsigned_magic)) != 0) {
        if (ctx->request.content_length >= 5u &&
            memcmp(ctx->request.body, "NEAPP", 5u) == 0 &&
            ctx->request.content_length >= 6u &&
            ((const uint8_t *)ctx->request.body)[5] >= 1u &&
            ((const uint8_t *)ctx->request.body)[5] <= 4u) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "LEGACY_SIGNED_FORMAT_REJECTED");
        }
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "UNKNOWN_FORMAT_REJECTED");
    }
    if (api_app_install_fill_ctx(&vctx) != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "App install policy unavailable");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    memset(&record, 0, sizeof(record));
    memset(&descriptor, 0, sizeof(descriptor));
    pair_state = APP_SIDECAR_LIFECYCLE_NONE;
    {
        aicam_bool_t ai_stopped = AICAM_FALSE;
        aicam_bool_t camera_stopped = AICAM_FALSE;
        if (ai_pipeline_is_running() && ai_pipeline_stop() == AICAM_OK) {
            ai_stopped = AICAM_TRUE;
        }
        if (device_service_camera_stop() == AICAM_OK) {
            camera_stopped = AICAM_TRUE;
        }
        result = app_pair_install(io, api_app_install_verify, &vctx, &trust,
                                  (const uint8_t *)ctx->request.body,
                                  ctx->request.content_length, &record,
                                  &descriptor, &pair_state);
        if (camera_stopped == AICAM_TRUE) {
            device_service_camera_start();
        }
        if (ai_stopped == AICAM_TRUE) {
            ai_pipeline_start();
        }
    }
    if (result == APP_PAIR_INSTALL_OK || result == APP_PAIR_INSTALL_ALREADY) {
        int32_t rebase_rc;
        LOG_SVC_INFO("app install: %s publisher=%s app=%s v%u.%u.%u",
                     app_pair_install_result_str(result), record.publisher_id, record.app_id,
                     record.version_major, record.version_minor, record.version_patch);
        rebase_rc = app_report_service_policy_rebase(&record);
        if (rebase_rc != APP_REPORT_ABI_OK) {
            LOG_SVC_WARN("app install: report policy rebase rc=%d", (int)rebase_rc);
        }
        response_json = app_install_record_json(
            &record, (result == APP_PAIR_INSTALL_ALREADY) ? "already_installed" : "installed");
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "state", "installed");
            if (app_sidecar_lifecycle_pair_history_query(io, &history,
                                                         &has_history) ==
                    APP_SIDECAR_LIFECYCLE_OK && has_history == AICAM_TRUE) {
                char pair_hex[APP_PAIR_TBS_SHA256_LEN * 2u + 1u];
                char release_seq[24];
                api_sidecar_hex(history.pair_tbs_sha256,
                                APP_PAIR_TBS_SHA256_LEN, pair_hex);
                (void)snprintf(release_seq, sizeof(release_seq), "%llu",
                               (unsigned long long)history.release_seq);
                cJSON_AddStringToObject(response_json, "release_seq", release_seq);
                cJSON_AddStringToObject(response_json, "pair_tbs_sha256", pair_hex);
            }
            ui_json = api_sidecar_descriptor_json(&descriptor, "active");
            if (ui_json != NULL) {
                cJSON_AddItemToObject(response_json, "ui", ui_json);
            }
        }
    } else {
        if (result == APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN) {
            LOG_SVC_ERROR("app install: storage state UNKNOWN, fail-closed, reboot resolves pending intent");
        } else {
            LOG_SVC_WARN("app install: refused reason=%s", app_pair_install_result_str(result));
        }
        response_json = cJSON_CreateObject();
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "status", "refused");
            cJSON_AddStringToObject(response_json, "reason",
                                    app_pair_install_result_str(result));
            cJSON_AddStringToObject(response_json, "pair_state",
                                    app_sidecar_lifecycle_state_str(pair_state));
            cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        }
    }

    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, response_json, "App install completed with result");
}

aicam_result_t app_install_state_handler(http_handler_context_t *ctx)
{
    api_app_install_verify_ctx_t vctx;
    const app_install_io_t *io;
    app_install_query_t query;
    app_install_result_t result;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t sidecar_state;
    app_sidecar_lifecycle_result_t sidecar_result;
    app_pair_history_t history;
    aicam_bool_t has_history = AICAM_FALSE;
    cJSON *root;
    cJSON *ui_json;
    app_uninstall_state_t uninstall_state;
    app_install_record_t committed;
    aicam_bool_t committed_present = AICAM_FALSE;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app install: query refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    result = app_install_uninstall_status(io, &uninstall_state, NULL);
    if (result != APP_INSTALL_OK || uninstall_state == APP_UNINSTALL_STATE_UNKNOWN) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", "UNINSTALL_STATUS_UNKNOWN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "cleanup_pending");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (app_install_query_committed(io, &committed, &committed_present) != APP_INSTALL_OK) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", "INSTALL_RECORD_UNKNOWN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (committed_present == AICAM_TRUE &&
        (strcmp(committed.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
         memcmp(committed.publisher_key_sha256,
                (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
                APP_INSTALL_SHA256_LEN) != 0)) {
        root = app_install_record_json(&committed, "unavailable");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unavailable");
        cJSON_AddStringToObject(root, "reason", "LEGACY_SIGNED_STATE_PRESERVED");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (app_sidecar_lifecycle_unsigned_preflight(
            io, (committed_present == AICAM_TRUE) ? &committed : NULL) ==
        APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unavailable");
        cJSON_AddStringToObject(root, "reason", "LEGACY_SIGNED_PAIR_STATE_PRESERVED");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (app_sidecar_lifecycle_pair_history_query(io, &history, &has_history) !=
        APP_SIDECAR_LIFECYCLE_OK) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", "PAIR_HISTORY_UNKNOWN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (app_install_uninstall_status(io, &uninstall_state, NULL) != APP_INSTALL_OK) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", "UNINSTALL_STATUS_UNKNOWN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    (void)api_app_install_fill_ctx(&vctx);
    memset(&query, 0, sizeof(query));
    result = app_install_query(io, api_app_install_verify, &vctx, &query);
    if (result != APP_INSTALL_OK) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", app_install_result_str(result));
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    if (query.state == APP_INSTALL_STATE_NONE) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "none");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        if (has_history == AICAM_TRUE) {
            char seq[24];
            char pair_hex[APP_PAIR_TBS_SHA256_LEN * 2u + 1u];
            (void)snprintf(seq, sizeof(seq), "%llu",
                           (unsigned long long)history.release_seq);
            api_sidecar_hex(history.pair_tbs_sha256,
                            APP_PAIR_TBS_SHA256_LEN, pair_hex);
            cJSON_AddStringToObject(root, "release_seq", seq);
            cJSON_AddStringToObject(root, "pair_tbs_sha256", pair_hex);
        }
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (query.state != APP_INSTALL_STATE_INSTALLED ||
        (strcmp(query.record.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 &&
         has_history != AICAM_TRUE) ||
        api_sidecar_fill_trust(&query.record, &trust) != AICAM_TRUE) {
        root = app_install_record_json(&query.record, "unavailable");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unavailable");
        cJSON_AddStringToObject(root, "reason",
                                (has_history == AICAM_TRUE) ? "PAIR_NOT_SERVABLE" :
                                                              "PAIR_HISTORY_MISSING");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    memset(&descriptor, 0, sizeof(descriptor));
    sidecar_state = APP_SIDECAR_LIFECYCLE_NONE;
    sidecar_result = app_sidecar_lifecycle_query(io, &trust, &query.record,
                                                  &descriptor, &sidecar_state);
    if (sidecar_result != APP_SIDECAR_LIFECYCLE_OK ||
        sidecar_state != APP_SIDECAR_LIFECYCLE_ACTIVE ||
        app_pair_is_authorized(io, &query.record, &descriptor) != AICAM_TRUE) {
        root = app_install_record_json(&query.record,
            (sidecar_state == APP_SIDECAR_LIFECYCLE_UNKNOWN) ? "unknown" : "unavailable");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state",
            (sidecar_state == APP_SIDECAR_LIFECYCLE_UNKNOWN) ? "unknown" : "unavailable");
        cJSON_AddStringToObject(root, "reason",
            (sidecar_state == APP_SIDECAR_LIFECYCLE_UNKNOWN) ? "PAIR_STORAGE_UNKNOWN" :
                                                               "PAIR_NOT_SERVABLE");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    root = app_install_record_json(&query.record, "installed");
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(root, "state", "installed");
    if (has_history == AICAM_TRUE) {
        char seq[24];
        char pair_hex[APP_PAIR_TBS_SHA256_LEN * 2u + 1u];
        (void)snprintf(seq, sizeof(seq), "%llu",
                       (unsigned long long)history.release_seq);
        api_sidecar_hex(history.pair_tbs_sha256,
                        APP_PAIR_TBS_SHA256_LEN, pair_hex);
        cJSON_AddStringToObject(root, "release_seq", seq);
        cJSON_AddStringToObject(root, "pair_tbs_sha256", pair_hex);
    }
    ui_json = api_sidecar_descriptor_json(&descriptor, "active");
    if (ui_json != NULL) {
        cJSON_AddItemToObject(root, "ui", ui_json);
    }
    app_install_state_add_running(root);
    return app_install_respond_json(ctx, root, "App install query result");
}

static void api_sidecar_hex(const uint8_t *data, size_t len, char *out)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        (void)snprintf(out + (i * 2u), 3u, "%02x", data[i]);
    }
    out[len * 2u] = '\0';
}

static aicam_bool_t api_sidecar_fill_trust(const app_install_record_t *native,
                                           app_sidecar_trust_t *trust)
{
    if (native == NULL || trust == NULL) {
        return AICAM_FALSE;
    }
    if (strcmp(native->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0 &&
        memcmp(native->publisher_key_sha256,
               (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
               APP_INSTALL_SHA256_LEN) == 0) {
        memset(trust, 0, sizeof(*trust));
        trust->publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
        trust->publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
        trust->app_id = native->app_id;
        trust->app_id_len = strlen(native->app_id);
        trust->native_tbs_sha256 = native->tbs_sha256;
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static cJSON *api_sidecar_descriptor_json(const app_sidecar_descriptor_t *descriptor,
                                         const char *status)
{
    cJSON *root;
    cJSON *files;
    char key_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char native_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char ui_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char generation[24];
    uint16_t i;

    if (descriptor == NULL) {
        return NULL;
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    cJSON_AddStringToObject(root, "publisher_id", descriptor->publisher_id);
    cJSON_AddStringToObject(root, "app_id", descriptor->app_id);
    cJSON_AddNumberToObject(root, "version_major", descriptor->version_major);
    cJSON_AddNumberToObject(root, "version_minor", descriptor->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", descriptor->version_patch);
    cJSON_AddNumberToObject(root, "host_ui_abi", descriptor->host_ui_abi);
    cJSON_AddNumberToObject(root, "bundle_len", descriptor->bundle_len);
    cJSON_AddNumberToObject(root, "manifest_len", descriptor->manifest_len);
    cJSON_AddNumberToObject(root, "asset_payload_len", descriptor->asset_payload_len);
    cJSON_AddNumberToObject(root, "file_count", descriptor->file_count);
    api_sidecar_hex(descriptor->publisher_key_sha256,
                    APP_SIDECAR_SHA256_LEN, key_hex);
    api_sidecar_hex(descriptor->native_tbs_sha256,
                    APP_SIDECAR_SHA256_LEN, native_hex);
    api_sidecar_hex(descriptor->ui_tbs_sha256,
                    APP_SIDECAR_SHA256_LEN, ui_hex);
    cJSON_AddStringToObject(root, "publisher_key_sha256", key_hex);
    cJSON_AddStringToObject(root, "native_tbs_sha256", native_hex);
    cJSON_AddStringToObject(root, "ui_tbs_sha256", ui_hex);
    (void)snprintf(generation, sizeof(generation), "%lu",
                   (unsigned long)descriptor->service_generation);
    cJSON_AddStringToObject(root, "service_generation", generation);
    files = cJSON_CreateArray();
    if (files == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    for (i = 0u; i < descriptor->file_count; i++) {
        cJSON *item = cJSON_CreateObject();
        char hash[APP_SIDECAR_SHA256_LEN * 2u + 1u];
        if (item == NULL) {
            cJSON_Delete(files);
            cJSON_Delete(root);
            return NULL;
        }
        api_sidecar_hex(descriptor->files[i].sha256,
                        APP_SIDECAR_SHA256_LEN, hash);
        cJSON_AddStringToObject(item, "path", descriptor->files[i].path);
        cJSON_AddStringToObject(item, "storage_path", descriptor->files[i].storage_path);
        cJSON_AddNumberToObject(item, "file_len", descriptor->files[i].file_len);
        cJSON_AddStringToObject(item, "sha256", hash);
        cJSON_AddItemToArray(files, item);
    }
    cJSON_AddItemToObject(root, "files", files);
    return root;
}

aicam_result_t app_install_remove_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *response_json = NULL;
    cJSON *request_json;
    cJSON *confirm_item;
    app_install_record_t removed;
    app_uninstall_result_t result;
    app_uninstall_state_t state;
    aicam_bool_t confirmed;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "DELETE")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only DELETE method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app uninstall: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Invalid JSON request body");
    }
    confirm_item = cJSON_GetObjectItem(request_json, "confirm_delete_data");
    if (confirm_item == NULL) {
        confirmed = AICAM_FALSE;
    } else if (!cJSON_IsBool(confirm_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "confirm_delete_data must be a boolean");
    } else {
        confirmed = cJSON_IsTrue(confirm_item) ? AICAM_TRUE : AICAM_FALSE;
    }
    cJSON_Delete(request_json);
    if (confirmed != AICAM_TRUE) {
        response_json = cJSON_CreateObject();
        if (response_json == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(response_json, "status", "cancelled");
        cJSON_AddBoolToObject(response_json, "side_effects", AICAM_FALSE);
        cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        return app_install_respond_json(ctx, response_json,
                                       "App uninstall cancelled");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    if (app_report_service_device_uninstall_prepare(io) != AICAM_OK) {
        app_report_service_device_uninstall_finish(io);
        response_json = cJSON_CreateObject();
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "status", "refused");
            cJSON_AddStringToObject(response_json, "reason",
                                    "REPORT_REVOCATION_UNCONFIRMED_OR_STORAGE_UNKNOWN");
            cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        }
        if (response_json == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        return app_install_respond_json(ctx, response_json,
                                       "App uninstall refused before local cleanup");
    }

    memset(&removed, 0, sizeof(removed));
    state = APP_UNINSTALL_STATE_NONE;
    result = app_install_uninstall_confirmed(
        io, AICAM_TRUE, app_report_service_device_cleanup_ops(), &removed, &state);
    app_report_service_device_uninstall_finish(io);

    if (result == APP_UNINSTALL_OK) {
        LOG_SVC_INFO("app uninstall: removed publisher=%s app=%s v%u.%u.%u",
                     removed.publisher_id, removed.app_id,
                     removed.version_major, removed.version_minor, removed.version_patch);
        response_json = app_install_record_json(&removed, "uninstalled");
    } else if (result == APP_UNINSTALL_CLEANUP_PENDING) {
        response_json = app_install_record_json(&removed, "cleanup_pending");
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "reason", "PURGE_UNCONFIRMED_OR_STORAGE_UNKNOWN");
        }
    } else {
        response_json = cJSON_CreateObject();
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "status", "refused");
            cJSON_AddStringToObject(response_json, "reason",
                                    (result == APP_UNINSTALL_ERR_NOT_FOUND)
                                        ? "NOT_FOUND"
                                        : ((result == APP_UNINSTALL_ERR_BUSY)
                                               ? "BUSY" : "UNINSTALL_FAILED"));
            cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        }
    }

    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, response_json, "App uninstall completed with result");
}

aicam_result_t app_install_run_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *root = NULL;
    app_host_session_start_out_t session_out;
    app_session_result_t session_result;
    app_run_outcome_t outcome;
    app_run_result_t result;
    char *log_text;
    size_t log_len;
    aicam_bool_t log_truncated;
    char caps_text[16];

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!web_server_high_privilege_allowed(ctx)) {
        LOG_SVC_WARN("app run: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&session_out, 0, sizeof(session_out));
    session_result = app_host_session_start(io, &session_out);
    if (session_result != APP_SESSION_ERR_PROFILE_UNSUPPORTED) {
        if (session_result == APP_SESSION_OK) {
            LOG_SVC_INFO("app run: session started publisher=%s app=%s v%u.%u.%u abi=0x%08x",
                         session_out.record.publisher_id, session_out.record.app_id,
                         (unsigned int)session_out.record.version_major,
                         (unsigned int)session_out.record.version_minor,
                         (unsigned int)session_out.record.version_patch,
                         (unsigned int)session_out.abi_version);
            root = app_install_record_json(&session_out.record, "started");
            if (root == NULL) {
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddNumberToObject(root, "abi_version", (double)session_out.abi_version);
            snprintf(caps_text, sizeof(caps_text), "0x%08x",
                     (unsigned int)session_out.granted_caps);
            cJSON_AddStringToObject(root, "granted_caps", caps_text);
            cJSON_AddNumberToObject(root, "event_max", (double)session_out.event_max);
        } else {
            if (session_result == APP_SESSION_ERR_STORAGE_STATE_UNKNOWN) {
                LOG_SVC_ERROR("app run: Pair/session storage UNKNOWN, fail-closed, no execution");
            } else {
                LOG_SVC_WARN("app run: session refused result=%s reason=%s",
                             app_session_result_str(session_result), session_out.reason);
            }
            root = cJSON_CreateObject();
            if (root == NULL) {
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddStringToObject(root, "status", "refused");
            cJSON_AddStringToObject(root, "reason", app_session_result_str(session_result));
            if (session_out.reason[0] != '\0' &&
                strcmp(session_out.reason, app_session_result_str(session_result)) != 0) {
                cJSON_AddStringToObject(root, "detail", session_out.reason);
            }
            cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        }
        cJSON_AddBoolToObject(root, "app_running",
                              (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
        return app_install_respond_json(ctx, root, "App run completed with result");
    }

    memset(&outcome, 0, sizeof(outcome));
    result = app_host_run_installed(io, &outcome);
    log_truncated = app_host_run_log_truncated();

    if (result == APP_RUN_OK) {
        LOG_SVC_INFO("app run: completed publisher=%s app=%s v%u.%u.%u entry_result=%d",
                     outcome.record.publisher_id, outcome.record.app_id,
                     outcome.record.version_major, outcome.record.version_minor,
                     outcome.record.version_patch, outcome.entry_result);
        root = app_install_record_json(&outcome.record, "completed");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddBoolToObject(root, "entry_returned",
                              (outcome.entry_returned == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
        cJSON_AddNumberToObject(root, "entry_result", (double)outcome.entry_result);
        cJSON_AddBoolToObject(root, "app_running",
                              (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
        log_text = (char *)malloc(APP_RUN_LOG_CAP + 1u);
        if (log_text != NULL) {
            log_len = app_host_run_take_log(log_text, APP_RUN_LOG_CAP + 1u);
            log_text[log_len] = '\0';
            cJSON_AddStringToObject(root, "log", log_text);
            free(log_text);
        }
        cJSON_AddBoolToObject(root, "log_truncated",
                              (log_truncated == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    } else {
        if (result == APP_RUN_ERR_STORAGE_STATE_UNKNOWN) {
            LOG_SVC_ERROR("app run: storage state UNKNOWN, fail-closed, no execution");
        } else {
            LOG_SVC_WARN("app run: refused result=%s reason=%s",
                         app_host_run_result_str(result), outcome.reason);
        }
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddStringToObject(root, "reason", app_host_run_result_str(result));
        if (outcome.reason[0] != '\0' &&
            strcmp(outcome.reason, app_host_run_result_str(result)) != 0) {
            cJSON_AddStringToObject(root, "detail", outcome.reason);
        }
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        cJSON_AddBoolToObject(root, "app_running",
                              (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    }

    return app_install_respond_json(ctx, root, "App run completed with result");
}

aicam_result_t web_api_register_app_install_module(void)
{
    size_t count = sizeof(g_app_install_routes) / sizeof(g_app_install_routes[0]);
    size_t i;
    aicam_result_t result;
    app_install_result_t resolve;

    LOG_SVC_INFO("Registering App Install API module...");

    app_host_run_init(app_host_run_device_backend());

    result = app_installer_storage_init();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("App install storage init failed: %d", result);
        return result;
    }

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_install_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_install_routes[i].path, result);
            return result;
        }
    }

    resolve = app_install_boot_resolve(app_installer_device_io());
    if (resolve != APP_INSTALL_OK) {
        LOG_SVC_ERROR("App install boot intent resolve fail-closed: %s, deferred to first API use",
                      app_install_result_str(resolve));
        app_install_boot_resolve_reset();
    } else {
        LOG_SVC_INFO("App install boot intent resolved");
    }

    LOG_SVC_INFO("App Install API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
