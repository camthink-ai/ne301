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
    cJSON_AddStringToObject(response_json, "active_type", communication_type_to_string(active_type));
    
    // selected_type: User selected type (for UI page display, may not be connected)
    communication_type_t selected_type = communication_get_selected_type();
    cJSON_AddStringToObject(response_json, "selected_type", communication_type_to_string(selected_type));
    
    // current_comm_type: Backward compatible alias for active_type
    cJSON_AddStringToObject(response_json, "current_comm_type", communication_type_to_string(active_type));
    
    // 3. Connection flag (based on active_type)
    aicam_bool_t has_connection = (active_type != COMM_TYPE_NONE);
    cJSON_AddBoolToObject(response_json, "has_connection", has_connection);
    
    // 4. Display name for status bar (based on active_type for connection status)
    const char* comm_display_name = "Not Connected";
    switch (active_type) {
        case COMM_TYPE_WIFI: comm_display_name = "WiFi"; break;
        case COMM_TYPE_CELLULAR: comm_display_name = "Cellular"; break;
        case COMM_TYPE_POE: comm_display_name = "PoE/Ethernet"; break;
        default: break;
    }
    cJSON_AddStringToObject(response_json, "active_display_name", comm_display_name);
    
    // 5. Selected type display name (for page navigation)
    const char* selected_display_name = "Not Selected";
    switch (selected_type) {
        case COMM_TYPE_WIFI: selected_display_name = "WiFi"; break;
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
    cJSON_AddStringToObject(response_json, "current_type", communication_type_to_string(current_type));
    
    // Get preferred type
    communication_type_t preferred_type = communication_get_preferred_type();
    cJSON_AddStringToObject(response_json, "preferred_type", communication_type_to_string(preferred_type));
    
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
                    
                    cJSON_AddStringToObject(type_json, "type", communication_type_to_string(t));
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
                        } else if (t == COMM_TYPE_POE) {
                            // PoE details
                            cJSON_AddStringToObject(detail_json, "interface", NETIF_NAME_ETH_WAN);
                        }
                        cJSON_AddItemToObject(type_json, "detail", detail_json);
                    }
                    
                    cJSON_AddItemToArray(types_array, type_json);
                    
                    // Add to available types if can switch
                    if (types[i].available) {
                        cJSON_AddItemToArray(available_array, cJSON_CreateString(communication_type_to_string(t)));
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
            cJSON_AddStringToObject(current_info, "type", communication_type_to_string(current_type));
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
    communication_type_t target_type = communication_type_from_string(type_str);
    
    if (target_type == COMM_TYPE_NONE || target_type >= COMM_TYPE_MAX) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid communication type (use wifi, cellular/4g, or poe/ethernet)");
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
    
    // Perform synchronous switch
    communication_switch_result_t switch_result;
    aicam_result_t result = communication_switch_type_sync(target_type, &switch_result, timeout_ms);
    
    // Create response
    cJSON* response_json = cJSON_CreateObject();
    if (!response_json) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(response_json, "success", switch_result.success);
    cJSON_AddStringToObject(response_json, "from_type", communication_type_to_string(switch_result.from_type));
    cJSON_AddStringToObject(response_json, "to_type", communication_type_to_string(switch_result.to_type));
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
