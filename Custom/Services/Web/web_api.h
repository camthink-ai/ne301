/**
 * @file web_api.h
 * @brief Web API Framework Header
 * @details Dynamic API registration framework with cJSON integration
 */

#ifndef WEB_API_H
#define WEB_API_H

#include "aicam_types.h"
#include "web_server.h"
#include "cJSON.h"
#include "api_business_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define API_PATH_PREFIX "/api/v1"

/* ==================== cJSON Helper Functions ==================== */

/**
 * @brief Parse request body as JSON
 * @param ctx HTTP handler context
 * @return JSON object or NULL if parsing failed
 */
cJSON* web_api_parse_body(http_handler_context_t* ctx);

/**
 * @brief Get string value from JSON
 * @param json JSON object
 * @param key Key name
 * @return String value or NULL if not found
 */
const char* web_api_get_string(cJSON* json, const char* key);

/**
 * @brief Get number value from JSON
 * @param json JSON object
 * @param key Key name
 * @return Number value or 0.0 if not found
 */
double web_api_get_number(cJSON* json, const char* key);

/**
 * @brief Get boolean value from JSON
 * @param json JSON object
 * @param key Key name
 * @return Boolean value or AICAM_FALSE if not found
 */
aicam_bool_t web_api_get_bool(cJSON* json, const char* key);

/**
 * @brief Get integer value from JSON
 * @param json JSON object
 * @param key Key name
 * @return Integer value or 0 if not found
 */
int web_api_get_int(cJSON* json, const char* key);

/**
 * @brief Get array from JSON
 * @param json JSON object
 * @param key Key name
 * @return Array object or NULL if not found
 */
cJSON* web_api_get_array(cJSON* json, const char* key);

/**
 * @brief Get object from JSON
 * @param json JSON object
 * @param key Key name
 * @return Object or NULL if not found
 */
cJSON* web_api_get_object(cJSON* json, const char* key);

/* ==================== Request Validation Functions ==================== */

/**
 * @brief Verify HTTP method
 * @param ctx HTTP handler context
 * @param method HTTP method
 * @return AICAM_TRUE if method is valid, AICAM_FALSE otherwise
 */
aicam_bool_t web_api_verify_method(http_handler_context_t* ctx, const char* method);

/* ==================== Response Builder Functions ==================== */

/**
 * @brief Print an unformatted cJSON object and send it as a success response
 * @param ctx HTTP handler context
 * @param root JSON object to send (deleted inside, also on failure)
 * @param message Response message
 * @return Operation result
 */
aicam_result_t api_response_json(http_handler_context_t* ctx, cJSON* root, const char* message);

/* ==================== JSON Field Accessor Functions ==================== */

/**
 * @brief Get a uint32 value from a JSON number item with range check
 * @param item JSON item (may be NULL)
 * @param out Output value
 * @return AICAM_TRUE if item is an integral number in [0, UINT32_MAX]
 */
aicam_bool_t api_json_get_u32(const cJSON* item, uint32_t* out);

/**
 * @brief Verify Content-Type
 * @param ctx HTTP handler context
 * @param content_type Content-Type
 * @return AICAM_TRUE if Content-Type is valid, AICAM_FALSE otherwise
 */
aicam_bool_t web_api_verify_content_type(http_handler_context_t* ctx, const char* content_type);

/* ==================== Client Information Functions ==================== */

/**
 * @brief Get client IP
 * @param ctx HTTP handler context
 * @param ip_buffer Output buffer
 * @param buffer_size Buffer size
 * @return Operation result
 */
aicam_result_t web_api_get_client_ip(http_handler_context_t* ctx, char* ip_buffer, size_t buffer_size);

/**
 * @brief Base64 encode
 * @param input Input buffer
 * @param in_len Input length
 * @param output Output buffer
 * @param out_size Output size
 * @return Operation result
 */
size_t web_api_base64_encode(const uint8_t* input, size_t in_len, char* output, size_t out_size);

/**
 * @brief Base64 decode
 * @param input Input buffer
 * @param output Output buffer
 * @param out_size Output size
 * @return Operation result
 */
size_t web_api_base64_decode(const char* input, uint8_t* output, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_H */