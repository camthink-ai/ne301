/**
 * @file api_capture_module.c
 * @brief Capture Settings API Module
 *   GET    /api/v1/capture/upload-config       -> capture_upload_config_t
 *   POST   /api/v1/capture/upload-config       -> update + reload coordinator
 *   GET    /api/v1/capture/queue               -> coordinator status
 *   GET    /api/v1/capture/records?state=&offset=&limit=&from=&to= -> list records
 *   POST   /api/v1/capture/records/retry       -> body: {"id":"..."} or {"ids":[...]} or {"all":true}
 *   DELETE /api/v1/capture/records?id=...      -> remove one record
 *   POST   /api/v1/capture/records/delete      -> body: {"ids":[...]} batch delete
 */

#include "api_capture_module.h"
#include "web_api.h"
#include "web_server.h"
#include "upload_coordinator.h"
#include "communication_service.h"
#include "json_config_mgr.h"
#include "debug.h"
#include "cJSON.h"
#include "buffer_mgr.h"
#include "mongoose.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== Helpers ==================== */

static const char *mode_str(capture_mode_t m)
{
    switch (m) {
    case CAPTURE_MODE_INSTANT:    return "instant";
    case CAPTURE_MODE_BATCH:      return "batch";
    case CAPTURE_MODE_SCHEDULED:  return "scheduled";
    case CAPTURE_MODE_LOCAL_ONLY: return "local_only";
    default:                      return "instant";
    }
}

static capture_mode_t parse_mode(const char *s)
{
    if (!s) return CAPTURE_MODE_INSTANT;
    if (strcmp(s, "batch") == 0)      return CAPTURE_MODE_BATCH;
    if (strcmp(s, "scheduled") == 0)  return CAPTURE_MODE_SCHEDULED;
    if (strcmp(s, "local_only") == 0) return CAPTURE_MODE_LOCAL_ONLY;
    return CAPTURE_MODE_INSTANT;
}

static const char *storage_str(capture_storage_t s)
{
    switch (s) {
    case CAPTURE_STORE_AUTO:  return "auto";
    case CAPTURE_STORE_FLASH: return "flash";
    case CAPTURE_STORE_SD:    return "sd";
    case CAPTURE_STORE_NONE:  return "none";
    default:                  return "auto";
    }
}

static capture_storage_t parse_storage(const char *s)
{
    if (!s) return CAPTURE_STORE_AUTO;
    if (strcmp(s, "flash") == 0) return CAPTURE_STORE_FLASH;
    if (strcmp(s, "sd")    == 0) return CAPTURE_STORE_SD;
    if (strcmp(s, "none")  == 0) return CAPTURE_STORE_NONE;
    return CAPTURE_STORE_AUTO;
}

static const char *policy_str(storage_policy_t p)
{
    return (p == STORAGE_POLICY_STOP) ? "stop" : "wrap";
}

static storage_policy_t parse_policy(const char *s)
{
    if (s && strcmp(s, "stop") == 0) return STORAGE_POLICY_STOP;
    return STORAGE_POLICY_WRAP;
}

static const char *proto_str(upload_proto_t p)
{
    return (p == UPLOAD_PROTO_WEBHOOK) ? "webhook" : "mqtt";
}

static upload_proto_t parse_proto(const char *s)
{
    if (s && strcmp(s, "webhook") == 0) return UPLOAD_PROTO_WEBHOOK;
    return UPLOAD_PROTO_MQTT;
}

static const char *state_str(record_state_t s)
{
    switch (s) {
    case RECORD_STATE_PENDING: return "pending";
    case RECORD_STATE_SENT:    return "sent";
    case RECORD_STATE_FAILED:  return "failed";
    case RECORD_STATE_LOCAL:   return "local";
    default:                   return "pending";
    }
}

