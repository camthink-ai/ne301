/**
 * @file api_ota_module.c
 * @brief OTA API Module Implementation
 * @details OTA (Over-The-Air) upgrade API implementation
 */

#include "api_ota_module.h"
#include "web_api.h"
#include "web_server.h"
#include "ota_service.h"
#include "ota_header.h"
#include "debug.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "generic_file.h"
#include "buffer_mgr.h"
#include "drtc.h"
#include "storage.h"
#include "nn.h"
#include "cmsis_os2.h"
#include "mqtt_service.h"
#include "system_service.h"
#include "device_service.h"
#include "ai_service.h"
#include "web_recovery.h"
#include "wifi.h"

#define OTA_WRITE_BUF_SIZE 1024
#define OTA_PRECHECK_DATA_SIZE 2048  // 2KB: 1KB OTA header + 1KB model package header
#define OTA_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes timeout for OTA upload
/* ==================== Global Variables ==================== */
static aicam_bool_t g_ota_upgrade_in_progress = AICAM_FALSE;
static uint32_t g_ota_last_activity_tick = 0;
static aicam_bool_t g_ota_stopped_mqtt = AICAM_FALSE;
static aicam_bool_t g_ota_stopped_camera = AICAM_FALSE;
static aicam_bool_t g_ota_stopped_ai = AICAM_FALSE;

typedef struct {
    upgrade_handle_t handle;
    firmware_header_t header_storage;
    uint32_t remaining_size;
    char *buffer;
} ota_export_ctx_t;

typedef struct {
    union {
        ota_header_t data;
        uint64_t align_dummy; // force 8 bytes alignment
        uint8_t raw[sizeof(ota_header_t)];
    } header_storage;

    size_t total_received;          // total bytes received
    size_t content_length;          // HTTP Content-Length
    FirmwareType fw_type_param;     // firmware type specified in the URL

    size_t header_received;         // number of bytes received for the header
    aicam_bool_t header_processed;  // whether the header has been processed

    firmware_header_t fw_header;    // parsed header information
    uint32_t running_crc32;         // running CRC32
    upgrade_handle_t upgrade_handle;

    uint8_t write_buf[OTA_WRITE_BUF_SIZE];
    size_t write_buf_pos;

    aicam_bool_t failed;
    aicam_bool_t initialized;
    aicam_bool_t mqtt_was_stopped;  // track if MQTT was stopped for OTA
    aicam_bool_t camera_was_stopped; // track if camera was stopped for OTA
    aicam_bool_t ai_was_stopped;     // track if AI pipeline was stopped for OTA
} ota_upload_ctx_t;


/* ==================== Helper Functions ==================== */

/**
 * @brief Update CRC32 checksum
 * @param crc Current CRC32 value
 * @param data Data to update CRC32
 * @param len Length of data
 * @return Updated CRC32 value
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Check if OTA service is running
 */
static aicam_bool_t is_ota_service_running(void) {
    service_state_t state = ota_service_get_state();
    return (state == SERVICE_STATE_RUNNING);
}


/**
 * @brief Parse firmware type from string
 */
static FirmwareType parse_firmware_type(const char* type_str) {
    if (!type_str) return FIRMWARE_APP;
    
    if (strcmp(type_str, "fsbl") == 0) {
        return FIRMWARE_FSBL;
    } else if (strcmp(type_str, "app") == 0) {
        return FIRMWARE_APP;
    } else if (strcmp(type_str, "web") == 0) {
        return FIRMWARE_WEB;
    } else if (strcmp(type_str, "ai_default") == 0) {
        return FIRMWARE_AI_1;
    } else if (strcmp(type_str, "ai") == 0) {
        return FIRMWARE_AI_2;
    } else if (strcmp(type_str, "wifi") == 0) {
        return FIRMWARE_WIFI;
    } else if (strcmp(type_str, "reserved2") == 0) {
        return FIRMWARE_RESERVED2;
    } else {
        return FIRMWARE_APP; // Default
    }
}


/**
 * @brief Stream export callback
 */
static void ota_stream_export_cb(struct mg_connection *c, int ev, void *ev_data) {
    ota_export_ctx_t *ctx = (ota_export_ctx_t *)c->fn_data;

    if (ev == MG_EV_POLL || ev == MG_EV_WRITE) {
        // point to check if the send buffer is full
        if (c->send.len > 8192) return;

        if (ctx->remaining_size > 0) {
            // read 1KB at a time
            size_t chunk_size = (ctx->remaining_size > 1024) ? 1024 : ctx->remaining_size;

            uint32_t bytes_read = ota_upgrade_read_chunk(&ctx->handle, ctx->buffer, chunk_size);

            if (bytes_read > 0) {
                mg_send(c, ctx->buffer, bytes_read);
                ctx->remaining_size -= bytes_read;
            } else {
                // read error, terminate
                LOG_SVC_ERROR("Read error during export");
                c->is_draining = 1;
            }
        } else {
            // send completed
            LOG_SVC_INFO("Export completed successfully");
            c->is_draining = 1; // mark the connection as draining to close
        }
    } else if (ev == MG_EV_CLOSE) {
        // clean up resources
        if (ctx) {
            if (ctx->buffer) buffer_free(ctx->buffer);
            buffer_free(ctx);
            c->fn_data = NULL;
        }
    }
}


