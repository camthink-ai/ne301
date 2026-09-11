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
#include "ota_bundle.h"
#include "json_config_mgr.h"

#define OTA_WRITE_BUF_SIZE 1024
#define OTA_PRECHECK_DATA_SIZE 2048  // 2KB: 1KB OTA header + 1KB model package header
#define OTA_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes timeout for OTA upload
#define OTA_BUNDLE_TIMEOUT_MS (10 * 60 * 1000)  // idle timeout for a bundle session (gaps between sub-uploads)
/* ==================== Global Variables ==================== */
static aicam_bool_t g_ota_upgrade_in_progress = AICAM_FALSE;
static uint32_t g_ota_last_activity_tick = 0;
static aicam_bool_t g_ota_stopped_mqtt = AICAM_FALSE;
static aicam_bool_t g_ota_stopped_camera = AICAM_FALSE;
static aicam_bool_t g_ota_stopped_ai = AICAM_FALSE;

/* ==================== OTA Bundle Session ====================
 * One-click full-upgrade session, orchestrated by the browser:
 *   POST bundle/precheck  -> device validates the 4KB bundle header against
 *                            its own partition table and returns a burn plan
 *   POST bundle/begin     -> arm the session + erase OTA info (every time:
 *                            the bundle is a forced full reflash, the next
 *                            boot rebuilds SystemState from the new layout)
 *   POST upload?direct=1&addr=0x...  -> per-firmware streaming burn at the
 *                            device-resolved explicit flash addresses
 *   POST bundle/finish    -> set the WiFi update flag on success, clear session
 * The device is the single source of truth: direct-mode uploads are accepted
 * only for a type/addr pair that the precheck plan itself resolved. */
typedef struct {
    aicam_bool_t planned;         /* this firmware type is in the bundle — burns
                                   * unless the user skips it at begin (skippable
                                   * entries only) */
    aicam_bool_t skippable;       /* user may skip this entry: same version AND
                                   * unmoved partition AND — FSBL only — an
                                   * unchanged layout (the bootloader binary
                                   * embeds the whole compile-time table) */
    uint32_t direct_addr;         /* resolved absolute flash address */
    uint32_t direct_size;         /* target partition size from the bundle table */
    aicam_bool_t completed;       /* its direct upload finished successfully —
                                   * finish(result=ok) refuses to clear the
                                   * session while a planned entry is missing */
} bundle_plan_entry_t;

typedef struct {
    aicam_bool_t prechecked;       /* header validated, plan built */
    aicam_bool_t active;           /* begin'ed, awaiting uploads + finish */
    aicam_bool_t has_wifi_update;  /* wifi entry in the bundle */
    uint32_t     last_activity;
    ota_bundle_header_t header;    /* 4KB validated copy */
    bundle_plan_entry_t plan[FIRMWARE_TYPE_COUNT];
} ota_bundle_session_t;

static ota_bundle_session_t g_bundle_session;

typedef struct {
    upgrade_handle_t handle;
    firmware_header_t header_storage;
    uint32_t remaining_size;
    char *buffer;
} ota_export_ctx_t;

