/**
 * @file api_webhook_module.c
 * @brief Webhook API Module - config get/set + test push
 */

#include "api_webhook_module.h"
#include "webhook_service.h"
#include "web_api.h"
#include "web_server.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "buffer_mgr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/* ==================== GET /api/v1/apps/webhook/config ==================== */

static aicam_result_t webhook_config_get_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    webhook_config_t cfg;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get webhook config");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "enable", cfg.enable);
    cJSON_AddStringToObject(resp, "url", cfg.url);
    cJSON_AddStringToObject(resp, "auth_type", cfg.auth_type);
    cJSON_AddStringToObject(resp, "secret", cfg.secret);

    /* CA certificate status */
    char *ca_cert = NULL;
    size_t ca_cert_len = 0;
    aicam_bool_t has_custom_ca = (json_config_get_webhook_ca_cert(&ca_cert, &ca_cert_len) == AICAM_OK && ca_cert_len > 0);
    if (ca_cert) buffer_free(ca_cert);
    cJSON_AddBoolToObject(resp, "has_custom_ca", has_custom_ca);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    aicam_result_t ret = api_response_success(ctx, json_str, "OK");
    return ret;
}

/* ==================== POST /api/v1/apps/webhook/config ==================== */

static aicam_result_t webhook_config_set_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON *req = web_api_parse_body(ctx);
    if (!req) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    webhook_config_t cfg;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current config");
    }

    cJSON *item;

    item = cJSON_GetObjectItem(req, "enable");
    if (item && cJSON_IsBool(item)) {
        cfg.enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    }

    item = cJSON_GetObjectItem(req, "url");
    if (item && cJSON_IsString(item)) {
        strncpy(cfg.url, item->valuestring, sizeof(cfg.url) - 1);
        cfg.url[sizeof(cfg.url) - 1] = '\0';
    }

    item = cJSON_GetObjectItem(req, "auth_type");
    if (item && cJSON_IsString(item)) {
        strncpy(cfg.auth_type, item->valuestring, sizeof(cfg.auth_type) - 1);
        cfg.auth_type[sizeof(cfg.auth_type) - 1] = '\0';
    }

    item = cJSON_GetObjectItem(req, "secret");
    if (item && cJSON_IsString(item)) {
        strncpy(cfg.secret, item->valuestring, sizeof(cfg.secret) - 1);
        cfg.secret[sizeof(cfg.secret) - 1] = '\0';
    }

    cJSON_Delete(req);

    if (json_config_set_webhook_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save webhook config");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddStringToObject(resp, "message", "Webhook configuration updated");

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "Webhook configuration updated");
}

/* ==================== POST /api/v1/apps/webhook/test ==================== */

static aicam_result_t webhook_test_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    aicam_result_t ret = webhook_service_test_push();

    cJSON *resp = cJSON_CreateObject();
    if (ret == AICAM_OK) {
        cJSON_AddBoolToObject(resp, "success", 1);
        cJSON_AddStringToObject(resp, "message", "Test push sent");
    } else {
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Test push failed");
    }

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, ret == AICAM_OK ? "Test push sent" : "Test push failed");
}

/* ==================== GET /api/v1/apps/webhook/ca-cert ==================== */

static aicam_result_t webhook_ca_cert_get_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    char *ca_cert = NULL;
    size_t ca_cert_len = 0;

    if (json_config_get_webhook_ca_cert(&ca_cert, &ca_cert_len) != AICAM_OK || !ca_cert) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "No CA certificate configured");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "ca_cert_data", ca_cert);
    cJSON_AddNumberToObject(resp, "ca_cert_length", (double)ca_cert_len);

    buffer_free(ca_cert);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "OK");
}

/* ==================== POST /api/v1/apps/webhook/ca-cert ==================== */

static aicam_result_t webhook_ca_cert_set_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON *req = web_api_parse_body(ctx);
    if (!req) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    cJSON *item = cJSON_GetObjectItem(req, "ca_cert_data");
    if (!item || !cJSON_IsString(item) || strlen(item->valuestring) == 0) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or empty ca_cert_data");
    }

    const char *cert_data = item->valuestring;
    size_t cert_len = strlen(cert_data);

    if (strncmp(cert_data, "-----BEGIN", 10) != 0) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid PEM format");
    }

    if (cert_len > 8192) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "CA certificate too large (max 8KB)");
    }

    aicam_result_t ret = json_config_set_webhook_ca_cert(cert_data, cert_len);
    cJSON_Delete(req);

    if (ret != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save CA certificate");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddStringToObject(resp, "message", "CA certificate saved");

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "CA certificate saved");
}

/* ==================== DELETE /api/v1/apps/webhook/ca-cert ==================== */

static aicam_result_t webhook_ca_cert_delete_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "DELETE")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    aicam_result_t ret = json_config_delete_webhook_ca_cert();
    if (ret != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to delete CA certificate");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddStringToObject(resp, "message", "CA certificate deleted");

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "CA certificate deleted");
}

/* ==================== Module Registration ==================== */

aicam_result_t web_api_register_webhook_module(void)
{
    api_route_t routes[] = {
        {
            .path = API_PATH_PREFIX "/apps/webhook/config",
            .method = "GET",
            .handler = webhook_config_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/webhook/config",
            .method = "POST",
            .handler = webhook_config_set_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/webhook/test",
            .method = "POST",
            .handler = webhook_test_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/webhook/ca-cert",
            .method = "GET",
            .handler = webhook_ca_cert_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/webhook/ca-cert",
            .method = "POST",
            .handler = webhook_ca_cert_set_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/webhook/ca-cert",
            .method = "DELETE",
            .handler = webhook_ca_cert_delete_handler,
            .require_auth = AICAM_TRUE
        },
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t ret = http_server_register_route(&routes[i]);
        if (ret != AICAM_OK) return ret;
    }

    return AICAM_OK;
}