static record_state_t parse_state(const char *s)
{
    if (!s) return RECORD_STATE_PENDING;
    if (strcmp(s, "sent")   == 0) return RECORD_STATE_SENT;
    if (strcmp(s, "failed") == 0) return RECORD_STATE_FAILED;
    if (strcmp(s, "local")  == 0) return RECORD_STATE_LOCAL;
    return RECORD_STATE_PENDING;
}

static int get_query_param(http_handler_context_t *ctx, const char *name,
                            char *buf, size_t buf_size)
{
    if (!ctx || !ctx->msg) return -1;
    return mg_http_get_var(&ctx->msg->query, name, buf, buf_size);
}

/* ==================== GET /api/v1/capture/upload-config ==================== */

static aicam_result_t upload_config_get(http_handler_context_t *ctx)
{
    capture_upload_config_t cfg;
    if (json_config_get_capture_upload_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to load config");
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "OOM");

    cJSON_AddNumberToObject(resp, "version", cfg.version);
    cJSON_AddStringToObject(resp, "mode", mode_str(cfg.mode));
    cJSON_AddStringToObject(resp, "storage", storage_str(cfg.storage));
    cJSON_AddStringToObject(resp, "policy", policy_str(cfg.policy));
    cJSON_AddStringToObject(resp, "upload_protocol", proto_str(cfg.upload_protocol));
    cJSON_AddBoolToObject  (resp, "retry_enable", cfg.retry_enable);
    cJSON_AddNumberToObject(resp, "retry_max_attempts", cfg.retry_max_attempts);
    cJSON_AddNumberToObject(resp, "batch_count", cfg.batch_count);
    cJSON *arr = cJSON_AddArrayToObject(resp, "schedule_minutes");
    for (uint8_t i = 0; i < cfg.schedule_node_count && i < CAPTURE_SCHEDULE_MAX_NODES; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(cfg.schedule_minutes[i]));
    }
    cJSON_AddNumberToObject(resp, "keep_sent_hours", cfg.keep_sent_hours);
    cJSON_AddNumberToObject(resp, "max_pending_records", cfg.max_pending_records);
    /* FLASH_MAX_RECORDS is a compile-time constant, not user-configurable.
     * Exposed so the web UI can display it next to the storage selector. */
    cJSON_AddNumberToObject(resp, "flash_max_records", FLASH_MAX_RECORDS);
    /* upload_network: "default" = COMM_TYPE_NONE (use system comm-pref), else
     * the comm type string (wifi/halow/cellular/poe). */
    cJSON_AddStringToObject(resp, "upload_network",
        cfg.upload_comm_type == 0 /*COMM_TYPE_NONE*/ ? "default"
                                                     : communication_type_to_string((communication_type_t)cfg.upload_comm_type));

    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== POST /api/v1/capture/upload-config ==================== */