/**
 * @brief send response to the client
 * @param c Mongoose connection
 * @param status HTTP status code
 * @param msg Message
 */
static void ota_send_response(struct mg_connection *c, int status, const char* msg) {
    if (!c) {
        return;
    }
    
    // Create http_handler_context_t from mg_connection
    // Note: In OTA stream mode, we don't have mg_http_message, so we create a minimal context
    http_handler_context_t ctx = {0};
    ctx.conn = c;
    ctx.msg = NULL;  // Not available in stream mode
    
    // Determine if it's a success or error response
    if (status == 0 || status == API_ERROR_NONE) {
        api_response_success(&ctx, NULL, msg ? msg : "success");
    } else {
        api_response_error(&ctx, status, msg ? msg : "Error");
    }
    
    // Send response using http_send_response
    http_send_response(&ctx);
}

static int flush_write_buffer(ota_upload_ctx_t *ctx) {
    if (ctx->write_buf_pos > 0) {
        if (ota_upgrade_write_chunk(&ctx->upgrade_handle, ctx->write_buf, ctx->write_buf_pos) != 0) {
            LOG_SVC_ERROR("Flash write failed (flush)");
            return -1;
        }
        ctx->write_buf_pos = 0; // reset the position
    }
    return 0;
}

/**
 * @brief process the OTA header verification and initialization
 * @param ctx OTA upload context
 * @return 0 on success, -1 on failure
 */
static int process_ota_header(ota_upload_ctx_t *ctx) {

    ota_header_t *header = &ctx->header_storage.data;

    // 1. check the firmware header
    if (ota_header_verify(header) != 0) {
        LOG_SVC_ERROR("Invalid firmware header magic/crc");
        return -1;
    }

    FirmwareType fw_type_from_header = FIRMWARE_APP;
    switch (header->fw_type) {
        case 0x01: fw_type_from_header = FIRMWARE_FSBL; break;
        case 0x02: fw_type_from_header = FIRMWARE_APP; break;
        case 0x03: fw_type_from_header = FIRMWARE_WEB; break;
        case 0x04: fw_type_from_header = FIRMWARE_AI_2; break;
        case 0x05: fw_type_from_header = FIRMWARE_AI_2; break;
        case 0x08: fw_type_from_header = FIRMWARE_WIFI; break;
        default: fw_type_from_header = FIRMWARE_APP; break;
    }

    LOG_SVC_INFO("Firmware type from header: %d, param: %d", fw_type_from_header, ctx->fw_type_param);
    if (fw_type_from_header != ctx->fw_type_param) {
        LOG_SVC_ERROR("Firmware type mismatch: header=%d, param=%d", fw_type_from_header, ctx->fw_type_param);
        return -1;
    }

    // 2. check the firmware size
    if (header->total_package_size != ctx->content_length) {
        LOG_SVC_ERROR("Firmware size mismatch: header=%u, http=%u", 
                     header->total_package_size, (uint32_t)ctx->content_length);
        return -1;
    }

    // 3. validate the firmware header options
    ctx->fw_header.file_size = header->total_package_size;
    memcpy(ctx->fw_header.version, header->fw_ver, sizeof(ctx->fw_header.version));
    ctx->fw_header.crc32 = header->fw_crc32;
    ctx->upgrade_handle.total_size = header->total_package_size;
    ctx->upgrade_handle.header = &ctx->fw_header;

    ota_validation_options_t options = {
        .validate_crc32 = AICAM_TRUE,
        .validate_signature = AICAM_FALSE,
        .validate_version = AICAM_FALSE, // usually set to FALSE in the development stage
        .validate_hardware = AICAM_TRUE,
        .validate_partition_size = AICAM_TRUE,
        .allow_downgrade = AICAM_FALSE,
        .min_version = 1,
        .max_version = 10
    };

    ota_validation_result_t val_res = ota_validate_firmware_header(&ctx->fw_header, ctx->fw_type_param, &options);
    if (val_res != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("Firmware header validation failed: %d", val_res);
        return -1;
    }

    // 4. validate the system state
    val_res = ota_validate_system_state(ctx->fw_type_param);
    if (val_res != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("System state validation failed: %d", val_res);
        return -1;
    }

    // 5. start the upgrade
    if (ota_upgrade_begin(&ctx->upgrade_handle, ctx->fw_type_param, &ctx->fw_header) != 0) {
        LOG_SVC_ERROR("upgrade_begin failed");
        return -1;
    }

    LOG_SVC_INFO("OTA Header Verified. Size: %u, CRC: 0x%08X. Writing...", 
                 ctx->fw_header.file_size, ctx->fw_header.crc32);
    
    return 0;
}

/* ==================== API Handlers for OTA ==================== */

/**
 * @brief Pre-check OTA header validation
 * @param header_data Raw data containing OTA header (1KB) + optional model package header (1KB for AI model)
 * @param data_len Length of header_data: 1KB for non-AI, 2KB for AI model
 * @param fw_type_param Firmware type from request parameter
 * @param expected_content_length Expected total content length (optional)
 * @return AICAM_OK on success, error code on failure
 */
