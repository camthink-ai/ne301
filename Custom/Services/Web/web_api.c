/**
 * @file web_api.c
 * @brief Web API Framework with cJSON Support
 * @details Dynamic API registration framework with cJSON integration
 */

 #include "web_api.h"
 #include "web_server.h"
 #include "mongoose.h"
 #include "aicam_types.h"
#include "cJSON.h"
#include "debug.h"
 #include <string.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <time.h>

 
/* ==================== cJSON Helper Functions ==================== */
/**
 * @brief Parse JSON request body
 */
static cJSON* web_api_parse_request_body(http_handler_context_t* ctx) {
    if (!ctx || !ctx->request.body) return NULL;
    
    return cJSON_Parse(ctx->request.body);
}

/**
 * @brief Get JSON string value
 */
static const char* web_api_get_json_string(cJSON* json, const char* key) {
    if (!json || !key) return NULL;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

/**
 * @brief Get JSON number value
 */
static double web_api_get_json_number(cJSON* json, const char* key) {
    if (!json || !key) return 0.0;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
}

/**
 * @brief Get JSON boolean value
 */
static aicam_bool_t web_api_get_json_bool(cJSON* json, const char* key) {
    if (!json || !key) return AICAM_FALSE;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    }
    return AICAM_TRUE;
}

/**
 * @brief Get JSON integer value
 */
static int web_api_get_json_int(cJSON* json, const char* key) {
    if (!json || !key) return 0;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsNumber(item)) ? (int)item->valueint : 0;
}

/**
 * @brief Get JSON array
 */
static cJSON* web_api_get_json_array(cJSON* json, const char* key) {
    if (!json || !key) return NULL;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsArray(item)) ? item : NULL;
}

/**
 * @brief Get JSON object
 */
static cJSON* web_api_get_json_object(cJSON* json, const char* key) {
    if (!json || !key) return NULL;
    
    cJSON* item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsObject(item)) ? item : NULL;
}

/* ==================== Public API Functions ==================== */

/**
 * @brief Parse request body as JSON
 */
cJSON* web_api_parse_body(http_handler_context_t* ctx) {
    return web_api_parse_request_body(ctx);
}

/**
 * @brief Print an unformatted cJSON object and send it as a success response
 */
aicam_result_t api_response_json(http_handler_context_t* ctx, cJSON* root, const char* message) {
    char* response_text;
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

/**
 * @brief Get a uint32 value from a JSON number item with range check
 */
aicam_bool_t api_json_get_u32(const cJSON* item, uint32_t* out) {
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

/**
 * @brief Get string value from JSON
 */
const char* web_api_get_string(cJSON* json, const char* key) {
    return web_api_get_json_string(json, key);
}

/**
 * @brief Get number value from JSON
 */
double web_api_get_number(cJSON* json, const char* key) {
    return web_api_get_json_number(json, key);
}

/**
 * @brief Get boolean value from JSON
 */
aicam_bool_t web_api_get_bool(cJSON* json, const char* key) {
    return web_api_get_json_bool(json, key);
}

/**
 * @brief Get integer value from JSON
 */
int web_api_get_int(cJSON* json, const char* key) {
    return web_api_get_json_int(json, key);
}

/**
 * @brief Get array from JSON
 */
cJSON* web_api_get_array(cJSON* json, const char* key) {
    return web_api_get_json_array(json, key);
}

/**
 * @brief Get object from JSON
 */
cJSON* web_api_get_object(cJSON* json, const char* key) {
    return web_api_get_json_object(json, key);
}
 
 /* ==================== Request Validation Functions ==================== */
 
 aicam_bool_t web_api_verify_method(http_handler_context_t* ctx, const char* method) {
     if (!ctx || !method) return AICAM_FALSE;
     return (strcasecmp(ctx->request.method, method) == 0);
 }
 
 aicam_bool_t web_api_verify_content_type(http_handler_context_t* ctx, const char* content_type) {
     if (!ctx || !content_type) return AICAM_FALSE;
     return (strstr(ctx->request.content_type, content_type) != NULL);
 }
 
 
 /* ==================== Client Information Functions ==================== */
 
 aicam_result_t web_api_get_client_ip(http_handler_context_t* ctx, char* ip_buffer, size_t buffer_size) {
     if (!ctx || !ip_buffer || buffer_size == 0) {
         return AICAM_ERROR_INVALID_PARAM;
     }
     strncpy(ip_buffer, ctx->request.client_ip, buffer_size - 1);
     ip_buffer[buffer_size - 1] = '\0';
     return AICAM_OK;
 }
 
 aicam_bool_t web_api_get_header(http_handler_context_t* ctx, const char* name, char* buf, size_t len) {
     if (!ctx || !ctx->msg || !name || !buf || len == 0) return AICAM_FALSE;
     struct mg_str* hdr = mg_http_get_header(ctx->msg, name);
     if (hdr) {
         snprintf(buf, len, "%.*s", (int)hdr->len, hdr->buf);
         return AICAM_TRUE;
     }
     return AICAM_FALSE;
 }
 
 aicam_result_t web_api_get_user_agent(http_handler_context_t* ctx, char* buf, size_t len) {
     if (web_api_get_header(ctx, "User-Agent", buf, len)) {
         return AICAM_OK;
     }
     return AICAM_ERROR_NOT_FOUND;
 }
 
 
 
 /* ==================== Utility Functions ==================== */
 
 aicam_result_t web_api_generate_uuid(char* uuid, size_t buffer_size) {
     if (!uuid || buffer_size < 37) return AICAM_ERROR_INVALID_PARAM;
     snprintf(uuid, buffer_size, "%08lx-%04x-%04x-%04x-%04x%08lx",
              (unsigned long)time(NULL), rand() & 0xffff, rand() & 0xffff,
              rand() & 0xffff, rand() & 0xffff, (unsigned long)rand());
     return AICAM_OK;
 }
 
 uint32_t web_api_string_hash(const char* str) {
     if (!str) return 0;
     uint32_t hash = 5381;
     int c;
     while ((c = *str++)) {
         hash = ((hash << 5) + hash) + c;
     }
     return hash;
 }
 
 size_t web_api_base64_encode(const uint8_t* input, size_t in_len, char* output, size_t out_size) {
     if (!input || !output || out_size == 0) return 0;
     return mg_base64_encode((const unsigned char *)input, in_len, output, out_size);
 }
 
 size_t web_api_base64_decode(const char* input, uint8_t* output, size_t out_size) {
     if (!input || !output || out_size == 0) return 0;
     int len = mg_base64_decode(input, strlen(input), (char*)output, out_size);
     return (len < 0) ? 0 : (size_t)len;
 }