static aicam_result_t upload_config_set(http_handler_context_t *ctx)
{
    cJSON *req = web_api_parse_body(ctx);
    if (!req) return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");

    capture_upload_config_t cfg;
    json_config_capture_upload_defaults(&cfg);
    json_config_get_capture_upload_config(&cfg);

    cJSON *item;

    if ((item = cJSON_GetObjectItem(req, "mode")) && cJSON_IsString(item))
        cfg.mode = parse_mode(item->valuestring);

    if ((item = cJSON_GetObjectItem(req, "storage")) && cJSON_IsString(item))
        cfg.storage = parse_storage(item->valuestring);

    if ((item = cJSON_GetObjectItem(req, "policy")) && cJSON_IsString(item))
        cfg.policy = parse_policy(item->valuestring);

    if ((item = cJSON_GetObjectItem(req, "upload_protocol")) && cJSON_IsString(item))
        cfg.upload_protocol = parse_proto(item->valuestring);

    if ((item = cJSON_GetObjectItem(req, "retry_enable")) && cJSON_IsBool(item))
        cfg.retry_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;

    if ((item = cJSON_GetObjectItem(req, "retry_max_attempts")) && cJSON_IsNumber(item))
        cfg.retry_max_attempts = (uint8_t)item->valueint;

    if ((item = cJSON_GetObjectItem(req, "batch_count")) && cJSON_IsNumber(item))
        cfg.batch_count = (uint16_t)item->valueint;

    if ((item = cJSON_GetObjectItem(req, "schedule_minutes")) && cJSON_IsArray(item)) {
        memset(cfg.schedule_minutes, 0, sizeof(cfg.schedule_minutes));
        int n = cJSON_GetArraySize(item);
        if (n > CAPTURE_SCHEDULE_MAX_NODES) n = CAPTURE_SCHEDULE_MAX_NODES;
        for (int i = 0; i < n; i++) {
            cJSON *e = cJSON_GetArrayItem(item, i);
            if (e && cJSON_IsNumber(e)) {
                int v = e->valueint;
                if (v < 0 || v > 1439) continue;
                cfg.schedule_minutes[i] = (uint16_t)v;
            }
        }
        cfg.schedule_node_count = (uint8_t)n;
    }

    if ((item = cJSON_GetObjectItem(req, "keep_sent_hours")) && cJSON_IsNumber(item))
        cfg.keep_sent_hours = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(req, "max_pending_records")) && cJSON_IsNumber(item))
        cfg.max_pending_records = (uint32_t)item->valueint;

    if ((item = cJSON_GetObjectItem(req, "upload_network")) && cJSON_IsString(item)) {
        if (strcasecmp(item->valuestring, "default") == 0) {
            cfg.upload_comm_type = 0; /* COMM_TYPE_NONE */
        } else {
            communication_type_t t = communication_type_from_string(item->valuestring);
            /* communication_type_from_string returns COMM_TYPE_NONE on no match;
             * but user explicitly set a non-"default" value - validate it's a
             * real type (not NONE) to avoid silently falling back to default. */
            if (t != COMM_TYPE_NONE) cfg.upload_comm_type = (uint32_t)t;
        }
    }

    cJSON_Delete(req);

    /* Cross-field validation (also enforced by setter, double-checked here for UX) */
    if (cfg.storage == CAPTURE_STORE_NONE && cfg.mode != CAPTURE_MODE_INSTANT) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
            "storage=none requires mode=instant");
    }

    if (json_config_set_capture_upload_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save config");
    }

    upload_coordinator_reload_config();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "Updated");
}

/* ==================== GET /api/v1/capture/queue ==================== */