static aicam_result_t ota_precheck_header(const uint8_t *header_data, size_t data_len, 
                                          FirmwareType fw_type_param, 
                                          size_t expected_content_length)
{
    // Determine required data size based on firmware type
    aicam_bool_t is_ai_model = (fw_type_param == FIRMWARE_AI_2 || fw_type_param == FIRMWARE_AI_1);
    size_t required_size = is_ai_model ? OTA_PRECHECK_DATA_SIZE : sizeof(ota_header_t);
    
    if (!header_data || data_len < required_size) {
        LOG_SVC_ERROR("Pre-check failed: insufficient data (received %lu, need %lu)", 
                      (unsigned long)data_len, (unsigned long)required_size);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // ============== Part 1: Validate OTA header (first 1KB) ==============
    ota_header_t *header = (ota_header_t *)header_data;
    
    // 1. Check firmware header magic/CRC
    if (ota_header_verify(header) != 0) {
        LOG_SVC_ERROR("Pre-check failed: Invalid firmware header magic/crc");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // 2. Check firmware type match
    FirmwareType fw_type_from_header = FIRMWARE_APP;
    switch (header->fw_type) {
        case 0x01: fw_type_from_header = FIRMWARE_FSBL; break;
        case 0x02: fw_type_from_header = FIRMWARE_APP; break;
        case 0x03: fw_type_from_header = FIRMWARE_WEB; break;
        case 0x04: fw_type_from_header = FIRMWARE_AI_2; break;
        case 0x05: fw_type_from_header = FIRMWARE_AI_2; break;
        case 0x08: fw_type_from_header = FIRMWARE_WIFI; break;
        default: fw_type_from_header = FIRMWARE_APP; break;
    }
    
    if (fw_type_from_header != fw_type_param) {
        LOG_SVC_ERROR("Pre-check failed: Firmware type mismatch (header=%d, param=%d)", 
                     fw_type_from_header, fw_type_param);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // 3. Check firmware size match (if expected_content_length is provided)
    if (expected_content_length > 0) {
        if (header->total_package_size != expected_content_length) {
            LOG_SVC_ERROR("Pre-check failed: Firmware size mismatch (header=%u, expected=%lu)", 
                         header->total_package_size, (unsigned long)expected_content_length);
            return AICAM_ERROR_INVALID_PARAM;
        }
    }
    
    // 4. Validate firmware header options (basic validation, no system state check)
    firmware_header_t fw_header;
    fw_header.file_size = header->total_package_size;
    memcpy(fw_header.version, header->fw_ver, sizeof(fw_header.version));
    fw_header.crc32 = header->fw_crc32;
    
    ota_validation_options_t options = {
        .validate_crc32 = AICAM_FALSE,      // Skip CRC32 check in pre-check (only check header structure)
        .validate_signature = AICAM_FALSE,
        .validate_version = AICAM_FALSE,
        .validate_hardware = AICAM_TRUE,    // Check hardware compatibility
        .validate_partition_size = AICAM_TRUE,  // Check partition size
        .allow_downgrade = AICAM_FALSE,
        .min_version = 1,
        .max_version = 10
    };
    
    ota_validation_result_t val_res = ota_validate_firmware_header(&fw_header, fw_type_param, &options);
    if (val_res != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("Pre-check failed: Firmware header validation failed: %d", val_res);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Pre-check passed: OTA header is valid (type=%d, size=%u, version=%.*s)", 
                 fw_type_from_header, header->total_package_size, 
                 (int)sizeof(header->fw_ver), header->fw_ver);
    
    // ============== Part 2: Validate model package header (second 1KB, for AI model only) ==============
    if (fw_type_param == FIRMWARE_AI_2 || fw_type_param == FIRMWARE_AI_1) {
        
        // Model package header starts at offset 1KB (after OTA header)
        const nn_package_header_t *model_header = (const nn_package_header_t *)(header_data + sizeof(ota_header_t));
        
        // Check model package magic number
        if (model_header->magic != MODEL_PACKAGE_MAGIC) {
            LOG_SVC_ERROR("Pre-check failed: Invalid model package magic (0x%08lX, expected 0x%08X)", 
                         (unsigned long)model_header->magic, MODEL_PACKAGE_MAGIC);
            return AICAM_ERROR_INVALID_PARAM;
        }
        
        // Check model package version
        if (model_header->version != MODEL_PACKAGE_VERSION) {
            LOG_SVC_ERROR("Pre-check failed: Incompatible model package version (0x%06lX, expected 0x%06X)", 
                         (unsigned long)model_header->version, MODEL_PACKAGE_VERSION);
            return AICAM_ERROR_INVALID_PARAM;
        }
        
        LOG_SVC_INFO("Pre-check passed: Model package header is valid (magic=0x%08lX, version=0x%06lX)", 
                     (unsigned long)model_header->magic, (unsigned long)model_header->version);
    }
    
    return AICAM_OK;
}

/**
 * @brief OTA pre-check handler
 * POST /api/v1/system/ota/precheck
 * Validates OTA header data before full upload: 1KB for non-AI, 2KB for AI model
 */
aicam_result_t ota_precheck_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    // Allow both application/octet-stream and no Content-Type (for compatibility)
    if (ctx->request.content_type[0] != '\0' && 
        strcmp(ctx->request.content_type, "application/octet-stream") != 0) {
        LOG_SVC_WARN("OTA pre-check: Unexpected Content-Type '%s', expected 'application/octet-stream'", 
                     ctx->request.content_type);
        // Don't fail, just warn - some clients may not set Content-Type correctly
    }
    
    // Check if request body is available
    if (!ctx->request.body || ctx->request.content_length == 0) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Request body is empty");
    }
    
    // Parse firmware type from query parameter first (needed for Content-Length check)
    char fw_type_str[32] = {0};
    if (mg_http_get_var(&ctx->msg->query, "firmwareType", fw_type_str, sizeof(fw_type_str)) <= 0) {
        strcpy(fw_type_str, "app");  // Default to app
    }
    
    FirmwareType fw_type = parse_firmware_type(fw_type_str);
    
    // Determine required Content-Length based on firmware type
    aicam_bool_t is_ai_model = (fw_type == FIRMWARE_AI_2 || fw_type == FIRMWARE_AI_1);
    size_t required_size = is_ai_model ? OTA_PRECHECK_DATA_SIZE : sizeof(ota_header_t);
    
    // Check Content-Length (1KB for non-AI, 2KB for AI model)
    if (ctx->request.content_length < required_size) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, 
                                  is_ai_model ? "Content-Length must be at least 2KB (2048 bytes) for AI model"
                                              : "Content-Length must be at least 1KB (1024 bytes)");
    }
    
    // Parse expected content length from query parameter (optional)
    size_t expected_content_length = 0;
    char content_length_str[32] = {0};
    if (mg_http_get_var(&ctx->msg->query, "contentLength", content_length_str, sizeof(content_length_str)) > 0) {
        expected_content_length = (size_t)atol(content_length_str);
    }
    
    LOG_SVC_INFO("OTA pre-check request: type=%d (%s), data_len=%lu, expected_size=%lu", 
                 fw_type, fw_type_str, (unsigned long)ctx->request.content_length, (unsigned long)expected_content_length);
    
    // Perform pre-check validation
    aicam_result_t result = ota_precheck_header(
        (const uint8_t *)ctx->request.body,
        ctx->request.content_length,
        fw_type,
        expected_content_length
    );
    
    if (result != AICAM_OK) {
        // Pre-check failed
        cJSON *response_data = cJSON_CreateObject();
        if (response_data) {
            cJSON_AddBoolToObject(response_data, "valid", cJSON_False);
            cJSON_AddStringToObject(response_data, "reason", "Header validation failed");
            
            char *data_str = cJSON_PrintUnformatted(response_data);
            if (data_str) {
                api_response_error(ctx, API_BUSINESS_ERROR_OTA_HEADER_VALIDATION_FAILED, "Pre-check validation failed");
                // Override response data with detailed error info
                if (ctx->response.data) {
                    cJSON_free(ctx->response.data);
                }
                ctx->response.data = data_str;
                cJSON_Delete(response_data);
                http_send_response(ctx);
                return result;
            }
            cJSON_Delete(response_data);
        }
        return api_response_error(ctx, API_BUSINESS_ERROR_OTA_HEADER_VALIDATION_FAILED, "Pre-check validation failed");
    }
    
    // Pre-check passed
    cJSON *response_data = cJSON_CreateObject();
    if (!response_data) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_data, "valid", cJSON_True);
    cJSON_AddStringToObject(response_data, "message", "Header validation passed");
    
    // Extract header info for response
    ota_header_t *header = (ota_header_t *)ctx->request.body;
    cJSON_AddNumberToObject(response_data, "firmware_size", header->total_package_size);
    
    // Extract full version string (including suffix) using unified interface
    char version_str[64] = {0};
    if (ota_header_get_full_version(header, version_str, sizeof(version_str)) != 0) {
        // Fallback to numeric version only
        ota_version_to_string(header->fw_ver, version_str, sizeof(version_str));
    }

    LOG_SVC_INFO("Firmware version: %s", version_str);
    
    cJSON_AddStringToObject(response_data, "firmware_version", version_str);
    cJSON_AddNumberToObject(response_data, "firmware_crc32", header->fw_crc32);
    
    char *data_str = cJSON_PrintUnformatted(response_data);
    if (!data_str) {
        cJSON_Delete(response_data);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to format response");
    }
    
    cJSON_Delete(response_data);
    
    // Send success response
    api_response_success(ctx, data_str, "Pre-check validation passed");
    
    return AICAM_OK;
}


