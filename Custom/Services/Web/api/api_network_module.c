/**
 * @file api_network_module.c
 * @brief Network API Module Implementation
 * @details Network management API implementation based on communication_service
 */

#include "api_network_module.h"
#include "web_api.h"
#include "communication_service.h"
#include "debug.h"
#include "cJSON.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "web_server.h"
#include "json_config_mgr.h"
#include "mm_halow_netif.h"
#if NETIF_WIFI_HALOW_IS_ENABLE
#include "mmwlan.h"
#endif
#include <ctype.h>
#include <strings.h>

/* ==================== Helper Functions ==================== */

/**
 * @brief Convert network interface type to string
 */
static const char* get_interface_type_string(netif_type_t type) {
    switch (type) {
        case NETIF_TYPE_WIRELESS:
            return "wireless";
        case NETIF_TYPE_LOCAL:
            return "local";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert network interface state to string
 */
static const char* get_interface_state_string(netif_state_t state) {
    switch (state) {
        case NETIF_STATE_UP:
            return "up";
        case NETIF_STATE_DOWN:
            return "down";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert security type to string
 */
static const char* network_comm_type_api_string(communication_type_t type) {
#if NETIF_WIFI_HALOW_IS_ENABLE
    if (type == COMM_TYPE_HALOW) {
        return "halow";
    }
#endif
    return communication_type_to_string(type);
}

static communication_type_t network_comm_type_from_string(const char *str)
{
#if NETIF_WIFI_HALOW_IS_ENABLE
    if (str != NULL && strcasecmp(str, "halow") == 0) {
        return COMM_TYPE_HALOW;
    }
#endif
    return communication_type_from_string(str);
}

static const char* get_security_type_string(wireless_security_t security) {
    switch (security) {
        case WIRELESS_OPEN:
            return "open";
        case WIRELESS_WEP:
            return "wep";
        case WIRELESS_WPA:
            return "wpa_psk";
        case WIRELESS_WPA2:
            return "wpa2_psk";
        case WIRELESS_WPA_WPA2_MIXED:
            return "wpa_wpa2_mixed";
        case WIRELESS_WPA3:
            return "wpa3_psk";
        case WIRELESS_SAE:
            return "wpa3_psk";
        default:
            return "unknown";
    }
}

/**
 * @brief Create network interface JSON object
 */
// TODO: Function reserved for future use
static cJSON* __attribute__((unused)) create_interface_json(const network_interface_status_t* interface) {
    if (!interface) return NULL;
    
    cJSON* interface_json = cJSON_CreateObject();
    if (!interface_json) return NULL;
    
    cJSON_AddStringToObject(interface_json, "name", interface->if_name);
    cJSON_AddStringToObject(interface_json, "type", get_interface_type_string(interface->type));
    cJSON_AddStringToObject(interface_json, "state", get_interface_state_string(interface->state));
    cJSON_AddBoolToObject(interface_json, "connected", interface->connected);
    cJSON_AddStringToObject(interface_json, "ip_address", interface->ip_addr);
    cJSON_AddStringToObject(interface_json, "mac_address", interface->mac_addr);
    
    if (interface->type == NETIF_TYPE_WIRELESS) {
        cJSON_AddStringToObject(interface_json, "ssid", interface->ssid);
        cJSON_AddNumberToObject(interface_json, "rssi", interface->rssi);
        cJSON_AddNumberToObject(interface_json, "channel", interface->channel);
    }
    
    return interface_json;
}

/**
 * @brief Create scan result JSON object
 */
static cJSON* create_scan_result_json(const network_scan_result_t* result) {
    if (!result) return NULL;
    
    cJSON* result_json = cJSON_CreateObject();
    if (!result_json) return NULL;
    
    cJSON_AddStringToObject(result_json, "ssid", result->ssid);
    cJSON_AddStringToObject(result_json, "bssid", result->bssid);
    cJSON_AddNumberToObject(result_json, "rssi", result->rssi);
    cJSON_AddNumberToObject(result_json, "channel", result->channel);
    cJSON_AddStringToObject(result_json, "security", get_security_type_string(result->security));
    cJSON_AddBoolToObject(result_json, "connected", result->connected);
    cJSON_AddBoolToObject(result_json, "is_known", result->is_known);
    cJSON_AddNumberToObject(result_json, "last_connected_time", result->last_connected_time);
    
    return result_json;
}

/* ==================== API Handler Functions ==================== */

/**
 * @brief GET /api/v1/system/network/status - Communication Overview
 * 
 * Lightweight API for status bar display and page routing.
 * Only returns essential communication status, no detailed WiFi/Cellular data.
 * 
 * For detailed data, use:
 * - GET /wifi/sta   - WiFi client status + scan results
 * - GET /wifi/ap    - WiFi hotspot config
 * - GET /cellular/status - Cellular status
 * - GET /poe/status - PoE status
 * 
 * Response:
 * {
 *   "service_state": "running",
 *   "current_comm_type": "wifi|cellular|poe|none",
 *   "has_connection": true|false,
 *   "current_comm_display_name": "WiFi",
 *   "available_comm_types": [{type, display_name, is_current, is_connected}],
 *   "current_comm_info": {status, ip_address, signal_strength, ...brief info}
 * }
 */
aicam_result_t network_status_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // 1. Service state (lightweight)
    service_state_t service_state = communication_service_get_state();
    const char* state_str = "unknown";
    switch (service_state) {
        case SERVICE_STATE_RUNNING: state_str = "running"; break;
        case SERVICE_STATE_INITIALIZED: state_str = "initialized"; break;
        case SERVICE_STATE_INITIALIZING: state_str = "initializing"; break;
        case SERVICE_STATE_ERROR: state_str = "error"; break;
        default: break;
    }
    cJSON_AddStringToObject(response_json, "service_state", state_str);
    cJSON_AddStringToObject(response_json, "service_version", communication_get_version());
    
    // 2. Communication types
    // active_type: Currently connected type (for actual data transmission)
    communication_type_t active_type = communication_get_current_type();
    cJSON_AddStringToObject(response_json, "active_type", network_comm_type_api_string(active_type));
    
    // selected_type: User selected type (for UI page display, may not be connected)
    communication_type_t selected_type = communication_get_selected_type();
    cJSON_AddStringToObject(response_json, "selected_type", network_comm_type_api_string(selected_type));
    
    // current_comm_type: Backward compatible alias for active_type
    cJSON_AddStringToObject(response_json, "current_comm_type", network_comm_type_api_string(active_type));
    
    // 3. Connection flag (based on active_type)
    aicam_bool_t has_connection = (active_type != COMM_TYPE_NONE);
    cJSON_AddBoolToObject(response_json, "has_connection", has_connection);
    
    // 4. Display name for status bar (based on active_type for connection status)
    const char* comm_display_name = "Not Connected";
    switch (active_type) {
        case COMM_TYPE_WIFI: comm_display_name = "WiFi"; break;
#if NETIF_WIFI_HALOW_IS_ENABLE
        case COMM_TYPE_HALOW: comm_display_name = "Wi-Fi HaLow"; break;
#endif
        case COMM_TYPE_CELLULAR: comm_display_name = "Cellular"; break;
        case COMM_TYPE_POE: comm_display_name = "PoE/Ethernet"; break;
        default: break;
    }
    cJSON_AddStringToObject(response_json, "active_display_name", comm_display_name);
    
    // 5. Selected type display name (for page navigation)
    const char* selected_display_name = "Not Selected";
    switch (selected_type) {
        case COMM_TYPE_WIFI: selected_display_name = "WiFi"; break;
#if NETIF_WIFI_HALOW_IS_ENABLE
        case COMM_TYPE_HALOW: selected_display_name = "Wi-Fi HaLow"; break;
#endif
        case COMM_TYPE_CELLULAR: selected_display_name = "Cellular"; break;
        case COMM_TYPE_POE: selected_display_name = "PoE/Ethernet"; break;
        default: break;
    }
    cJSON_AddStringToObject(response_json, "selected_display_name", selected_display_name);
    
    // Backward compatible
    cJSON_AddStringToObject(response_json, "current_comm_display_name", comm_display_name);
    
    // 6. Available types for switching dropdown (minimal info)
    cJSON* available_comm_types = cJSON_CreateArray();
    uint32_t available_count = 0;
    
    if (communication_is_type_available(COMM_TYPE_WIFI)) {
        cJSON* type_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(type_obj, "type", "wifi");
        cJSON_AddStringToObject(type_obj, "display_name", "WiFi");
        cJSON_AddBoolToObject(type_obj, "is_selected", selected_type == COMM_TYPE_WIFI);
        cJSON_AddBoolToObject(type_obj, "is_active", active_type == COMM_TYPE_WIFI);
        cJSON_AddBoolToObject(type_obj, "is_connected", communication_is_type_connected(COMM_TYPE_WIFI));
        // Backward compatible
        cJSON_AddBoolToObject(type_obj, "is_current", active_type == COMM_TYPE_WIFI);
        cJSON_AddItemToArray(available_comm_types, type_obj);
        available_count++;
    }
#if NETIF_WIFI_HALOW_IS_ENABLE
    if (communication_is_type_available(COMM_TYPE_HALOW)) {
        cJSON* type_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(type_obj, "type", "halow");
        cJSON_AddStringToObject(type_obj, "display_name", "Wi-Fi HaLow");
        cJSON_AddBoolToObject(type_obj, "is_selected", selected_type == COMM_TYPE_HALOW);
        cJSON_AddBoolToObject(type_obj, "is_active", active_type == COMM_TYPE_HALOW);
        cJSON_AddBoolToObject(type_obj, "is_connected", communication_is_type_connected(COMM_TYPE_HALOW));
        cJSON_AddBoolToObject(type_obj, "is_current", active_type == COMM_TYPE_HALOW);
        cJSON_AddItemToArray(available_comm_types, type_obj);
        available_count++;
    }
#endif
    if (communication_is_type_available(COMM_TYPE_CELLULAR)) {
        cJSON* type_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(type_obj, "type", "cellular");
        cJSON_AddStringToObject(type_obj, "display_name", "Cellular");
        cJSON_AddBoolToObject(type_obj, "is_selected", selected_type == COMM_TYPE_CELLULAR);
        cJSON_AddBoolToObject(type_obj, "is_active", active_type == COMM_TYPE_CELLULAR);
        cJSON_AddBoolToObject(type_obj, "is_connected", communication_is_type_connected(COMM_TYPE_CELLULAR));
        // Backward compatible
        cJSON_AddBoolToObject(type_obj, "is_current", active_type == COMM_TYPE_CELLULAR);
        cJSON_AddItemToArray(available_comm_types, type_obj);
        available_count++;
    }
    if (communication_is_type_available(COMM_TYPE_POE)) {
        cJSON* type_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(type_obj, "type", "poe");
        cJSON_AddStringToObject(type_obj, "display_name", "PoE/Ethernet");
        cJSON_AddBoolToObject(type_obj, "is_selected", selected_type == COMM_TYPE_POE);
        cJSON_AddBoolToObject(type_obj, "is_active", active_type == COMM_TYPE_POE);
        cJSON_AddBoolToObject(type_obj, "is_connected", communication_is_type_connected(COMM_TYPE_POE));
        // Backward compatible
        cJSON_AddBoolToObject(type_obj, "is_current", active_type == COMM_TYPE_POE);
        cJSON_AddItemToArray(available_comm_types, type_obj);
        available_count++;
    }
    cJSON_AddItemToObject(response_json, "available_comm_types", available_comm_types);
    cJSON_AddNumberToObject(response_json, "available_comm_count", available_count);

    /* Selection mode: manual (NVS preferred set) vs auto (priority-based at startup) */
    communication_type_t preferred_type = communication_get_preferred_type();
    aicam_bool_t auto_priority = communication_get_auto_priority();
    cJSON_AddStringToObject(response_json, "preferred_type", network_comm_type_api_string(preferred_type));
    cJSON_AddBoolToObject(response_json, "auto_priority", auto_priority);
    cJSON_AddBoolToObject(response_json, "is_auto_selection", preferred_type == COMM_TYPE_NONE);
    cJSON_AddStringToObject(response_json, "selection_mode",
                            preferred_type == COMM_TYPE_NONE ? "auto" : "manual");
    
    // 7. Current connection brief info (for status bar, based on active_type)
    cJSON* current_comm_info = cJSON_CreateObject();
    if (active_type == COMM_TYPE_NONE) {
        cJSON_AddStringToObject(current_comm_info, "status", "disconnected");
        cJSON_AddStringToObject(current_comm_info, "ip_address", "");
        cJSON_AddNumberToObject(current_comm_info, "signal_strength", 0);
        
        // Suggest type for quick connection
        communication_type_t suggested = communication_get_default_type();
        if (suggested != COMM_TYPE_NONE) {
            cJSON_AddStringToObject(current_comm_info, "suggested_type", communication_type_to_string(suggested));
        }
        
        // Hint message for user
        if (selected_type != COMM_TYPE_NONE) {
            cJSON_AddStringToObject(current_comm_info, "message", 
                "No active connection. Configure the selected type to connect.");
        } else {
            cJSON_AddStringToObject(current_comm_info, "message", 
                "No active connection. Select a communication type to configure.");
        }
    } else {
        communication_type_info_t type_info;
        if (communication_get_type_info(active_type, &type_info) == AICAM_OK) {
            cJSON_AddStringToObject(current_comm_info, "status", communication_status_to_string(type_info.status));
            cJSON_AddStringToObject(current_comm_info, "ip_address", type_info.ip_addr);
            cJSON_AddNumberToObject(current_comm_info, "signal_strength", type_info.signal_strength);
        }
        
        // Add ONE key identifier for status bar display
        if (active_type == COMM_TYPE_WIFI) {
            network_interface_status_t wifi_status;
            if (communication_get_interface_status(NETIF_NAME_WIFI_STA, &wifi_status) == AICAM_OK) {
                cJSON_AddStringToObject(current_comm_info, "ssid", wifi_status.ssid);
            }
#if NETIF_WIFI_HALOW_IS_ENABLE
        } else if (active_type == COMM_TYPE_HALOW) {
            network_interface_status_t hw_status;
            if (communication_get_interface_status(NETIF_NAME_WIFI_HALOW, &hw_status) == AICAM_OK) {
                cJSON_AddStringToObject(current_comm_info, "ssid", hw_status.ssid);
            }
#endif
        } else if (active_type == COMM_TYPE_CELLULAR) {
            cellular_detail_info_t cell_info;
            if (communication_cellular_get_detail_info(&cell_info) == AICAM_OK) {
                cJSON_AddStringToObject(current_comm_info, "isp", cell_info.isp);
            }
        }
    }
    cJSON_AddItemToObject(response_json, "current_comm_info", current_comm_info);
    
    // Serialize and respond
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "Network status retrieved successfully");
}

/**
 * @brief GET /api/v1/system/network/wifi/sta - WiFi STA (Client) Status
 * 
 * Returns WiFi client connection status and available networks.
 * Call this when entering WiFi management page.
 * 
 * Response:
 * {
 *   "connected": true,
 *   "ssid": "NetworkName",
 *   "rssi": -45,
 *   "ip_address": "192.168.1.100",
 *   "scan_results": {
 *     "known_networks": [...],
 *     "unknown_networks": [...]
 *   }
 * }
 */
aicam_result_t network_wifi_sta_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // WiFi STA interface status
    network_interface_status_t sta_status;
    aicam_result_t result = communication_get_interface_status(NETIF_NAME_WIFI_STA, &sta_status);
    
    if (result == AICAM_OK) {
        cJSON_AddBoolToObject(response_json, "connected", sta_status.connected);
        cJSON_AddStringToObject(response_json, "ssid", sta_status.ssid);
        cJSON_AddStringToObject(response_json, "bssid", sta_status.bssid);
        cJSON_AddNumberToObject(response_json, "rssi", sta_status.rssi);
        cJSON_AddNumberToObject(response_json, "channel", sta_status.channel);
        cJSON_AddStringToObject(response_json, "ip_address", sta_status.ip_addr);
        cJSON_AddStringToObject(response_json, "mac_address", sta_status.mac_addr);
        cJSON_AddStringToObject(response_json, "state", get_interface_state_string(sta_status.state));
    } else {
        cJSON_AddBoolToObject(response_json, "connected", AICAM_FALSE);
        cJSON_AddStringToObject(response_json, "ssid", "");
        cJSON_AddStringToObject(response_json, "ip_address", "");
    }
    
    // Scan results (known + unknown networks)
    classified_scan_results_t scan_results;
    result = communication_get_classified_scan_results(&scan_results);
    if (result == AICAM_OK) {
        // add current connected STA to known networks list, if exists, overwrite
        if (sta_status.connected) {
            network_scan_result_t current = {0};
            strncpy(current.ssid, sta_status.ssid, sizeof(current.ssid) - 1);
            strncpy(current.bssid, sta_status.bssid, sizeof(current.bssid) - 1);
            current.rssi               = sta_status.rssi;
            current.channel            = sta_status.channel;
            current.connected          = AICAM_TRUE;
            current.is_known           = AICAM_TRUE;
            current.last_connected_time = 0;

            netif_config_t sta_config;
            if (communication_get_interface_config(NETIF_NAME_WIFI_STA, &sta_config) == AICAM_OK) {
                current.security = sta_config.wireless_cfg.security;
            } else {
                current.security = WIRELESS_OPEN;
            }

            const uint32_t known_capacity =
              (uint32_t)(sizeof(scan_results.known_networks) / sizeof(scan_results.known_networks[0]));
            int32_t existing_idx = -1;
            for (uint32_t i = 0; i < scan_results.known_count; i++) {
                if (strcmp(scan_results.known_networks[i].ssid, current.ssid) == 0
                    && strcmp(scan_results.known_networks[i].bssid, current.bssid) == 0) {
                    existing_idx = (int32_t)i;
                    break;
                }
            }

            if (existing_idx >= 0) {
                scan_results.known_networks[existing_idx] = current;
            } else if (scan_results.known_count < known_capacity) {
                scan_results.known_networks[scan_results.known_count++] = current;
            } else {
                // if array is full, use latest connection to overwrite the last item
                scan_results.known_networks[known_capacity - 1] = current;
                scan_results.known_count                         = known_capacity;
            }
        }

        cJSON* scan_json = cJSON_CreateObject();
        
        // Known networks
        cJSON* known_array = cJSON_CreateArray();
        for (uint32_t i = 0; i < scan_results.known_count; i++) {
            cJSON* network_json = create_scan_result_json(&scan_results.known_networks[i]);
            if (network_json) {
                cJSON_AddItemToArray(known_array, network_json);
            }
        }
        cJSON_AddItemToObject(scan_json, "known_networks", known_array);
        cJSON_AddNumberToObject(scan_json, "known_count", scan_results.known_count);
        
        // Unknown networks
        cJSON* unknown_array = cJSON_CreateArray();
        for (uint32_t i = 0; i < scan_results.unknown_count; i++) {
            cJSON* network_json = create_scan_result_json(&scan_results.unknown_networks[i]);
            if (network_json) {
                cJSON_AddItemToArray(unknown_array, network_json);
            }
        }
        cJSON_AddItemToObject(scan_json, "unknown_networks", unknown_array);
        cJSON_AddNumberToObject(scan_json, "unknown_count", scan_results.unknown_count);
        
        cJSON_AddItemToObject(response_json, "scan_results", scan_json);
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "WiFi STA status retrieved successfully");
}

/**
 * @brief GET /api/v1/system/network/wifi/ap - WiFi AP (Hotspot) Config
 * 
 * Returns WiFi hotspot configuration.
 * Call this when entering hotspot settings page.
 * 
 * Response:
 * {
 *   "enabled": true,
 *   "ssid": "AICamera_AP",
 *   "password_set": true,
 *   "ap_sleep_time": 300,
 *   "ip_address": "192.168.4.1"
 * }
 */
aicam_result_t network_wifi_ap_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // WiFi AP interface status
    network_interface_status_t ap_status;
    aicam_result_t result = communication_get_interface_status(NETIF_NAME_WIFI_AP, &ap_status);
    
    if (result == AICAM_OK) {
        cJSON_AddBoolToObject(response_json, "enabled", ap_status.state == NETIF_STATE_UP);
        cJSON_AddStringToObject(response_json, "state", get_interface_state_string(ap_status.state));
        cJSON_AddStringToObject(response_json, "ip_address", ap_status.ip_addr);
        cJSON_AddStringToObject(response_json, "mac_address", ap_status.mac_addr);
        cJSON_AddNumberToObject(response_json, "channel", ap_status.channel);
    } else {
        cJSON_AddBoolToObject(response_json, "enabled", AICAM_FALSE);
    }
    
    // AP configuration from NVS
    network_service_config_t network_service_config;
    result = json_config_get_network_service_config(&network_service_config);
    if (result == AICAM_OK) {
        cJSON_AddStringToObject(response_json, "ssid", network_service_config.ssid);
        cJSON_AddStringToObject(response_json, "password", network_service_config.password);
        cJSON_AddNumberToObject(response_json, "ap_sleep_time", network_service_config.ap_sleep_time);
    }
    
    // Security type from config
    netif_config_t ap_config;
    result = communication_get_interface_config(NETIF_NAME_WIFI_AP, &ap_config);
    if (result == AICAM_OK) {
        cJSON_AddStringToObject(response_json, "security", get_security_type_string(ap_config.wireless_cfg.security));
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "WiFi AP config retrieved successfully");
}