typedef struct {
    uint32_t   magic;             /* OTA_UPLOAD_CTX_MAGIC - connection router tag */
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

    aicam_bool_t direct_mode;      /* bundle direct-address upload (?direct=1&addr=...) */
    uint32_t direct_addr;          /* absolute flash address required by the bundle plan */
    uint8_t *fsbl_stage_buf;       /* FSBL payload staged in PSRAM until CRC verifies */
    size_t fsbl_stage_size;
    size_t fsbl_stage_pos;

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

/* ==================== OTA Bundle Helpers ==================== */

/**
 * @brief Map a bundle entry fw_type code to the device FirmwareType
 * @return FIRMWARE_TYPE_COUNT when the code is not bundle-supported
 */
static FirmwareType bundle_fw_type_to_firmware(uint8_t fw_type) {
    switch (fw_type) {
        case 0x01: return FIRMWARE_FSBL;
        case 0x02: return FIRMWARE_APP;
        case 0x03: return FIRMWARE_WEB;
        case 0x04: return FIRMWARE_AI_2;   /* "ai" — AI slot resolved at runtime */
        case 0x08: return FIRMWARE_WIFI;
        default:   return FIRMWARE_TYPE_COUNT;
    }
}

/**
 * @brief Plan type string used both in the precheck JSON and by the frontend
 */
static const char *bundle_fw_type_name(FirmwareType type) {
    switch (type) {
        case FIRMWARE_FSBL: return "fsbl";
        case FIRMWARE_APP:  return "app";
        case FIRMWARE_WEB:  return "web";
        case FIRMWARE_AI_1:
        case FIRMWARE_AI_2: return "ai";
        case FIRMWARE_WIFI: return "wifi";
        default:            return "unknown";
    }
}

/**
 * @brief Which bundle partition table entry a direct-mode burn targets
 *
 * Direct mode always burns APP to the bundle's APP1 base and the AI model to
 * the bundle's AI_1 base: after the OTA info partition is blanked, the new
 * firmware rebuilds SystemState with slot A active, i.e. APP1 / AI_1.  The
 * NVS ai_1_active flag is reset on the AI burn to match.
 */
static bundle_part_id_t bundle_direct_part_id(FirmwareType type) {
    switch (type) {
        case FIRMWARE_FSBL: return BUNDLE_PART_FSBL;
        case FIRMWARE_APP:  return BUNDLE_PART_APP1;
        case FIRMWARE_WEB:  return BUNDLE_PART_WEB;
        case FIRMWARE_AI_1:
        case FIRMWARE_AI_2: return BUNDLE_PART_AI_1;
        case FIRMWARE_WIFI: return BUNDLE_PART_WIFI;
        default:            return BUNDLE_PART_ID_COUNT;
    }
}

/**
 * @brief Current version string of the running firmware of a bundle type
 */
static void bundle_current_version(FirmwareType type, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '0';
    buf[1] = '\0';

    if (type == FIRMWARE_WIFI) {
        /* WiFi firmware version is tracked by the 917 host, not slot state */
        if (wifi_get_running_version(buf, buf_size) != 0) {
            snprintf(buf, buf_size, "0.0.0.0");
        }
        return;
    }

    FirmwareType state_type = type;
    if (type == FIRMWARE_AI_1 || type == FIRMWARE_AI_2) {
        /* Inverted NVS naming: ai_1_active == TRUE means AI_2 is active */
        state_type = json_config_get_ai_1_active() ? FIRMWARE_AI_2 : FIRMWARE_AI_1;
    }

    SystemState *state = ota_get_system_state();
    if (!state) return;
    int slot_idx = state->active_slot[state_type];
    if (slot_idx != SLOT_A && slot_idx != SLOT_B) slot_idx = SLOT_A;
    ota_version_to_string(state->slot[state_type][slot_idx].version, buf, buf_size);
}

/**
 * @brief Wipe the bundle session (after finish / abort / timeout)
 */
static void bundle_session_clear(void) {
    memset(&g_bundle_session, 0, sizeof(g_bundle_session));
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

    // 1.5 device-model gate: a package stamped for another device model
    // (e.g. an NE302 build on an NE301) is rejected before any flash is
    // touched — this covers BOTH the normal and the bundle-direct paths.
    // 0 = unstamped (legacy packages): allowed.
    if (header->device_model != 0 && header->device_model != OTA_DEVICE_MODEL) {
        LOG_SVC_ERROR("Firmware model mismatch: package 0x%04X, device 0x%04X",
                      (unsigned)header->device_model, (unsigned)OTA_DEVICE_MODEL);
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

    // 4. direct-address mode (bundle forced burn): validate against the
    //    prechecked bundle plan instead of the compiled-in partition table.
    //    Triple gate: session armed + type in plan + address is the one the
    //    device itself resolved at precheck. No AB slots, no system state.
    if (ctx->direct_mode) {
        if (!g_bundle_session.active) {
            LOG_SVC_ERROR("Direct upload rejected: no active bundle session");
            return -1;
        }
        if (ctx->fw_type_param >= FIRMWARE_TYPE_COUNT ||
            !g_bundle_session.plan[ctx->fw_type_param].planned) {
            LOG_SVC_ERROR("Direct upload rejected: firmware type %d not in bundle plan",
                          ctx->fw_type_param);
            return -1;
        }
        if (ctx->direct_addr != g_bundle_session.plan[ctx->fw_type_param].direct_addr) {
            LOG_SVC_ERROR("Direct upload rejected: addr 0x%08X != plan 0x%08X",
                          ctx->direct_addr,
                          g_bundle_session.plan[ctx->fw_type_param].direct_addr);
            return -1;
        }
        if (ctx->fw_header.file_size > g_bundle_session.plan[ctx->fw_type_param].direct_size) {
            LOG_SVC_ERROR("Direct upload rejected: size %u > partition %u",
                          ctx->fw_header.file_size,
                          g_bundle_session.plan[ctx->fw_type_param].direct_size);
            return -1;
        }
    } else {
        // 4a. normal single-firmware path — mutually exclusive with a bundle
        //     session (which has already erased OTA info)
        if (g_bundle_session.active) {
            LOG_SVC_ERROR("Normal upload rejected: bundle session in progress");
            return -1;
        }
        // 4b. validate the firmware header options (old-layout partition table)
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

        // 4b. validate the system state
        val_res = ota_validate_system_state(ctx->fw_type_param);
        if (val_res != OTA_VALIDATION_OK) {
            LOG_SVC_ERROR("System state validation failed: %d", val_res);
            return -1;
        }
    }

    // 5. FSBL is the one firmware whose corruption bricks the device, so its
    //    payload is staged in PSRAM and CRC-verified BEFORE any flash erase.
    //    upgrade_begin (which erases the target region) is deferred to
    //    finalize; on CRC failure nothing was touched.
    if (ctx->fw_type_param == FIRMWARE_FSBL) {
        size_t payload_size = ctx->content_length - sizeof(ota_header_t);
        if (payload_size == 0 || payload_size > FSBL_SIZE) {
            LOG_SVC_ERROR("FSBL payload size %u out of range", (unsigned)payload_size);
            return -1;
        }
        ctx->fsbl_stage_buf = (uint8_t *)buffer_calloc(1, payload_size);
        if (!ctx->fsbl_stage_buf) {
            LOG_SVC_ERROR("FSBL stage buffer alloc failed (%u bytes)", (unsigned)payload_size);
            return -1;
        }
        ctx->fsbl_stage_size = payload_size;
        ctx->fsbl_stage_pos = 0;
        LOG_SVC_INFO("FSBL staged write: %u bytes buffered in PSRAM", (unsigned)payload_size);
        return 0;
    }

    // 6. start the upgrade
    if (ctx->direct_mode) {
        if (ota_upgrade_begin_direct(&ctx->upgrade_handle, ctx->fw_type_param,
                                     &ctx->fw_header, ctx->direct_addr) != 0) {
            LOG_SVC_ERROR("upgrade_begin_direct failed (addr=0x%08X)", ctx->direct_addr);
            return -1;
        }
    } else if (ota_upgrade_begin(&ctx->upgrade_handle, ctx->fw_type_param, &ctx->fw_header) != 0) {
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

    // 1.5 Device-model gate (early feedback; the upload re-checks)
    if (header->device_model != 0 && header->device_model != OTA_DEVICE_MODEL) {
        LOG_SVC_ERROR("Pre-check failed: firmware model mismatch (package 0x%04X, device 0x%04X)",
                      (unsigned)header->device_model, (unsigned)OTA_DEVICE_MODEL);
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
            /* NB: third arg is a C boolean value, NOT a cJSON_True/False type
             * tag (cJSON_False == 1 is truthy and would emit true). */
            cJSON_AddBoolToObject(response_data, "valid", 0);
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
    
    cJSON_AddBoolToObject(response_data, "valid", 1);
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

    /* Partition-layout drift warning: the package stamps a CRC of the table
     * it was built for; a different CRC on the running firmware means the
     * layout moved in between — single-firmware burn addresses come from
     * THIS firmware's compile-time table, so the UI warns and offers the
     * bundle path (which carries its own table and migrates). 0 = unstamped
     * (old packages), check skipped. */
    {
        uint32_t dev_table_crc = ota_bundle_part_table_crc();
        cJSON_AddBoolToObject(response_data, "part_table_changed",
                              (header->part_table_crc != 0 &&
                               header->part_table_crc != dev_table_crc) ? 1 : 0);
        cJSON_AddNumberToObject(response_data, "device_table_crc", (double)dev_table_crc);
        cJSON_AddNumberToObject(response_data, "package_table_crc",
                                (double)header->part_table_crc);
    }
    
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


/* ==================== OTA Bundle Session Handlers ==================== */

/**
 * @brief Bundle pre-check handler
 * POST /api/v1/system/ota/bundle/precheck
 * Body: the first 4096 bytes of the bundle file (the bundle header).
 *
 * The device is the single source of truth: it validates the header and the
 * bundle's partition table (fixed partitions must not move) and answers with
 * the forced burn plan (every entry burns, at device-resolved direct
 * addresses — no AB slots). The browser then uploads each sub-package in
 * plan order.
 */
aicam_result_t ota_bundle_precheck_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!ctx->request.body || ctx->request.content_length < BUNDLE_HEADER_SIZE) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Request body must be the 4096-byte bundle header");
    }

    /* A previously armed (stale) session — e.g. the browser refreshed mid-burn —
     * is taken over, not rejected: the precheck below clears and rebuilds the
     * session from the new header, and the next begin re-arms it (re-erasing
     * OTA info is idempotent). Only a genuinely in-flight upload blocks. */
    if (g_bundle_session.active) {
        LOG_SVC_INFO("OTA bundle precheck: taking over stale session (page refresh / re-entry)");
    }

    if (g_ota_upgrade_in_progress) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "OTA upload in progress");
    }

    /* Validate into the session slot (4KB, no stack); any failure wipes it */
    bundle_session_clear();
    memcpy(&g_bundle_session.header, ctx->request.body, BUNDLE_HEADER_SIZE);

    if (ota_bundle_header_verify(&g_bundle_session.header) != 0) {
        bundle_session_clear();
        return api_response_error(ctx, API_BUSINESS_ERROR_OTA_HEADER_VALIDATION_FAILED,
                                  "Invalid bundle header (magic/size/CRC)");
    }

    /* Device-model gate — before anything with side effects (bundle/begin
     * erases OTA info): a bundle built for another model is rejected here.
     * Each embedded sub-package carries the same stamp and is re-checked at
     * upload time. 0 = unstamped (legacy bundles): allowed. */
    if (g_bundle_session.header.device_model != 0 &&
        g_bundle_session.header.device_model != OTA_DEVICE_MODEL) {
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Bundle built for a different device model");
    }

    if (ota_bundle_entries_sane(&g_bundle_session.header) != 0) {
        bundle_session_clear();
        return api_response_error(ctx, API_BUSINESS_ERROR_OTA_HEADER_VALIDATION_FAILED,
                                  "Invalid bundle entry table");
    }

    /* Layout policy: the bundle path always burns direct to the bundle's own
     * partition table (no AB slots, no layout-changed distinction). The table
     * must be physically possible (no overlap, inside flash) and FSBL must be
     * declared and unmoved — the boot ROM address is silicon-fixed. Any other
     * partition may move; the user-visible cost (settings/storage reset) is
     * reported via layout_changed/layout_diff and warned in the UI. */
    if (ota_bundle_layout_change_valid(&g_bundle_session.header) != 0) {
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Invalid partition table (FSBL must not move; no overlap / within flash)");
    }
    aicam_bool_t layout_changed =
        (ota_bundle_layout_matches_device(&g_bundle_session.header) != 0) ? AICAM_TRUE : AICAM_FALSE;

    /* ---- pass 1: validate entries, resolve plan (every entry burns) ---- */
    uint32_t total_update_bytes = 0;
    aicam_bool_t has_fsbl = AICAM_FALSE;
    for (uint32_t i = 0; i < g_bundle_session.header.entry_count; i++) {
        const bundle_entry_t *entry = &g_bundle_session.header.entries[i];

        FirmwareType fw = bundle_fw_type_to_firmware(entry->fw_type);
        if (fw == FIRMWARE_TYPE_COUNT) {
            bundle_session_clear();
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Unsupported fw_type in bundle");
        }
        if (g_bundle_session.plan[fw].planned) {
            bundle_session_clear();
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Duplicate firmware type in bundle");
        }

        /* direct-mode target: resolve from the bundle's partition table */
        uint32_t direct_addr = 0, direct_size = 0;
        bundle_part_id_t part_id = bundle_direct_part_id(fw);
        if (ota_bundle_part_lookup(&g_bundle_session.header, (uint8_t)part_id,
                                   &direct_addr, &direct_size) != 0) {
            bundle_session_clear();
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "Bundle partition table is missing a required partition");
        }
        if (entry->size > direct_size) {
            bundle_session_clear();
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "Bundle entry larger than its target partition");
        }

        g_bundle_session.plan[fw].planned = AICAM_TRUE;
        g_bundle_session.plan[fw].completed = AICAM_FALSE;
        g_bundle_session.plan[fw].direct_addr = direct_addr;
        g_bundle_session.plan[fw].direct_size = direct_size;
        total_update_bytes += entry->size;
        if (fw == FIRMWARE_WIFI) {
            g_bundle_session.has_wifi_update = AICAM_TRUE;
        } else if (fw == FIRMWARE_FSBL) {
            has_fsbl = AICAM_TRUE;
        }
    }

    /* A layout-changing bundle MUST carry FSBL: the bootloader embeds the
     * compile-time partition table (SystemState rebuild + APP copy source),
     * so migrating layouts without a matching bootloader leaves the device
     * reading the new layout from the old addresses — unbootable. */
    if (layout_changed && !has_fsbl) {
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Layout-changing bundle must contain FSBL "
                                  "(the bootloader embeds the partition table)");
    }

    /* ...and every burned partition the bundle MOVES or RESIZES must carry
     * its firmware: after the burn the system runs on the bundle's table, so
     * e.g. a moved APP1 in a bundle packed with --exclude app would boot from
     * a blank address, and a resized one can have its stale tail overwritten
     * by a later partition's image. */
    if (layout_changed &&
        ota_bundle_moved_burn_part_missing_fw(&g_bundle_session.header) >= 0) {
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Layout-changing bundle moves or resizes a burn partition without its firmware");
    }

    /* ---- pass 2: build the plan JSON ---- */
    cJSON *entries_arr = cJSON_CreateArray();
    cJSON *resp = cJSON_CreateObject();
    if (!entries_arr || !resp) {
        if (entries_arr) cJSON_Delete(entries_arr);
        if (resp) cJSON_Delete(resp);
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    for (uint32_t i = 0; i < g_bundle_session.header.entry_count; i++) {
        const bundle_entry_t *entry = &g_bundle_session.header.entries[i];
        FirmwareType fw = bundle_fw_type_to_firmware(entry->fw_type);

        char cur_ver[24] = {0}, new_ver[24] = {0};
        bundle_current_version(fw, cur_ver, sizeof(cur_ver));
        ota_version_to_string(entry->version, new_ver, sizeof(new_ver));

        /* Skippable = same version AND unmoved partition AND — for FSBL —
         * an unchanged layout: the bootloader binary embeds the whole
         * compile-time table (SystemState rebuild + APP copy source), so a
         * layout drift makes the running FSBL stale even when its own
         * partition never moved and its version matches. begin re-validates
         * the skip list against this flag — the device, not the browser,
         * decides what is skippable. */
        aicam_bool_t same_version =
            (strcmp(cur_ver, new_ver) == 0) ? AICAM_TRUE : AICAM_FALSE;
        uint32_t dev_base = 0, dev_size = 0;
        aicam_bool_t same_addr =
            (ota_bundle_device_part((uint8_t)bundle_direct_part_id(fw),
                                    &dev_base, &dev_size) == 0 &&
             dev_base == g_bundle_session.plan[fw].direct_addr &&
             dev_size == g_bundle_session.plan[fw].direct_size)
                ? AICAM_TRUE : AICAM_FALSE;
        g_bundle_session.plan[fw].skippable =
            (same_version && same_addr && !(fw == FIRMWARE_FSBL && layout_changed))
                ? AICAM_TRUE : AICAM_FALSE;

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "type", bundle_fw_type_name(fw));
        cJSON_AddNumberToObject(item, "fw_type", entry->fw_type);
        cJSON_AddBoolToObject(item, "skippable",
                              g_bundle_session.plan[fw].skippable != 0);
        cJSON_AddStringToObject(item, "cur_version", cur_ver);
        cJSON_AddStringToObject(item, "new_version", new_ver);
        cJSON_AddNumberToObject(item, "size", (double)entry->size);
        /* byte offset of this sub-package within the bundle file (after the
         * 4096-byte header) — the browser slices file uploads from it */
        cJSON_AddNumberToObject(item, "offset", (double)entry->offset);
        /* burn address resolved by the device from the bundle partition table */
        cJSON_AddNumberToObject(item, "addr", (double)g_bundle_session.plan[fw].direct_addr);
        cJSON_AddItemToArray(entries_arr, item);
    }

    g_bundle_session.prechecked = AICAM_TRUE;
    g_bundle_session.last_activity = osKernelGetTickCount();

    cJSON_AddBoolToObject(resp, "layout_changed", layout_changed != 0);
    if (layout_changed) {
        /* Per-partition diff (device compile-time table vs bundle table) so the
         * UI can show exactly which partitions move and where — the warning
         * text alone is not actionable when debugging a false positive. */
        cJSON *diff_arr = cJSON_CreateArray();
        uint8_t bundle_seen[BUNDLE_PART_ID_COUNT] = {0};
        for (uint32_t i = 0; diff_arr && i < g_bundle_session.header.part_count &&
                            i < BUNDLE_MAX_PARTS; i++) {
            const bundle_part_t *p = &g_bundle_session.header.parts[i];
            if (p->part_id >= BUNDLE_PART_ID_COUNT) continue;
            bundle_seen[p->part_id] = 1;
            uint32_t dev_base = 0, dev_size = 0;
            if (ota_bundle_device_part(p->part_id, &dev_base, &dev_size) != 0 ||
                dev_base != p->base || dev_size != p->size) {
                cJSON *d = cJSON_CreateObject();
                if (d) {
                    cJSON_AddStringToObject(d, "part", ota_bundle_part_name(p->part_id));
                    cJSON_AddNumberToObject(d, "device_base", (double)dev_base);
                    cJSON_AddNumberToObject(d, "device_size", (double)dev_size);
                    cJSON_AddNumberToObject(d, "bundle_base", (double)p->base);
                    cJSON_AddNumberToObject(d, "bundle_size", (double)p->size);
                    cJSON_AddItemToArray(diff_arr, d);
                }
            }
        }
        /* ids absent from the bundle table count as a diff too */
        for (int id = 0; diff_arr && id < BUNDLE_PART_ID_COUNT; id++) {
            if (!bundle_seen[id]) {
                uint32_t dev_base = 0, dev_size = 0;
                ota_bundle_device_part((uint8_t)id, &dev_base, &dev_size);
                cJSON *d = cJSON_CreateObject();
                if (d) {
                    cJSON_AddStringToObject(d, "part", ota_bundle_part_name((uint8_t)id));
                    cJSON_AddNumberToObject(d, "device_base", (double)dev_base);
                    cJSON_AddNumberToObject(d, "device_size", (double)dev_size);
                    cJSON_AddNumberToObject(d, "bundle_base", -1);
                    cJSON_AddNumberToObject(d, "bundle_size", -1);
                    cJSON_AddItemToArray(diff_arr, d);
                }
            }
        }
        if (diff_arr) cJSON_AddItemToObject(resp, "layout_diff", diff_arr);
    }
    cJSON_AddNumberToObject(resp, "total_bytes", (double)total_update_bytes);
    cJSON_AddItemToObject(resp, "entries", entries_arr);

    char *data_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!data_str) {
        bundle_session_clear();
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to format response");
    }

    LOG_SVC_INFO("Bundle precheck passed: forced direct burn, entries=%u update_bytes=%u layout_changed=%d",
                 g_bundle_session.header.entry_count, total_update_bytes, (int)layout_changed);

    api_response_success(ctx, data_str, "Bundle precheck passed");
    return AICAM_OK;
}