void ota_upload_stream_processor(struct mg_connection *c, int ev, void *ev_data) {
    ota_upload_ctx_t *ctx = (ota_upload_ctx_t *)c->fn_data;

    if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        if (ctx) {
            LOG_SVC_INFO("OTA upload cleanup (Event: %d)", ev);
            aicam_bool_t mqtt_was_stopped = ctx->mqtt_was_stopped;
            aicam_bool_t camera_was_stopped = ctx->camera_was_stopped;
            aicam_bool_t ai_was_stopped = ctx->ai_was_stopped;
            buffer_free(ctx);
            c->fn_data = NULL;
            g_ota_upgrade_in_progress = AICAM_FALSE;
            g_ota_stopped_mqtt = AICAM_FALSE;
            g_ota_stopped_camera = AICAM_FALSE;
            g_ota_stopped_ai = AICAM_FALSE;
            if (mqtt_was_stopped) {
                mqtt_service_start();
                LOG_SVC_INFO("MQTT service restarted after OTA cleanup");
            }
            if (camera_was_stopped) {
                device_service_camera_start();
                LOG_SVC_INFO("Camera restarted after OTA cleanup");
            }
            if (ai_was_stopped) {
                ai_pipeline_start();
                LOG_SVC_INFO("AI pipeline restarted after OTA cleanup");
            }
        }
        return;
    }

    // -----------------------------
    // HTTP header parsing (initialization)
    // -----------------------------
    if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* Handle CORS preflight OPTIONS request */
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            mg_http_reply(c, 200,
                            "Access-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                            "Access-Control-Max-Age: 86400\r\n",
                            "");
            return;
        }
        
        struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
        size_t total_len = cl ? (size_t)atol(cl->buf) : 0;
        
        if (total_len < sizeof(ota_header_t) || total_len > 100 * 1024 * 1024) { 
            ota_send_response(c, API_ERROR_INVALID_REQUEST, "Invalid Content-Length");
            return;
        }

        if (g_ota_upgrade_in_progress) {
            ota_send_response(c, API_ERROR_INVALID_REQUEST, "OTA already in progress");
            return;
        }

        char fw_type_str[32] = {0};
        if (mg_http_get_var(&hm->query, "firmwareType", fw_type_str, sizeof(fw_type_str)) <= 0) {
            strcpy(fw_type_str, "app"); 
        }

        ctx = (ota_upload_ctx_t *)buffer_calloc(1, sizeof(ota_upload_ctx_t));
        if (!ctx) {
            ota_send_response(c, API_ERROR_INTERNAL_ERROR, "OOM");
            return;
        }
        
        ctx->content_length = total_len;
        ctx->fw_type_param = parse_firmware_type(fw_type_str);
        ctx->initialized = AICAM_TRUE;
        ctx->running_crc32 = 0xFFFFFFFF; 
        c->fn_data = ctx; 

        LOG_SVC_INFO("OTA Stream Init: type=%d (%s), len=%u", 
                     ctx->fw_type_param, fw_type_str, (unsigned int)ctx->content_length);

        // Stop MQTT service during OTA to free up network resources
        // MQTT auto-reconnect can interfere with OTA upload due to network lock contention
        ctx->mqtt_was_stopped = AICAM_FALSE;
        g_ota_stopped_mqtt = AICAM_FALSE;
        if (mqtt_service_is_running()) {
            if (mqtt_service_stop() == AICAM_OK) {
                ctx->mqtt_was_stopped = AICAM_TRUE;
                g_ota_stopped_mqtt = AICAM_TRUE;
                LOG_SVC_INFO("MQTT service stopped for OTA upgrade");
            }
        }

        // Stop AI pipeline first (before camera) to avoid race condition where
        // AI node tries to get pipe2 buffer after camera stops producing frames
        ctx->ai_was_stopped = AICAM_FALSE;
        g_ota_stopped_ai = AICAM_FALSE;
        if (ai_pipeline_is_running()) {
            if (ai_pipeline_stop() == AICAM_OK) {
                ctx->ai_was_stopped = AICAM_TRUE;
                g_ota_stopped_ai = AICAM_TRUE;
                LOG_SVC_INFO("AI pipeline stopped for OTA upgrade");
            }
        }

        // Stop camera during OTA to prevent XSPI bus contention during flash erase
        ctx->camera_was_stopped = AICAM_FALSE;
        g_ota_stopped_camera = AICAM_FALSE;
        if (device_service_camera_stop() == AICAM_OK) {
            ctx->camera_was_stopped = AICAM_TRUE;
            g_ota_stopped_camera = AICAM_TRUE;
            LOG_SVC_INFO("Camera stopped for OTA upgrade (XSPI bus contention prevention)");
        }

        // Wait for any in-progress capture to complete before starting flash erase
        if (system_service_capture_in_progress()) {
            LOG_SVC_INFO("OTA waiting for capture to complete...");
            uint32_t wait_start = osKernelGetTickCount();
            while (system_service_capture_in_progress() &&
                   (osKernelGetTickCount() - wait_start) < 30000) {
                osDelay(500);
            }
            if (system_service_capture_in_progress()) {
                LOG_SVC_WARN("OTA proceeding despite capture still in progress (timeout)");
            } else {
                LOG_SVC_INFO("Capture completed, OTA proceeding");
            }
        }

        g_ota_upgrade_in_progress = AICAM_TRUE;
        g_ota_last_activity_tick = osKernelGetTickCount();

        // remove the Mongoose HTTP protocol handler, switch to Raw TCP mode
        c->pfn = NULL; 
        mg_iobuf_del(&c->recv, 0, hm->head.len); // delete the header

        return;  // important: return after initialization to avoid falling through to cleanup
    }

    // -----------------------------
    // data processing (buffer header + write)
    // -----------------------------
    if (ctx && ctx->initialized && !ctx->failed && c->recv.len > 0) {
        g_ota_last_activity_tick = osKernelGetTickCount();  // update activity timestamp
        
        size_t processed = 0;
        uint8_t *data = (uint8_t *)c->recv.buf;
        size_t len = c->recv.len;

        // A. header buffering stage
        if (!ctx->header_processed) {
            size_t needed = sizeof(ota_header_t) - ctx->header_received;
            size_t to_copy = (len < needed) ? len : needed;

            memcpy(ctx->header_storage.raw + ctx->header_received, data, to_copy);
            ctx->header_received += to_copy;
            processed += to_copy;

            // header received complete
            if (ctx->header_received == sizeof(ota_header_t)) {
                if (process_ota_header(ctx) != 0) {
                    ctx->failed = AICAM_TRUE;
                    ota_send_response(c, API_ERROR_INVALID_REQUEST, "Header verification failed");
                    goto cleanup;
                }
                ctx->header_processed = AICAM_TRUE;

                // Header is also part of the firmware for most types and gets written
                // to flash. FSBL and WiFi are exceptions: their OTA header is used only
                // for web verification and is NOT written — the payload (flash image)
                // is written directly to the partition base.
                if (ctx->fw_type_param != FIRMWARE_FSBL && ctx->fw_type_param != FIRMWARE_WIFI) {
                    memcpy(ctx->write_buf, ctx->header_storage.raw, sizeof(ota_header_t));
                    ctx->write_buf_pos = sizeof(ota_header_t);
                }
                // Header does not participate in the Payload CRC calculation, so here we don't update the CRC
            }
        }

        // B. data writing stage
        if (ctx->header_processed && processed < len) {
            uint8_t *payload = data + processed;
            size_t payload_len = len - processed;

            // 1. update the CRC32 
            ctx->running_crc32 = crc32_update(ctx->running_crc32, payload, payload_len);

            // 2. write to Flash 
            size_t remaining_payload = payload_len;
            while (remaining_payload > 0) {
                size_t space_left = OTA_WRITE_BUF_SIZE - ctx->write_buf_pos;
                size_t chunk = (remaining_payload < space_left) ? remaining_payload : space_left;

                memcpy(ctx->write_buf + ctx->write_buf_pos, payload, chunk);
                ctx->write_buf_pos += chunk;
                payload += chunk;
                remaining_payload -= chunk;

                // buffer is full, flush to Flash
                if (ctx->write_buf_pos == OTA_WRITE_BUF_SIZE) {
                    if (flush_write_buffer(ctx) != 0) {
                        ctx->failed = AICAM_TRUE;
                        ota_send_response(c, API_ERROR_INTERNAL_ERROR, "Flash write failed");
                        goto cleanup;
                    }
                }
            }
        }

        ctx->total_received += len; 
        

        // release the processed memory
        mg_iobuf_del(&c->recv, 0, len);

        // C. progress log
        if (ctx->total_received % (256 * 1024) < len || (ctx->total_received == ctx->content_length)) {
             uint32_t percent = (uint32_t)(ctx->total_received * 100 / ctx->content_length);
             LOG_SVC_INFO("OTA Progress: %u%% (%u / %u)", percent, (unsigned int)ctx->total_received, (unsigned int)ctx->content_length);
        }

        // -----------------------------
        // stage 3: end verification 
        // -----------------------------
        if (ctx->total_received >= ctx->content_length) {
            LOG_SVC_INFO("Transfer Complete. Finalizing...");

            if (flush_write_buffer(ctx) != 0) {
                ctx->failed = AICAM_TRUE;
                ota_send_response(c, API_ERROR_INTERNAL_ERROR, "Flash flush failed");
                goto cleanup;
            }

            // Finalize CRC
            ctx->running_crc32 ^= 0xFFFFFFFF;

            // Verify CRC (Step 6 check)
            if (ctx->running_crc32 != ctx->fw_header.crc32) {
                LOG_SVC_ERROR("CRC32 mismatch: calc=0x%08X, header=0x%08X", 
                              ctx->running_crc32, ctx->fw_header.crc32);
                ctx->failed = AICAM_TRUE;
                ota_send_response(c, API_ERROR_INTERNAL_ERROR, "CRC32 verification failed");
                goto cleanup;
            }

            // Finish upgrade
            if (ota_upgrade_finish(&ctx->upgrade_handle) != 0) {
                LOG_SVC_ERROR("upgrade_finish failed");
                ctx->failed = AICAM_TRUE;
                ota_send_response(c, API_ERROR_INTERNAL_ERROR, "Upgrade finish failed");
                goto cleanup;
            }

            // Update json config
            if (ctx->fw_type_param == FIRMWARE_AI_2) {
                json_config_set_ai_1_active(AICAM_TRUE);
            }

            // Web firmware lives in a single-slot partition at WEB_BASE, so the
            // freshly written asset.bin is already addressable. Reload the web
            // assets now so the new UI is live immediately (and exit recovery
            // mode if we were serving the built-in recovery page).
            if (ctx->fw_type_param == FIRMWARE_WEB) {
                web_recovery_reload_assets();
            }

            // WiFi firmware is written to WIFI_FW_BASE (flash_header_t + .rps).
            LOG_SVC_INFO("OTA Success!");
            ota_send_response(c, API_ERROR_NONE, "Upgrade successful");
            goto cleanup;
        }
        return;
    }

    // No data to process yet, just return and wait for more data
    return;