/**
 * @brief POST /api/v1/system/network/wifi - Configure WiFi settings
 */
aicam_result_t network_wifi_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    // get network service configuration
    network_service_config_t network_service_config;
    aicam_result_t result = json_config_get_network_service_config(&network_service_config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }
    
    // Extract interface type (ap or sta)
    cJSON* interface_item = cJSON_GetObjectItem(request_json, "interface");
    if (!interface_item || !cJSON_IsString(interface_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'interface' field");
    }
    
    const char* interface_str = cJSON_GetStringValue(interface_item);
    const char* if_name = NULL;
    aicam_bool_t ssid_changed = AICAM_FALSE;
    aicam_bool_t password_changed = AICAM_FALSE;

    if (strcmp(interface_str, NETIF_NAME_WIFI_AP) == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(interface_str, NETIF_NAME_WIFI_STA) == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid interface type (use 'ap' or 'wl')");
    }
    
    // Extract SSID
    cJSON* ssid_item = cJSON_GetObjectItem(request_json, "ssid");
    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'ssid' field");
    }
    
    const char* ssid = cJSON_GetStringValue(ssid_item);
    if (strlen(ssid) == 0 || strlen(ssid) >= 32) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "SSID must be 1-31 characters");
    }
    if (strcmp(network_service_config.ssid, ssid) != 0) {
        ssid_changed = AICAM_TRUE;
        strncpy(network_service_config.ssid, ssid, sizeof(network_service_config.ssid) - 1);
        network_service_config.ssid[sizeof(network_service_config.ssid) - 1] = '\0';
    }
    
        
    // Extract password (optional)
    const char* password = "";
    cJSON* password_item = cJSON_GetObjectItem(request_json, "password");
    if (password_item && cJSON_IsString(password_item)) {
        password = cJSON_GetStringValue(password_item);
        if (strlen(password) > 0) {
            if (strlen(password) < 8 || strlen(password) >= 64) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Password must be 8-63 characters");
            }
        }
        if (strcmp(network_service_config.password, password) != 0) {
            password_changed = AICAM_TRUE;
            strncpy(network_service_config.password, password, sizeof(network_service_config.password) - 1);
            network_service_config.password[sizeof(network_service_config.password) - 1] = '\0';
        }
    }
    
    // Extract AP sleep time (optional, only for AP mode)
    uint32_t ap_sleep_time = 0;
    if (strcmp(interface_str, "ap") == 0) {
        cJSON* sleep_time_item = cJSON_GetObjectItem(request_json, "ap_sleep_time");
        if (sleep_time_item && cJSON_IsNumber(sleep_time_item)) {
            ap_sleep_time = (uint32_t)cJSON_GetNumberValue(sleep_time_item);
            if (ap_sleep_time > 3600) { // Max 1 hour
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "AP sleep time must be <= 3600 seconds");
            }
        }
        network_service_config.ap_sleep_time = ap_sleep_time;
        //update to web server
        result = web_server_ap_sleep_timer_update(ap_sleep_time);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set AP sleep time");
        }
    }

    // Extract bssid (optional)
    const char* bssid = "";
    cJSON* bssid_item = cJSON_GetObjectItem(request_json, "bssid");
    if (bssid_item && cJSON_IsString(bssid_item)) {
        bssid = cJSON_GetStringValue(bssid_item);
    }
    
    // Get current configuration first 
    netif_config_t config;
    result = communication_get_interface_config(if_name, &config);
    if (result != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current interface configuration");
    }
    
    // Update wireless configuration
    strncpy(config.wireless_cfg.ssid, ssid, sizeof(config.wireless_cfg.ssid) - 1);
    config.wireless_cfg.ssid[sizeof(config.wireless_cfg.ssid) - 1] = '\0';
    
    if (strlen(password) > 0) {
        strncpy(config.wireless_cfg.pw, password, sizeof(config.wireless_cfg.pw) - 1);
        config.wireless_cfg.pw[sizeof(config.wireless_cfg.pw) - 1] = '\0';
        config.wireless_cfg.security = WIRELESS_WPA_WPA2_MIXED;
    } else {
        memset(config.wireless_cfg.pw, 0, sizeof(config.wireless_cfg.pw));
        config.wireless_cfg.security = WIRELESS_OPEN;
    }

    if (strlen(bssid) > 0 && strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        unsigned int bssid_bytes[6];
        sscanf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
               &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
               &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]);
        for (int i = 0; i < 6; i++) {
            config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
        }
    }


    
    
    // Apply configuration through communication service layer
    if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0 && !ssid_changed && !password_changed) {
        LOG_SVC_INFO("AP mode and ssid not changed, skip configuration");
    }
    else {
        result = communication_configure_interface(if_name, &config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_BUSINESS_ERROR_NETWORK_TIMEOUT, "Failed to configure WiFi interface");
        }
    }

    // store config to json_config_mgr(only for AP mode)
    if (strcmp(interface_str, "ap") == 0) {
        result = json_config_set_network_service_config(&network_service_config);
        if (result != AICAM_OK) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set network service configuration");
        }
    }

    
    cJSON_Delete(request_json);
    
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to configure WiFi interface");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "WiFi configuration updated successfully");
    cJSON_AddStringToObject(response_json, "interface", interface_str);
    cJSON_AddStringToObject(response_json, "ssid", ssid);

    
    if (strcmp(interface_str, "ap") == 0 && ap_sleep_time > 0) {
        cJSON_AddNumberToObject(response_json, "ap_sleep_time", ap_sleep_time);
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "WiFi configuration updated successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/scan - Refresh network scan list
 */
aicam_result_t network_scan_refresh_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Create immediate response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // start scan
    start_network_scan();

    cJSON_AddStringToObject(response_json, "status", "scan_started");
    cJSON_AddStringToObject(response_json, "message", "Network scan started successfully in background task");

  
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Network scan refresh request processed");
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/disconnect - Disconnect WiFi interface
 */
aicam_result_t network_disconnect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract interface type (ap or sta)
    cJSON* interface_item = cJSON_GetObjectItem(request_json, "interface");
    if (!interface_item || !cJSON_IsString(interface_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'interface' field");
    }
    
    const char* interface_str = cJSON_GetStringValue(interface_item);
    const char* if_name = NULL;
    
    if (strcmp(interface_str, "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(interface_str, "sta") == 0 || strcmp(interface_str, "wl") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid interface type (use 'ap', 'sta', or 'wl')");
    }
    
    cJSON_Delete(request_json);
    
    // Stop the specified interface
    aicam_result_t result = communication_disconnect_network(if_name);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to disconnect WiFi interface");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "message", "WiFi interface disconnected successfully");
    cJSON_AddStringToObject(response_json, "interface", interface_str);
    cJSON_AddStringToObject(response_json, "if_name", if_name);
    cJSON_AddStringToObject(response_json, "status", "disconnected");
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "WiFi interface disconnected successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/delete - Delete known network
 */
aicam_result_t network_delete_known_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // Check if communication service is running
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    // Parse JSON request body
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract SSID
    cJSON* ssid_item = cJSON_GetObjectItem(request_json, "ssid");
    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'ssid' field");
    }
    
    const char* ssid = cJSON_GetStringValue(ssid_item);
    if (strlen(ssid) == 0 || strlen(ssid) >= 32) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "SSID must be 1-31 characters");
    }
    
    // Extract BSSID
    cJSON* bssid_item = cJSON_GetObjectItem(request_json, "bssid");
    if (!bssid_item || !cJSON_IsString(bssid_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'bssid' field");
    }
    
    const char* bssid = cJSON_GetStringValue(bssid_item);
    if (strlen(bssid) == 0) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "BSSID cannot be empty");
    }
    
    // Validate BSSID format (should be XX:XX:XX:XX:XX:XX)
    if (strlen(bssid) != 17) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "BSSID must be in format XX:XX:XX:XX:XX:XX");
    }
    
    // Basic BSSID format validation
    int valid_bssid = 1;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (bssid[i] != ':') {
                valid_bssid = 0;
                break;
            }
        } else {
            if (!((bssid[i] >= '0' && bssid[i] <= '9') || 
                  (bssid[i] >= 'A' && bssid[i] <= 'F') || 
                  (bssid[i] >= 'a' && bssid[i] <= 'f'))) {
                valid_bssid = 0;
                break;
            }
        }
    }
    
    if (!valid_bssid) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid BSSID format");
    }
    
    cJSON_Delete(request_json);
    
    // Delete the known network
    aicam_result_t result = communication_delete_known_network(ssid, bssid);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to delete known network");
    }
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddStringToObject(response_json, "status", "deleted");
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Known network deleted successfully");
    //hal_mem_free(json_string);
    
    return api_result;
}