static aicam_result_t capture_queue_get(http_handler_context_t *ctx)
{
    upload_coordinator_status_t st;
    if (upload_coordinator_get_status(&st) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Status unavailable");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "mode", mode_str(st.mode));
    cJSON_AddStringToObject(resp, "storage", storage_str(st.storage));
    cJSON_AddBoolToObject  (resp, "initialized", st.initialized);
    cJSON_AddBoolToObject  (resp, "running", st.running);
    cJSON_AddBoolToObject  (resp, "storage_full", st.storage_full);
    cJSON_AddStringToObject(resp, "actual_fs", st.actual_fs == FS_SD ? "sd" : "flash");
    cJSON_AddNumberToObject(resp, "pending_count", st.pending_count);
    cJSON_AddNumberToObject(resp, "sent_count",    st.sent_count);
    cJSON_AddNumberToObject(resp, "failed_count",  st.failed_count);
    cJSON_AddNumberToObject(resp, "local_count",   st.local_count);
    cJSON_AddNumberToObject(resp, "bytes_used_kb",      (double)st.bytes_used_kb);
    cJSON_AddNumberToObject(resp, "bytes_available_kb", (double)st.bytes_available_kb);
    cJSON_AddNumberToObject(resp, "next_scheduled_at",  (double)st.next_scheduled_at);

    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== GET /api/v1/capture/records ==================== */

static aicam_result_t records_get(http_handler_context_t *ctx)
{
    char state_buf[16] = {0}, off_buf[16] = {0}, lim_buf[16] = {0};
    char from_buf[24] = {0}, to_buf[24] = {0}, sort_buf[8] = {0};
    get_query_param(ctx, "state",  state_buf, sizeof(state_buf));
    get_query_param(ctx, "offset", off_buf,   sizeof(off_buf));
    get_query_param(ctx, "limit",  lim_buf,   sizeof(lim_buf));
    get_query_param(ctx, "from",   from_buf,  sizeof(from_buf));
    get_query_param(ctx, "to",     to_buf,    sizeof(to_buf));
    get_query_param(ctx, "sort",   sort_buf,  sizeof(sort_buf));

    record_state_t st = parse_state(state_buf[0] ? state_buf : "pending");
    uint32_t offset = (uint32_t)atoi(off_buf);
    uint32_t limit  = (uint32_t)atoi(lim_buf);
    if (limit == 0) limit = 50;
    if (limit > 200) limit = 200;
    /* Default sort: desc (newest first). "asc" → oldest first. */
    aicam_bool_t sort_desc = (!sort_buf[0] || strncmp(sort_buf, "desc", 4) == 0);

    uint64_t from_ts = from_buf[0] ? (uint64_t)atoll(from_buf) : 0;
    uint64_t to_ts   = to_buf[0]   ? (uint64_t)atoll(to_buf)   : UINT64_MAX;

    /* Range filter + sort + pagination are all handled inside
     * upload_coordinator (which traverses per-date subdirs efficiently).
     * The API layer just allocates the page buffer and serializes. */
    record_info_t *recs = (record_info_t *)buffer_calloc(limit, sizeof(record_info_t));
    if (!recs) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "OOM");

    int n = upload_coordinator_list_records(st, offset, limit,
                                            from_ts, to_ts, sort_desc,
                                            recs, (int)limit);
    /* Skip the full-count traverse (upload_coordinator_count_records) - it's
     * O(all records) and causes web timeouts with thousands of files. Instead,
     * estimate total from the page result: if the page is full (n == limit),
     * there's at least one more → total = offset + n + 1 (enables "Next"). If
     * not full, total = offset + n (exact, last page). This gives correct
     * pagination without traversing the entire directory tree. */
    uint32_t total = offset + (uint32_t)n + ((uint32_t)n >= limit ? 1 : 0);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "state", state_str(st));
    cJSON_AddNumberToObject(resp, "offset", offset);
    cJSON_AddNumberToObject(resp, "limit",  limit);
    cJSON_AddNumberToObject(resp, "total",  total);

    cJSON *arr = cJSON_AddArrayToObject(resp, "records");
    for (int i = 0; i < n; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "id", recs[i].id);
        cJSON_AddStringToObject(r, "state", state_str(recs[i].state));
        cJSON_AddNumberToObject(r, "timestamp",   (double)recs[i].timestamp);
        cJSON_AddNumberToObject(r, "size",        recs[i].size);
        cJSON_AddNumberToObject(r, "retry_count", recs[i].retry_count);
        cJSON_AddStringToObject(r, "trigger",     recs[i].trigger);
        cJSON_AddStringToObject(r, "last_error",  recs[i].last_error);
        cJSON_AddBoolToObject  (r, "has_inference", recs[i].has_inference);
        cJSON_AddItemToArray(arr, r);
    }

    buffer_free(recs);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== POST /api/v1/capture/records/retry ==================== */