cleanup:
    {
        aicam_bool_t mqtt_was_stopped = ctx ? ctx->mqtt_was_stopped : AICAM_FALSE;
        aicam_bool_t camera_was_stopped = ctx ? ctx->camera_was_stopped : AICAM_FALSE;
        aicam_bool_t ai_was_stopped = ctx ? ctx->ai_was_stopped : AICAM_FALSE;
        if (ctx && (ctx->failed || ctx->total_received >= ctx->content_length)) {
            buffer_free(ctx);
            c->fn_data = NULL;
        }
        g_ota_upgrade_in_progress = AICAM_FALSE;
        g_ota_stopped_mqtt = AICAM_FALSE;
        g_ota_stopped_camera = AICAM_FALSE;
        g_ota_stopped_ai = AICAM_FALSE;
        // Restart MQTT service if it was stopped for OTA
        if (mqtt_was_stopped) {
            mqtt_service_start();
            LOG_SVC_INFO("MQTT service restarted after OTA completion");
        }
        // Restart camera if it was stopped for OTA
        if (camera_was_stopped) {
            device_service_camera_start();
            LOG_SVC_INFO("Camera restarted after OTA completion");
        }
        // Restart AI pipeline if it was stopped for OTA
        if (ai_was_stopped) {
            ai_pipeline_start();
            LOG_SVC_INFO("AI pipeline restarted after OTA completion");
        }
    }
}