#if NETIF_WIFI_HALOW_IS_ENABLE
/* ==================== HaLow APIs ==================== */

static void halow_region_to_cc(const char *region, char *out_cc, size_t out_len)
{
    size_t i;

    if (out_cc == NULL || out_len == 0U) {
        return;
    }
    strncpy(out_cc, NETIF_WIFI_HALOW_DEFAULT_COUNTRY, out_len - 1U);
    out_cc[out_len - 1U] = '\0';
    if (region == NULL || region[0] == '\0') {
        return;
    }
    for (i = 0; region[i] != '\0' && i < out_len - 1U; i++) {
        out_cc[i] = (char)toupper((unsigned char)region[i]);
    }
    out_cc[i] = '\0';
}

static int halow_country_code_same(const char *a, const char *b)
{
    size_t i;

    if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0') {
        return 0;
    }

    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) {
            return 0;
        }
    }
    return (a[i] == '\0' && b[i] == '\0');
}

static void halow_cc_to_api_region(const char *cc, char *out, size_t out_len)
{
    size_t i;

    if (out == NULL || out_len == 0U) {
        return;
    }
    out[0] = '\0';
    if (cc == NULL || cc[0] == '\0') {
        return;
    }
    for (i = 0; cc[i] != '\0' && i < out_len - 1U; i++) {
        out[i] = (char)tolower((unsigned char)cc[i]);
    }
    out[i] = '\0';
}

static void halow_fill_supported_regions(cJSON *arr)
{
    unsigned count;
    unsigned i;
    char cc[MM_HALOW_REGDOMAIN_CC_LEN];
    char region[MM_HALOW_REGDOMAIN_CC_LEN];

    if (arr == NULL) {
        return;
    }
    count = mm_halow_regdomain_count();
    for (i = 0; i < count; i++) {
        if (mm_halow_regdomain_get_code(i, cc, sizeof(cc)) != 0) {
            continue;
        }
        halow_cc_to_api_region(cc, region, sizeof(region));
        if (region[0] != '\0') {
            cJSON_AddItemToArray(arr, cJSON_CreateString(region));
        }
    }
}

static wireless_security_t halow_parse_security_string(const char *str, wireless_security_t default_sec)
{
    if (str == NULL || str[0] == '\0') {
        return default_sec;
    }
    if (strcmp(str, "open") == 0) {
        return WIRELESS_OPEN;
    }
    if (strcmp(str, "wep") == 0) {
        return WIRELESS_WEP;
    }
    if (strcmp(str, "wpa") == 0) {
        return WIRELESS_WPA;
    }
    if (strcmp(str, "wpa2") == 0 || strcmp(str, "wpa2_psk") == 0) {
        return WIRELESS_WPA2;
    }
    if (strcmp(str, "wpa_wpa2_mixed") == 0) {
        return WIRELESS_WPA_WPA2_MIXED;
    }
    if (strcmp(str, "wpa3") == 0 || strcmp(str, "wpa3_psk") == 0) {
        return WIRELESS_WPA3;
    }
    return default_sec;
}

static aicam_bool_t halow_scan_matches_saved(const network_service_config_t *sys_net,
                                              const char *scan_ssid,
                                              const char *scan_bssid)
{
    if (sys_net == NULL || scan_ssid == NULL || sys_net->halow_ssid[0] == '\0') {
        return AICAM_FALSE;
    }
    if (strcmp(scan_ssid, sys_net->halow_ssid) != 0) {
        return AICAM_FALSE;
    }
    if (sys_net->halow_bssid[0] != '\0' && scan_bssid != NULL && scan_bssid[0] != '\0' &&
        strcmp(scan_bssid, sys_net->halow_bssid) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t halow_scan_quick_connect_allowed(const network_service_config_t *sys_net,
                                                     const char *scan_ssid,
                                                     const char *scan_bssid,
                                                     wireless_security_t scan_sec)
{
    if (!halow_scan_matches_saved(sys_net, scan_ssid, scan_bssid)) {
        return AICAM_FALSE;
    }
    if (scan_sec == WIRELESS_OPEN || (wireless_security_t)sys_net->halow_security == WIRELESS_OPEN) {
        return AICAM_TRUE;
    }
    return (sys_net->halow_password[0] != '\0') ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_result_t halow_apply_region_to_netif(const char *region, network_service_config_t *sys_net)
{
    char cc[MM_HALOW_REGDOMAIN_CC_LEN];
    netif_config_t cfg;

    if (region == NULL || region[0] == '\0') {
        return AICAM_ERROR_INVALID_PARAM;
    }

    halow_region_to_cc(region, cc, sizeof(cc));
    if (!mm_halow_regdomain_is_supported(cc)) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    memset(&cfg, 0, sizeof(cfg));
    if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        return AICAM_ERROR;
    }

    if (halow_country_code_same(cfg.halow_cfg.country_code, cc)) {
        if (sys_net != NULL) {
            if (!halow_country_code_same(sys_net->halow_country_code, cc)) {
                strncpy(sys_net->halow_country_code, cc, sizeof(sys_net->halow_country_code) - 1U);
                sys_net->halow_country_code[sizeof(sys_net->halow_country_code) - 1U] = '\0';
                if (json_config_set_network_service_config(sys_net) != AICAM_OK) {
                    return AICAM_ERROR;
                }
            }
        }
        return AICAM_OK;
    }

    strncpy(cfg.halow_cfg.country_code, cc, sizeof(cfg.halow_cfg.country_code) - 1U);
    cfg.halow_cfg.country_code[sizeof(cfg.halow_cfg.country_code) - 1U] = '\0';
    if (nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        return AICAM_ERROR;
    }

    if (sys_net != NULL) {
        strncpy(sys_net->halow_country_code, cc, sizeof(sys_net->halow_country_code) - 1U);
        sys_net->halow_country_code[sizeof(sys_net->halow_country_code) - 1U] = '\0';
        if (json_config_set_network_service_config(sys_net) != AICAM_OK) {
            return AICAM_ERROR;
        }
    }

    return AICAM_OK;
}

/**
 * @brief GET /api/v1/system/network/halow/sta
 */
aicam_result_t network_halow_sta_handler(http_handler_context_t *ctx)
{
    network_service_config_t sys_net = {0};
    aicam_bool_t sys_net_ok;
    netif_info_t hw_info;
    wireless_scan_result_t *scan;
    cJSON *response_json;
    aicam_bool_t connected;
    const char *ssid_out;
    const char *region;
    char region_buf[MM_HALOW_REGDOMAIN_CC_LEN];
    char query_region[MM_HALOW_REGDOMAIN_CC_LEN];
    cJSON *scan_json;
    cJSON *known;
    cJSON *unknown;
    uint32_t unknown_count = 0;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    sys_net_ok = (json_config_get_network_service_config(&sys_net) == AICAM_OK) ? AICAM_TRUE : AICAM_FALSE;

    if (http_parse_query_param(ctx->request.query_string, "region", query_region, sizeof(query_region))) {
        if (query_region[0] != '\0') {
            if (halow_apply_region_to_netif(query_region, sys_net_ok ? &sys_net : NULL) == AICAM_OK) {
                sys_net_ok = AICAM_TRUE;
            }
        }
    }

    memset(&hw_info, 0, sizeof(hw_info));
    (void)nm_get_netif_info(NETIF_NAME_WIFI_HALOW, &hw_info);
    scan = nm_wireless_get_scan_result_ex(NETIF_NAME_WIFI_HALOW);

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    connected = (hw_info.state == NETIF_STATE_UP) ? AICAM_TRUE : AICAM_FALSE;
    cJSON_AddBoolToObject(response_json, "connected", connected);

    ssid_out = hw_info.wireless_cfg.ssid;
    if ((ssid_out == NULL || ssid_out[0] == '\0') && sys_net_ok && sys_net.halow_ssid[0] != '\0') {
        ssid_out = sys_net.halow_ssid;
    }
    cJSON_AddStringToObject(response_json, "ssid", ssid_out ? ssid_out : "");

    {
        char bssid_str[18] = {0};
        if (NETIF_MAC_IS_UNICAST(hw_info.wireless_cfg.bssid)) {
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     hw_info.wireless_cfg.bssid[0], hw_info.wireless_cfg.bssid[1], hw_info.wireless_cfg.bssid[2],
                     hw_info.wireless_cfg.bssid[3], hw_info.wireless_cfg.bssid[4], hw_info.wireless_cfg.bssid[5]);
        } else if (sys_net_ok && sys_net.halow_bssid[0] != '\0') {
            strncpy(bssid_str, sys_net.halow_bssid, sizeof(bssid_str) - 1U);
            bssid_str[sizeof(bssid_str) - 1U] = '\0';
        }
        cJSON_AddStringToObject(response_json, "bssid", bssid_str);
    }
    cJSON_AddNumberToObject(response_json, "rssi", hw_info.rssi);
    cJSON_AddNumberToObject(response_json, "channel", hw_info.wireless_cfg.channel);
    {
        char ip_str[16];
        char mask_str[16];
        char gw_str[16];
        char mac_str[18];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 hw_info.ip_addr[0], hw_info.ip_addr[1], hw_info.ip_addr[2], hw_info.ip_addr[3]);
        snprintf(mask_str, sizeof(mask_str), "%d.%d.%d.%d",
                 hw_info.netmask[0], hw_info.netmask[1], hw_info.netmask[2], hw_info.netmask[3]);
        snprintf(gw_str, sizeof(gw_str), "%d.%d.%d.%d",
                 hw_info.gw[0], hw_info.gw[1], hw_info.gw[2], hw_info.gw[3]);
        cJSON_AddStringToObject(response_json, "ip_address", ip_str);
        cJSON_AddStringToObject(response_json, "netmask", mask_str);
        cJSON_AddStringToObject(response_json, "gateway", gw_str);
        if (sys_net_ok) {
            cJSON_AddStringToObject(response_json, "ip_mode",
                                   (sys_net.halow_ip_mode == POE_IP_MODE_STATIC) ? "static" : "dhcp");
        } else {
            cJSON_AddStringToObject(response_json, "ip_mode",
                                   (hw_info.ip_mode == NETIF_IP_MODE_DHCP) ? "dhcp" : "static");
        }
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 hw_info.if_mac[0], hw_info.if_mac[1], hw_info.if_mac[2],
                 hw_info.if_mac[3], hw_info.if_mac[4], hw_info.if_mac[5]);
        cJSON_AddStringToObject(response_json, "mac_address", mac_str);
    }
    cJSON_AddStringToObject(response_json, "state", connected ? "connected" : "disconnected");

    region_buf[0] = '\0';
    if (sys_net_ok && sys_net.halow_country_code[0] != '\0') {
        halow_cc_to_api_region(sys_net.halow_country_code, region_buf, sizeof(region_buf));
    } else if (hw_info.halow_cfg.country_code[0] != '\0') {
        halow_cc_to_api_region(hw_info.halow_cfg.country_code, region_buf, sizeof(region_buf));
    }
    region = (region_buf[0] != '\0') ? region_buf : "cn";
    cJSON_AddStringToObject(response_json, "region", region);

    {
        cJSON *supported = cJSON_CreateArray();
        halow_fill_supported_regions(supported);
        cJSON_AddItemToObject(response_json, "supported_regions", supported);
    }

    scan_json = cJSON_CreateObject();
    known = cJSON_CreateArray();
    unknown = cJSON_CreateArray();

    if (sys_net_ok && sys_net.halow_ssid[0] != '\0') {
        network_scan_result_t known_entry = {0};
        strncpy(known_entry.ssid, sys_net.halow_ssid, sizeof(known_entry.ssid) - 1U);
        known_entry.security = (wireless_security_t)sys_net.halow_security;
        known_entry.is_known = AICAM_TRUE;
        known_entry.connected = connected && ssid_out && (strcmp(known_entry.ssid, ssid_out) == 0);
        if (known_entry.connected) {
            snprintf(known_entry.bssid, sizeof(known_entry.bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                     hw_info.wireless_cfg.bssid[0], hw_info.wireless_cfg.bssid[1], hw_info.wireless_cfg.bssid[2],
                     hw_info.wireless_cfg.bssid[3], hw_info.wireless_cfg.bssid[4], hw_info.wireless_cfg.bssid[5]);
            known_entry.rssi = hw_info.rssi;
            known_entry.channel = hw_info.wireless_cfg.channel;
        }
        {
            cJSON *known_json = create_scan_result_json(&known_entry);
            if (known_json) {
                cJSON_AddItemToArray(known, known_json);
            }
        }
    }

    if (scan && scan->scan_info && scan->scan_count > 0) {
        char cur_bssid[18] = {0};
        snprintf(cur_bssid, sizeof(cur_bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 hw_info.wireless_cfg.bssid[0], hw_info.wireless_cfg.bssid[1], hw_info.wireless_cfg.bssid[2],
                 hw_info.wireless_cfg.bssid[3], hw_info.wireless_cfg.bssid[4], hw_info.wireless_cfg.bssid[5]);
        for (uint8_t i = 0; i < scan->scan_count; i++) {
            const wireless_scan_info_t *si = &scan->scan_info[i];
            cJSON *n;
            char bssid_str[18];

            if (connected && ssid_out && ssid_out[0] != '\0' &&
                strncmp(si->ssid, ssid_out, sizeof(si->ssid)) == 0) {
                snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                         si->bssid[0], si->bssid[1], si->bssid[2],
                         si->bssid[3], si->bssid[4], si->bssid[5]);
                if (strncmp(cur_bssid, "00:00:00:00:00:00", sizeof(cur_bssid)) != 0 &&
                    strlen(cur_bssid) == 17 &&
                    strcmp(cur_bssid, bssid_str) != 0) {
                    /* same SSID, different AP */
                } else {
                    continue;
                }
            }

            n = cJSON_CreateObject();
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     si->bssid[0], si->bssid[1], si->bssid[2],
                     si->bssid[3], si->bssid[4], si->bssid[5]);
            cJSON_AddStringToObject(n, "ssid", si->ssid);
            cJSON_AddStringToObject(n, "bssid", bssid_str);
            cJSON_AddNumberToObject(n, "rssi", si->rssi);
            cJSON_AddNumberToObject(n, "channel", (int)si->channel);
            cJSON_AddStringToObject(n, "security", get_security_type_string((wireless_security_t)si->security));
            cJSON_AddBoolToObject(n, "connected", 0);
            if (sys_net_ok) {
                aicam_bool_t is_saved = halow_scan_matches_saved(&sys_net, si->ssid, bssid_str);
                aicam_bool_t quick_connect = halow_scan_quick_connect_allowed(
                    &sys_net, si->ssid, bssid_str, (wireless_security_t)si->security);
                cJSON_AddBoolToObject(n, "is_known", is_saved);
                cJSON_AddBoolToObject(n, "quick_connect", quick_connect);
            } else {
                cJSON_AddBoolToObject(n, "is_known", 0);
                cJSON_AddBoolToObject(n, "quick_connect", 0);
            }
            cJSON_AddNumberToObject(n, "last_connected_time", 0);
            cJSON_AddItemToArray(unknown, n);
            unknown_count++;
        }
    }

    cJSON_AddItemToObject(scan_json, "known_networks", known);
    cJSON_AddItemToObject(scan_json, "unknown_networks", unknown);
    cJSON_AddNumberToObject(scan_json, "known_count", (sys_net_ok && sys_net.halow_ssid[0] != '\0') ? 1 : 0);
    cJSON_AddNumberToObject(scan_json, "unknown_count", unknown_count);
    cJSON_AddBoolToObject(scan_json, "scan_in_progress", mm_halow_is_scan_in_progress() ? 1 : 0);
    cJSON_AddItemToObject(response_json, "scan_results", scan_json);
    cJSON_AddBoolToObject(response_json, "scan_in_progress", mm_halow_is_scan_in_progress() ? 1 : 0);

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow STA retrieved successfully");
}