static aicam_result_t records_retry(http_handler_context_t *ctx)
{
    cJSON *req = web_api_parse_body(ctx);
    int reset = 0;
    if (req) {
        cJSON *all = cJSON_GetObjectItem(req, "all");
        cJSON *id  = cJSON_GetObjectItem(req, "id");
        cJSON *ids = cJSON_GetObjectItem(req, "ids");
        if (all && cJSON_IsBool(all) && cJSON_IsTrue(all)) {
            reset = upload_coordinator_retry_all_failed();
        } else if (ids && cJSON_IsArray(ids)) {
            int n = cJSON_GetArraySize(ids);
            for (int i = 0; i < n; i++) {
                cJSON *item = cJSON_GetArrayItem(ids, i);
                if (item && cJSON_IsString(item) && item->valuestring[0]) {
                    if (upload_coordinator_retry_record(item->valuestring) == AICAM_OK)
                        reset++;
                }
            }
        } else if (id && cJSON_IsString(id) && id->valuestring[0]) {
            aicam_result_t r = upload_coordinator_retry_record(id->valuestring);
            if (r == AICAM_OK) reset = 1;
        }
        cJSON_Delete(req);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "reset_count", reset);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== DELETE /api/v1/capture/records (single) ==================== */

static aicam_result_t records_delete_single(http_handler_context_t *ctx)
{
    char id_buf[64] = {0};
    get_query_param(ctx, "id", id_buf, sizeof(id_buf));
    if (!id_buf[0]) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing id");
    }
    aicam_result_t r = upload_coordinator_delete_record(id_buf);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", r == AICAM_OK);
    cJSON_AddStringToObject(resp, "id", id_buf);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== POST /api/v1/capture/records/delete (batch) ==================== */

static aicam_result_t records_delete_batch(http_handler_context_t *ctx)
{
    cJSON *req = web_api_parse_body(ctx);
    int deleted = 0;
    if (req) {
        cJSON *ids = cJSON_GetObjectItem(req, "ids");
        if (ids && cJSON_IsArray(ids)) {
            int n = cJSON_GetArraySize(ids);
            for (int i = 0; i < n; i++) {
                cJSON *item = cJSON_GetArrayItem(ids, i);
                if (item && cJSON_IsString(item) && item->valuestring[0]) {
                    if (upload_coordinator_delete_record(item->valuestring) == AICAM_OK)
                        deleted++;
                }
            }
        }
        cJSON_Delete(req);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "deleted_count", deleted);
    char *json = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json) return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Serialize failed");
    return api_response_success(ctx, json, "OK");
}

/* ==================== Dispatchers ==================== */

static aicam_result_t upload_config_handler(http_handler_context_t *ctx)
{
    if (web_api_verify_method(ctx, "GET"))  return upload_config_get(ctx);
    if (web_api_verify_method(ctx, "POST")) return upload_config_set(ctx);
    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
}

static aicam_result_t queue_handler(http_handler_context_t *ctx)
{
    if (web_api_verify_method(ctx, "GET")) return capture_queue_get(ctx);
    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
}

static aicam_result_t records_handler(http_handler_context_t *ctx)
{
    if (web_api_verify_method(ctx, "GET"))    return records_get(ctx);
    if (web_api_verify_method(ctx, "DELETE")) return records_delete_single(ctx);
    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
}

static aicam_result_t records_retry_handler(http_handler_context_t *ctx)
{
    if (web_api_verify_method(ctx, "POST")) return records_retry(ctx);
    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
}

static aicam_result_t records_delete_batch_handler(http_handler_context_t *ctx)
{
    if (web_api_verify_method(ctx, "POST")) return records_delete_batch(ctx);
    return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
}

/* ==================== Registration ==================== */

aicam_result_t web_api_register_capture_module(void)
{
    api_route_t routes[] = {
        { .path = API_PATH_PREFIX "/capture/upload-config", .method = "GET",
          .handler = upload_config_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/upload-config", .method = "POST",
          .handler = upload_config_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/queue", .method = "GET",
          .handler = queue_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/records", .method = "GET",
          .handler = records_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/records", .method = "DELETE",
          .handler = records_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/records/retry", .method = "POST",
          .handler = records_retry_handler, .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX "/capture/records/delete", .method = "POST",
          .handler = records_delete_batch_handler, .require_auth = AICAM_TRUE },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t r = http_server_register_route(&routes[i]);
        if (r != AICAM_OK) return r;
    }
    return AICAM_OK;
}