/**
 * @brief Arm a prechecked bundle session
 * POST /api/v1/system/ota/bundle/begin
 */
aicam_result_t ota_bundle_begin_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!g_bundle_session.prechecked) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "No prechecked bundle (POST bundle/precheck first)");
    }

    if (g_ota_upgrade_in_progress) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "OTA upload in progress");
    }

    /* Optional user skip list ({"skip": ["wifi", ...]}): same-version entries
     * only, never all of them. Validated here so a buggy or hostile client can
     * neither skip a version-differing firmware nor end up with nothing to
     * burn after OTA info has been erased. */
    cJSON *request = web_api_parse_body(ctx);
    cJSON *skip_arr = request ? cJSON_GetObjectItem(request, "skip") : NULL;
    uint32_t burn_count = 0;
    for (int fw = 0; fw < FIRMWARE_TYPE_COUNT; fw++) {
        if (g_bundle_session.plan[fw].planned) burn_count++;
    }
    if (skip_arr) {
        if (!cJSON_IsArray(skip_arr)) {
            cJSON_Delete(request);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "\"skip\" must be an array of type names");
        }
        cJSON *name = NULL;
        cJSON_ArrayForEach(name, skip_arr) {
            /* Resolve through the same fw_type-code mapping the plan itself
             * is keyed by: the name "ai" must land on FIRMWARE_AI_2 (the slot
             * bundles burn to), never AI_1 — a plain enum scan matches AI_1
             * first (bundle_fw_type_name maps both AI ids to "ai") and finds
             * nothing planned there, rejecting valid skip requests. */
            FirmwareType fw = FIRMWARE_TYPE_COUNT;
            if (cJSON_IsString(name)) {
                static const uint8_t bundle_fw_codes[] = {0x01, 0x02, 0x03, 0x04, 0x08};
                for (size_t k = 0; k < sizeof(bundle_fw_codes); k++) {
                    FirmwareType t = bundle_fw_type_to_firmware(bundle_fw_codes[k]);
                    if (strcmp(name->valuestring, bundle_fw_type_name(t)) == 0) {
                        fw = t;
                        break;
                    }
                }
            }
            if (fw == FIRMWARE_TYPE_COUNT || !g_bundle_session.plan[fw].planned) {
                cJSON_Delete(request);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "Skip list names a firmware not in the bundle");
            }
            if (!g_bundle_session.plan[fw].skippable) {
                cJSON_Delete(request);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "Not skippable: needs same version and unmoved "
                                          "partition (FSBL additionally needs an unchanged layout)");
            }
            g_bundle_session.plan[fw].planned = AICAM_FALSE;
            if (fw == FIRMWARE_WIFI) {
                /* the .rps was not re-staged: no push flag, or reboot would
                 * push a stale image from WIFI_FW_BASE */
                g_bundle_session.has_wifi_update = AICAM_FALSE;
            }
            burn_count--;
            LOG_SVC_INFO("OTA bundle: %s skipped by user (same version)",
                         bundle_fw_type_name(fw));
        }
        if (burn_count == 0) {
            cJSON_Delete(request);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                      "Cannot skip every firmware — nothing would burn");
        }
    }
    if (request) cJSON_Delete(request);

    if (!g_bundle_session.active) {
        g_bundle_session.active = AICAM_TRUE;
        /* Forced full-reflash semantics: OTA info is invalidated up front so
         * the next boot rebuilds SystemState from the freshly burned layout
         * (defaults to slot A = APP1/AI_1, matching the direct burn targets).
         * Done here — not at the FSBL burn — so it also happens for bundles
         * that exclude FSBL. */
        upgrade_erase_ota_info();
        LOG_SVC_INFO("OTA bundle session armed: forced direct burn, %u/%u entries (OTA info erased)",
                     burn_count, g_bundle_session.header.entry_count);
    }
    g_bundle_session.last_activity = osKernelGetTickCount();

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddNumberToObject(resp, "burn_count", (double)burn_count);
        char *data_str = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        if (data_str) {
            return api_response_success(ctx, data_str, "Bundle session started");
        }
    }
    return api_response_success(ctx, NULL, "Bundle session started");
}