/**
 * @brief GET /api/v1/system/network/halow/region
 */
aicam_result_t network_halow_region_get_handler(http_handler_context_t *ctx)
{
    network_service_config_t sys_net = {0};
    const char *region;
    char region_buf[MM_HALOW_REGDOMAIN_CC_LEN];
    cJSON *response_json;
    cJSON *supported;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    region_buf[0] = '\0';
    if (json_config_get_network_service_config(&sys_net) == AICAM_OK && sys_net.halow_country_code[0] != '\0') {
        halow_cc_to_api_region(sys_net.halow_country_code, region_buf, sizeof(region_buf));
    } else {
        netif_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        (void)nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg);
        halow_cc_to_api_region(cfg.halow_cfg.country_code, region_buf, sizeof(region_buf));
    }
    region = (region_buf[0] != '\0') ? region_buf : "cn";

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "region", region);
    supported = cJSON_CreateArray();
    halow_fill_supported_regions(supported);
    cJSON_AddItemToObject(response_json, "supported_regions", supported);

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow region retrieved");
}

/**
 * @brief PUT /api/v1/system/network/halow/region
 */
aicam_result_t network_halow_region_set_handler(http_handler_context_t *ctx)
{
    cJSON *request_json;
    const char *region;
    char region_buf[MM_HALOW_REGDOMAIN_CC_LEN];
    network_service_config_t sys_net = {0};
    cJSON *response_json;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "PUT")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only PUT method is allowed");
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    region = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "region"));
    if (region == NULL || region[0] == '\0') {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'region'");
    }
    strncpy(region_buf, region, sizeof(region_buf) - 1U);
    region_buf[sizeof(region_buf) - 1U] = '\0';
    region = region_buf;
    cJSON_Delete(request_json);

    if (json_config_get_network_service_config(&sys_net) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }
    if (halow_apply_region_to_netif(region, &sys_net) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid or unsupported region");
    }

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "region", region);
    cJSON_AddStringToObject(response_json, "message", "Region updated, rescan recommended");
    cJSON_AddBoolToObject(response_json, "scan_required", 1);

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow region updated");
}

/**
 * @brief POST /api/v1/system/network/halow/scan
 */
aicam_result_t network_halow_scan_handler(http_handler_context_t *ctx)
{
    cJSON *response_json;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    (void)nm_wireless_update_scan_result_ex(NETIF_NAME_WIFI_HALOW, 0);

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "status", "scan_started");
    cJSON_AddStringToObject(response_json, "message", "HaLow scan started in background");

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow scan started");
}

/**
 * @brief POST /api/v1/system/network/halow (connect)
 */
aicam_result_t network_halow_connect_handler(http_handler_context_t *ctx)
{
    cJSON *request_json;
    const char *ssid;
    const char *password;
    const char *region;
    const char *security_str;
    const char *bssid;
    wireless_security_t security;
    netif_config_t cfg;
    network_service_config_t sys_net = {0};
    communication_switch_result_t sw;
    cJSON *response_json;
    char *json_string;
    cJSON *use_saved_json;
    aicam_bool_t use_saved_password = AICAM_FALSE;
    const char *saved_password = "";

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    ssid = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "ssid"));
    password = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "password"));
    region = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "region"));
    security_str = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "security"));
    bssid = cJSON_GetStringValue(cJSON_GetObjectItem(request_json, "bssid"));
    use_saved_json = cJSON_GetObjectItem(request_json, "use_saved_password");
    use_saved_password = (use_saved_json != NULL && cJSON_IsTrue(use_saved_json)) ? AICAM_TRUE : AICAM_FALSE;

    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) >= 32) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'ssid'");
    }

    if (json_config_get_network_service_config(&sys_net) != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }

    if (use_saved_password) {
        if (!halow_scan_matches_saved(&sys_net, ssid, bssid)) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Saved credentials not available for this network");
        }
        saved_password = sys_net.halow_password;
        if (saved_password[0] == '\0' &&
            (wireless_security_t)sys_net.halow_security != WIRELESS_OPEN) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "No saved password for this network");
        }
        password = saved_password;
        security = (wireless_security_t)sys_net.halow_security;
    } else {
        if (password == NULL) {
            password = "";
        }
        if (password[0] != '\0' && (strlen(password) < 8 || strlen(password) >= 64)) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Password must be 8-63 characters");
        }
        security = halow_parse_security_string(security_str,
            (password[0] == '\0') ? WIRELESS_OPEN : WIRELESS_SAE);
    }

    memset(&cfg, 0, sizeof(cfg));
    if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get HaLow interface configuration");
    }

    strncpy(cfg.wireless_cfg.ssid, ssid, sizeof(cfg.wireless_cfg.ssid) - 1U);
    cfg.wireless_cfg.ssid[sizeof(cfg.wireless_cfg.ssid) - 1U] = '\0';
    strncpy(cfg.wireless_cfg.pw, password, sizeof(cfg.wireless_cfg.pw) - 1U);
    cfg.wireless_cfg.pw[sizeof(cfg.wireless_cfg.pw) - 1U] = '\0';
    cfg.wireless_cfg.security = security;

    if (bssid != NULL && strlen(bssid) == 17) {
        unsigned int bssid_bytes[6];
        if (sscanf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                   &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) == 6) {
            for (int i = 0; i < 6; i++) {
                cfg.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
            }
        }
    }

    if (region != NULL && region[0] != '\0') {
        char cc[MM_HALOW_REGDOMAIN_CC_LEN];
        halow_region_to_cc(region, cc, sizeof(cc));
        if (!mm_halow_regdomain_is_supported(cc)) {
            cJSON_Delete(request_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid or unsupported region");
        }
        strncpy(cfg.halow_cfg.country_code, cc, sizeof(cfg.halow_cfg.country_code) - 1U);
        cfg.halow_cfg.country_code[sizeof(cfg.halow_cfg.country_code) - 1U] = '\0';
        strncpy(sys_net.halow_country_code, cc, sizeof(sys_net.halow_country_code) - 1U);
        sys_net.halow_country_code[sizeof(sys_net.halow_country_code) - 1U] = '\0';
    }

    cfg.ip_mode = (sys_net.halow_ip_mode == POE_IP_MODE_STATIC) ?
                  NETIF_IP_MODE_STATIC : NETIF_IP_MODE_DHCP;
    memcpy(cfg.ip_addr, sys_net.halow_ip_addr, sizeof(cfg.ip_addr));
    memcpy(cfg.netmask, sys_net.halow_netmask, sizeof(cfg.netmask));
    memcpy(cfg.gw, sys_net.halow_gateway, sizeof(cfg.gw));

    if (nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to configure HaLow interface");
    }

    strncpy(sys_net.halow_ssid, cfg.wireless_cfg.ssid, sizeof(sys_net.halow_ssid) - 1U);
    strncpy(sys_net.halow_password, cfg.wireless_cfg.pw, sizeof(sys_net.halow_password) - 1U);
    sys_net.halow_security = (uint32_t)cfg.wireless_cfg.security;
    if (NETIF_MAC_IS_UNICAST(cfg.wireless_cfg.bssid)) {
        snprintf(sys_net.halow_bssid, sizeof(sys_net.halow_bssid),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 cfg.wireless_cfg.bssid[0], cfg.wireless_cfg.bssid[1],
                 cfg.wireless_cfg.bssid[2], cfg.wireless_cfg.bssid[3],
                 cfg.wireless_cfg.bssid[4], cfg.wireless_cfg.bssid[5]);
    } else {
        sys_net.halow_bssid[0] = '\0';
    }
    if (json_config_set_network_service_config(&sys_net) != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save network service configuration");
    }

    cJSON_Delete(request_json);

    memset(&sw, 0, sizeof(sw));
    if (communication_switch_type_sync(COMM_TYPE_HALOW, &sw, 30000, AICAM_FALSE) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to switch to HaLow");
    }

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "message", "HaLow connected successfully");
    cJSON_AddStringToObject(response_json, "interface", "halow");
    cJSON_AddStringToObject(response_json, "ssid", cfg.wireless_cfg.ssid);
    if (bssid != NULL) {
        cJSON_AddStringToObject(response_json, "bssid", bssid);
    }

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow connect requested");
}

/**
 * @brief POST /api/v1/system/network/halow/disconnect
 */
aicam_result_t network_halow_disconnect_handler(http_handler_context_t *ctx)
{
    cJSON *response_json;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    (void)nm_ctrl_netif_down(NETIF_NAME_WIFI_HALOW);

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "message", "HaLow disconnected successfully");
    cJSON_AddStringToObject(response_json, "interface", "halow");
    cJSON_AddStringToObject(response_json, "status", "disconnected");

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow disconnected");
}

/**
 * @brief POST /api/v1/system/network/halow/delete
 */
aicam_result_t network_halow_delete_handler(http_handler_context_t *ctx)
{
    network_service_config_t sys_net = {0};
    netif_config_t cfg;
    cJSON *response_json;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (json_config_get_network_service_config(&sys_net) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }

    (void)nm_ctrl_netif_down(NETIF_NAME_WIFI_HALOW);

    memset(&cfg, 0, sizeof(cfg));
    if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) == 0) {
        memset(cfg.wireless_cfg.ssid, 0, sizeof(cfg.wireless_cfg.ssid));
        memset(cfg.wireless_cfg.pw, 0, sizeof(cfg.wireless_cfg.pw));
        memset(cfg.wireless_cfg.bssid, 0, sizeof(cfg.wireless_cfg.bssid));
        cfg.wireless_cfg.security = WIRELESS_OPEN;
        (void)nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg);
    }

    sys_net.halow_ssid[0] = '\0';
    sys_net.halow_password[0] = '\0';
    sys_net.halow_security = (uint32_t)WIRELESS_OPEN;
    sys_net.halow_bssid[0] = '\0';

    if (json_config_set_network_service_config(&sys_net) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save network service configuration");
    }

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    cJSON_AddStringToObject(response_json, "status", "deleted");
    cJSON_AddStringToObject(response_json, "message", "HaLow network cleared");

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow cleared");
}

static void halow_copy_ip_service_to_cfg(const network_service_config_t *sys, netif_config_t *cfg)
{
    cfg->ip_mode = (sys->halow_ip_mode == POE_IP_MODE_STATIC) ?
                   NETIF_IP_MODE_STATIC : NETIF_IP_MODE_DHCP;
    memcpy(cfg->ip_addr, sys->halow_ip_addr, sizeof(cfg->ip_addr));
    memcpy(cfg->netmask, sys->halow_netmask, sizeof(cfg->netmask));
    memcpy(cfg->gw, sys->halow_gateway, sizeof(cfg->gw));
}

static void halow_fill_ip_json(cJSON *obj, netif_ip_mode_t mode,
                               const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4])
{
    char buf[16];

    cJSON_AddStringToObject(obj, "ip_mode",
                            (mode == NETIF_IP_MODE_DHCP) ? "dhcp" : "static");
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    cJSON_AddStringToObject(obj, "ip_address", buf);
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);
    cJSON_AddStringToObject(obj, "netmask", buf);
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
    cJSON_AddStringToObject(obj, "gateway", buf);
}

static int halow_parse_ipv4_item(cJSON *root, const char *key, uint8_t out[4])
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    const char *str;
    unsigned int a, b, c, d;

    if (item == NULL || !cJSON_IsString(item)) {
        return -1;
    }
    str = cJSON_GetStringValue(item);
    if (str == NULL || sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return -1;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return 0;
}

/**
 * @brief GET/POST /api/v1/system/network/halow/ip
 */
