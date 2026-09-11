/**
 * @file web_service.c
 * @brief Web Service Implementation
 */

#include "web_service.h"
#include "web_server.h"
#include "web_api.h"
#include "web_assets.h"
#include "aicam_types.h"
#include "debug.h"
#include "websocket_stream_server.h"
#include "api_auth_module.h"
#include "api_work_mode_module.h"
#include "api_model_validation_module.h"
#include "api_ai_management_module.h"
#include "api_mqtt_module.h"
#include "api_network_module.h"
#include "api_device_module.h"
#include "api_ota_module.h"
#include "api_rtmp_module.h"
#include "api_rtsp_module.h"
#include "api_webhook_module.h"
#include "api_capture_module.h"
#include "api_preview_module.h"
#include "api_isp_module.h"
#include "api_file_module.h"
#include "web_recovery.h"
#include "mem_map.h"
#include <string.h>

/* ==================== Web Service Context ==================== */

typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
    http_server_config_t config;
    websocket_stream_config_t ws_config;
} web_service_context_t;

static web_service_context_t g_web_service = {0};
#define WEB_ASSETS_FLASH_ADDRESS WEB_BASE


/* ==================== Web Service Implementation ==================== */

aicam_result_t web_service_init(void *config)
{
    if (g_web_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_CORE_INFO("Initializing Web Service...");
    

     // Set default configuration
    memset(&g_web_service.config, 0, sizeof(http_server_config_t));
    g_web_service.config = HTTP_SERVER_CONFIG_INIT();
    // Override with provided config if available
    if (config) {
        memcpy(&g_web_service.config, config, sizeof(http_server_config_t));
    }

    aicam_result_t result = http_server_init(&g_web_service.config);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("HTTP server initialization failed: %d", result);
        return result;
    }
    
    // Initialize WebSocket stream server configuration
    websocket_stream_get_default_config(&g_web_service.ws_config);


    // Initialize API gateway
    result = api_gateway_init(API_PATH_PREFIX);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("API gateway initialization failed: %d", result);
        return result;
    }

    // register api module
    web_api_register_auth_module();
    web_api_register_work_mode_module();
    web_api_register_model_validation_module();
    web_api_register_ai_management_module();
    web_api_register_mqtt_module();
    web_api_register_network_module();
    web_api_register_device_module();
    web_api_register_ota_module();
    web_api_register_rtmp_module();
    web_api_register_rtsp_module();
    web_api_register_webhook_module();
    web_api_register_capture_module();
    web_api_register_preview_module();
    web_api_register_isp_module();
    web_api_register_file_module();

    // Initialize static resources
    result = web_asset_adapter_init((const uint8_t*)WEB_ASSETS_FLASH_ADDRESS);
    if (result != AICAM_OK) {
        // The web asset firmware is missing/corrupt (e.g. after an OTA moved the
        // web partition address). Don't abort the whole service — fall back to
        // the built-in recovery page so the user can reflash the web firmware.
        LOG_CORE_WARN("Failed to initialize static resources: %d, entering recovery mode", result);
        web_recovery_activate();
    }
    
    // Initialize WebSocket stream server
    result = websocket_stream_server_init(&g_web_service.ws_config);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("WebSocket stream server initialization failed: %d", result);
        return result;
    }
    
    
    g_web_service.initialized = AICAM_TRUE;
    g_web_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_CORE_INFO("Web Service initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t web_service_start(void)
{
    if (!g_web_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_web_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_CORE_INFO("Starting Web Service...");
    
    // Start HTTP server
    aicam_result_t result = http_server_start();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("HTTP server start failed: %d", result);
        return result;
    }
    
    // Start WebSocket stream server
    result = websocket_stream_server_start();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("WebSocket stream server start failed: %d", result);
        return result;
    }
    
    // Start video stream
    // result = websocket_stream_server_start_stream(1);
    // if (result != AICAM_OK) {
    //     LOG_CORE_ERROR("Failed to start video stream: %d", result);
    //     websocket_stream_server_stop();
    //     return result;
    // }
    
    g_web_service.running = AICAM_TRUE;
    g_web_service.state = SERVICE_STATE_RUNNING;
    
    LOG_CORE_INFO("Web Service started successfully on port %d", g_web_service.config.port);
    
    return AICAM_OK;
}

aicam_result_t web_service_stop(void)
{
    if (!g_web_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_web_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_CORE_INFO("Stopping Web Service...");
    
    // Stop video stream
    aicam_result_t result = websocket_stream_server_stop_stream();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to stop video stream: %d", result);
    }
    
    // Stop WebSocket stream server
    result = websocket_stream_server_stop();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("WebSocket stream server stop failed: %d", result);
    }
    
    // Stop HTTP server
    result = http_server_stop();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("HTTP server stop failed: %d", result);
        return result;
    }
    
    g_web_service.running = AICAM_FALSE;
    g_web_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_CORE_INFO("Web Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t web_service_deinit(void)
{
    if (!g_web_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_web_service.running) {
        web_service_stop();
    }
    
    LOG_CORE_INFO("Deinitializing Web Service...");
    
    // Deinitialize WebSocket stream server
    aicam_result_t result = websocket_stream_server_deinit();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("WebSocket stream server deinitialization failed: %d", result);
    }
    
    // Deinitialize HTTP server
    result = http_server_deinit();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("HTTP server deinitialization failed: %d", result);
    }
    
    // Reset context
    memset(&g_web_service, 0, sizeof(web_service_context_t));
    
    LOG_CORE_INFO("Web Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t web_service_get_state(void)
{
    return g_web_service.state;
}

/* ==================== Web Service Configuration API ==================== */

aicam_result_t web_service_set_config(const http_server_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (g_web_service.running) {
        return AICAM_ERROR_BUSY;
    }
    
    memcpy(&g_web_service.config, config, sizeof(http_server_config_t));
    
    return AICAM_OK;
}

aicam_result_t web_service_get_config(http_server_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(config, &g_web_service.config, sizeof(http_server_config_t));
    
    return AICAM_OK;
}