/**
 * @brief Finish (or abort) a bundle session
 * POST /api/v1/system/ota/bundle/finish   body: {"result":"ok"|"abort"}
 *
 * "ok": if the plan included a WiFi update, set the NVS update flag so the
 *       next reboot pushes the staged .rps to the 917 (no reboot here —
 *       the frontend drives the reboot after this call).
 * "abort": just drop the session; already-burned firmwares stay burned
 *       (idempotent — re-running the bundle simply burns them again).
 */
aicam_result_t ota_bundle_finish_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (!g_bundle_session.prechecked) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "No bundle session to finish");
    }

    const char *result = NULL;
    cJSON *request = web_api_parse_body(ctx);
    if (request) {
        cJSON *result_item = cJSON_GetObjectItem(request, "result");
        if (result_item && cJSON_IsString(result_item)) {
            result = result_item->valuestring;
        }
    }
    aicam_bool_t ok = (result && strcmp(result, "ok") == 0) ? AICAM_TRUE : AICAM_FALSE;

    if (ok) {
        /* Refuse a premature "ok": begin already erased the OTA state, so
         * clearing the session with a planned-but-unburned entry and the
         * documented reboot would boot from missing firmware. */
        static const uint8_t bundle_fw_codes[] = {0x01, 0x02, 0x03, 0x04, 0x08};
        for (size_t k = 0; k < sizeof(bundle_fw_codes); k++) {
            FirmwareType t = bundle_fw_type_to_firmware(bundle_fw_codes[k]);
            if (t < FIRMWARE_TYPE_COUNT && g_bundle_session.plan[t].planned &&
                !g_bundle_session.plan[t].completed) {
                if (request) cJSON_Delete(request);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                          "Bundle plan incomplete: firmware not yet uploaded");
            }
        }
    }

    aicam_bool_t wifi_pending = AICAM_FALSE;
    if (ok && g_bundle_session.has_wifi_update) {
        wifi_mark_update_pending();
        wifi_pending = AICAM_TRUE;
    }

    LOG_SVC_INFO("OTA bundle session %s (wifi_pending=%d)", ok ? "finished" : "aborted", wifi_pending);

    if (request) cJSON_Delete(request);
    bundle_session_clear();

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddBoolToObject(resp, "wifi_pending", wifi_pending != 0);
        char *data_str = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        if (data_str) {
            return api_response_success(ctx, data_str, ok ? "Bundle finished" : "Bundle aborted");
        }
    }
    return api_response_success(ctx, NULL, ok ? "Bundle finished" : "Bundle aborted");
}