aicam_result_t network_halow_ip_handler(http_handler_context_t *ctx)
{
    netif_config_t cfg;
    netif_info_t hw_info;
    network_service_config_t sys_net = {0};
    cJSON *response_json;
    char *json_string;
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    netif_ip_mode_t cfg_mode;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    if (json_config_get_network_service_config(&sys_net) != AICAM_OK) {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }

    cfg_mode = (sys_net.halow_ip_mode == POE_IP_MODE_STATIC) ?
               NETIF_IP_MODE_STATIC : NETIF_IP_MODE_DHCP;
    memcpy(ip, sys_net.halow_ip_addr, sizeof(ip));
    memcpy(mask, sys_net.halow_netmask, sizeof(mask));
    memcpy(gw, sys_net.halow_gateway, sizeof(gw));

    if (strcmp(ctx->request.method, "GET") == 0) {
        memset(&hw_info, 0, sizeof(hw_info));
        if (nm_get_netif_info(NETIF_NAME_WIFI_HALOW, &hw_info) == AICAM_OK &&
            hw_info.state == NETIF_STATE_UP &&
            cfg_mode == NETIF_IP_MODE_DHCP) {
            memcpy(ip, hw_info.ip_addr, sizeof(ip));
            memcpy(mask, hw_info.netmask, sizeof(mask));
            memcpy(gw, hw_info.gw, sizeof(gw));
        }

        halow_fill_ip_json(response_json, cfg_mode, ip, mask, gw);
    } else if (strcmp(ctx->request.method, "POST") == 0) {
        cJSON *request_json;
        cJSON *ip_mode_item;
        aicam_bool_t apply_live = AICAM_FALSE;

        request_json = web_api_parse_body(ctx);
        if (!request_json) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }

        ip_mode_item = cJSON_GetObjectItem(request_json, "ip_mode");
        if (ip_mode_item && cJSON_IsString(ip_mode_item)) {
            const char *mode_str = cJSON_GetStringValue(ip_mode_item);
            if (mode_str != NULL && strcmp(mode_str, "dhcp") == 0) {
                sys_net.halow_ip_mode = POE_IP_MODE_DHCP;
                cfg_mode = NETIF_IP_MODE_DHCP;
            } else if (mode_str != NULL && strcmp(mode_str, "static") == 0) {
                sys_net.halow_ip_mode = POE_IP_MODE_STATIC;
                cfg_mode = NETIF_IP_MODE_STATIC;
                if (halow_parse_ipv4_item(request_json, "ip_address", sys_net.halow_ip_addr) != 0 ||
                    halow_parse_ipv4_item(request_json, "netmask", sys_net.halow_netmask) != 0 ||
                    halow_parse_ipv4_item(request_json, "gateway", sys_net.halow_gateway) != 0) {
                    cJSON_Delete(request_json);
                    cJSON_Delete(response_json);
                    return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                              "Static mode requires valid ip_address, netmask and gateway");
                }
                memcpy(ip, sys_net.halow_ip_addr, sizeof(ip));
                memcpy(mask, sys_net.halow_netmask, sizeof(mask));
                memcpy(gw, sys_net.halow_gateway, sizeof(gw));
            } else {
                cJSON_Delete(request_json);
                cJSON_Delete(response_json);
                return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "ip_mode must be dhcp or static");
            }
        }

        if (json_config_set_network_service_config(&sys_net) != AICAM_OK) {
            cJSON_Delete(request_json);
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save HaLow IP configuration");
        }

        if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
            cJSON_Delete(request_json);
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get HaLow interface configuration");
        }

        halow_copy_ip_service_to_cfg(&sys_net, &cfg);
        if (nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
            cJSON_Delete(request_json);
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply HaLow IP configuration");
        }

        memset(&hw_info, 0, sizeof(hw_info));
        if (nm_get_netif_info(NETIF_NAME_WIFI_HALOW, &hw_info) == AICAM_OK &&
            hw_info.state == NETIF_STATE_UP) {
            apply_live = AICAM_TRUE;
        }
        cJSON_Delete(request_json);

        /*
         * When disconnected but mmipal is still alive from a prior session, push the
         * new IP into the stack now so the next UP does not reuse stale settings.
         */
        if (mm_halow_apply_ip_config() != 0 && apply_live) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply HaLow IP configuration");
        }

        if (cfg_mode == NETIF_IP_MODE_DHCP && apply_live) {
            memcpy(ip, hw_info.ip_addr, sizeof(ip));
            memcpy(mask, hw_info.netmask, sizeof(mask));
            memcpy(gw, hw_info.gw, sizeof(gw));
        }

        halow_fill_ip_json(response_json, cfg_mode, ip, mask, gw);
        cJSON_AddStringToObject(response_json, "message", "HaLow IP configuration updated");
    } else {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET or POST method is allowed");
    }

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow IP configuration");
}

static void halow_fill_radio_limits_json(cJSON *limits, const char *country_code)
{
    cJSON *tx_limits;
    cJSON *scan_limits;
    int tx_max;

    if (limits == NULL) {
        return;
    }

    tx_limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(tx_limits, "min", 0);
    tx_max = mm_halow_get_regdomain_max_tx_dbm(country_code);
    cJSON_AddNumberToObject(tx_limits, "max", (tx_max > 0) ? tx_max : 30);
    cJSON_AddItemToObject(limits, "tx_power_dbm", tx_limits);

    scan_limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(scan_limits, "min", MMWLAN_SCAN_MIN_DWELL_TIME_MS);
    cJSON_AddNumberToObject(scan_limits, "max", NETIF_WIFI_HALOW_MAX_SCAN_DWELL);
    cJSON_AddNumberToObject(scan_limits, "default", MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS);
    cJSON_AddItemToObject(limits, "scan_dwell_ms", scan_limits);
}

static void halow_fill_radio_json(cJSON *obj, const network_service_config_t *sys_net)
{
    netif_config_t cfg;
    uint16_t tx_pwr = sys_net->halow_tx_power_dbm;
    uint32_t dwell = sys_net->halow_scan_dwell_ms;
    int32_t mcs = sys_net->halow_rc_mcs;
    int32_t bw = sys_net->halow_rc_bw_mhz;
    int32_t gi = sys_net->halow_rc_gi;
    const char *country_code = sys_net->halow_country_code;
    cJSON *limits;

    if (obj == NULL || sys_net == NULL) {
        return;
    }

    if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) == 0) {
        tx_pwr = cfg.halow_cfg.tx_power_dbm;
        dwell = cfg.halow_cfg.scan_dwell_ms;
        mcs = (int32_t)cfg.halow_cfg.rc_mcs;
        bw = (int32_t)cfg.halow_cfg.rc_bw_mhz;
        gi = (int32_t)cfg.halow_cfg.rc_gi;
        if (cfg.halow_cfg.country_code[0] != '\0') {
            country_code = cfg.halow_cfg.country_code;
        }
    }

    cJSON_AddNumberToObject(obj, "tx_power_dbm", tx_pwr);
    cJSON_AddNumberToObject(obj, "scan_dwell_ms", dwell);
    cJSON_AddNumberToObject(obj, "rate_mcs", mcs);
    cJSON_AddNumberToObject(obj, "rate_bw_mhz", bw);
    cJSON_AddNumberToObject(obj, "rate_gi", gi);

    limits = cJSON_CreateObject();
    halow_fill_radio_limits_json(limits, country_code);
    cJSON_AddItemToObject(obj, "limits", limits);
}

static aicam_result_t halow_validate_radio_fields(uint16_t tx_power_dbm, uint32_t scan_dwell_ms,
                                                  int32_t rate_mcs, int32_t rate_bw_mhz, int32_t rate_gi,
                                                  const char *country_code)
{
    int tx_max = mm_halow_get_regdomain_max_tx_dbm(country_code);

    if (tx_max <= 0) {
        tx_max = 30;
    }
    if (tx_power_dbm > (uint16_t)tx_max) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (scan_dwell_ms < MMWLAN_SCAN_MIN_DWELL_TIME_MS ||
        scan_dwell_ms > NETIF_WIFI_HALOW_MAX_SCAN_DWELL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (rate_mcs < -1 || rate_mcs > 9) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (rate_bw_mhz != -1 && rate_bw_mhz != 1 && rate_bw_mhz != 2 &&
        rate_bw_mhz != 4 && rate_bw_mhz != 8) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (rate_gi < -1 || rate_gi > 1) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    return AICAM_OK;
}

static aicam_result_t halow_apply_radio_to_netif(network_service_config_t *sys_net)
{
    netif_config_t cfg;

    if (sys_net == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (nm_get_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        return AICAM_ERROR;
    }

    cfg.halow_cfg.tx_power_dbm = sys_net->halow_tx_power_dbm;
    cfg.halow_cfg.scan_dwell_ms = sys_net->halow_scan_dwell_ms;
    cfg.halow_cfg.rc_mcs = (int8_t)sys_net->halow_rc_mcs;
    cfg.halow_cfg.rc_bw_mhz = (int8_t)sys_net->halow_rc_bw_mhz;
    cfg.halow_cfg.rc_gi = (int8_t)sys_net->halow_rc_gi;

    if (nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg) != 0) {
        return AICAM_ERROR;
    }

    /*
     * Push radio settings into the running stack immediately. nm_set_netif_cfg()
     * may down/up when connected; these calls match the dedicated CLI paths
     * (ifconfig hw txpwr / rate / scan_cfg) and avoid stale runtime state.
     */
    (void)mm_halow_set_tx_power(sys_net->halow_tx_power_dbm);
    (void)mm_halow_set_rate_override((int8_t)sys_net->halow_rc_mcs,
                                     (int8_t)sys_net->halow_rc_bw_mhz,
                                     (int8_t)sys_net->halow_rc_gi);
    (void)mm_halow_set_scan_config(sys_net->halow_scan_dwell_ms, cfg.halow_cfg.ndp_probe_enabled);

    if (json_config_set_network_service_config(sys_net) != AICAM_OK) {
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief GET/PUT /api/v1/system/network/halow/radio
 */
aicam_result_t network_halow_radio_handler(http_handler_context_t *ctx)
{
    network_service_config_t sys_net = {0};
    cJSON *response_json;
    char *json_string;

    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    if (json_config_get_network_service_config(&sys_net) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get network service configuration");
    }

    response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }

    if (strcmp(ctx->request.method, "GET") == 0) {
        halow_fill_radio_json(response_json, &sys_net);
    } else if (strcmp(ctx->request.method, "PUT") == 0) {
        cJSON *request_json;
        cJSON *item;
        uint16_t tx_power_dbm = sys_net.halow_tx_power_dbm;
        uint32_t scan_dwell_ms = sys_net.halow_scan_dwell_ms;
        int32_t rate_mcs = sys_net.halow_rc_mcs;
        int32_t rate_bw_mhz = sys_net.halow_rc_bw_mhz;
        int32_t rate_gi = sys_net.halow_rc_gi;

        request_json = web_api_parse_body(ctx);
        if (!request_json) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }

        item = cJSON_GetObjectItem(request_json, "tx_power_dbm");
        if (item != NULL && cJSON_IsNumber(item)) {
            tx_power_dbm = (uint16_t)item->valueint;
        }
        item = cJSON_GetObjectItem(request_json, "scan_dwell_ms");
        if (item != NULL && cJSON_IsNumber(item)) {
            scan_dwell_ms = (uint32_t)item->valueint;
        }
        item = cJSON_GetObjectItem(request_json, "rate_mcs");
        if (item != NULL && cJSON_IsNumber(item)) {
            rate_mcs = (int32_t)item->valueint;
        }
        item = cJSON_GetObjectItem(request_json, "rate_bw_mhz");
        if (item != NULL && cJSON_IsNumber(item)) {
            rate_bw_mhz = (int32_t)item->valueint;
        }
        item = cJSON_GetObjectItem(request_json, "rate_gi");
        if (item != NULL && cJSON_IsNumber(item)) {
            rate_gi = (int32_t)item->valueint;
        }
        cJSON_Delete(request_json);

        if (halow_validate_radio_fields(tx_power_dbm, scan_dwell_ms, rate_mcs, rate_bw_mhz, rate_gi,
                                        sys_net.halow_country_code) != AICAM_OK) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid HaLow radio parameters");
        }

        sys_net.halow_tx_power_dbm = tx_power_dbm;
        sys_net.halow_scan_dwell_ms = scan_dwell_ms;
        sys_net.halow_rc_mcs = rate_mcs;
        sys_net.halow_rc_bw_mhz = rate_bw_mhz;
        sys_net.halow_rc_gi = rate_gi;

        if (halow_apply_radio_to_netif(&sys_net) != AICAM_OK) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply HaLow radio configuration");
        }

        halow_fill_radio_json(response_json, &sys_net);
        cJSON_AddStringToObject(response_json, "message", "HaLow radio configuration updated");
    } else {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET or PUT method is allowed");
    }

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_string, "HaLow radio configuration");
}
#endif /* NETIF_WIFI_HALOW_IS_ENABLE */

/* ==================== Communication Type APIs ==================== */

/**
 * @brief GET /api/v1/system/network/comm/types - Get all communication types
 * 
 * Response format for frontend:
 * {
 *   "current_type": "wifi",           // Current active communication type
 *   "current_type_info": {...},       // Detailed info of current type
 *   "preferred_type": "none",         // User preferred type
 *   "auto_priority": true,            // Auto priority enabled
 *   "available_types": ["wifi", "cellular"],  // Types available for switching
 *   "types": [                        // All types with detailed info
 *     {
 *       "type": "wifi",
 *       "status": "connected",
 *       "available": true,
 *       "connected": true,
 *       "can_switch": true,           // Can switch to this type
 *       "ip_address": "192.168.1.100",
 *       "signal_strength": -50,
 *       "priority": 1,
 *       "display_name": "WiFi",       // Display name for UI
 *       "detail": {...}               // Type-specific details
 *     }
 *   ]
 * }
 */