aicam_result_t ota_upload_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // return ok
    return api_response_success(ctx, NULL, "OTA upload handler called");
}

aicam_result_t ota_upgrade_local_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Parse firmware_type from the JSON body.  For WiFi, this is the trigger
    // point that sets NVS wifi_mode=update so the next reboot pushes the .rps
    // that was already written to WIFI_FW_BASE by the streaming upload.  For
    // all other firmware types this remains a no-op (the streaming upload
    // already handled everything in its upgrade_finish / slot-switch path).
    cJSON *request = web_api_parse_body(ctx);
    if (request) {
        cJSON *type_item = cJSON_GetObjectItem(request, "firmware_type");
        if (type_item && cJSON_IsString(type_item)) {
            const char *fw_type_str = type_item->valuestring;
            if (strcmp(fw_type_str, "wifi") == 0) {
                wifi_mark_update_pending();
                LOG_SVC_INFO("WiFi update flag set (upgrade-local confirm)");
            }
        }
        cJSON_Delete(request);
    }

    return api_response_success(ctx, NULL, "OK");
}


aicam_result_t ota_export_firmware_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // validate the Content-Type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // check if the OTA service is running
    if (!is_ota_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "OTA service is not running");
    }
    
    LOG_SVC_INFO("OTA export firmware handler called");
    LOG_SVC_INFO("Request body size: %lu bytes", (unsigned long)ctx->request.content_length);
    
    // parse the request parameters
    cJSON *request = web_api_parse_body(ctx);
    if (!request) {
        LOG_SVC_ERROR("Failed to parse JSON request body");
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request");
    }
    
    LOG_SVC_INFO("JSON request parsed successfully");
    
    // extract the parameters
    cJSON *type_item = cJSON_GetObjectItem(request, "firmware_type");
    cJSON *filename_item = cJSON_GetObjectItem(request, "filename");
    
    LOG_SVC_INFO("Extracted parameters:");
    LOG_SVC_INFO("  firmware_type: %s", type_item && cJSON_IsString(type_item) ? type_item->valuestring : "NULL");
    LOG_SVC_INFO("  filename: %s", filename_item && cJSON_IsString(filename_item) ? filename_item->valuestring : "NULL");
    
    if (!type_item || !cJSON_IsString(type_item)) {
        LOG_SVC_ERROR("Missing or invalid 'firmware_type' parameter");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'firmware_type' parameter");
    }
    
    if (!filename_item || !cJSON_IsString(filename_item)) {
        LOG_SVC_ERROR("Missing or invalid 'filename' parameter");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'filename' parameter");
    }
    
    // parse the parameters
    FirmwareType fw_type = parse_firmware_type(type_item->valuestring);
    if(fw_type == FIRMWARE_AI_2) {
        if(!json_config_get_ai_1_active()) {
            fw_type = FIRMWARE_AI_1;
        }
    }
    char export_filename[256] = {0};
    memcpy(export_filename, filename_item->valuestring, strlen(filename_item->valuestring));
    
    LOG_SVC_INFO("Parsed parameters:");
    LOG_SVC_INFO("  firmware_type: %d (%s)", fw_type, type_item->valuestring);
    LOG_SVC_INFO("  export_filename: %s", export_filename);
    
    // get the current active slot
    SystemState *sys_state = ota_get_system_state();
    if (!sys_state) {
        LOG_SVC_ERROR("Failed to get system state");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get system state");
    }
    
    int slot_idx = sys_state->active_slot[fw_type];
    LOG_SVC_INFO("Current active slot for firmware type %d: %d", fw_type, slot_idx);
    
    cJSON_Delete(request);
    
    // get the slot information
    slot_info_t *slot_info = &sys_state->slot[fw_type][slot_idx];
    uint32_t firmware_size = slot_info->firmware_size;
    
    LOG_SVC_INFO("Starting firmware export: type=%d, slot=%d, size=%u bytes", fw_type, slot_idx, firmware_size);
    
    if (firmware_size == 0) {
        LOG_SVC_ERROR("Firmware size is 0 for type=%d, slot=%d", fw_type, slot_idx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Firmware size is 0");
    }

    
    // prepare the export context
    ota_export_ctx_t *export_ctx = (ota_export_ctx_t *)buffer_calloc(1, sizeof(ota_export_ctx_t));
    if (!export_ctx) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for export context");
    }
    
    memset(export_ctx, 0, sizeof(ota_export_ctx_t));
    export_ctx->handle.header = &export_ctx->header_storage;
    export_ctx->buffer = (char *)buffer_calloc(1, 1024); // allocate 1KB buffer
    if (!export_ctx->buffer) {
        buffer_free(export_ctx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for export buffer");
    }

    //begin the export read
    if (ota_upgrade_read_begin(&export_ctx->handle, fw_type, slot_idx) != 0) {
        buffer_free(export_ctx->buffer);
        buffer_free(export_ctx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to begin read");
    }
    export_ctx->remaining_size = export_ctx->handle.total_size;

    //send response header
    mg_printf(ctx->conn, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %u\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Connection: close\r\n" 
        "\r\n", 
        (unsigned int)export_ctx->remaining_size, export_filename);


    //set the callback function
    ctx->conn->fn = ota_stream_export_cb;
    ctx->conn->fn_data = export_ctx;
    
    LOG_SVC_INFO("Firmware export started: %s, %u bytes", export_filename, (unsigned int)export_ctx->remaining_size);
    
    return AICAM_ERROR_NOT_SENT_AGAIN;
}



/* ==================== Route Registration ==================== */

/**
 * @brief OTA API module routes
 */
static const api_route_t ota_module_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/precheck",
        .handler = ota_precheck_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/upload",
        .handler = ota_upload_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/upgrade-local",
        .handler = ota_upgrade_local_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/export",
        .handler = ota_export_firmware_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register OTA API module
 */
aicam_result_t web_api_register_ota_module(void)
{
    LOG_SVC_INFO("Registering OTA API module...");
    
    // Register each route
    for (size_t i = 0; i < sizeof(ota_module_routes) / sizeof(ota_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&ota_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", ota_module_routes[i].path, result);
            return result;
        }
    }
    
    LOG_SVC_INFO("OTA API module registered successfully (%u routes)", 
                (unsigned int)sizeof(ota_module_routes) / sizeof(ota_module_routes[0]));
    
    return AICAM_OK;
}

/**
 * @brief Check OTA upload timeout and reset state if necessary
 * @return AICAM_TRUE if timeout occurred and state was reset, AICAM_FALSE otherwise
 */
aicam_bool_t ota_check_timeout(void)
{
    if (!g_ota_upgrade_in_progress) {
        return AICAM_FALSE;
    }
    
    uint32_t current_tick = osKernelGetTickCount();
    uint32_t elapsed = current_tick - g_ota_last_activity_tick;
    
    if (elapsed > OTA_TIMEOUT_MS) {
        LOG_SVC_WARN("OTA upload timeout after %u ms, resetting state", elapsed);
        g_ota_upgrade_in_progress = AICAM_FALSE;
        g_ota_last_activity_tick = 0;

        // Restart services that were stopped for OTA
        if (g_ota_stopped_ai) {
            ai_pipeline_start();
            LOG_SVC_INFO("AI pipeline restarted after OTA timeout");
        }
        if (g_ota_stopped_camera) {
            device_service_camera_start();
            LOG_SVC_INFO("Camera restarted after OTA timeout");
        }
        if (g_ota_stopped_mqtt) {
            mqtt_service_start();
            LOG_SVC_INFO("MQTT service restarted after OTA timeout");
        }
        g_ota_stopped_mqtt = AICAM_FALSE;
        g_ota_stopped_camera = AICAM_FALSE;
        g_ota_stopped_ai = AICAM_FALSE;

        return AICAM_TRUE;
    }
    
    return AICAM_FALSE;
}

/**
 * @brief Force reset OTA upload state
 */
void ota_reset_upload_state(void)
{
    if (g_ota_upgrade_in_progress) {
        LOG_SVC_WARN("Force resetting OTA upload state");
    }
    g_ota_upgrade_in_progress = AICAM_FALSE;
    g_ota_last_activity_tick = 0;

    // Restart services that were stopped for OTA
    if (g_ota_stopped_ai) {
        ai_pipeline_start();
        LOG_SVC_INFO("AI pipeline restarted after OTA state reset");
    }
    if (g_ota_stopped_camera) {
        device_service_camera_start();
        LOG_SVC_INFO("Camera restarted after OTA state reset");
    }
    if (g_ota_stopped_mqtt) {
        mqtt_service_start();
        LOG_SVC_INFO("MQTT service restarted after OTA state reset");
    }
    g_ota_stopped_mqtt = AICAM_FALSE;
    g_ota_stopped_camera = AICAM_FALSE;
    g_ota_stopped_ai = AICAM_FALSE;
}

/**
 * @brief Check if OTA upload is in progress
 * @return AICAM_TRUE if OTA upload is in progress, AICAM_FALSE otherwise
 */
aicam_bool_t ota_is_upload_in_progress(void)
{
    return g_ota_upgrade_in_progress;
}