void ota_upload_stream_processor(struct mg_connection *c, int ev, void *ev_data) {
    ota_upload_ctx_t *ctx = (ota_upload_ctx_t *)c->fn_data;

    /* Type guard for detached (post-HDRS) events: the router dispatches by
     * ctx magic, never touch a context that is not ours. The FIRST call
     * (MG_EV_HTTP_HDRS) arrives with fn_data still pointing at the server
     * instance - that phase creates and attaches the ctx, so it must skip
     * the check (same structure as file_upload_stream_processor). */
    if (ev != MG_EV_HTTP_HDRS) {
        if (!ctx || ctx->magic != OTA_UPLOAD_CTX_MAGIC) return;
    }

    if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        if (ctx) {
            LOG_SVC_INFO("OTA upload cleanup (Event: %d)", ev);
            aicam_bool_t mqtt_was_stopped = ctx->mqtt_was_stopped;
            aicam_bool_t camera_was_stopped = ctx->camera_was_stopped;
            aicam_bool_t ai_was_stopped = ctx->ai_was_stopped;
            if (ctx->fsbl_stage_buf) {
                buffer_free(ctx->fsbl_stage_buf);
                ctx->fsbl_stage_buf = NULL;
            }
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
        ctx->magic = OTA_UPLOAD_CTX_MAGIC;

        ctx->content_length = total_len;
        ctx->fw_type_param = parse_firmware_type(fw_type_str);

        /* Bundle direct-address mode (?direct=1&addr=0x...): only valid under
         * an armed bundle session. Fail fast, before any service is stopped
         * or flash is touched. */
        char direct_str[8] = {0};
        if (mg_http_get_var(&hm->query, "direct", direct_str, sizeof(direct_str)) > 0 &&
            direct_str[0] == '1') {
            char addr_str[16] = {0};
            ctx->direct_mode = AICAM_TRUE;
            if (mg_http_get_var(&hm->query, "addr", addr_str, sizeof(addr_str)) > 0) {
                ctx->direct_addr = (uint32_t)strtoul(addr_str, NULL, 0);
            }
            if (!g_bundle_session.active || ctx->direct_addr == 0) {
                buffer_free(ctx);
                ota_send_response(c, API_ERROR_INVALID_REQUEST,
                                  "Direct upload requires an active bundle session");
                return;
            }
        }
        if (g_bundle_session.active) {
            g_bundle_session.last_activity = osKernelGetTickCount();
        }

        ctx->initialized = AICAM_TRUE;
        ctx->running_crc32 = 0xFFFFFFFF;
        c->fn_data = ctx;

        LOG_SVC_INFO("OTA Stream Init: type=%d (%s), len=%u",
                     ctx->fw_type_param, fw_type_str, (unsigned int)ctx->content_length);

        /* This upload request bypasses web_server_handle_request() (the only
         * place the AP sleep timer is normally refreshed), so touch it here —
         * and again on data below — or a >90s burn in low-power mode would be
         * killed mid-flash by the "no web operation" sleep path. */
        web_server_ap_sleep_timer_reset();

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

        /* Streaming data IS web activity: keep refreshing the AP sleep /
         * idle timer during multi-minute burns (raw-TCP mode never passes
         * through web_server_handle_request()). Throttled to 1 Hz — the
         * reset is an O(1) timestamp write. */
        {
            static uint32_t last_ap_touch_tick = 0;
            uint32_t now_tick = osKernelGetTickCount();
            if (now_tick - last_ap_touch_tick >= 1000) {
                last_ap_touch_tick = now_tick;
                web_server_ap_sleep_timer_reset();
            }
        }

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

            // 2. write to Flash — or, for the staged FSBL path, buffer the
            //    payload in PSRAM; flash is only touched after CRC verifies
            if (ctx->fsbl_stage_buf) {
                if (ctx->fsbl_stage_pos + payload_len > ctx->fsbl_stage_size) {
                    ctx->failed = AICAM_TRUE;
                    ota_send_response(c, API_ERROR_INVALID_REQUEST, "FSBL payload overflow");
                    goto cleanup;
                }
                memcpy(ctx->fsbl_stage_buf + ctx->fsbl_stage_pos, payload, payload_len);
                ctx->fsbl_stage_pos += payload_len;
            } else {
                // 2b. write to Flash
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

            // Staged FSBL commit: the CRC just verified, so NOW it is safe to
            // erase + write. (OTA info was already blanked at bundle/begin.)
            if (ctx->fsbl_stage_buf) {
                int begin_rc;
                if (ctx->direct_mode) {
                    begin_rc = ota_upgrade_begin_direct(&ctx->upgrade_handle, FIRMWARE_FSBL,
                                                        &ctx->fw_header, ctx->direct_addr);
                } else {
                    begin_rc = ota_upgrade_begin(&ctx->upgrade_handle, FIRMWARE_FSBL, &ctx->fw_header);
                }
                if (begin_rc != 0) {
                    ctx->failed = AICAM_TRUE;
                    ota_send_response(c, API_ERROR_INTERNAL_ERROR, "FSBL upgrade begin failed");
                    goto cleanup;
                }

                const uint8_t *stage_p = ctx->fsbl_stage_buf;
                size_t stage_remain = ctx->fsbl_stage_pos;
                while (stage_remain > 0) {
                    size_t chunk = (stage_remain > 4096) ? 4096 : stage_remain;
                    if (ota_upgrade_write_chunk(&ctx->upgrade_handle, stage_p, chunk) != 0) {
                        ctx->failed = AICAM_TRUE;
                        ota_send_response(c, API_ERROR_INTERNAL_ERROR, "FSBL flash write failed");
                        goto cleanup;
                    }
                    stage_p += chunk;
                    stage_remain -= chunk;
                }
                LOG_SVC_INFO("FSBL staged write committed (%u bytes, CRC OK)",
                             (unsigned)ctx->fsbl_stage_pos);
                buffer_free(ctx->fsbl_stage_buf);
                ctx->fsbl_stage_buf = NULL;
            }

            // Finish upgrade (no-op for system state in direct mode)
            if (ota_upgrade_finish(&ctx->upgrade_handle) != 0) {
                LOG_SVC_ERROR("upgrade_finish failed");
                ctx->failed = AICAM_TRUE;
                ota_send_response(c, API_ERROR_INTERNAL_ERROR, "Upgrade finish failed");
                goto cleanup;
            }

            // Update json config. Direct mode burned the model to the bundle's
            // AI_1 slot and the rebuilt SystemState boots slot A (AI_1), so the
            // NVS flag must say AI_1 active (FALSE) instead of AI_2 (TRUE).
            if (ctx->fw_type_param == FIRMWARE_AI_1 || ctx->fw_type_param == FIRMWARE_AI_2) {
                json_config_set_ai_1_active(ctx->direct_mode ? AICAM_FALSE : AICAM_TRUE);
            }

            // Web firmware lives in a single-slot partition at WEB_BASE, so the
            // freshly written asset.bin is already addressable. Reload the web
            // assets now so the new UI is live immediately (and exit recovery
            // mode if we were serving the built-in recovery page). Skipped in
            // direct mode: the new WEB base is unknown to the running firmware
            // (old mem_map), and a reboot follows the bundle anyway.
            if (ctx->fw_type_param == FIRMWARE_WEB && !ctx->direct_mode) {
                web_recovery_reload_assets();
            }

            // WiFi firmware is written to WIFI_FW_BASE (flash_header_t + .rps).
            /* Bundle direct burn finished for this entry: mark it so
             * bundle/finish can refuse a premature "ok" while something in
             * the plan is still missing. */
            if (ctx->direct_mode && g_bundle_session.active) {
                if (ctx->fw_type_param == FIRMWARE_AI_1 || ctx->fw_type_param == FIRMWARE_AI_2) {
                    g_bundle_session.plan[FIRMWARE_AI_2].completed = AICAM_TRUE;
                } else if (ctx->fw_type_param < FIRMWARE_TYPE_COUNT) {
                    g_bundle_session.plan[ctx->fw_type_param].completed = AICAM_TRUE;
                }
            }
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
            if (ctx->fsbl_stage_buf) {
                buffer_free(ctx->fsbl_stage_buf);
                ctx->fsbl_stage_buf = NULL;
            }
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
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/bundle/precheck",
        .handler = ota_bundle_precheck_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/bundle/begin",
        .handler = ota_bundle_begin_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/bundle/finish",
        .handler = ota_bundle_finish_handler,
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
    uint32_t current_tick = osKernelGetTickCount();

    /* Bundle sessions span several sequential uploads with idle gaps while
     * the browser slices the next sub-package; expire the whole session on
     * the longer bundle timeout. */
    if (g_bundle_session.prechecked &&
        (current_tick - g_bundle_session.last_activity) > OTA_BUNDLE_TIMEOUT_MS) {
        LOG_SVC_WARN("OTA bundle session expired after %u ms, clearing",
                     (unsigned)(current_tick - g_bundle_session.last_activity));
        bundle_session_clear();
    }

    if (!g_ota_upgrade_in_progress) {
        return AICAM_FALSE;
    }

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