aicam_result_t network_comm_types_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Get current communication type
    communication_type_t current_type = communication_get_current_type();
    cJSON_AddStringToObject(response_json, "current_type", network_comm_type_api_string(current_type));
    
    // Get preferred type
    communication_type_t preferred_type = communication_get_preferred_type();
    cJSON_AddStringToObject(response_json, "preferred_type", network_comm_type_api_string(preferred_type));
    
    // Get auto priority setting
    aicam_bool_t auto_priority = communication_get_auto_priority();
    cJSON_AddBoolToObject(response_json, "auto_priority", auto_priority);
    
    // Get all types info
    communication_type_info_t types[COMM_TYPE_MAX];
    uint32_t count = 0;
    aicam_result_t result = communication_get_all_types(types, COMM_TYPE_MAX, &count);
    
    // Create available types array for easy frontend switching
    cJSON* available_array = cJSON_CreateArray();
    
    if (result == AICAM_OK) {
        cJSON* types_array = cJSON_CreateArray();
        if (types_array) {
            for (uint32_t i = 0; i < count; i++) {
                cJSON* type_json = cJSON_CreateObject();
                if (type_json) {
                    communication_type_t t = types[i].type;
                    aicam_bool_t is_connected = (types[i].status == COMM_STATUS_CONNECTED);
                    aicam_bool_t can_switch = types[i].available && (t != current_type);
                    
                    cJSON_AddStringToObject(type_json, "type", network_comm_type_api_string(t));
                    cJSON_AddStringToObject(type_json, "status", communication_status_to_string(types[i].status));
                    cJSON_AddBoolToObject(type_json, "available", types[i].available);
                    cJSON_AddBoolToObject(type_json, "connected", is_connected);
                    cJSON_AddBoolToObject(type_json, "can_switch", can_switch);
                    cJSON_AddBoolToObject(type_json, "is_current", t == current_type);
                    cJSON_AddStringToObject(type_json, "ip_address", types[i].ip_addr);
                    cJSON_AddNumberToObject(type_json, "signal_strength", types[i].signal_strength);
                    cJSON_AddNumberToObject(type_json, "priority", types[i].priority);
                    
                    // Add display name for UI
                    const char* display_name = "Unknown";
                    switch (t) {
                        case COMM_TYPE_WIFI: display_name = "WiFi"; break;
#if NETIF_WIFI_HALOW_IS_ENABLE
                        case COMM_TYPE_HALOW: display_name = "Wi-Fi HaLow"; break;
#endif
                        case COMM_TYPE_CELLULAR: display_name = "Cellular"; break;
                        case COMM_TYPE_POE: display_name = "PoE/Ethernet"; break;
                        default: break;
                    }
                    cJSON_AddStringToObject(type_json, "display_name", display_name);
                    
                    // Add type-specific details
                    cJSON* detail_json = cJSON_CreateObject();
                    if (detail_json) {
                        if (t == COMM_TYPE_CELLULAR && communication_cellular_is_available()) {
                            // Cellular details
                            char imei[32] = {0};
                            communication_cellular_get_imei(imei);
                            cJSON_AddStringToObject(detail_json, "imei", imei);
                            
                            cellular_connection_settings_t settings;
                            if (communication_cellular_get_settings(&settings) == AICAM_OK) {
                                cJSON_AddStringToObject(detail_json, "apn", settings.apn);
                            }
                            
                            cellular_detail_info_t cell_info;
                            if (communication_cellular_get_detail_info(&cell_info) == AICAM_OK) {
                                cJSON_AddStringToObject(detail_json, "isp", cell_info.isp);
                                cJSON_AddStringToObject(detail_json, "network_type", cell_info.network_type);
                                cJSON_AddNumberToObject(detail_json, "csq", cell_info.csq);
                            }
                        } else if (t == COMM_TYPE_WIFI) {
                            // WiFi details - get from interface status
                            network_interface_status_t if_status;
                            if (communication_get_interface_status(NETIF_NAME_WIFI_STA, &if_status) == AICAM_OK) {
                                cJSON_AddStringToObject(detail_json, "ssid", if_status.ssid);
                                cJSON_AddNumberToObject(detail_json, "rssi", if_status.rssi);
                                cJSON_AddNumberToObject(detail_json, "channel", if_status.channel);
                            }
#if NETIF_WIFI_HALOW_IS_ENABLE
                        } else if (t == COMM_TYPE_HALOW) {
                            network_interface_status_t if_status;
                            if (communication_get_interface_status(NETIF_NAME_WIFI_HALOW, &if_status) == AICAM_OK) {
                                cJSON_AddStringToObject(detail_json, "ssid", if_status.ssid);
                                cJSON_AddNumberToObject(detail_json, "rssi", if_status.rssi);
                                cJSON_AddStringToObject(detail_json, "interface", NETIF_NAME_WIFI_HALOW);
                            }
#endif
                        } else if (t == COMM_TYPE_POE) {
                            // PoE details
                            cJSON_AddStringToObject(detail_json, "interface", NETIF_NAME_ETH_WAN);
                        }
                        cJSON_AddItemToObject(type_json, "detail", detail_json);
                    }
                    
                    cJSON_AddItemToArray(types_array, type_json);
                    
                    // Add to available types if can switch
                    if (types[i].available) {
                        cJSON_AddItemToArray(available_array, cJSON_CreateString(network_comm_type_api_string(t)));
                    }
                }
            }
            cJSON_AddItemToObject(response_json, "types", types_array);
        }
    }
    
    cJSON_AddItemToObject(response_json, "available_types", available_array);
    cJSON_AddNumberToObject(response_json, "type_count", count);
    
    // Add current type detailed info for quick access
    cJSON* current_info = cJSON_CreateObject();
    if (current_info) {
        communication_type_info_t current_type_info;
        if (communication_get_type_info(current_type, &current_type_info) == AICAM_OK) {
            cJSON_AddStringToObject(current_info, "type", network_comm_type_api_string(current_type));
            cJSON_AddStringToObject(current_info, "status", communication_status_to_string(current_type_info.status));
            cJSON_AddStringToObject(current_info, "ip_address", current_type_info.ip_addr);
            cJSON_AddNumberToObject(current_info, "signal_strength", current_type_info.signal_strength);
            
            // Add type-specific current info
            if (current_type == COMM_TYPE_CELLULAR && communication_cellular_is_available()) {
                cellular_detail_info_t cell_info;
                if (communication_cellular_get_detail_info(&cell_info) == AICAM_OK) {
                    cJSON_AddStringToObject(current_info, "isp", cell_info.isp);
                    cJSON_AddStringToObject(current_info, "network_type", cell_info.network_type);
                    cJSON_AddNumberToObject(current_info, "csq", cell_info.csq);
                    cJSON_AddNumberToObject(current_info, "signal_level", cell_info.signal_level);
                }
            } else if (current_type == COMM_TYPE_WIFI) {
                network_interface_status_t if_status;
                if (communication_get_interface_status(NETIF_NAME_WIFI_STA, &if_status) == AICAM_OK) {
                    cJSON_AddStringToObject(current_info, "ssid", if_status.ssid);
                    cJSON_AddNumberToObject(current_info, "rssi", if_status.rssi);
                }
#if NETIF_WIFI_HALOW_IS_ENABLE
            } else if (current_type == COMM_TYPE_HALOW) {
                network_interface_status_t if_status;
                if (communication_get_interface_status(NETIF_NAME_WIFI_HALOW, &if_status) == AICAM_OK) {
                    cJSON_AddStringToObject(current_info, "ssid", if_status.ssid);
                    cJSON_AddNumberToObject(current_info, "rssi", if_status.rssi);
                }
#endif
            }
        }
        cJSON_AddItemToObject(response_json, "current_type_info", current_info);
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Communication types retrieved successfully");
    return api_result;
}

/**
 * @brief POST /api/v1/system/network/comm/switch - Switch communication type
 */
aicam_result_t network_comm_switch_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract target type
    cJSON* type_item = cJSON_GetObjectItem(request_json, "type");
    if (!type_item || !cJSON_IsString(type_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'type' field");
    }
    
    const char* type_str = cJSON_GetStringValue(type_item);
    communication_type_t target_type = network_comm_type_from_string(type_str);
    
    if (target_type == COMM_TYPE_NONE || target_type >= COMM_TYPE_MAX) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid communication type (use wifi, halow, cellular/4g, or poe/ethernet)");
    }
    
    // Optional timeout
    uint32_t timeout_ms = 30000;
    cJSON* timeout_item = cJSON_GetObjectItem(request_json, "timeout_ms");
    if (timeout_item && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)cJSON_GetNumberValue(timeout_item);
    }
    
    cJSON_Delete(request_json);
    
    // Check if type is available
    if (!communication_is_type_available(target_type)) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Communication type is not available");
    }
    
    // Perform synchronous switch (HaLow: scan for saved SSID before attempting UP)
    communication_switch_result_t switch_result = {0};
    aicam_bool_t scan_before_connect = AICAM_FALSE;
#if NETIF_WIFI_HALOW_IS_ENABLE
    if (target_type == COMM_TYPE_HALOW) {
        scan_before_connect = AICAM_TRUE;
    }
#endif
    communication_switch_session_begin();
    aicam_result_t result = communication_switch_type_sync(target_type, &switch_result, timeout_ms,
                                                           scan_before_connect);
    communication_switch_session_end();
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", switch_result.success);
    cJSON_AddStringToObject(response_json, "from_type", network_comm_type_api_string(switch_result.from_type));
    cJSON_AddStringToObject(response_json, "to_type", network_comm_type_api_string(switch_result.to_type));
    cJSON_AddNumberToObject(response_json, "switch_time_ms", switch_result.switch_time_ms);
    
    if (!switch_result.success) {
        cJSON_AddStringToObject(response_json, "error", switch_result.error_message);
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK && switch_result.success) {
        return api_response_success(ctx, json_string, "Communication type switched successfully");
    } else {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, switch_result.error_message);
    }
}

/**
 * @brief POST /api/v1/system/network/comm/prefer - Set preferred communication type
 */
aicam_result_t network_comm_prefer_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    // Handle both GET and POST
    const char* method = ctx->request.method;
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    if (strcmp(method, "GET") == 0) {
        // GET - return current preferred type
        communication_type_t preferred = communication_get_preferred_type();
        cJSON_AddStringToObject(response_json, "preferred_type", communication_type_to_string(preferred));
        cJSON_AddBoolToObject(response_json, "auto_priority", communication_get_auto_priority());
    } else if (strcmp(method, "POST") == 0) {
        // POST - set preferred type
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Extract preferred type
        cJSON* type_item = cJSON_GetObjectItem(request_json, "preferred_type");
        if (type_item && cJSON_IsString(type_item)) {
            const char* type_str = cJSON_GetStringValue(type_item);
            communication_type_t target = communication_type_from_string(type_str);
            communication_set_preferred_type(target);
            cJSON_AddStringToObject(response_json, "preferred_type", communication_type_to_string(target));
        }
        
        // Extract auto priority setting
        cJSON* auto_priority_item = cJSON_GetObjectItem(request_json, "auto_priority");
        if (auto_priority_item && cJSON_IsBool(auto_priority_item)) {
            aicam_bool_t auto_priority = cJSON_IsTrue(auto_priority_item) ? AICAM_TRUE : AICAM_FALSE;
            communication_set_auto_priority(auto_priority);
            cJSON_AddBoolToObject(response_json, "auto_priority", auto_priority);
        }
        
        cJSON_Delete(request_json);
    } else {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET or POST method is allowed");
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "Preference operation completed");
}

/**
 * @brief POST /api/v1/system/network/comm/priority - Apply priority-based selection
 */
aicam_result_t network_comm_priority_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    aicam_result_t result = communication_apply_priority();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    communication_type_t current = communication_get_current_type();
    cJSON_AddStringToObject(response_json, "current_type", communication_type_to_string(current));
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "Priority applied successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply priority");
    }
}

/* ==================== Cellular/4G APIs ==================== */

/**
 * @brief GET /api/v1/system/network/cellular/status - Get cellular status
 */
aicam_result_t network_cellular_status_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Check if cellular is available
    aicam_bool_t available = communication_cellular_is_available();
    cJSON_AddBoolToObject(response_json, "available", available);
    
    if (!available) {
        cJSON_AddStringToObject(response_json, "status", "unavailable");
        cJSON_AddStringToObject(response_json, "message", "Cellular module not available");
    } else {
        // Get cellular status
        communication_status_t status = communication_cellular_get_status();
        cJSON_AddStringToObject(response_json, "status", communication_status_to_string(status));
        
        // Get IMEI
        char imei[32] = {0};
        if (communication_cellular_get_imei(imei) == AICAM_OK) {
            cJSON_AddStringToObject(response_json, "imei", imei);
        }
        
        // Get settings
        cellular_connection_settings_t settings;
        if (communication_cellular_get_settings(&settings) == AICAM_OK) {
            cJSON* settings_json = cJSON_CreateObject();
            if (settings_json) {
                cJSON_AddStringToObject(settings_json, "apn", settings.apn);
                cJSON_AddStringToObject(settings_json, "username", settings.username);
                cJSON_AddStringToObject(settings_json, "password", settings.password);
                cJSON_AddStringToObject(settings_json, "pin_code", settings.pin_code);
                cJSON_AddNumberToObject(settings_json, "authentication", settings.authentication);
                cJSON_AddBoolToObject(settings_json, "enable_roaming", settings.enable_roaming);
                cJSON_AddNumberToObject(settings_json, "operator", settings.operator);
                cJSON_AddStringToObject(settings_json, "plmn", settings.plmn);
                cJSON_AddItemToObject(response_json, "settings", settings_json);
            }
        }
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "Cellular status retrieved successfully");
}

/**
 * @brief POST /api/v1/system/network/cellular/settings - Configure cellular settings
 */
aicam_result_t network_cellular_settings_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    const char* method = ctx->request.method;
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    if (strcmp(method, "GET") == 0) {
        // GET - return current settings
        cellular_connection_settings_t settings;
        if (communication_cellular_get_settings(&settings) == AICAM_OK) {
            cJSON_AddStringToObject(response_json, "apn", settings.apn);
            cJSON_AddStringToObject(response_json, "username", settings.username);
            cJSON_AddStringToObject(response_json, "password", settings.password);
            cJSON_AddStringToObject(response_json, "pin_code", settings.pin_code);
            cJSON_AddNumberToObject(response_json, "authentication", settings.authentication);
            cJSON_AddBoolToObject(response_json, "enable_roaming", settings.enable_roaming);
            cJSON_AddNumberToObject(response_json, "operator", settings.operator);
            cJSON_AddStringToObject(response_json, "plmn", settings.plmn);
        }
    } else if (strcmp(method, "POST") == 0) {
        // POST - update settings
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Get current settings first
        cellular_connection_settings_t settings;
        communication_cellular_get_settings(&settings);
        
        // Update fields if provided
        cJSON* apn_item = cJSON_GetObjectItem(request_json, "apn");
        if (apn_item && cJSON_IsString(apn_item)) {
            strncpy(settings.apn, cJSON_GetStringValue(apn_item), sizeof(settings.apn) - 1);
            settings.apn[sizeof(settings.apn) - 1] = '\0';
        }
        
        cJSON* username_item = cJSON_GetObjectItem(request_json, "username");
        if (username_item && cJSON_IsString(username_item)) {
            strncpy(settings.username, cJSON_GetStringValue(username_item), sizeof(settings.username) - 1);
            settings.username[sizeof(settings.username) - 1] = '\0';
        }
        
        cJSON* password_item = cJSON_GetObjectItem(request_json, "password");
        if (password_item && cJSON_IsString(password_item)) {
            strncpy(settings.password, cJSON_GetStringValue(password_item), sizeof(settings.password) - 1);
            settings.password[sizeof(settings.password) - 1] = '\0';
        }
        
        cJSON* pin_item = cJSON_GetObjectItem(request_json, "pin_code");
        if (pin_item && cJSON_IsString(pin_item)) {
            strncpy(settings.pin_code, cJSON_GetStringValue(pin_item), sizeof(settings.pin_code) - 1);
            settings.pin_code[sizeof(settings.pin_code) - 1] = '\0';
        }
        
        cJSON* auth_item = cJSON_GetObjectItem(request_json, "authentication");
        if (auth_item && cJSON_IsNumber(auth_item)) {
            settings.authentication = (cellular_auth_type_t)(int)cJSON_GetNumberValue(auth_item);
        }
        
        cJSON* roaming_item = cJSON_GetObjectItem(request_json, "enable_roaming");
        if (roaming_item && cJSON_IsBool(roaming_item)) {
            settings.enable_roaming = cJSON_IsTrue(roaming_item) ? AICAM_TRUE : AICAM_FALSE;
        }

        cJSON* operator_item = cJSON_GetObjectItem(request_json, "operator");
        if (operator_item && cJSON_IsNumber(operator_item)) {
            int op = (int)cJSON_GetNumberValue(operator_item);
            if (op >= 0 && op <= 4) {
                settings.operator = (uint8_t)op;
            }
        }

        cJSON* plmn_item = cJSON_GetObjectItem(request_json, "plmn");
        if (plmn_item && cJSON_IsString(plmn_item)) {
            strncpy(settings.plmn, cJSON_GetStringValue(plmn_item), sizeof(settings.plmn) - 1);
            settings.plmn[sizeof(settings.plmn) - 1] = '\0';
        }

        // Apply settings
        aicam_result_t result = communication_cellular_set_settings(&settings);
        if (result != AICAM_OK) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set cellular settings");
        }
        
        // Check if save requested
        cJSON* save_item = cJSON_GetObjectItem(request_json, "save");
        if (save_item && cJSON_IsBool(save_item) && cJSON_IsTrue(save_item)) {
            communication_cellular_save_settings();
        }

        cJSON_Delete(request_json);
        
        cJSON_AddStringToObject(response_json, "message", "Cellular settings updated");
        cJSON_AddStringToObject(response_json, "apn", settings.apn);
        cJSON_AddBoolToObject(response_json, "enable_roaming", settings.enable_roaming);
    } else {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET or POST method is allowed");
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "Cellular settings operation completed");
}

/**
 * @brief POST /api/v1/system/network/cellular/save - Save cellular settings to NVS
 */
aicam_result_t network_cellular_save_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    aicam_result_t result = communication_cellular_save_settings();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "Cellular settings saved successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save cellular settings");
    }
}

/**
 * @brief POST /api/v1/system/network/cellular/connect - Connect cellular network
 */
aicam_result_t network_cellular_connect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    aicam_result_t result = communication_cellular_connect();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "status", 
        communication_status_to_string(communication_cellular_get_status()));
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "Cellular connection initiated");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to connect cellular network");
    }
}

/**
 * @brief POST /api/v1/system/network/cellular/disconnect - Disconnect cellular network
 */
aicam_result_t network_cellular_disconnect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    aicam_result_t result = communication_cellular_disconnect();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "status", 
        communication_status_to_string(communication_cellular_get_status()));
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "Cellular disconnected successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to disconnect cellular network");
    }
}

/**
 * @brief GET /api/v1/system/network/cellular/info - Get cellular detailed information
 */
aicam_result_t network_cellular_info_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cellular_detail_info_t info;
    aicam_result_t result = communication_cellular_get_detail_info(&info);
    
    if (result == AICAM_OK) {
        // Status
        cJSON_AddStringToObject(response_json, "network_status", communication_status_to_string(info.network_status));
        cJSON_AddStringToObject(response_json, "sim_status", info.sim_status);
        
        // Device info
        cJSON_AddStringToObject(response_json, "model", info.model);
        cJSON_AddStringToObject(response_json, "version", info.version);
        cJSON_AddStringToObject(response_json, "imei", info.imei);
        cJSON_AddStringToObject(response_json, "imsi", info.imsi);
        cJSON_AddStringToObject(response_json, "iccid", info.iccid);
        
        // Network info
        cJSON_AddStringToObject(response_json, "isp", info.isp);
        cJSON_AddStringToObject(response_json, "network_type", info.network_type);
        cJSON_AddStringToObject(response_json, "register_status", info.register_status);
        cJSON_AddStringToObject(response_json, "plmn_id", info.plmn_id);
        cJSON_AddStringToObject(response_json, "lac", info.lac);
        cJSON_AddStringToObject(response_json, "cell_id", info.cell_id);
        
        // Signal info
        cJSON_AddNumberToObject(response_json, "signal_level", info.signal_level);
        cJSON_AddNumberToObject(response_json, "csq", info.csq);
        cJSON_AddNumberToObject(response_json, "csq_level", info.csq_level);
        cJSON_AddNumberToObject(response_json, "rssi", info.rssi);
        
        // IPv4 info
        cJSON_AddStringToObject(response_json, "ipv4_address", info.ipv4_address);
        cJSON_AddStringToObject(response_json, "ipv4_gateway", info.ipv4_gateway);
        cJSON_AddStringToObject(response_json, "ipv4_dns", info.ipv4_dns);
        
        // IPv6 info
        cJSON_AddStringToObject(response_json, "ipv6_address", info.ipv6_address);
        cJSON_AddStringToObject(response_json, "ipv6_gateway", info.ipv6_gateway);
        cJSON_AddStringToObject(response_json, "ipv6_dns", info.ipv6_dns);
        
        // Connection info
        cJSON_AddNumberToObject(response_json, "connection_duration_sec", info.connection_duration_sec);
    } else {
        cJSON_AddStringToObject(response_json, "error", "Failed to get cellular info");
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "Cellular info retrieved successfully");
}

/**
 * @brief POST /api/v1/system/network/cellular/refresh - Refresh cellular information
 */
aicam_result_t network_cellular_refresh_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    aicam_result_t result = communication_cellular_refresh_info();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "Cellular info refreshed");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to refresh cellular info");
    }
}

/**
 * @brief POST /api/v1/system/network/cellular/at - Send AT command
 */
aicam_result_t network_cellular_at_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_cellular_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Cellular module not available");
    }
    
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Extract command
    cJSON* cmd_item = cJSON_GetObjectItem(request_json, "command");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'command' field");
    }
    
    const char* command = cJSON_GetStringValue(cmd_item);
    
    // Optional timeout
    uint32_t timeout_ms = 5000;
    cJSON* timeout_item = cJSON_GetObjectItem(request_json, "timeout_ms");
    if (timeout_item && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)cJSON_GetNumberValue(timeout_item);
    }
    
    cJSON_Delete(request_json);
    
    // Send AT command
    char response_buf[512] = {0};
    aicam_result_t result = communication_cellular_send_at_command(command, response_buf, sizeof(response_buf), timeout_ms);
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "command", command);
    cJSON_AddStringToObject(response_json, "response", response_buf);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "AT command sent successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_BUSINESS_ERROR_AT_COMMAND_FAILED, "AT command failed");
    }
}

/* ==================== PoE/Ethernet APIs ==================== */

/**
 * @brief GET /api/v1/system/network/poe/status - Get PoE status
 */
aicam_result_t network_poe_status_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Check if PoE is available
    aicam_bool_t available = communication_poe_is_available();
    cJSON_AddBoolToObject(response_json, "available", available);
    
    if (!available) {
        cJSON_AddStringToObject(response_json, "status", "unavailable");
        cJSON_AddStringToObject(response_json, "message", "PoE/Ethernet module not available");
    } else {
        // Get PoE status
        communication_status_t status = communication_poe_get_status();
        cJSON_AddStringToObject(response_json, "status", communication_status_to_string(status));
        
        // Get type info for more details
        communication_type_info_t info;
        if (communication_get_type_info(COMM_TYPE_POE, &info) == AICAM_OK) {
            cJSON_AddStringToObject(response_json, "ip_address", info.ip_addr);
            cJSON_AddBoolToObject(response_json, "connected", info.status == COMM_STATUS_CONNECTED);
        }
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "PoE status retrieved successfully");
}

/**
 * @brief POST /api/v1/system/network/poe/connect - Connect PoE network
 */
aicam_result_t network_poe_connect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    aicam_result_t result = communication_poe_connect();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "status", 
        communication_status_to_string(communication_poe_get_status()));
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "PoE connection initiated");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to connect PoE network");
    }
}

/**
 * @brief POST /api/v1/system/network/poe/disconnect - Disconnect PoE network
 */
aicam_result_t network_poe_disconnect_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    aicam_result_t result = communication_poe_disconnect();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "status", 
        communication_status_to_string(communication_poe_get_status()));
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "PoE disconnected successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to disconnect PoE network");
    }
}

/**
 * @brief GET /api/v1/system/network/poe/info - Get PoE detailed information
 * 
 * Response:
 * {
 *   "available": true,
 *   "network_status": "connected",
 *   "status_code": 3,
 *   "status_message": "Connected",
 *   "ip_mode": "dhcp",
 *   "ip_address": "192.168.1.100",
 *   "netmask": "255.255.255.0",
 *   "gateway": "192.168.1.1",
 *   "dns_primary": "8.8.8.8",
 *   "dns_secondary": "8.8.4.4",
 *   "hostname": "aicamera",
 *   "mac_address": "00:11:22:33:44:55",
 *   "interface_name": "wn",
 *   "link_up": true,
 *   "poe_powered": true,
 *   "connection_duration_sec": 3600,
 *   "dhcp_lease_time": 86400,
 *   "dhcp_lease_remaining": 43200
 * }
 */
aicam_result_t network_poe_info_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Check if PoE is available
    aicam_bool_t available = communication_poe_is_available();
    cJSON_AddBoolToObject(response_json, "available", available);
    
    if (!available) {
        cJSON_AddStringToObject(response_json, "network_status", "unavailable");
        cJSON_AddNumberToObject(response_json, "status_code", POE_STATUS_OFFLINE);
        cJSON_AddStringToObject(response_json, "status_message", poe_status_code_to_string(POE_STATUS_OFFLINE));
        cJSON_AddStringToObject(response_json, "message", "PoE/Ethernet module not available");
    } else {
        // Get detailed PoE info
        poe_detail_info_t info;
        aicam_result_t result = communication_poe_get_detail_info(&info);
        
        if (result == AICAM_OK) {
            // Network status
            cJSON_AddStringToObject(response_json, "network_status", communication_status_to_string(info.network_status));
            cJSON_AddNumberToObject(response_json, "status_code", info.status_code);
            cJSON_AddStringToObject(response_json, "status_message", info.status_message);
            
            // IP configuration
            cJSON_AddStringToObject(response_json, "ip_mode", info.ip_mode == 0 ? "dhcp" : "static");
            cJSON_AddStringToObject(response_json, "ip_address", info.ip_address);
            cJSON_AddStringToObject(response_json, "netmask", info.netmask);
            cJSON_AddStringToObject(response_json, "gateway", info.gateway);
            cJSON_AddStringToObject(response_json, "dns_primary", info.dns_primary);
            cJSON_AddStringToObject(response_json, "dns_secondary", info.dns_secondary);
            cJSON_AddStringToObject(response_json, "hostname", info.hostname);
            
            // Hardware info
            cJSON_AddStringToObject(response_json, "mac_address", info.mac_address);
            cJSON_AddStringToObject(response_json, "interface_name", info.interface_name);
            cJSON_AddBoolToObject(response_json, "link_up", info.link_up);
            cJSON_AddBoolToObject(response_json, "poe_powered", info.poe_powered);
            
            // Connection statistics
            cJSON_AddNumberToObject(response_json, "connection_duration_sec", info.connection_duration_sec);
            cJSON_AddNumberToObject(response_json, "connection_start_time", info.connection_start_time);
            cJSON_AddNumberToObject(response_json, "dhcp_lease_time", info.dhcp_lease_time);
            cJSON_AddNumberToObject(response_json, "dhcp_lease_remaining", info.dhcp_lease_remaining);
            
            // Event counters
            cJSON_AddNumberToObject(response_json, "connect_count", info.connect_count);
            cJSON_AddNumberToObject(response_json, "disconnect_count", info.disconnect_count);
            cJSON_AddNumberToObject(response_json, "dhcp_fail_count", info.dhcp_fail_count);
            cJSON_AddNumberToObject(response_json, "last_error_code", info.last_error_code);
        } else {
            cJSON_AddStringToObject(response_json, "error", "Failed to get PoE info");
        }
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "PoE info retrieved successfully");
}

/**
 * @brief GET/POST /api/v1/system/network/poe/config - Get or set PoE configuration
 * 
 * GET Response / POST Request:
 * {
 *   "ip_mode": "dhcp|static",
 *   "ip_address": "192.168.1.100",
 *   "netmask": "255.255.255.0",
 *   "gateway": "192.168.1.1",
 *   "dns_primary": "8.8.8.8",
 *   "dns_secondary": "8.8.4.4",
 *   "hostname": "aicamera",
 *   "dhcp_timeout_ms": 30000,
 *   "dhcp_retry_count": 3,
 *   "auto_reconnect": true,
 *   "validate_gateway": true,
 *   "detect_ip_conflict": true
 * }
 */
aicam_result_t network_poe_config_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    const char* method = ctx->request.method;
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    if (strcmp(method, "GET") == 0) {
        // GET - return current configuration
        poe_config_persist_t poe_cfg;
        aicam_result_t result = json_config_get_poe_config(&poe_cfg);
        
        if (result == AICAM_OK) {
            // IP mode
            cJSON_AddStringToObject(response_json, "ip_mode", 
                poe_cfg.ip_mode == POE_IP_MODE_DHCP ? "dhcp" : "static");
            
            // Static IP configuration
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                    poe_cfg.ip_addr[0], poe_cfg.ip_addr[1], 
                    poe_cfg.ip_addr[2], poe_cfg.ip_addr[3]);
            cJSON_AddStringToObject(response_json, "ip_address", ip_str);
            
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                    poe_cfg.netmask[0], poe_cfg.netmask[1], 
                    poe_cfg.netmask[2], poe_cfg.netmask[3]);
            cJSON_AddStringToObject(response_json, "netmask", ip_str);
            
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                    poe_cfg.gateway[0], poe_cfg.gateway[1], 
                    poe_cfg.gateway[2], poe_cfg.gateway[3]);
            cJSON_AddStringToObject(response_json, "gateway", ip_str);
            
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                    poe_cfg.dns_primary[0], poe_cfg.dns_primary[1], 
                    poe_cfg.dns_primary[2], poe_cfg.dns_primary[3]);
            cJSON_AddStringToObject(response_json, "dns_primary", ip_str);
            
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                    poe_cfg.dns_secondary[0], poe_cfg.dns_secondary[1], 
                    poe_cfg.dns_secondary[2], poe_cfg.dns_secondary[3]);
            cJSON_AddStringToObject(response_json, "dns_secondary", ip_str);
            
            cJSON_AddStringToObject(response_json, "hostname", poe_cfg.hostname);
            
            // DHCP settings
            cJSON_AddNumberToObject(response_json, "dhcp_timeout_ms", poe_cfg.dhcp_timeout_ms);
            cJSON_AddNumberToObject(response_json, "dhcp_retry_count", poe_cfg.dhcp_retry_count);
            cJSON_AddNumberToObject(response_json, "dhcp_retry_interval_ms", poe_cfg.dhcp_retry_interval_ms);
            
            // Recovery settings
            cJSON_AddNumberToObject(response_json, "power_recovery_delay_ms", poe_cfg.power_recovery_delay_ms);
            cJSON_AddBoolToObject(response_json, "auto_reconnect", poe_cfg.auto_reconnect);
            cJSON_AddBoolToObject(response_json, "persist_last_ip", poe_cfg.persist_last_ip);
            
            // Validation settings
            cJSON_AddBoolToObject(response_json, "validate_gateway", poe_cfg.validate_gateway);
            cJSON_AddBoolToObject(response_json, "detect_ip_conflict", poe_cfg.detect_ip_conflict);
        } else {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get PoE configuration");
        }
    } else if (strcmp(method, "POST") == 0) {
        // POST - update configuration
        cJSON* request_json = web_api_parse_body(ctx);
        if (!request_json) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
        }
        
        // Get current configuration first
        poe_config_persist_t poe_cfg;
        json_config_get_poe_config(&poe_cfg);
        
        // Update IP mode if provided
        cJSON* ip_mode_item = cJSON_GetObjectItem(request_json, "ip_mode");
        if (ip_mode_item && cJSON_IsString(ip_mode_item)) {
            const char* mode_str = cJSON_GetStringValue(ip_mode_item);
            if (strcmp(mode_str, "dhcp") == 0) {
                poe_cfg.ip_mode = POE_IP_MODE_DHCP;
            } else if (strcmp(mode_str, "static") == 0) {
                poe_cfg.ip_mode = POE_IP_MODE_STATIC;
            }
        }
        
        // Helper to parse IP string to array
        #define PARSE_IP_TO_ARRAY(json_key, target_array) do { \
            cJSON* item = cJSON_GetObjectItem(request_json, json_key); \
            if (item && cJSON_IsString(item)) { \
                const char* ip_str = cJSON_GetStringValue(item); \
                unsigned int a, b, c, d; \
                if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) { \
                    target_array[0] = (uint8_t)a; \
                    target_array[1] = (uint8_t)b; \
                    target_array[2] = (uint8_t)c; \
                    target_array[3] = (uint8_t)d; \
                } \
            } \
        } while(0)
        
        // Parse IP addresses
        PARSE_IP_TO_ARRAY("ip_address", poe_cfg.ip_addr);
        PARSE_IP_TO_ARRAY("netmask", poe_cfg.netmask);
        PARSE_IP_TO_ARRAY("gateway", poe_cfg.gateway);
        PARSE_IP_TO_ARRAY("dns_primary", poe_cfg.dns_primary);
        PARSE_IP_TO_ARRAY("dns_secondary", poe_cfg.dns_secondary);
        
        #undef PARSE_IP_TO_ARRAY
        
        // Parse hostname
        cJSON* hostname_item = cJSON_GetObjectItem(request_json, "hostname");
        if (hostname_item && cJSON_IsString(hostname_item)) {
            strncpy(poe_cfg.hostname, cJSON_GetStringValue(hostname_item), sizeof(poe_cfg.hostname) - 1);
            poe_cfg.hostname[sizeof(poe_cfg.hostname) - 1] = '\0';
        }
        
        // Parse DHCP settings
        cJSON* dhcp_timeout_item = cJSON_GetObjectItem(request_json, "dhcp_timeout_ms");
        if (dhcp_timeout_item && cJSON_IsNumber(dhcp_timeout_item)) {
            poe_cfg.dhcp_timeout_ms = (uint32_t)cJSON_GetNumberValue(dhcp_timeout_item);
        }
        
        cJSON* dhcp_retry_item = cJSON_GetObjectItem(request_json, "dhcp_retry_count");
        if (dhcp_retry_item && cJSON_IsNumber(dhcp_retry_item)) {
            poe_cfg.dhcp_retry_count = (uint32_t)cJSON_GetNumberValue(dhcp_retry_item);
        }
        
        // Parse recovery settings
        cJSON* auto_reconnect_item = cJSON_GetObjectItem(request_json, "auto_reconnect");
        if (auto_reconnect_item && cJSON_IsBool(auto_reconnect_item)) {
            poe_cfg.auto_reconnect = cJSON_IsTrue(auto_reconnect_item) ? AICAM_TRUE : AICAM_FALSE;
        }
        
        // Parse validation settings
        cJSON* validate_gw_item = cJSON_GetObjectItem(request_json, "validate_gateway");
        if (validate_gw_item && cJSON_IsBool(validate_gw_item)) {
            poe_cfg.validate_gateway = cJSON_IsTrue(validate_gw_item) ? AICAM_TRUE : AICAM_FALSE;
        }
        
        cJSON* detect_conflict_item = cJSON_GetObjectItem(request_json, "detect_ip_conflict");
        if (detect_conflict_item && cJSON_IsBool(detect_conflict_item)) {
            poe_cfg.detect_ip_conflict = cJSON_IsTrue(detect_conflict_item) ? AICAM_TRUE : AICAM_FALSE;
        }
        
        cJSON_Delete(request_json);
        
        // Save configuration
        aicam_result_t result = json_config_set_poe_config(&poe_cfg);
        if (result != AICAM_OK) {
            cJSON_Delete(response_json);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set PoE configuration");
        }
        
        cJSON_AddStringToObject(response_json, "message", "PoE configuration updated successfully");
        cJSON_AddStringToObject(response_json, "ip_mode", 
            poe_cfg.ip_mode == POE_IP_MODE_DHCP ? "dhcp" : "static");
    } else {
        cJSON_Delete(response_json);
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET or POST method is allowed");
    }
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "PoE config operation completed");
}

/**
 * @brief POST /api/v1/system/network/poe/validate - Validate PoE static configuration
 * 
 * Request:
 * {
 *   "ip_address": "192.168.1.100",
 *   "netmask": "255.255.255.0",
 *   "gateway": "192.168.1.1",
 *   "dns_primary": "8.8.8.8",
 *   "check_gateway": true,
 *   "check_conflict": true
 * }
 * 
 * Response:
 * {
 *   "valid": true|false,
 *   "errors": [],
 *   "warnings": [],
 *   "gateway_reachable": true|false,
 *   "ip_conflict": false
 * }
 */
aicam_result_t network_poe_validate_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    cJSON* request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }
    
    // Parse configuration to validate
    poe_static_config_t config;
    memset(&config, 0, sizeof(config));
    
    // Helper to parse IP string to array
    #define PARSE_IP_TO_ARRAY(json_key, target_array) do { \
        cJSON* item = cJSON_GetObjectItem(request_json, json_key); \
        if (item && cJSON_IsString(item)) { \
            const char* ip_str = cJSON_GetStringValue(item); \
            unsigned int a, b, c, d; \
            if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) { \
                target_array[0] = (uint8_t)a; \
                target_array[1] = (uint8_t)b; \
                target_array[2] = (uint8_t)c; \
                target_array[3] = (uint8_t)d; \
            } \
        } \
    } while(0)
    
    PARSE_IP_TO_ARRAY("ip_address", config.ip_addr);
    PARSE_IP_TO_ARRAY("netmask", config.netmask);
    PARSE_IP_TO_ARRAY("gateway", config.gateway);
    PARSE_IP_TO_ARRAY("dns_primary", config.dns_primary);
    PARSE_IP_TO_ARRAY("dns_secondary", config.dns_secondary);
    
    #undef PARSE_IP_TO_ARRAY
    
    // Parse hostname
    cJSON* hostname_item = cJSON_GetObjectItem(request_json, "hostname");
    if (hostname_item && cJSON_IsString(hostname_item)) {
        strncpy(config.hostname, cJSON_GetStringValue(hostname_item), sizeof(config.hostname) - 1);
    }
    
    // Check options
    aicam_bool_t check_gateway = AICAM_FALSE;
    cJSON* check_gw_item = cJSON_GetObjectItem(request_json, "check_gateway");
    if (check_gw_item && cJSON_IsBool(check_gw_item) && cJSON_IsTrue(check_gw_item)) {
        check_gateway = AICAM_TRUE;
    }
    
    aicam_bool_t check_conflict = AICAM_FALSE;
    cJSON* check_conflict_item = cJSON_GetObjectItem(request_json, "check_conflict");
    if (check_conflict_item && cJSON_IsBool(check_conflict_item) && cJSON_IsTrue(check_conflict_item)) {
        check_conflict = AICAM_TRUE;
    }
    
    cJSON_Delete(request_json);
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON* errors_array = cJSON_CreateArray();
    cJSON* warnings_array = cJSON_CreateArray();
    aicam_bool_t is_valid = AICAM_TRUE;
    
    // Validate static configuration
    char error_msg[128] = {0};
    aicam_result_t result = communication_poe_validate_static_config(&config, error_msg, sizeof(error_msg));
    if (result != AICAM_OK) {
        is_valid = AICAM_FALSE;
        cJSON_AddItemToArray(errors_array, cJSON_CreateString(error_msg));
    }
    
    // Check IP conflict if requested
    aicam_bool_t ip_conflict = AICAM_FALSE;
    if (check_conflict && is_valid) {
        result = communication_poe_check_ip_conflict(config.ip_addr);
        if (result != AICAM_OK) {
            ip_conflict = AICAM_TRUE;
            is_valid = AICAM_FALSE;
            cJSON_AddItemToArray(errors_array, cJSON_CreateString("IP address conflict detected"));
        }
    }
    cJSON_AddBoolToObject(response_json, "ip_conflict", ip_conflict);
    
    // Check gateway reachability if requested
    aicam_bool_t gateway_reachable = AICAM_TRUE;
    if (check_gateway && is_valid) {
        result = communication_poe_check_gateway(config.gateway, 3000);
        if (result != AICAM_OK) {
            gateway_reachable = AICAM_FALSE;
            // This is a warning, not an error
            cJSON_AddItemToArray(warnings_array, cJSON_CreateString("Gateway may not be reachable"));
        }
    }
    cJSON_AddBoolToObject(response_json, "gateway_reachable", gateway_reachable);
    
    cJSON_AddBoolToObject(response_json, "valid", is_valid);
    cJSON_AddItemToObject(response_json, "errors", errors_array);
    cJSON_AddItemToObject(response_json, "warnings", warnings_array);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    return api_response_success(ctx, json_string, "PoE configuration validation completed");
}

/**
 * @brief POST /api/v1/system/network/poe/apply - Apply PoE configuration
 */
aicam_result_t network_poe_apply_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    aicam_result_t result = communication_poe_apply_config();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    cJSON_AddStringToObject(response_json, "status", 
        communication_status_to_string(communication_poe_get_status()));
    
    // Get current IP mode
    uint8_t ip_mode = communication_poe_get_ip_mode();
    cJSON_AddStringToObject(response_json, "ip_mode", ip_mode == 0 ? "dhcp" : "static");
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "PoE configuration applied successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to apply PoE configuration");
    }
}

/**
 * @brief POST /api/v1/system/network/poe/save - Save PoE configuration to persistent storage
 */
aicam_result_t network_poe_save_handler(http_handler_context_t *ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    if (!communication_is_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "Communication service is not running");
    }
    
    if (!communication_poe_is_available()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "PoE/Ethernet module not available");
    }
    
    aicam_result_t result = communication_poe_save_config();
    
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", result == AICAM_OK);
    
    char* json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    if (result == AICAM_OK) {
        return api_response_success(ctx, json_string, "PoE configuration saved successfully");
    } else {
        cJSON_free(json_string);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save PoE configuration");
    }
}

/* ==================== Module Registration ==================== */

/**
 * @brief Network API routes
 */
static const api_route_t network_module_routes[] = {
    // === Communication Overview (Lightweight) ===
    {
        .path = API_PATH_PREFIX"/system/network/status",
        .method = "GET",
        .handler = network_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    
    // === WiFi APIs ===
    {
        .path = API_PATH_PREFIX"/system/network/wifi/sta",
        .method = "GET",
        .handler = network_wifi_sta_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi/ap",
        .method = "GET",
        .handler = network_wifi_ap_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi/config",
        .method = "POST",
        .handler = network_wifi_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi/scan",
        .method = "POST",
        .handler = network_scan_refresh_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi/disconnect",
        .method = "POST",
        .handler = network_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/wifi/delete",
        .method = "POST",
        .handler = network_delete_known_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },

#if NETIF_WIFI_HALOW_IS_ENABLE
    // === HaLow APIs ===
    {
        .path = API_PATH_PREFIX"/system/network/halow/sta",
        .method = "GET",
        .handler = network_halow_sta_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/region",
        .method = "GET",
        .handler = network_halow_region_get_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/region",
        .method = "PUT",
        .handler = network_halow_region_set_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/scan",
        .method = "POST",
        .handler = network_halow_scan_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow",
        .method = "POST",
        .handler = network_halow_connect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/disconnect",
        .method = "POST",
        .handler = network_halow_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/delete",
        .method = "POST",
        .handler = network_halow_delete_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/ip",
        .method = "GET",
        .handler = network_halow_ip_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/ip",
        .method = "POST",
        .handler = network_halow_ip_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/radio",
        .method = "GET",
        .handler = network_halow_radio_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/halow/radio",
        .method = "PUT",
        .handler = network_halow_radio_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
#endif
    
    // === Legacy WiFi endpoints (for backward compatibility) ===
    {
        .path = API_PATH_PREFIX"/system/network/wifi",
        .method = "POST",
        .handler = network_wifi_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/scan",
        .method = "POST",
        .handler = network_scan_refresh_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/disconnect",
        .method = "POST",
        .handler = network_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/delete",
        .method = "POST",
        .handler = network_delete_known_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    
    // === Communication Type Management APIs ===
    {
        .path = API_PATH_PREFIX"/system/network/comm/types",
        .method = "GET",
        .handler = network_comm_types_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/comm/switch",
        .method = "POST",
        .handler = network_comm_switch_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/comm/prefer",
        .method = "GET",
        .handler = network_comm_prefer_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/comm/prefer",
        .method = "POST",
        .handler = network_comm_prefer_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/comm/priority",
        .method = "POST",
        .handler = network_comm_priority_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    
    // === Cellular/4G APIs ===
    {
        .path = API_PATH_PREFIX"/system/network/cellular/status",
        .method = "GET",
        .handler = network_cellular_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/settings",
        .method = "GET",
        .handler = network_cellular_settings_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/settings",
        .method = "POST",
        .handler = network_cellular_settings_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/save",
        .method = "POST",
        .handler = network_cellular_save_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/connect",
        .method = "POST",
        .handler = network_cellular_connect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/disconnect",
        .method = "POST",
        .handler = network_cellular_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/info",
        .method = "GET",
        .handler = network_cellular_info_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/refresh",
        .method = "POST",
        .handler = network_cellular_refresh_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/cellular/at",
        .method = "POST",
        .handler = network_cellular_at_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    
    // === PoE/Ethernet APIs ===
    {
        .path = API_PATH_PREFIX"/system/network/poe/status",
        .method = "GET",
        .handler = network_poe_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/connect",
        .method = "POST",
        .handler = network_poe_connect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/disconnect",
        .method = "POST",
        .handler = network_poe_disconnect_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/info",
        .method = "GET",
        .handler = network_poe_info_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/config",
        .method = "GET",
        .handler = network_poe_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/config",
        .method = "POST",
        .handler = network_poe_config_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/validate",
        .method = "POST",
        .handler = network_poe_validate_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/apply",
        .method = "POST",
        .handler = network_poe_apply_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX"/system/network/poe/save",
        .method = "POST",
        .handler = network_poe_save_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register network API module
 */
aicam_result_t web_api_register_network_module(void) {
    LOG_SVC_INFO("Registering Network API module...");
    
    // Register each route
    for (size_t i = 0; i < sizeof(network_module_routes) / sizeof(network_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&network_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", network_module_routes[i].path, result);
            return result;
        }
    }
    
    LOG_SVC_INFO("Network API module registered successfully (%zu routes)", 
                sizeof(network_module_routes) / sizeof(network_module_routes[0]));
    
    return AICAM_OK;
}

/* Suppress unused function warning - reserved for future use */
static void __attribute__((unused)) _suppress_unused_warnings(void) {
    (void)create_interface_json;
}
