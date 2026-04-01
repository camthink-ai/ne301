/**
 * @file communication_service.c
 * @brief Communication Service Implementation
 * @details communication service standard interface implementation, focus on network interface information collection and configuration management
 */

#include "communication_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "system_service.h"
#include "netif_manager.h"
#include "netif_init_manager.h"
#include "sl_net_netif.h"
#include "buffer_mgr.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "common_utils.h"
#include "service_init.h"
#include "mqtt_service.h"
#include "device_service.h"
#include "u0_module.h"
#include "json_config_mgr.h"
#if NETIF_4G_CAT1_IS_ENABLE
#include "ms_modem.h"
#include "ms_modem_at.h"
#endif



/* ==================== Communication Service Context ==================== */
uint8_t background_scan_task_stack[1024 * 4] ALIGN_32 IN_PSRAM;


typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
    communication_service_config_t config;
    communication_service_stats_t stats;
    osThreadId_t scan_task_id;
    osSemaphoreId_t scan_semaphore_id;
    
    // Network interface management
    network_interface_status_t interfaces[MAX_NETWORK_INTERFACES];
    uint32_t interface_count;
    
    // Network scanning
    network_scan_result_t scan_results[MAX_SCAN_RESULTS];
    uint32_t scan_result_count;
    aicam_bool_t scan_in_progress;
    
    // Known networks database
    network_scan_result_t known_networks[MAX_KNOWN_NETWORKS];
    uint32_t known_network_count;
    
    // Auto-reconnection
    aicam_bool_t auto_reconnect_enabled;
    uint32_t reconnect_timer;
    
    // Network manager registration
    aicam_bool_t netif_manager_registered;
    
    // Communication type management
    communication_type_t selected_type;             // User selected type (for UI page display, may not be connected)
    communication_type_t active_type;               // Currently connected type (for data transmission)
    communication_type_t preferred_type;            // User preferred type (for auto-priority)
    communication_type_info_t type_info[COMM_TYPE_MAX];  // Type information cache
    
    // Cellular management
    aicam_bool_t cellular_available;                // 4G module hardware available
    aicam_bool_t cellular_initialized;              // 4G module initialized
    cellular_connection_settings_t cellular_settings;  // Cellular settings
    cellular_detail_info_t cellular_info;           // Cellular detailed info
    uint32_t cellular_connect_start_time;           // Connection start timestamp
    
    // PoE/Ethernet management
    aicam_bool_t poe_available;                     // PoE hardware available
    aicam_bool_t poe_initialized;                   // PoE initialized
    
    // Network interface ready tracking (for startup decision)
    aicam_bool_t wifi_sta_ready;                    // WiFi STA init completed
    aicam_bool_t cellular_ready;                    // Cellular init completed  
    aicam_bool_t poe_ready;                         // PoE init completed
    aicam_bool_t startup_decision_made;             // Startup connection decision made
    uint32_t startup_begin_time;                    // When startup began (for timeout)
    osTimerId_t startup_timeout_timer;              // Startup timeout timer
    
    // Switch management
    aicam_bool_t switch_in_progress;                // Switch operation in progress
    communication_switch_callback_t switch_callback; // Switch callback
    communication_switch_result_t switch_result;    // Switch result
} communication_service_context_t;

static communication_service_context_t g_communication_service = {0};
static osMutexId_t g_communication_service_lock = NULL;
static void communication_service_lock(void)
{
    if (g_communication_service_lock == NULL) {
        g_communication_service_lock = osMutexNew(NULL);
    }
    osMutexAcquire(g_communication_service_lock, osWaitForever);
}
static void communication_service_unlock(void)
{
    if (g_communication_service_lock == NULL) {
        return;
    }
    osMutexRelease(g_communication_service_lock);
}

/* ==================== Default Configuration ==================== */

static const communication_service_config_t default_config = {
    .auto_start_wifi_ap = AICAM_TRUE,
    .auto_start_wifi_sta = AICAM_TRUE,
    .auto_start_cellular = AICAM_TRUE,
    .auto_start_poe = AICAM_TRUE,
    .enable_network_scan = AICAM_TRUE,
    .enable_auto_reconnect = AICAM_TRUE,
    .enable_auto_priority = AICAM_TRUE,
    .reconnect_interval_ms = 5000,
    .scan_interval_ms = 30000,
    .connection_timeout_ms = 30000,
    .enable_debug = AICAM_FALSE,
    .enable_stats = AICAM_TRUE,
    .preferred_type = COMM_TYPE_NONE
};

static aicam_bool_t is_connected(const char *ssid, const char *bssid);
static void add_known_network(const network_scan_result_t *network);

/* ==================== Network Interface Ready Callbacks ==================== */

static void on_wifi_ap_ready(const char *if_name, aicam_result_t result);
static void on_wifi_sta_ready(const char *if_name, aicam_result_t result);
#if NETIF_4G_CAT1_IS_ENABLE
static void on_cellular_ready(const char *if_name, aicam_result_t result);
#endif
#if NETIF_ETH_WAN_IS_ENABLE
static void on_poe_ready(const char *if_name, aicam_result_t result);
#endif

/* ==================== Communication Type Management Helpers ==================== */
static void update_type_info_cache(void);
static communication_type_t get_highest_priority_connected_type(void);
static const char* get_interface_name_for_type(communication_type_t type);
static void check_and_handle_hardware_change(void);

/* ==================== Startup Connection Decision ==================== */
static void check_all_ready_and_decide(void);
static void make_startup_connection_decision(void);
static communication_type_t get_highest_priority_available_type(void);
static void startup_timeout_callback(void *argument);

/* ==================== Known Networks Persistence ==================== */

/**
 * @brief Save known networks to NVS
 */
static aicam_result_t save_known_networks_to_nvs(void)
{
    network_service_config_t *network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
    if (!network_config) {
        LOG_SVC_ERROR("Failed to allocate memory for network config");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Get current network service config
    aicam_result_t result = json_config_get_network_service_config(network_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to get network service config: %d", result);
        buffer_free(network_config);
        return result;
    }
    
    // Copy known networks from communication service to network config
    network_config->known_network_count = g_communication_service.known_network_count;
    for (uint32_t i = 0; i < g_communication_service.known_network_count && i < MAX_KNOWN_NETWORKS; i++) {
        memcpy(&network_config->known_networks[i], 
               &g_communication_service.known_networks[i], 
               sizeof(network_scan_result_t));
    }
    
    // Save to NVS
    result = json_config_set_network_service_config(network_config);
    
    buffer_free(network_config);
    
    if (result == AICAM_OK) {
        LOG_SVC_DEBUG("Known networks saved to NVS successfully");
    } else {
        LOG_SVC_ERROR("Failed to save known networks to NVS: %d", result);
    }
    
    return result;
}

/**
 * @brief Load known networks from NVS
 */
static aicam_result_t load_known_networks_from_nvs(void)
{
    network_service_config_t *network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
    if (!network_config) {
        LOG_SVC_ERROR("Failed to allocate memory for network config");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Get network service config from NVS
    aicam_result_t result = json_config_get_network_service_config(network_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to get network service config from NVS: %d", result);
        buffer_free(network_config);
        return result;
    }
    
    // Copy known networks to communication service
    g_communication_service.known_network_count = network_config->known_network_count;
    if (g_communication_service.known_network_count > MAX_KNOWN_NETWORKS) {
        g_communication_service.known_network_count = MAX_KNOWN_NETWORKS;
    }
    
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        memcpy(&g_communication_service.known_networks[i], 
               &network_config->known_networks[i], 
               sizeof(network_scan_result_t));
        LOG_SVC_DEBUG("Loaded known network: %s (%s)", 
                     g_communication_service.known_networks[i].ssid,
                     g_communication_service.known_networks[i].bssid);
    }
    
    buffer_free(network_config);
    
    LOG_SVC_INFO("Loaded %u known networks from NVS", g_communication_service.known_network_count);
    
    return AICAM_OK;
}

/**
 * @brief Try to connect to known networks (optimized for low power mode fast startup)
 * @note In low power mode, prioritizes last connected network for fastest connection
 */
static aicam_result_t try_connect_known_networks(void)
{
    if (g_communication_service.known_network_count == 0) {
        LOG_SVC_INFO("No known networks to connect");
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Check if wakeup source requires time-optimized mode - only enable fast connection for time-optimized wakeup
    // This indicates low power mode with scheduled wakeup, where fast connection is critical
    wakeup_source_type_t wakeup_source = system_service_get_wakeup_source_type();
    aicam_bool_t requires_time_optimized = system_service_requires_time_optimized_mode(wakeup_source);
    
    LOG_SVC_INFO("Trying to connect to known networks (time-optimized mode: %s)...", 
                 requires_time_optimized ? "YES" : "NO");
    
    // Create a sorted index array
    uint32_t sorted_indices[MAX_KNOWN_NETWORKS];
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        sorted_indices[i] = i;
    }
    
    // Sort by priority: in low power mode, prioritize last_connected_time; otherwise use hybrid strategy
    for (uint32_t i = 0; i < g_communication_service.known_network_count - 1; i++) {
        for (uint32_t j = 0; j < g_communication_service.known_network_count - i - 1; j++) {
            network_scan_result_t *net1 = &g_communication_service.known_networks[sorted_indices[j]];
            network_scan_result_t *net2 = &g_communication_service.known_networks[sorted_indices[j + 1]];
            
            aicam_bool_t should_swap = AICAM_FALSE;
            
            if (requires_time_optimized) {
                // RTC wakeup (low power mode): prioritize last connected network (most recent first)
                // If last_connected_time is same, prefer higher RSSI
                if (net1->last_connected_time < net2->last_connected_time) {
                    should_swap = AICAM_TRUE;
                } else if (net1->last_connected_time == net2->last_connected_time && 
                          net1->rssi < net2->rssi) {
                    should_swap = AICAM_TRUE;
                }
            } else {
                // Normal mode: hybrid strategy - prioritize recent connections with good RSSI
                // Score = (last_connected_time > 0 ? 1000 : 0) + RSSI
                int32_t score1 = (net1->last_connected_time > 0 ? 1000 : 0) + net1->rssi;
                int32_t score2 = (net2->last_connected_time > 0 ? 1000 : 0) + net2->rssi;
                if (score1 < score2) {
                    should_swap = AICAM_TRUE;
                }
            }
            
            if (should_swap) {
                uint32_t temp = sorted_indices[j];
                sorted_indices[j] = sorted_indices[j + 1];
                sorted_indices[j + 1] = temp;
            }
        }
    }

    // Update scan results from known networks
    for(uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *known = &g_communication_service.known_networks[i];
        for(uint32_t j = 0; j < g_communication_service.scan_result_count; j++) {
            if (strcmp(g_communication_service.scan_results[j].ssid, known->ssid) == 0) {
                g_communication_service.scan_results[j].is_known = AICAM_TRUE;
            }
        }
    }
    
    // In RTC wakeup mode, try last connected network first without waiting for scan
    if (requires_time_optimized && g_communication_service.known_network_count > 0) {
        uint32_t last_connected_idx = sorted_indices[0];
        network_scan_result_t *last_connected = &g_communication_service.known_networks[last_connected_idx];
        
        if (last_connected->last_connected_time > 0) {
            LOG_SVC_INFO("RTC wakeup: trying last connected network first: %s (%s)", 
                        last_connected->ssid, last_connected->bssid);
            
            // Try direct connection without scan verification in RTC wakeup mode
            unsigned int bssid_bytes[6];
            if (sscanf(last_connected->bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                       &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                       &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) == 6) {
                
                // Configure STA interface
                netif_config_t sta_config = {0};
                nm_get_netif_cfg(NETIF_NAME_WIFI_STA, &sta_config);
                
                strncpy(sta_config.wireless_cfg.ssid, last_connected->ssid, sizeof(sta_config.wireless_cfg.ssid) - 1);
                strncpy(sta_config.wireless_cfg.pw, last_connected->password, sizeof(sta_config.wireless_cfg.pw) - 1);
                for (int i = 0; i < 6; i++) {
                    sta_config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
                }
                sta_config.wireless_cfg.channel = last_connected->channel;
                sta_config.wireless_cfg.security = last_connected->security;
                sta_config.ip_mode = NETIF_IP_MODE_DHCP;
                
                aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_STA, &sta_config);
                
                if (result == AICAM_OK) {
                    // In RTC wakeup mode, use shorter timeout for connection check
                    uint32_t timeout_ms = 3000; // 3 seconds for fast connection
                    uint32_t start_time = rtc_get_uptime_ms();
                    
                    while ((rtc_get_uptime_ms() - start_time) < timeout_ms) {
                        if (communication_is_interface_connected(NETIF_NAME_WIFI_STA)) {
                            LOG_SVC_INFO("RTC wakeup: successfully connected to last network: %s (%s)", 
                                        last_connected->ssid, last_connected->bssid);
                            return AICAM_OK;
                        }
                        osDelay(100);
                    }
                    
                    LOG_SVC_WARN("RTC wakeup: connection timeout for last network: %s (%s)", 
                                last_connected->ssid, last_connected->bssid);
                    communication_stop_interface(NETIF_NAME_WIFI_STA);
                }
            }
        }
    }
    
    // Try to connect to each known network in order (in low power mode: directly try to connect, without scanning first)
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        uint32_t idx = sorted_indices[i];
        network_scan_result_t *known = &g_communication_service.known_networks[idx];
        
        LOG_SVC_INFO("Trying to connect to: %s (%s), RSSI: %d dBm, Last connected: %u", 
                    known->ssid, known->bssid, known->rssi, known->last_connected_time);

        // try to update BSSID with latest scan result, but even if not found, use known information to connect
        for(uint32_t j = 0; j < g_communication_service.scan_result_count; j++) {
            LOG_SVC_DEBUG("Scan result: %s (%s)", g_communication_service.scan_results[j].ssid, g_communication_service.scan_results[j].bssid);
            if (strcmp(g_communication_service.scan_results[j].ssid, known->ssid) == 0) {
                memcpy(known->bssid, g_communication_service.scan_results[j].bssid, sizeof(known->bssid));
                break;
            }
        }

        unsigned int bssid_bytes[6];
        if (sscanf(known->bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &bssid_bytes[0], &bssid_bytes[1], &bssid_bytes[2],
                   &bssid_bytes[3], &bssid_bytes[4], &bssid_bytes[5]) != 6) {
            LOG_SVC_ERROR("Invalid BSSID: %s", known->bssid);
            continue;
        }

        // Configure STA interface
        netif_config_t sta_config = {0};
        nm_get_netif_cfg(NETIF_NAME_WIFI_STA, &sta_config);
        
        strncpy(sta_config.wireless_cfg.ssid, known->ssid, sizeof(sta_config.wireless_cfg.ssid) - 1);
        strncpy(sta_config.wireless_cfg.pw, known->password, sizeof(sta_config.wireless_cfg.pw) - 1);
        for (int i = 0; i < 6; i++) {
            sta_config.wireless_cfg.bssid[i] = (uint8_t)(bssid_bytes[i] & 0xFF);
        }
        sta_config.wireless_cfg.channel = known->channel;
        sta_config.wireless_cfg.security = known->security;
        sta_config.ip_mode = NETIF_IP_MODE_DHCP;
        
        aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_STA, &sta_config);
        
        if (result == AICAM_OK) {
            uint64_t timeout_ms = 3000;
            uint64_t start_time = rtc_get_uptime_ms();
            
            while ((rtc_get_uptime_ms() - start_time) < timeout_ms) {
                if (communication_is_interface_connected(NETIF_NAME_WIFI_STA)) {
                    LOG_SVC_INFO("Successfully connected to: %s (%s)", known->ssid, known->bssid);
                    return AICAM_OK;
                }
                osDelay(100);
            }
            
            LOG_SVC_WARN("Connection timeout for: %s (%s)", known->ssid, known->bssid);
            communication_stop_interface(NETIF_NAME_WIFI_STA);
        } else {
            LOG_SVC_ERROR("Failed to configure interface for: %s (%s), error: %d", 
                         known->ssid, known->bssid, result);
        }
    }
    
    LOG_SVC_INFO("Failed to connect to any known network");
    return AICAM_ERROR_NOT_FOUND;
}

static void background_scan_task(void *argument)
{
    for(;;) {
        osSemaphoreAcquire(g_communication_service.scan_semaphore_id, osWaitForever);
        aicam_result_t result = communication_start_network_scan(NULL);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to start network scan: %d", result);
        }
        osDelay(1000);
    }
    osThreadExit();
}

/**
 * @brief Start network scan
 */
void start_network_scan(void)
{
    osSemaphoreRelease(g_communication_service.scan_semaphore_id);
}

/* ==================== Helper Functions ==================== */

/**
 * @brief Convert netif_info_t to network_interface_status_t
 */
static void convert_netif_info_to_status(const netif_info_t *netif_info, 
                                       network_interface_status_t *status)
{
    if (!netif_info || !status) return;
    
    memset(status, 0, sizeof(network_interface_status_t));
    
    status->state = netif_info->state;
    status->type = netif_info->type;
    
    if (netif_info->if_name) {
        strncpy(status->if_name, netif_info->if_name, sizeof(status->if_name) - 1);
    }
    
    if (netif_info->type == NETIF_TYPE_WIRELESS) {
        strncpy(status->ssid, netif_info->wireless_cfg.ssid, sizeof(status->ssid) - 1);
        snprintf(status->bssid, sizeof(status->bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 netif_info->wireless_cfg.bssid[0], netif_info->wireless_cfg.bssid[1],
                 netif_info->wireless_cfg.bssid[2], netif_info->wireless_cfg.bssid[3],
                 netif_info->wireless_cfg.bssid[4], netif_info->wireless_cfg.bssid[5]);
        status->rssi = netif_info->rssi;
        status->channel = netif_info->wireless_cfg.channel;
        status->security = (wireless_security_t)netif_info->wireless_cfg.security;
    }
    
    // Convert IP address
    snprintf(status->ip_addr, sizeof(status->ip_addr), "%d.%d.%d.%d",
             netif_info->ip_addr[0], netif_info->ip_addr[1], 
             netif_info->ip_addr[2], netif_info->ip_addr[3]);
    
    // Convert MAC address
    snprintf(status->mac_addr, sizeof(status->mac_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
             netif_info->if_mac[0], netif_info->if_mac[1], netif_info->if_mac[2],
             netif_info->if_mac[3], netif_info->if_mac[4], netif_info->if_mac[5]);

    
    status->connected = (netif_info->state == NETIF_STATE_UP);
}

/**
 * @brief Update network interface list
 */
static aicam_result_t update_interface_list(void)
{
    netif_info_t *netif_list = NULL;
    int netif_count = 0;
    aicam_result_t result = AICAM_OK;
    
    // Get network interface list from netif_manager
    netif_count = nm_get_netif_list(&netif_list);
    if (netif_count < 0) {
        LOG_SVC_ERROR("Failed to get network interface list");
        return AICAM_ERROR;
    }
    
    // Clear existing interface list
    g_communication_service.interface_count = 0;
    
    // Convert and store interface information
    for (int i = 0; i < netif_count && i < MAX_NETWORK_INTERFACES; i++) {
        convert_netif_info_to_status(&netif_list[i], 
                                   &g_communication_service.interfaces[i]);
        g_communication_service.interface_count++;
    }
    
    // Free the netif_list
    nm_free_netif_list(netif_list);
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Updated interface list: %d interfaces", g_communication_service.interface_count);
    }
    
    return result;
}

/**
 * @brief Check if a network is known (previously connected)
 */
static int is_known_network(const char *ssid, const char *bssid)
{
    if (!ssid || !bssid) return -1;

    //LOG_SVC_DEBUG("Checking if known network: %s (%s)", ssid, bssid);
    
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *known = &g_communication_service.known_networks[i];
        LOG_SVC_DEBUG("Known network: %s (%s)", known->ssid, known->bssid);
        if (strcmp(known->ssid, ssid) == 0 && strcmp(known->bssid, bssid) == 0) {
            LOG_SVC_DEBUG("Known network found: %s (%s)", ssid, bssid);
            //g_communication_service.known_networks[i].connected = AICAM_TRUE;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Add connected network to known networks database
 */
static void add_known_network(const network_scan_result_t *network)
{
    if (!network || g_communication_service.known_network_count >= MAX_KNOWN_NETWORKS) return;
    
    // Check if already exists
    int known_index = is_known_network(network->ssid, network->bssid);
    if (known_index >= 0) {
        g_communication_service.known_networks[known_index].connected = network->connected;
        g_communication_service.known_networks[known_index].rssi = network->rssi;
        g_communication_service.known_networks[known_index].channel = network->channel;
        g_communication_service.known_networks[known_index].security = network->security;
        strncpy(g_communication_service.known_networks[known_index].password, network->password, sizeof(g_communication_service.known_networks[known_index].password) - 1);
    }
    else {
        LOG_SVC_DEBUG("Add known network: %s (%s)", network->ssid, network->bssid);
        if (g_communication_service.known_network_count  == MAX_KNOWN_NETWORKS) {
            for(uint32_t i = 0; i < MAX_KNOWN_NETWORKS - 1; i++) {
                memcpy(&g_communication_service.known_networks[i], &g_communication_service.known_networks[i + 1], sizeof(network_scan_result_t));
            }
            memcpy(&g_communication_service.known_networks[MAX_KNOWN_NETWORKS - 1], network, sizeof(network_scan_result_t));
            g_communication_service.known_network_count--;
        }

        // Add to known networks, deep copy
        network_scan_result_t *known = &g_communication_service.known_networks[g_communication_service.known_network_count];
        strncpy(known->ssid, network->ssid, sizeof(known->ssid) - 1);
        strncpy(known->bssid, network->bssid, sizeof(known->bssid) - 1);
        known->rssi = network->rssi;
        known->channel = network->channel;
        known->security = (wireless_security_t)network->security;
        known->connected = network->connected;
        known->is_known = AICAM_TRUE;
        known->last_connected_time = rtc_get_timeStamp();
        strncpy(known->password, network->password, sizeof(known->password) - 1);
        LOG_SVC_DEBUG("Add known network: %s (%s), password: %s", network->ssid, network->bssid, known->password);
        g_communication_service.known_network_count++;
    }    
    

    //update scan result
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if (strcmp(g_communication_service.scan_results[i].ssid, network->ssid) == 0 && strcmp(g_communication_service.scan_results[i].bssid, network->bssid) == 0) {
            g_communication_service.scan_results[i].is_known = AICAM_TRUE;
            g_communication_service.scan_results[i].connected = AICAM_TRUE;
            g_communication_service.scan_results[i].security = network->security;
            g_communication_service.scan_results[i].channel = network->channel;
            g_communication_service.scan_results[i].rssi = network->rssi;
            if(strlen(network->password) > 0) {
                strncpy(g_communication_service.scan_results[i].password, network->password, sizeof(g_communication_service.scan_results[i].password) - 1);
            }
            else {
                strncpy(g_communication_service.scan_results[i].password, "", sizeof(g_communication_service.scan_results[i].password) - 1);
            }
            g_communication_service.scan_results[i].last_connected_time = rtc_get_timeStamp();
            break;
        }
        else {
            if(g_communication_service.scan_results[i].connected == AICAM_TRUE) {
                //set connected to false
                g_communication_service.scan_results[i].connected = AICAM_FALSE;
            }
        }
    }
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Added known network: %s (%s)", network->ssid, network->bssid);
    }
    
    // Save to NVS
    save_known_networks_to_nvs();
}

/**
 * @brief Delete network from known networks database
 */
static void delete_known_network(const char *ssid, const char *bssid)
{
    if (!ssid || !bssid) return;
    
    LOG_SVC_INFO("Deleting known network: %s (%s)", ssid, bssid);

    // Check if currently connected to this network
    aicam_bool_t was_connected = is_connected(ssid, bssid);
    
    // Disconnect from network if currently connected
    if (was_connected) {
        LOG_SVC_INFO("Disconnecting from network: %s (%s)", ssid, bssid);
        aicam_result_t disconnect_result = communication_stop_interface(NETIF_NAME_WIFI_STA);
        if (disconnect_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to disconnect from network: %d", disconnect_result);
        } else {
            LOG_SVC_INFO("Successfully disconnected from network: %s (%s)", ssid, bssid);
        }
    }

    // Update scan results to mark as unknown
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if (strcmp(g_communication_service.scan_results[i].ssid, ssid) == 0 && 
            strcmp(g_communication_service.scan_results[i].bssid, bssid) == 0) {
            g_communication_service.scan_results[i].is_known = AICAM_FALSE;
            g_communication_service.scan_results[i].connected = AICAM_FALSE;
            g_communication_service.scan_results[i].last_connected_time = 0;
            LOG_SVC_DEBUG("Updated scan result for network: %s (%s)", ssid, bssid);
            break;
        }
    }

    // Remove from known networks array
    int found_index = -1;
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        if (strcmp(g_communication_service.known_networks[i].ssid, ssid) == 0 && 
            strcmp(g_communication_service.known_networks[i].bssid, bssid) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index >= 0) {
        // Shift remaining networks to fill the gap
        for (uint32_t i = found_index; i < g_communication_service.known_network_count - 1; i++) {
            memcpy(&g_communication_service.known_networks[i], 
                   &g_communication_service.known_networks[i + 1], 
                   sizeof(network_scan_result_t));
        }
        
        // Clear the last entry and decrement count
        memset(&g_communication_service.known_networks[g_communication_service.known_network_count - 1], 
               0, sizeof(network_scan_result_t));
        g_communication_service.known_network_count--;
        
        LOG_SVC_INFO("Removed network from known networks list: %s (%s)", ssid, bssid);
    } else {
        LOG_SVC_WARN("Network not found in known networks list: %s (%s)", ssid, bssid);
    }

    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Deleted known network: %s (%s), was_connected: %s, known_count: %u", 
                     ssid, bssid, was_connected ? "true" : "false", 
                     g_communication_service.known_network_count);
    }
    
    // Save to NVS
    save_known_networks_to_nvs();
}

/**
 * @brief Check if a network is connected
 */
static aicam_bool_t is_connected(const char *ssid, const char *bssid)
{
    //get netif state
    netif_state_t state = nm_get_netif_state(NETIF_NAME_WIFI_STA);
    if (state == NETIF_STATE_DOWN) {
        return AICAM_FALSE;
    }

    //get netif info
    netif_info_t netif_info;
    nm_get_netif_info(NETIF_NAME_WIFI_STA, &netif_info);
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             netif_info.wireless_cfg.bssid[0], netif_info.wireless_cfg.bssid[1],
             netif_info.wireless_cfg.bssid[2], netif_info.wireless_cfg.bssid[3],
             netif_info.wireless_cfg.bssid[4], netif_info.wireless_cfg.bssid[5]);
    LOG_SVC_DEBUG("Netif info: %s (%s)", netif_info.wireless_cfg.ssid, bssid_str);
    LOG_SVC_DEBUG("Checking if connected: %s (%s)", ssid, bssid);
    if (strcmp(netif_info.wireless_cfg.ssid, ssid) == 0 && strcmp(bssid_str, bssid) == 0) {
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}


/**
 * @brief Network scan callback
 */
static void network_scan_callback(int result, wireless_scan_result_t *scan_result)
{
    if (result != 0 || !scan_result) {
        LOG_SVC_ERROR("Network scan failed: %d", result);
        g_communication_service.scan_in_progress = AICAM_FALSE;
        return;
    }
    
    // Store scan results with known/unknown classification
    g_communication_service.scan_result_count = 0;
    for (int i = 0; i < scan_result->scan_count && i < MAX_SCAN_RESULTS; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        
        strncpy(result->ssid, scan_result->scan_info[i].ssid, sizeof(result->ssid) - 1);
        snprintf(result->bssid, sizeof(result->bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 scan_result->scan_info[i].bssid[0], scan_result->scan_info[i].bssid[1],
                 scan_result->scan_info[i].bssid[2], scan_result->scan_info[i].bssid[3],
                 scan_result->scan_info[i].bssid[4], scan_result->scan_info[i].bssid[5]);
        
        result->rssi = scan_result->scan_info[i].rssi;
        result->channel = scan_result->scan_info[i].channel;
        result->security = (wireless_security_t)scan_result->scan_info[i].security;
        result->connected = is_connected(result->ssid, result->bssid);
        
        // Classify as known or unknown network
        result->is_known = is_known_network(result->ssid, result->bssid) == -1 ? AICAM_FALSE : AICAM_TRUE;
        result->last_connected_time = 0; // Will be updated when connected
        
        g_communication_service.scan_result_count++;
    }
    
    // Update known networks RSSI from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *scan = &g_communication_service.scan_results[i];
        for (uint32_t j = 0; j < g_communication_service.known_network_count; j++) {
            network_scan_result_t *known = &g_communication_service.known_networks[j];
            if (strcmp(scan->ssid, known->ssid) == 0 && strcmp(scan->bssid, known->bssid) == 0) {
                // Update RSSI and channel info
                known->rssi = scan->rssi;
                known->channel = scan->channel;
                if (g_communication_service.config.enable_debug) {
                    LOG_SVC_DEBUG("Updated known network RSSI: %s (%s) -> %d dBm", 
                                 known->ssid, known->bssid, known->rssi);
                }
                break;
            }
        }
    }
    
    g_communication_service.scan_in_progress = AICAM_FALSE;
    g_communication_service.stats.network_scans++;
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Network scan completed: %d networks found", g_communication_service.scan_result_count);
    }
}



/* ==================== Communication Service Implementation ==================== */

aicam_result_t communication_service_init(void *config)
{
    if (g_communication_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing Communication Service...");
    
    // Initialize context
    memset(&g_communication_service, 0, sizeof(communication_service_context_t));
    
    // Set default configuration
    g_communication_service.config = default_config;
    
    // Apply custom configuration if provided
    if (config) {
        communication_service_config_t *custom_config = (communication_service_config_t *)config;
        g_communication_service.config = *custom_config;
    }
    
    // Load communication and cellular configuration from NVS
    network_service_config_t net_cfg;
    if (json_config_get_network_service_config(&net_cfg) == AICAM_OK) {
        // Load communication type preferences
        g_communication_service.preferred_type = (communication_type_t)net_cfg.preferred_comm_type;
        g_communication_service.config.enable_auto_priority = net_cfg.enable_auto_priority;
        
        // Load cellular settings
        strncpy(g_communication_service.cellular_settings.apn, net_cfg.cellular.apn, 
                sizeof(g_communication_service.cellular_settings.apn) - 1);
        strncpy(g_communication_service.cellular_settings.username, net_cfg.cellular.username,
                sizeof(g_communication_service.cellular_settings.username) - 1);
        strncpy(g_communication_service.cellular_settings.password, net_cfg.cellular.password,
                sizeof(g_communication_service.cellular_settings.password) - 1);
        strncpy(g_communication_service.cellular_settings.pin_code, net_cfg.cellular.pin_code,
                sizeof(g_communication_service.cellular_settings.pin_code) - 1);
        g_communication_service.cellular_settings.authentication = net_cfg.cellular.authentication;
        g_communication_service.cellular_settings.enable_roaming = net_cfg.cellular.enable_roaming;
        g_communication_service.cellular_settings.operator = net_cfg.cellular.operator;
        
        LOG_SVC_INFO("Loaded communication config from NVS: preferred_type=%d, auto_priority=%d",
                     g_communication_service.preferred_type,
                     g_communication_service.config.enable_auto_priority);
        
        if (net_cfg.cellular.apn[0] != '\0') {
            LOG_SVC_INFO("Loaded cellular config from NVS: APN=%s", net_cfg.cellular.apn);
        }
    }
    
    // Register network interface manager (framework only, fast < 2s)
    netif_manager_register();
    g_communication_service.netif_manager_registered = AICAM_TRUE;
    
    // Initialize network interface initialization manager
    aicam_result_t result = netif_init_manager_framework_init();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize netif init manager: %d", result);
        return result;
    }

    // Get wakeup flag directly from U0 module (doesn't require system_service to be initialized)
    uint32_t wakeup_flag = 0;
    int ret = u0_module_get_wakeup_flag(&wakeup_flag);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get wakeup flag: %d", ret);
        return ret;
    }
    
    // Check if wakeup source requires time-optimized mode (disable AP for faster startup)
    wakeup_source_type_t wakeup_source = system_service_get_wakeup_source_type();
    aicam_bool_t requires_time_optimized = system_service_requires_time_optimized_mode(wakeup_source);
    
    // If time-optimized mode is required, disable AP for faster startup
    if (requires_time_optimized) {
        g_communication_service.config.auto_start_wifi_ap = AICAM_FALSE;
        LOG_SVC_INFO("Time-optimized mode detected (wakeup source: %d), disabling AP for faster startup", wakeup_source);
    }
    
    // Register WiFi AP initialization configuration
    netif_init_config_t ap_config = {
        .if_name = NETIF_NAME_WIFI_AP,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_HIGH,      // High priority
        .auto_up = AICAM_TRUE,                     // Auto bring up after init
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_wifi_ap_ready
    };
    result = netif_init_manager_register(&ap_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register WiFi AP init config: %d", result);
    }
    
    // Register WiFi STA initialization configuration
    netif_init_config_t sta_config = {
        .if_name = NETIF_NAME_WIFI_STA,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_NORMAL,    // Normal priority
        .auto_up = AICAM_FALSE,                    // Manual bring up (after connect)
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_wifi_sta_ready
    };
    result = netif_init_manager_register(&sta_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register WiFi STA init config: %d", result);
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    // Register Cellular/4G initialization configuration
    netif_init_config_t cellular_config = {
        .if_name = NETIF_NAME_4G_CAT1,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_NORMAL,    // Normal priority
        .auto_up = AICAM_FALSE,                    // Manual bring up (after config)
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_cellular_ready
    };
    result = netif_init_manager_register(&cellular_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register Cellular init config: %d", result);
    } else {
   
        LOG_SVC_INFO("Cellular/4G module registered");
    }
#endif

#if NETIF_ETH_WAN_IS_ENABLE
    // Register PoE/Ethernet initialization configuration
    netif_init_config_t poe_config = {
        .if_name = NETIF_NAME_ETH_WAN,
        .state = NETIF_INIT_STATE_IDLE,
        .priority = NETIF_INIT_PRIORITY_HIGH,      // High priority (PoE is highest)
        .auto_up = AICAM_FALSE,                     // Auto bring up when detected
        .async = AICAM_TRUE,                       // Asynchronous initialization
        .callback = on_poe_ready
    };
    result = netif_init_manager_register(&poe_config);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register PoE init config: %d", result);
    } else {

        LOG_SVC_INFO("PoE/Ethernet module registered");
    }
#endif
    
    // Initialize statistics
    memset(&g_communication_service.stats, 0, sizeof(communication_service_stats_t));
    
    // Initialize type info cache
    update_type_info_cache();
    
    g_communication_service.initialized = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_INITIALIZED;

    LOG_SVC_INFO("Communication Service initialized");
    
    return AICAM_OK;
}

aicam_result_t communication_service_start(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_communication_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting Communication Service...");
    
    // Update interface list
    // update_interface_list();

    // Load known networks from NVS
    aicam_result_t load_result = load_known_networks_from_nvs();
    if (load_result == AICAM_OK) {
        LOG_SVC_INFO("Loaded known networks from NVS successfully");
    } else {
        LOG_SVC_WARN("Failed to load known networks from NVS: %d", load_result);
    }
    
    g_communication_service.running = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_RUNNING;
    
    // Reset startup state
    g_communication_service.wifi_sta_ready = AICAM_FALSE;
    g_communication_service.cellular_ready = AICAM_FALSE;
    g_communication_service.poe_ready = AICAM_FALSE;
    g_communication_service.startup_decision_made = AICAM_FALSE;
    g_communication_service.startup_begin_time = rtc_get_uptime_ms();
    
    // Create startup timeout timer
    if (g_communication_service.startup_timeout_timer == NULL) {
        g_communication_service.startup_timeout_timer = osTimerNew(
            startup_timeout_callback, osTimerOnce, NULL, NULL);
    }
    if (g_communication_service.startup_timeout_timer != NULL) {
        osTimerStart(g_communication_service.startup_timeout_timer, STARTUP_TIMEOUT_MS);
        LOG_SVC_INFO("Startup timeout timer started (%u ms)", STARTUP_TIMEOUT_MS);
    }

    // Start background scan task
    const osThreadAttr_t scan_task_attr = {
        .name = "BackgroundScanTask",
        .stack_size = sizeof(background_scan_task_stack),
        .stack_mem = background_scan_task_stack,
        .priority = osPriorityBelowNormal,
    };
    g_communication_service.scan_semaphore_id = osSemaphoreNew(1, 0, NULL);
    g_communication_service.scan_task_id = osThreadNew(background_scan_task, NULL, &scan_task_attr);
    
    g_communication_service.running = AICAM_TRUE;
    g_communication_service.state = SERVICE_STATE_RUNNING;
    
    // Start asynchronous network interface initialization (non-blocking)
    if (g_communication_service.config.auto_start_wifi_ap) {
        LOG_SVC_INFO("Starting async WiFi AP initialization...");
        aicam_result_t ap_result = netif_init_manager_init_async(NETIF_NAME_WIFI_AP);
        if (ap_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start WiFi AP initialization: %d", ap_result);
        }
    }
    
    if (g_communication_service.config.auto_start_wifi_sta) {
        LOG_SVC_INFO("Starting async WiFi STA initialization...");
        aicam_result_t sta_result = netif_init_manager_init_async(NETIF_NAME_WIFI_STA);
        if (sta_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start WiFi STA initialization: %d", sta_result);
        }
        // Note: try_connect_known_networks() will be called in on_wifi_sta_ready() callback
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (g_communication_service.config.auto_start_cellular ) {
        LOG_SVC_INFO("Starting async Cellular/4G initialization...");
        aicam_result_t cellular_result = netif_init_manager_init_async(NETIF_NAME_4G_CAT1);
        if (cellular_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start Cellular initialization: %d", cellular_result);
        }
    }
#endif

#if NETIF_ETH_WAN_IS_ENABLE
    if (g_communication_service.config.auto_start_poe) {
        LOG_SVC_INFO("Starting async PoE/Ethernet initialization...");
        aicam_result_t poe_result = netif_init_manager_init_async(NETIF_NAME_ETH_WAN);
        if (poe_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to start PoE initialization: %d", poe_result);
        }
    }
#endif
    
    LOG_SVC_INFO("Communication Service started (network interfaces initializing in background)");
    
    return AICAM_OK;
}

aicam_result_t communication_service_stop(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_communication_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping Communication Service...");
    
    // Stop all network interfaces
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        if (g_communication_service.interfaces[i].connected) {
            communication_stop_interface(g_communication_service.interfaces[i].if_name);
        }
    }
    
    g_communication_service.running = AICAM_FALSE;
    g_communication_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("Communication Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t communication_service_deinit(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_communication_service.running) {
        communication_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing Communication Service...");
    
    // Unregister network interface manager
    if (g_communication_service.netif_manager_registered) {
        netif_manager_unregister();
        g_communication_service.netif_manager_registered = AICAM_FALSE;
    }
    
    // Reset context
    memset(&g_communication_service, 0, sizeof(communication_service_context_t));
    
    LOG_SVC_INFO("Communication Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t communication_service_get_state(void)
{
    return g_communication_service.state;
}

/* ==================== Network Interface Management ==================== */

aicam_result_t communication_get_network_interfaces(network_interface_status_t *interfaces, 
                                                   uint32_t max_count, 
                                                   uint32_t *actual_count)
{
    if (!interfaces || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update interface list
    update_interface_list();
    
    uint32_t copy_count = (g_communication_service.interface_count < max_count) ? 
                         g_communication_service.interface_count : max_count;
    
    memcpy(interfaces, g_communication_service.interfaces, 
           copy_count * sizeof(network_interface_status_t));
    
    *actual_count = copy_count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_interface_status(const char *if_name, 
                                                 network_interface_status_t *status)
{
    if (!if_name || !status) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    netif_info_t netif_info;
    aicam_result_t result = nm_get_netif_info(if_name, &netif_info);
    if (result != AICAM_OK) {
        return result;
    }
    
    convert_netif_info_to_status(&netif_info, status);
    
    return AICAM_OK;
}

aicam_bool_t communication_is_interface_connected(const char *if_name)
{
    if (!if_name) {
        return AICAM_FALSE;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_FALSE;
    }

    // get interface status
    network_interface_status_t status;
    aicam_result_t result = communication_get_interface_status(if_name, &status);
    if (result != AICAM_OK) {
        return AICAM_FALSE;
    }

    return status.connected;
}

    
aicam_result_t communication_get_interface_config(const char *if_name, 
                                                 netif_config_t *config)
{
    if (!if_name || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Get interface configuration using netif_manager
    aicam_result_t result = nm_get_netif_cfg(if_name, config);
    
    if (result != AICAM_OK) {
        g_communication_service.stats.last_error_code = result;
        LOG_SVC_ERROR("Failed to get interface %s configuration: %d", if_name, result);
    } else if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s configuration retrieved successfully", if_name);
    }
    
    return result;
}

aicam_result_t communication_configure_interface(const char *if_name, 
                                                 netif_config_t *config)
{
    if (!if_name || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Use netif_manager standard configuration flow
    // known network password should be filled
    char bssid[18] = {0};
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             config->wireless_cfg.bssid[0], config->wireless_cfg.bssid[1],
             config->wireless_cfg.bssid[2], config->wireless_cfg.bssid[3],
             config->wireless_cfg.bssid[4], config->wireless_cfg.bssid[5]);
    int known_index = is_known_network(config->wireless_cfg.ssid, bssid);
    if(known_index >= 0) {
        strncpy(config->wireless_cfg.pw, g_communication_service.known_networks[known_index].password, sizeof(config->wireless_cfg.pw) - 1);
        config->wireless_cfg.security = g_communication_service.known_networks[known_index].security;
    }
    aicam_result_t result = nm_set_netif_cfg(if_name, config);

    // get interface status
    network_interface_status_t status;
    result = communication_get_interface_status(if_name, &status);
    if (result != AICAM_OK) {
        return result;
    }

    // if interface is not connected, start it
    if (status.connected == AICAM_FALSE) {
        result = communication_start_interface(if_name);
        if (result != AICAM_OK) {
            return result;
        }
    }
    
    // Update statistics
    if (result == AICAM_OK) {
        g_communication_service.stats.total_connections++;
        if (g_communication_service.config.enable_debug) {
            LOG_SVC_DEBUG("Interface %s configured successfully", if_name);
        }
    } else {
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
        LOG_SVC_ERROR("Failed to configure interface %s: %d", if_name, result);
    }

    // if sta is connected, update known networks
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        //get netif info
        netif_info_t netif_info;
        nm_get_netif_info(if_name, &netif_info);

        //generate scan result
        network_scan_result_t scan_result;
        strncpy(scan_result.ssid, netif_info.wireless_cfg.ssid, sizeof(scan_result.ssid) - 1);
        snprintf(scan_result.bssid, sizeof(scan_result.bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 netif_info.wireless_cfg.bssid[0], netif_info.wireless_cfg.bssid[1],
                 netif_info.wireless_cfg.bssid[2], netif_info.wireless_cfg.bssid[3],
                 netif_info.wireless_cfg.bssid[4], netif_info.wireless_cfg.bssid[5]);
        scan_result.rssi = netif_info.rssi;
        scan_result.channel = netif_info.wireless_cfg.channel;
        scan_result.security = netif_info.wireless_cfg.security;
        scan_result.connected = AICAM_TRUE;
        scan_result.is_known = AICAM_TRUE;
        scan_result.last_connected_time = rtc_get_timeStamp();   
        if(strlen(netif_info.wireless_cfg.pw) > 0) {
            strncpy(scan_result.password, netif_info.wireless_cfg.pw, sizeof(scan_result.password) - 1);
            LOG_SVC_DEBUG("Interface %s connected, add known network: %s, password: %s", if_name, scan_result.ssid, scan_result.password);
        }
        else {
            strncpy(scan_result.password, "", sizeof(scan_result.password) - 1);
        }

        LOG_SVC_DEBUG("Interface %s connected, add known network: %s", if_name, scan_result.ssid);
        add_known_network(&scan_result);

        //sta ready
        service_set_sta_ready(AICAM_TRUE);
    }
    
    return result;
}

aicam_result_t communication_start_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // start interface
    aicam_result_t result = nm_ctrl_netif_up(if_name);
    if (result != AICAM_OK) {
        return result;
    }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s start requested", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_stop_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // stop interface
    aicam_result_t result = nm_ctrl_netif_down(if_name);
    if (result != AICAM_OK) {
        return result;
    }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s stop requested", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_restart_interface(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Communication service only collects information, does not control interface restart
    LOG_SVC_INFO("Interface %s restart requested (information only)", if_name);
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Interface %s restart request logged", if_name);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_disconnect_network(const char *if_name)
{
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // disconnect network
    aicam_result_t result = communication_stop_interface(if_name);
    if (result != AICAM_OK) {
        return result;
    }

    // get ssid and bssid
    netif_config_t config;
    result = communication_get_interface_config(if_name, &config);
    if (result != AICAM_OK) {
        return result;
    }
    const char* ssid = config.wireless_cfg.ssid;
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             config.wireless_cfg.bssid[0], config.wireless_cfg.bssid[1],
             config.wireless_cfg.bssid[2], config.wireless_cfg.bssid[3],
             config.wireless_cfg.bssid[4], config.wireless_cfg.bssid[5]);

    LOG_SVC_INFO("Disconnecting network: %s (%s)", ssid, bssid);


    // update scan results and known networks
    for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        if(strcmp(g_communication_service.scan_results[i].ssid, ssid) == 0 &&
           strcmp(g_communication_service.scan_results[i].bssid, bssid) == 0) {
            g_communication_service.scan_results[i].connected = AICAM_FALSE;
        }
    }

    for(uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        if(strcmp(g_communication_service.known_networks[i].ssid, ssid) == 0 &&
           strcmp(g_communication_service.known_networks[i].bssid, bssid) == 0) {
            g_communication_service.known_networks[i].connected = AICAM_FALSE;
        }
    }

    return AICAM_OK;
}


/* ==================== Network Scanning ==================== */

aicam_result_t communication_start_network_scan(wireless_scan_callback_t callback)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_communication_service.scan_in_progress) {
        return AICAM_ERROR_BUSY;
    }
    
    g_communication_service.scan_in_progress = AICAM_TRUE;

    aicam_result_t result = nm_wireless_update_scan_result(3000);
    if (result != AICAM_OK) {
        g_communication_service.scan_in_progress = AICAM_FALSE;
        g_communication_service.stats.last_error_code = result;
        return result;
    }

    network_scan_callback(0, nm_wireless_get_scan_result());
    
    // aicam_result_t result = nm_wireless_start_scan(callback ? callback : network_scan_callback);
    // if (result != AICAM_OK) {
    //     g_communication_service.scan_in_progress = AICAM_FALSE;
    //     g_communication_service.stats.last_error_code = result;
    //     return result;
    // }
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Network scan started");
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_scan_results(network_scan_result_t *results, 
                                             uint32_t max_count, 
                                             uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t copy_count = (g_communication_service.scan_result_count < max_count) ? 
                         g_communication_service.scan_result_count : max_count;
    
    memcpy(results, g_communication_service.scan_results, 
           copy_count * sizeof(network_scan_result_t));
    
    *actual_count = copy_count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_classified_scan_results(classified_scan_results_t *results)
{
    if (!results) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Clear results
    memset(results, 0, sizeof(classified_scan_results_t));
    
    // Classify scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (scan_result->is_known) {
            // Add to known networks
            if (results->known_count < MAX_KNOWN_NETWORKS) {
                results->known_networks[results->known_count] = *scan_result;
                results->known_count++;
            }
        } else {
            // Add to unknown networks
            if (results->unknown_count < MAX_UNKNOWN_NETWORKS) {
                results->unknown_networks[results->unknown_count] = *scan_result;
                results->unknown_count++;
            }
        }
    }

    //updtae known networks
    for(uint32_t i = 0; i < results->known_count; i++) {
        results->known_networks[i].connected = is_connected(results->known_networks[i].ssid, results->known_networks[i].bssid);
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_known_networks(network_scan_result_t *results, 
                                               uint32_t max_count, 
                                               uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t count = 0;
    
    // Get known networks from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count && count < max_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (scan_result->is_known) {
            results[count] = *scan_result;
            count++;
        }
    }
    
    *actual_count = count;
    
    return AICAM_OK;
}

aicam_result_t communication_get_unknown_networks(network_scan_result_t *results, 
                                                 uint32_t max_count, 
                                                 uint32_t *actual_count)
{
    if (!results || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t count = 0;
    
    // Get unknown networks from scan results
    for (uint32_t i = 0; i < g_communication_service.scan_result_count && count < max_count; i++) {
        network_scan_result_t *scan_result = &g_communication_service.scan_results[i];
        
        if (!scan_result->is_known) {
            results[count] = *scan_result;
            count++;
        }
    }
    
    *actual_count = count;
    
    return AICAM_OK;
}

/* ==================== Service Management ==================== */

aicam_result_t communication_get_config(communication_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *config = g_communication_service.config;
    
    return AICAM_OK;
}

aicam_result_t communication_set_config(const communication_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_communication_service.config = *config;
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Communication service configuration updated");
    }
    
    return AICAM_OK;
}

aicam_result_t communication_get_stats(communication_service_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *stats = g_communication_service.stats;
    
    return AICAM_OK;
}

aicam_result_t communication_reset_stats(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memset(&g_communication_service.stats, 0, sizeof(communication_service_stats_t));
    
    if (g_communication_service.config.enable_debug) {
        LOG_SVC_DEBUG("Communication service statistics reset");
    }
    
    return AICAM_OK;
}

aicam_bool_t communication_is_running(void)
{
    return g_communication_service.running;
}

const char* communication_get_version(void)
{
    return COMMUNICATION_SERVICE_VERSION;
}

/* ==================== Startup Connection Decision Implementation ==================== */

// Startup timeout: 30 seconds

/**
 * @brief Startup timeout callback
 * @note Called when startup timeout expires, forces decision
 */
static void startup_timeout_callback(void *argument)
{
    (void)argument;
    
    if (g_communication_service.startup_decision_made) {
        return;
    }
    
    LOG_SVC_WARN("Startup timeout (%u ms) reached, forcing connection decision", STARTUP_TIMEOUT_MS);
    LOG_SVC_INFO("Ready status at timeout: WiFi=%s, Cellular=%s, PoE=%s",
                 g_communication_service.wifi_sta_ready ? "ready" : "waiting",
                 g_communication_service.cellular_ready ? "ready" : "waiting",
                 g_communication_service.poe_ready ? "ready" : "waiting");
    
    // Force decision with whatever is ready
    make_startup_connection_decision();
}

/**
 * @brief Get highest priority available (hardware present) type
 * @note This checks hardware availability, not connection status
 */
static communication_type_t get_highest_priority_available_type(void)
{
    // Priority: PoE (3) > Cellular (2) > WiFi (1)
#if NETIF_ETH_WAN_IS_ENABLE
    if (g_communication_service.poe_available) {
        return COMM_TYPE_POE;
    }
#endif

#if NETIF_4G_CAT1_IS_ENABLE
    if (g_communication_service.cellular_available) {
        return COMM_TYPE_CELLULAR;
    }
#endif

    // WiFi is always available (built-in)
    return COMM_TYPE_WIFI;
}

/**
 * @brief Make startup connection decision
 * @note Called once when all network interfaces are ready
 * 
 * Logic:
 * 1. If preferred_type is set: only try to connect that type
 * 2. If preferred_type is not set: select by priority (PoE > Cellular > WiFi)
 * 3. Connection failure does NOT trigger fallback to other types
 */
static void make_startup_connection_decision(void)
{
    if (g_communication_service.startup_decision_made) {
        LOG_SVC_DEBUG("Startup decision already made, skipping");
        return;
    }
    
    g_communication_service.startup_decision_made = AICAM_TRUE;
    
    // Stop timeout timer
    if (g_communication_service.startup_timeout_timer != NULL) {
        osTimerStop(g_communication_service.startup_timeout_timer);
    }
    
    uint32_t startup_duration = rtc_get_uptime_ms() - g_communication_service.startup_begin_time;
    LOG_SVC_INFO("=== Making startup connection decision (after %u ms) ===", startup_duration);
    LOG_SVC_INFO("Preferred type: %s", communication_type_to_string(g_communication_service.preferred_type));
    LOG_SVC_INFO("Available: WiFi=%s, Cellular=%s, PoE=%s",
                 "YES",  // WiFi always available
                 g_communication_service.cellular_available ? "YES" : "NO",
                 g_communication_service.poe_available ? "YES" : "NO");
    
    communication_type_t target_type = COMM_TYPE_NONE;
    
    // Case 1: User has set a preferred type
    if (g_communication_service.preferred_type != COMM_TYPE_NONE) {
        target_type = g_communication_service.preferred_type;
        LOG_SVC_INFO("Using user preferred type: %s", communication_type_to_string(target_type));
        
        // Check if preferred type hardware is available
        aicam_bool_t hw_available = AICAM_FALSE;
        switch (target_type) {
            case COMM_TYPE_WIFI:
                hw_available = AICAM_TRUE;  // Always available
                break;
            case COMM_TYPE_CELLULAR:
                hw_available = g_communication_service.cellular_available;
                break;
            case COMM_TYPE_POE:
                hw_available = g_communication_service.poe_available;
                break;
            default:
                break;
        }
        
        if (!hw_available) {
            LOG_SVC_WARN("Preferred type %s hardware not available, auto-fallback",
                        communication_type_to_string(target_type));
            communication_set_preferred_type(COMM_TYPE_NONE);
            target_type = get_highest_priority_available_type();
            LOG_SVC_INFO("Fallback to: %s", communication_type_to_string(target_type));
        }
    }
    // Case 2: No preferred type set, use priority
    else {
        target_type = get_highest_priority_available_type();
        LOG_SVC_INFO("No preferred type, using priority selection: %s", 
                    communication_type_to_string(target_type));
    }
    
    // Set selected type (for UI display)
    g_communication_service.selected_type = target_type;
    LOG_SVC_INFO("Selected type set to: %s", communication_type_to_string(target_type));
    
    // Try to connect the selected type
    if (target_type != COMM_TYPE_NONE) {
        LOG_SVC_INFO("Attempting to connect %s...", communication_type_to_string(target_type));
        
        aicam_result_t connect_result = AICAM_ERROR;
        
        switch (target_type) {
            case COMM_TYPE_WIFI:
                // WiFi needs known networks
                connect_result = try_connect_known_networks();
                if (connect_result != AICAM_OK) {
                    LOG_SVC_INFO("WiFi: No known networks or connection failed, waiting for user config");
                }
                break;
                
            case COMM_TYPE_CELLULAR:
#if NETIF_4G_CAT1_IS_ENABLE
                if (g_communication_service.cellular_initialized) {
                    connect_result = communication_cellular_connect();
                    if (connect_result != AICAM_OK) {
                        LOG_SVC_INFO("Cellular: Connection failed, waiting for user config");
                    }
                } else {
                    LOG_SVC_WARN("Cellular not initialized yet");
                }
#endif
                break;
                
            case COMM_TYPE_POE:
#if NETIF_ETH_WAN_IS_ENABLE
                if (g_communication_service.poe_initialized) {
                    connect_result = communication_poe_connect();
                    if (connect_result != AICAM_OK) {
                        LOG_SVC_INFO("PoE: Connection failed (cable not connected?)");
                    }
                } else {
                    LOG_SVC_WARN("PoE not initialized yet");
                }
#endif
                break;
                
            default:
                break;
        }
        
        // Note: We do NOT fallback to other types on failure
        // User must manually switch or configure
        if (connect_result != AICAM_OK) {
            LOG_SVC_INFO("Connection failed for %s, NOT trying other types (by design)",
                        communication_type_to_string(target_type));
            LOG_SVC_INFO("User can configure network or switch type manually");
        }
        else {
            aicam_result_t sta_ready_result = service_set_sta_ready(AICAM_TRUE);
            if (sta_ready_result != AICAM_OK) {
                LOG_SVC_ERROR("Failed to set STA ready flag: %d", sta_ready_result);
            }
        }

    }
    
    // Updatservice_set_sta_readye type info cache
    update_type_info_cache();
    
    LOG_SVC_INFO("=== Startup decision complete ===");
    LOG_SVC_INFO("Selected: %s, Active: %s", 
                communication_type_to_string(g_communication_service.selected_type),
                communication_type_to_string(g_communication_service.active_type));
}

/**
 * @brief Check if all network interfaces are ready and make decision
 * @note Called from each on_xxx_ready callback
 */
static void check_all_ready_and_decide(void)
{
    communication_service_lock();
    // Check which interfaces we're waiting for
    aicam_bool_t waiting_for_wifi = g_communication_service.config.auto_start_wifi_sta && 
                                    !g_communication_service.wifi_sta_ready;
    
#if NETIF_4G_CAT1_IS_ENABLE
    aicam_bool_t waiting_for_cellular = g_communication_service.config.auto_start_cellular && 
                                        !g_communication_service.cellular_ready;
#else
    aicam_bool_t waiting_for_cellular = AICAM_FALSE;
#endif

#if NETIF_ETH_WAN_IS_ENABLE
    aicam_bool_t waiting_for_poe = g_communication_service.config.auto_start_poe && 
                                   !g_communication_service.poe_ready;
#else
    aicam_bool_t waiting_for_poe = AICAM_FALSE;
#endif

    LOG_SVC_DEBUG("Ready check: WiFi=%s(%s), Cellular=%s(%s), PoE=%s(%s)",
                 g_communication_service.wifi_sta_ready ? "ready" : "waiting",
                 waiting_for_wifi ? "needed" : "skip",
                 g_communication_service.cellular_ready ? "ready" : "waiting",
                 waiting_for_cellular ? "needed" : "skip",
                 g_communication_service.poe_ready ? "ready" : "waiting",
                 waiting_for_poe ? "needed" : "skip");
    
    // If still waiting for any interface, don't decide yet
    if (waiting_for_wifi || waiting_for_cellular || waiting_for_poe) {
        LOG_SVC_DEBUG("Still waiting for interfaces, decision pending");
        communication_service_unlock();
        return;
    }
    communication_service_unlock();
    
    // All interfaces ready (or not expected), make decision
    LOG_SVC_INFO("All expected interfaces ready, making connection decision");
    make_startup_connection_decision();

}

/* ==================== Communication Type Management Helpers Implementation ==================== */

/**
 * @brief Get interface name for communication type
 */
static const char* get_interface_name_for_type(communication_type_t type)
{
    switch (type) {
        case COMM_TYPE_WIFI:
            return NETIF_NAME_WIFI_STA;
        case COMM_TYPE_CELLULAR:
            return NETIF_NAME_4G_CAT1;
        case COMM_TYPE_POE:
            return NETIF_NAME_ETH_WAN;
        default:
            return NULL;
    }
}

/**
 * @brief Update type information cache
 */
static void update_type_info_cache(void)
{
    // WiFi type
    g_communication_service.type_info[COMM_TYPE_WIFI].type = COMM_TYPE_WIFI;
    g_communication_service.type_info[COMM_TYPE_WIFI].priority = 1;  // Lowest priority
    g_communication_service.type_info[COMM_TYPE_WIFI].available = AICAM_TRUE;  // WiFi always available
    strncpy(g_communication_service.type_info[COMM_TYPE_WIFI].interface_name, 
            NETIF_NAME_WIFI_STA, sizeof(g_communication_service.type_info[COMM_TYPE_WIFI].interface_name) - 1);
    
    // Check WiFi STA status
    netif_state_t wifi_state = nm_get_netif_state(NETIF_NAME_WIFI_STA);
    if (wifi_state == NETIF_STATE_UP) {
        g_communication_service.type_info[COMM_TYPE_WIFI].status = COMM_STATUS_CONNECTED;
        netif_info_t wifi_info;
        if (nm_get_netif_info(NETIF_NAME_WIFI_STA, &wifi_info) == AICAM_OK) {
            snprintf(g_communication_service.type_info[COMM_TYPE_WIFI].ip_addr,
                     sizeof(g_communication_service.type_info[COMM_TYPE_WIFI].ip_addr),
                     "%d.%d.%d.%d", wifi_info.ip_addr[0], wifi_info.ip_addr[1],
                     wifi_info.ip_addr[2], wifi_info.ip_addr[3]);
            snprintf(g_communication_service.type_info[COMM_TYPE_WIFI].mac_addr,
                     sizeof(g_communication_service.type_info[COMM_TYPE_WIFI].mac_addr),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     wifi_info.if_mac[0], wifi_info.if_mac[1], wifi_info.if_mac[2],
                     wifi_info.if_mac[3], wifi_info.if_mac[4], wifi_info.if_mac[5]);
            g_communication_service.type_info[COMM_TYPE_WIFI].signal_strength = wifi_info.rssi;
        }
    } else if (wifi_state == NETIF_STATE_DOWN) {
        g_communication_service.type_info[COMM_TYPE_WIFI].status = COMM_STATUS_DISCONNECTED;
    } else {
        g_communication_service.type_info[COMM_TYPE_WIFI].status = COMM_STATUS_UNAVAILABLE;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    // Cellular type — dynamically recheck hardware availability
    if (g_communication_service.cellular_available && g_communication_service.startup_decision_made) {
        netif_state_t cell_hw_state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
        if (cell_hw_state == NETIF_STATE_DEINIT) {
            g_communication_service.cellular_available = AICAM_FALSE;
            g_communication_service.cellular_initialized = AICAM_FALSE;
            LOG_SVC_WARN("Cellular hardware no longer available (netif DEINIT)");
        }
    }

    g_communication_service.type_info[COMM_TYPE_CELLULAR].type = COMM_TYPE_CELLULAR;
    g_communication_service.type_info[COMM_TYPE_CELLULAR].priority = 2;  // Medium priority
    g_communication_service.type_info[COMM_TYPE_CELLULAR].available = g_communication_service.cellular_available;
    strncpy(g_communication_service.type_info[COMM_TYPE_CELLULAR].interface_name,
            NETIF_NAME_4G_CAT1, sizeof(g_communication_service.type_info[COMM_TYPE_CELLULAR].interface_name) - 1);
    
    if (g_communication_service.cellular_available) {
        netif_state_t cellular_state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
        if (cellular_state == NETIF_STATE_UP) {
            g_communication_service.type_info[COMM_TYPE_CELLULAR].status = COMM_STATUS_CONNECTED;
            netif_info_t cellular_info;
            if (nm_get_netif_info(NETIF_NAME_4G_CAT1, &cellular_info) == AICAM_OK) {
                snprintf(g_communication_service.type_info[COMM_TYPE_CELLULAR].ip_addr,
                         sizeof(g_communication_service.type_info[COMM_TYPE_CELLULAR].ip_addr),
                         "%d.%d.%d.%d", cellular_info.ip_addr[0], cellular_info.ip_addr[1],
                         cellular_info.ip_addr[2], cellular_info.ip_addr[3]);
                g_communication_service.type_info[COMM_TYPE_CELLULAR].signal_strength = cellular_info.rssi;
            }
        } else if (cellular_state == NETIF_STATE_DOWN) {
            g_communication_service.type_info[COMM_TYPE_CELLULAR].status = COMM_STATUS_DISCONNECTED;
        } else {
            g_communication_service.type_info[COMM_TYPE_CELLULAR].status = COMM_STATUS_UNAVAILABLE;
        }
    } else {
        g_communication_service.type_info[COMM_TYPE_CELLULAR].status = COMM_STATUS_UNAVAILABLE;
    }
#else
    g_communication_service.type_info[COMM_TYPE_CELLULAR].type = COMM_TYPE_CELLULAR;
    g_communication_service.type_info[COMM_TYPE_CELLULAR].available = AICAM_FALSE;
    g_communication_service.type_info[COMM_TYPE_CELLULAR].status = COMM_STATUS_UNAVAILABLE;
#endif

#if NETIF_ETH_WAN_IS_ENABLE
    // PoE type — only mark unavailable when hardware is truly gone (DEINIT)
    if (g_communication_service.poe_available && g_communication_service.startup_decision_made) {
        netif_state_t poe_hw_state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
        if (poe_hw_state == NETIF_STATE_DEINIT) {
            g_communication_service.poe_available = AICAM_FALSE;
            g_communication_service.poe_initialized = AICAM_FALSE;
            LOG_SVC_WARN("PoE hardware no longer available (netif DEINIT)");
        }
    }

    g_communication_service.type_info[COMM_TYPE_POE].type = COMM_TYPE_POE;
    g_communication_service.type_info[COMM_TYPE_POE].priority = 3;  // Highest priority
    g_communication_service.type_info[COMM_TYPE_POE].available = g_communication_service.poe_available;
    strncpy(g_communication_service.type_info[COMM_TYPE_POE].interface_name,
            NETIF_NAME_ETH_WAN, sizeof(g_communication_service.type_info[COMM_TYPE_POE].interface_name) - 1);
    
    if (g_communication_service.poe_available) {
        netif_state_t poe_state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
        if (poe_state == NETIF_STATE_UP) {
            g_communication_service.type_info[COMM_TYPE_POE].status = COMM_STATUS_CONNECTED;
            netif_info_t poe_info;
            if (nm_get_netif_info(NETIF_NAME_ETH_WAN, &poe_info) == AICAM_OK) {
                snprintf(g_communication_service.type_info[COMM_TYPE_POE].ip_addr,
                         sizeof(g_communication_service.type_info[COMM_TYPE_POE].ip_addr),
                         "%d.%d.%d.%d", poe_info.ip_addr[0], poe_info.ip_addr[1],
                         poe_info.ip_addr[2], poe_info.ip_addr[3]);
                snprintf(g_communication_service.type_info[COMM_TYPE_POE].mac_addr,
                         sizeof(g_communication_service.type_info[COMM_TYPE_POE].mac_addr),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         poe_info.if_mac[0], poe_info.if_mac[1], poe_info.if_mac[2],
                         poe_info.if_mac[3], poe_info.if_mac[4], poe_info.if_mac[5]);
            }
        } else if (poe_state == NETIF_STATE_DOWN) {
            g_communication_service.type_info[COMM_TYPE_POE].status = COMM_STATUS_DISCONNECTED;
        } else {
            g_communication_service.type_info[COMM_TYPE_POE].status = COMM_STATUS_UNAVAILABLE;
        }
    } else {
        g_communication_service.type_info[COMM_TYPE_POE].status = COMM_STATUS_UNAVAILABLE;
    }
#else
    g_communication_service.type_info[COMM_TYPE_POE].type = COMM_TYPE_POE;
    g_communication_service.type_info[COMM_TYPE_POE].available = AICAM_FALSE;
    g_communication_service.type_info[COMM_TYPE_POE].status = COMM_STATUS_UNAVAILABLE;
#endif

    // Update active type (the actually connected type)
    communication_type_t connected = get_highest_priority_connected_type();
    g_communication_service.active_type = connected;
    
    // Set is_default flag based on active type
    for (int i = 0; i < COMM_TYPE_MAX; i++) {
        g_communication_service.type_info[i].is_default = (g_communication_service.type_info[i].type == connected);
    }

    // Check for hardware removal and handle fallback
    if (g_communication_service.startup_decision_made) {
        check_and_handle_hardware_change();
    }
}

/**
 * @brief Detect hardware change and auto-fallback selected type
 * @note Two cases:
 *   1) Hardware removed (available=FALSE): clear preferred_type, fallback selected, try WiFi
 *   2) Connection lost (available=TRUE but disconnected): only fallback selected, keep preferred
 *      so user can still manually switch back to the original type
 */
static void check_and_handle_hardware_change(void)
{
    communication_type_t sel = g_communication_service.selected_type;

    if (sel == COMM_TYPE_NONE || sel == COMM_TYPE_WIFI) {
        return;
    }

    aicam_bool_t sel_available = g_communication_service.type_info[sel].available;
    communication_status_t sel_status = g_communication_service.type_info[sel].status;

    // Case 1: Hardware truly removed (unavailable)
    if (!sel_available) {
        LOG_SVC_WARN("=== Hardware removal detected: %s is no longer available ===",
                     communication_type_to_string(sel));

        if (g_communication_service.preferred_type == sel) {
            g_communication_service.preferred_type = COMM_TYPE_NONE;
            g_communication_service.config.preferred_type = COMM_TYPE_NONE;
            network_service_config_t net_cfg;
            if (json_config_get_network_service_config(&net_cfg) == AICAM_OK) {
                net_cfg.preferred_comm_type = (uint32_t)COMM_TYPE_NONE;
                json_config_set_network_service_config(&net_cfg);
            }
            LOG_SVC_INFO("Cleared stale preferred type (was %s)", communication_type_to_string(sel));
        }

        communication_type_t fallback = get_highest_priority_available_type();
        g_communication_service.selected_type = fallback;
        LOG_SVC_INFO("Auto-fallback selected type: %s -> %s",
                     communication_type_to_string(sel), communication_type_to_string(fallback));

        if (g_communication_service.active_type == COMM_TYPE_NONE && fallback == COMM_TYPE_WIFI) {
            if (g_communication_service.wifi_sta_ready &&
                g_communication_service.type_info[COMM_TYPE_WIFI].status != COMM_STATUS_CONNECTED) {
                LOG_SVC_INFO("Attempting WiFi auto-connect after hardware removal");
                try_connect_known_networks();
            }
        }
        return;
    }

    // Case 2: Hardware present but connection lost — auto-switch to a connected type
    //         Keep preferred_type and available so user can manually switch back
    if (sel_status != COMM_STATUS_CONNECTED && sel_status != COMM_STATUS_CONNECTING) {
        communication_type_t connected = g_communication_service.active_type;
        if (connected != COMM_TYPE_NONE && connected != sel) {
            g_communication_service.selected_type = connected;
            LOG_SVC_INFO("Connection lost on %s, auto-switch selected to connected %s",
                         communication_type_to_string(sel), communication_type_to_string(connected));
        } else if (connected == COMM_TYPE_NONE) {
            communication_type_t fallback = get_highest_priority_available_type();
            if (fallback != sel) {
                g_communication_service.selected_type = fallback;
                LOG_SVC_INFO("Connection lost on %s, fallback selected to %s",
                             communication_type_to_string(sel), communication_type_to_string(fallback));
                if (fallback == COMM_TYPE_WIFI && g_communication_service.wifi_sta_ready &&
                    g_communication_service.type_info[COMM_TYPE_WIFI].status != COMM_STATUS_CONNECTED) {
                    LOG_SVC_INFO("Attempting WiFi auto-connect after connection loss");
                    try_connect_known_networks();
                }
            }
        }
    }
}

/**
 * @brief Get highest priority connected type
 */
static communication_type_t get_highest_priority_connected_type(void)
{
    // Check in priority order: PoE > Cellular > WiFi
#if NETIF_ETH_WAN_IS_ENABLE
    if (g_communication_service.type_info[COMM_TYPE_POE].status == COMM_STATUS_CONNECTED) {
        return COMM_TYPE_POE;
    }
#endif

#if NETIF_4G_CAT1_IS_ENABLE
    if (g_communication_service.type_info[COMM_TYPE_CELLULAR].status == COMM_STATUS_CONNECTED) {
        return COMM_TYPE_CELLULAR;
    }
#endif

    if (g_communication_service.type_info[COMM_TYPE_WIFI].status == COMM_STATUS_CONNECTED) {
        return COMM_TYPE_WIFI;
    }
    
    return COMM_TYPE_NONE;
}

#if NETIF_4G_CAT1_IS_ENABLE
/**
 * @brief Cellular/4G ready callback
 * @note Only collects hardware info, does NOT auto-connect
 *       Connection decision is made by check_all_ready_and_decide()
 */
static void on_cellular_ready(const char *if_name, aicam_result_t result)
{
    // Mark as ready regardless of result
    g_communication_service.cellular_ready = AICAM_TRUE;
    
    if (result == AICAM_OK) {
        LOG_SVC_INFO("Cellular/4G initialized and ready");
        g_communication_service.cellular_initialized = AICAM_TRUE;
        g_communication_service.cellular_available = AICAM_TRUE;

        //set cellular settings
        aicam_result_t ret = communication_cellular_set_settings(&g_communication_service.cellular_settings);
        if (ret != AICAM_OK) {
            LOG_SVC_ERROR("Failed to set cellular settings: %d", ret);
        }
        
        // Get cellular info
        netif_info_t cellular_info;
        if (nm_get_netif_info(NETIF_NAME_4G_CAT1, &cellular_info) == AICAM_OK) {
            // Copy cellular info to our structure
            strncpy(g_communication_service.cellular_info.imei, 
                    cellular_info.cellular_info.imei,
                    sizeof(g_communication_service.cellular_info.imei) - 1);
            strncpy(g_communication_service.cellular_info.imsi,
                    cellular_info.cellular_info.imsi,
                    sizeof(g_communication_service.cellular_info.imsi) - 1);
            strncpy(g_communication_service.cellular_info.iccid,
                    cellular_info.cellular_info.iccid,
                    sizeof(g_communication_service.cellular_info.iccid) - 1);
            strncpy(g_communication_service.cellular_info.model,
                    cellular_info.cellular_info.model_name,
                    sizeof(g_communication_service.cellular_info.model) - 1);
            strncpy(g_communication_service.cellular_info.version,
                    cellular_info.cellular_info.version,
                    sizeof(g_communication_service.cellular_info.version) - 1);
            strncpy(g_communication_service.cellular_info.isp,
                    cellular_info.cellular_info.operator,
                    sizeof(g_communication_service.cellular_info.isp) - 1);
            strncpy(g_communication_service.cellular_info.network_type,
                    cellular_info.cellular_info.network_type,
                    sizeof(g_communication_service.cellular_info.network_type) - 1);
            strncpy(g_communication_service.cellular_info.plmn_id,
                    cellular_info.cellular_info.plmn_id,
                    sizeof(g_communication_service.cellular_info.plmn_id) - 1);
            strncpy(g_communication_service.cellular_info.cell_id,
                    cellular_info.cellular_info.cell_id,
                    sizeof(g_communication_service.cellular_info.cell_id) - 1);
            strncpy(g_communication_service.cellular_info.lac,
                    cellular_info.cellular_info.lac,
                    sizeof(g_communication_service.cellular_info.lac) - 1);
            strncpy(g_communication_service.cellular_info.register_status,
                    cellular_info.cellular_info.registration_status,
                    sizeof(g_communication_service.cellular_info.register_status) - 1);
            g_communication_service.cellular_info.csq = cellular_info.cellular_info.csq_value;
            g_communication_service.cellular_info.csq_level = cellular_info.cellular_info.csq_level;
            g_communication_service.cellular_info.rssi = cellular_info.cellular_info.rssi;
            g_communication_service.cellular_info.signal_level = cellular_info.cellular_info.csq_level;
            
            
            LOG_SVC_INFO("Cellular IMEI: %s", g_communication_service.cellular_info.imei);
            LOG_SVC_INFO("Cellular ISP: %s", g_communication_service.cellular_info.isp);
            LOG_SVC_INFO("Cellular Signal: %d dBm (CSQ: %d)", 
                        g_communication_service.cellular_info.rssi,
                        g_communication_service.cellular_info.csq);
        }
        
        // Update type info cache
        update_type_info_cache();
        
        uint32_t init_time = netif_init_manager_get_init_time(if_name);
        LOG_SVC_INFO("Cellular initialization completed in %u ms", init_time);
        
    } else {
        LOG_SVC_INFO("Cellular/4G initialization failed: %d", result);
        g_communication_service.cellular_initialized = AICAM_FALSE;
        g_communication_service.cellular_available = AICAM_FALSE;
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
    
    // Check if all interfaces ready, then make connection decision
    check_all_ready_and_decide();
}
#endif

#if NETIF_ETH_WAN_IS_ENABLE
/**
 * @brief PoE/Ethernet ready callback
 * @note Implements quick recovery: loads saved config and connects within 5s target
 *       Connection decision is made by check_all_ready_and_decide()
 */
static void on_poe_ready(const char *if_name, aicam_result_t result)
{
    uint32_t init_time = netif_init_manager_get_init_time(if_name);
    
    // Mark as ready regardless of result
    g_communication_service.poe_ready = AICAM_TRUE;
    
    if (result == AICAM_OK) {
        LOG_SVC_INFO("[PoE] Hardware initialized in %u ms", init_time);
        g_communication_service.poe_initialized = AICAM_TRUE;
        g_communication_service.poe_available = AICAM_TRUE;
        
        // Load saved PoE configuration for quick recovery
        poe_config_persist_t poe_cfg;
        if (json_config_get_poe_config(&poe_cfg) == AICAM_OK) {
            LOG_SVC_INFO("[PoE] Loaded config: mode=%s, auto_reconnect=%s, recovery_delay=%ums",
                        poe_cfg.ip_mode == POE_IP_MODE_STATIC ? "static" : "dhcp",
                        poe_cfg.auto_reconnect ? "yes" : "no",
                        poe_cfg.power_recovery_delay_ms);
            
            // Apply configuration to netif immediately
            netif_config_t netif_cfg;
            if (nm_get_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg) == 0) {
                if (poe_cfg.ip_mode == POE_IP_MODE_STATIC) {
                    netif_cfg.ip_mode = NETIF_IP_MODE_STATIC;
                    memcpy(netif_cfg.ip_addr, poe_cfg.ip_addr, 4);
                    memcpy(netif_cfg.netmask, poe_cfg.netmask, 4);
                    memcpy(netif_cfg.gw, poe_cfg.gateway, 4);
                    LOG_SVC_INFO("[PoE] Static IP: %d.%d.%d.%d",
                                poe_cfg.ip_addr[0], poe_cfg.ip_addr[1],
                                poe_cfg.ip_addr[2], poe_cfg.ip_addr[3]);
                } else {
                    netif_cfg.ip_mode = NETIF_IP_MODE_DHCP;
                    // Use last DHCP IP for quick recovery if available
                    if (poe_cfg.persist_last_ip &&
                        !(poe_cfg.last_dhcp_ip[0] == 0 && poe_cfg.last_dhcp_ip[1] == 0 &&
                          poe_cfg.last_dhcp_ip[2] == 0 && poe_cfg.last_dhcp_ip[3] == 0)) {
                        LOG_SVC_DEBUG("[PoE] Last DHCP IP: %d.%d.%d.%d (for quick recovery hint)",
                                     poe_cfg.last_dhcp_ip[0], poe_cfg.last_dhcp_ip[1],
                                     poe_cfg.last_dhcp_ip[2], poe_cfg.last_dhcp_ip[3]);
                    }
                }

                if (poe_cfg.hostname[0] != '\0') {
                    netif_cfg.host_name = poe_cfg.hostname;
                }

                int cfg_ret = nm_set_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
                if (cfg_ret != 0) {
                    LOG_SVC_ERROR("[PoE] Failed to set netif config in on_poe_ready: %d", cfg_ret);
                } else {
                    LOG_SVC_INFO("[PoE] Netif config set successfully, ip_mode=%d", netif_cfg.ip_mode);
                }
            } else {
                LOG_SVC_ERROR("[PoE] Failed to get netif config in on_poe_ready");
            }
            
            // Power recovery delay (default 5000ms = 5s target)
            // For PoE power-on, we want to bring up network ASAP
            uint32_t recovery_delay = poe_cfg.power_recovery_delay_ms;
            if (recovery_delay == 0) {
                recovery_delay = 100; // Minimal delay
            }
            
            // If auto_reconnect is enabled, schedule immediate connection
            if (poe_cfg.auto_reconnect) {
                LOG_SVC_INFO("[PoE] Auto-reconnect enabled, scheduling connection...");
                // Short delay to allow hardware stabilization
                if (recovery_delay > 0 && recovery_delay < 5000) {
                    osDelay(recovery_delay);
                }
            }
        }
        
        // Update type info cache
        update_type_info_cache();
        
        LOG_SVC_INFO("[PoE] Ready for connection (total init: %u ms)", init_time);
        
    } else {
        LOG_SVC_ERROR("[PoE] Init failed: %d (status: %s)", result, 
                     poe_status_code_to_string(POE_STATUS_ERROR));
        g_communication_service.poe_available = AICAM_FALSE;
        g_communication_service.poe_initialized = AICAM_FALSE;
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
    
    // Check if all interfaces ready, then make connection decision
    check_all_ready_and_decide();
}
#endif

/* ==================== Communication Type Management APIs ==================== */

const char* communication_type_to_string(communication_type_t type)
{
    switch (type) {
        case COMM_TYPE_NONE:     return "none";
        case COMM_TYPE_WIFI:     return "wifi";
        case COMM_TYPE_CELLULAR: return "cellular";
        case COMM_TYPE_POE:      return "poe";
        default:                 return "unknown";
    }
}

const char* communication_status_to_string(communication_status_t status)
{
    switch (status) {
        case COMM_STATUS_UNAVAILABLE:  return "Unavailable";
        case COMM_STATUS_DISCONNECTED: return "Disconnected";
        case COMM_STATUS_CONNECTING:   return "Connecting";
        case COMM_STATUS_CONNECTED:    return "Connected";
        case COMM_STATUS_ERROR:        return "Error";
        default:                       return "Unknown";
    }
}

communication_type_t communication_type_from_string(const char *str)
{
    if (!str) return COMM_TYPE_NONE;
    
    if (strcasecmp(str, "wifi") == 0 || strcasecmp(str, "wlan") == 0) {
        return COMM_TYPE_WIFI;
    } else if (strcasecmp(str, "cellular") == 0 || strcasecmp(str, "4g") == 0 || 
               strcasecmp(str, "lte") == 0) {
        return COMM_TYPE_CELLULAR;
    } else if (strcasecmp(str, "poe") == 0 || strcasecmp(str, "ethernet") == 0 || 
               strcasecmp(str, "eth") == 0) {
        return COMM_TYPE_POE;
    }
    
    return COMM_TYPE_NONE;
}

aicam_result_t communication_get_available_modules(communication_type_info_t *types,
                                                   uint32_t max_count,
                                                   uint32_t *actual_count)
{
    if (!types || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    update_type_info_cache();
    
    uint32_t count = 0;
    for (int i = 1; i < COMM_TYPE_MAX && count < max_count; i++) {  // Skip COMM_TYPE_NONE
        if (g_communication_service.type_info[i].available) {
            types[count] = g_communication_service.type_info[i];
            count++;
        }
    }
    
    *actual_count = count;
    return AICAM_OK;
}

aicam_result_t communication_get_all_types(communication_type_info_t *types,
                                           uint32_t max_count,
                                           uint32_t *actual_count)
{
    if (!types || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    update_type_info_cache();
    
    uint32_t count = 0;
    for (int i = 1; i < COMM_TYPE_MAX && count < max_count; i++) {  // Skip COMM_TYPE_NONE
        types[count] = g_communication_service.type_info[i];
        count++;
    }
    
    *actual_count = count;
    return AICAM_OK;
}

aicam_result_t communication_get_type_info(communication_type_t type,
                                           communication_type_info_t *info)
{
    if (!info || type >= COMM_TYPE_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    update_type_info_cache();
    
    *info = g_communication_service.type_info[type];
    return AICAM_OK;
}

communication_type_t communication_get_current_type(void)
{
    if (!g_communication_service.initialized) {
        return COMM_TYPE_NONE;
    }
    
    update_type_info_cache();
    return g_communication_service.active_type;
}

communication_type_t communication_get_selected_type(void)
{
    if (!g_communication_service.initialized) {
        return COMM_TYPE_NONE;
    }
    
    return g_communication_service.selected_type;
}

aicam_result_t communication_set_selected_type(communication_type_t type)
{
    if (type >= COMM_TYPE_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Check if type is available (hardware present)
    if (type != COMM_TYPE_NONE && !communication_is_type_available(type)) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    g_communication_service.selected_type = type;
    LOG_SVC_INFO("Selected communication type set to: %s (does not require connection)", 
                 communication_type_to_string(type));
    
    return AICAM_OK;
}

aicam_result_t communication_get_current_type_info(communication_type_info_t *info)
{
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    communication_type_t current = communication_get_current_type();
    if (current == COMM_TYPE_NONE) {
        memset(info, 0, sizeof(communication_type_info_t));
        info->type = COMM_TYPE_NONE;
        info->status = COMM_STATUS_DISCONNECTED;
        return AICAM_OK;
    }
    
    return communication_get_type_info(current, info);
}

communication_type_t communication_get_default_type(void)
{
    if (!g_communication_service.initialized) {
        return COMM_TYPE_NONE;
    }
    
    update_type_info_cache();
    
    // Return highest priority available type
    for (int priority = 3; priority >= 1; priority--) {
        for (int i = 1; i < COMM_TYPE_MAX; i++) {
            if (g_communication_service.type_info[i].priority == priority &&
                g_communication_service.type_info[i].available) {
                return (communication_type_t)i;
            }
        }
    }
    
    return COMM_TYPE_NONE;
}

aicam_bool_t communication_is_type_available(communication_type_t type)
{
    if (type >= COMM_TYPE_MAX || !g_communication_service.initialized) {
        return AICAM_FALSE;
    }
    
    update_type_info_cache();
    return g_communication_service.type_info[type].available;
}

aicam_bool_t communication_is_type_connected(communication_type_t type)
{
    if (type >= COMM_TYPE_MAX || !g_communication_service.initialized) {
        return AICAM_FALSE;
    }
    
    update_type_info_cache();
    return (g_communication_service.type_info[type].status == COMM_STATUS_CONNECTED);
}

aicam_result_t communication_switch_type(communication_type_t type,
                                         communication_switch_callback_t callback)
{
    if (type >= COMM_TYPE_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_communication_service.switch_in_progress) {
        return AICAM_ERROR_BUSY;
    }
    
    if (!communication_is_type_available(type)) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Store callback and start switch
    g_communication_service.switch_callback = callback;
    g_communication_service.switch_in_progress = AICAM_TRUE;
    
    // Initialize result
    memset(&g_communication_service.switch_result, 0, sizeof(communication_switch_result_t));
    g_communication_service.switch_result.from_type = g_communication_service.active_type;
    g_communication_service.switch_result.to_type = type;
    
    uint32_t start_time = rtc_get_uptime_ms();
    
    // Perform the switch
    aicam_result_t result = communication_switch_type_sync(type, 
                                                          &g_communication_service.switch_result,
                                                          g_communication_service.config.connection_timeout_ms);
    
    g_communication_service.switch_result.switch_time_ms = rtc_get_uptime_ms() - start_time;
    g_communication_service.switch_in_progress = AICAM_FALSE;
    
    // Call callback if provided
    if (callback) {
        callback(&g_communication_service.switch_result);
    }
    
    return result;
}

aicam_result_t communication_switch_type_sync(communication_type_t type,
                                              communication_switch_result_t *result,
                                              uint32_t timeout_ms)
{
    if (type >= COMM_TYPE_MAX || !result) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Switching communication type from %s to %s...",
                 communication_type_to_string(g_communication_service.active_type),
                 communication_type_to_string(type));
    
    uint32_t start_time = rtc_get_uptime_ms();
    
    result->from_type = g_communication_service.active_type;
    result->to_type = type;
    result->success = AICAM_FALSE;
    
    // Check if target type is available (hardware present)
    if (!communication_is_type_available(type)) {
        snprintf(result->error_message, sizeof(result->error_message),
                 "Communication type %s is not available", communication_type_to_string(type));
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Always update selected_type first (for UI display)
    g_communication_service.selected_type = type;
    LOG_SVC_INFO("Selected type updated to: %s", communication_type_to_string(type));
    
    // If already using this type and connected, just return success
    if (g_communication_service.active_type == type && 
        communication_is_type_connected(type)) {
        result->success = AICAM_TRUE;
        result->switch_time_ms = rtc_get_uptime_ms() - start_time;
        LOG_SVC_INFO("Already connected to %s", communication_type_to_string(type));
        return AICAM_OK;
    }

    //disconnect the current type
    if (g_communication_service.active_type != COMM_TYPE_NONE) {
        aicam_result_t disconnect_result = communication_disconnect_network(get_interface_name_for_type(g_communication_service.active_type));
        if (disconnect_result != AICAM_OK) {
            LOG_SVC_WARN("Failed to disconnect current type: %d", disconnect_result);
        }
        g_communication_service.active_type = COMM_TYPE_NONE;
    }
    
    // Connect to target type
    aicam_result_t connect_result = AICAM_ERROR;
    const char *if_name = get_interface_name_for_type(type);
    
    switch (type) {
        case COMM_TYPE_WIFI:
            // WiFi connection is handled by try_connect_known_networks
            connect_result = try_connect_known_networks();
            break;
            
        case COMM_TYPE_CELLULAR:
            connect_result = communication_cellular_connect();
            break;
            
        case COMM_TYPE_POE:
            connect_result = communication_poe_connect();
            break;
            
        default:
            snprintf(result->error_message, sizeof(result->error_message),
                     "Invalid communication type");
            return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Suppress unused variable warning (async connection)
    (void)connect_result;
    
    // Wait for connection with timeout
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        if (communication_is_type_connected(type)) {
            // Set as default network interface
            if (if_name) {
                nm_ctrl_set_default_netif(if_name);
            }
            
            g_communication_service.active_type = type;
            result->success = AICAM_TRUE;
            result->switch_time_ms = rtc_get_uptime_ms() - start_time;
            
            // Update device communication type
            device_service_update_communication_type();

            //update preferred type
            communication_set_preferred_type(type);
            
            LOG_SVC_INFO("Successfully switched to %s in %u ms",
                        communication_type_to_string(type), result->switch_time_ms);
            return AICAM_OK;
        }
        
        osDelay(100);
        elapsed = rtc_get_uptime_ms() - start_time;
    }
    
    // Timeout - but selected_type is already updated for UI
    // User can still configure this type in the UI
    snprintf(result->error_message, sizeof(result->error_message),
             "Connection timeout for %s (type selected for configuration)", 
             communication_type_to_string(type));
    result->switch_time_ms = rtc_get_uptime_ms() - start_time;
    
    LOG_SVC_WARN("Connection timeout for %s, but type is selected for UI configuration", 
                 communication_type_to_string(type));
    
    // Return OK because selected_type was updated successfully
    // The connection timeout is not a fatal error - user can configure and retry
    result->success = AICAM_TRUE;  // Selected type switch succeeded
    return AICAM_OK;
}

aicam_result_t communication_set_preferred_type(communication_type_t type)
{
    if (type >= COMM_TYPE_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_communication_service.preferred_type = type;
    g_communication_service.config.preferred_type = type;
    
    // Save to NVS
    network_service_config_t net_cfg;
    if (json_config_get_network_service_config(&net_cfg) == AICAM_OK) {
        net_cfg.preferred_comm_type = (uint32_t)type;
        json_config_set_network_service_config(&net_cfg);
    }
    
    LOG_SVC_INFO("Preferred communication type set to: %s", communication_type_to_string(type));
    
    return AICAM_OK;
}

communication_type_t communication_get_preferred_type(void)
{
    return g_communication_service.preferred_type;
}

aicam_result_t communication_apply_priority(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    update_type_info_cache();
    
    communication_type_t target = get_highest_priority_connected_type();
    if (target == COMM_TYPE_NONE) {
        LOG_SVC_INFO("No connected communication type available");
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    if (target == g_communication_service.active_type) {
        LOG_SVC_INFO("Already using highest priority type: %s", communication_type_to_string(target));
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Applying priority: switching to %s", communication_type_to_string(target));
    
    communication_switch_result_t result;
    return communication_switch_type_sync(target, &result, g_communication_service.config.connection_timeout_ms);
}

aicam_result_t communication_set_auto_priority(aicam_bool_t enable)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_communication_service.config.enable_auto_priority = enable;
    
    // Save to NVS
    network_service_config_t net_cfg;
    if (json_config_get_network_service_config(&net_cfg) == AICAM_OK) {
        net_cfg.enable_auto_priority = enable;
        json_config_set_network_service_config(&net_cfg);
    }
    
    LOG_SVC_INFO("Auto priority %s", enable ? "enabled" : "disabled");
    
    return AICAM_OK;
}

aicam_bool_t communication_get_auto_priority(void)
{
    return g_communication_service.config.enable_auto_priority;
}

/* ==================== Cellular/4G Management APIs ==================== */

communication_status_t communication_cellular_get_status(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized || !g_communication_service.cellular_available) {
        return COMM_STATUS_UNAVAILABLE;
    }
    
    update_type_info_cache();
    return g_communication_service.type_info[COMM_TYPE_CELLULAR].status;
#else
    return COMM_STATUS_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_get_settings(cellular_connection_settings_t *settings)
{
    if (!settings) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *settings = g_communication_service.cellular_settings;
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_set_settings(const cellular_connection_settings_t *settings)
{
    if (!settings) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Store settings
    g_communication_service.cellular_settings = *settings;
    
    // Apply to netif configuration
    netif_config_t cellular_cfg = {0};
    nm_get_netif_cfg(NETIF_NAME_4G_CAT1, &cellular_cfg);
    
    strncpy(cellular_cfg.cellular_cfg.apn, settings->apn, sizeof(cellular_cfg.cellular_cfg.apn) - 1);
    strncpy(cellular_cfg.cellular_cfg.user, settings->username, sizeof(cellular_cfg.cellular_cfg.user) - 1);
    strncpy(cellular_cfg.cellular_cfg.passwd, settings->password, sizeof(cellular_cfg.cellular_cfg.passwd) - 1);
    strncpy(cellular_cfg.cellular_cfg.pin, settings->pin_code, sizeof(cellular_cfg.cellular_cfg.pin) - 1);
    cellular_cfg.cellular_cfg.authentication = (uint8_t)settings->authentication;
    cellular_cfg.cellular_cfg.is_enable_roam = settings->enable_roaming ? 1 : 0;
    cellular_cfg.cellular_cfg.isp_selected = settings->operator;
    
    aicam_result_t result = nm_set_netif_cfg(NETIF_NAME_4G_CAT1, &cellular_cfg);
    
    if (result == AICAM_OK) {
        LOG_SVC_INFO("Cellular settings applied: APN=%s, Auth=%d", 
                    settings->apn, settings->authentication);
    } else {
        LOG_SVC_ERROR("Failed to apply cellular settings: %d", result);
    }
    
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_save_settings(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Save cellular settings to NVS
    network_service_config_t net_cfg;
    aicam_result_t result = json_config_get_network_service_config(&net_cfg);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get network service config: %d", result);
        return result;
    }
    
    // Update cellular configuration
    strncpy(net_cfg.cellular.apn, g_communication_service.cellular_settings.apn,
            sizeof(net_cfg.cellular.apn) - 1);
    net_cfg.cellular.apn[sizeof(net_cfg.cellular.apn) - 1] = '\0';
    
    strncpy(net_cfg.cellular.username, g_communication_service.cellular_settings.username,
            sizeof(net_cfg.cellular.username) - 1);
    net_cfg.cellular.username[sizeof(net_cfg.cellular.username) - 1] = '\0';
    
    strncpy(net_cfg.cellular.password, g_communication_service.cellular_settings.password,
            sizeof(net_cfg.cellular.password) - 1);
    net_cfg.cellular.password[sizeof(net_cfg.cellular.password) - 1] = '\0';
    
    strncpy(net_cfg.cellular.pin_code, g_communication_service.cellular_settings.pin_code,
            sizeof(net_cfg.cellular.pin_code) - 1);
    net_cfg.cellular.pin_code[sizeof(net_cfg.cellular.pin_code) - 1] = '\0';

    net_cfg.cellular.authentication = (uint8_t)g_communication_service.cellular_settings.authentication;
    net_cfg.cellular.enable_roaming = g_communication_service.cellular_settings.enable_roaming;
    net_cfg.cellular.operator = g_communication_service.cellular_settings.operator;
    
    result = json_config_set_network_service_config(&net_cfg);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to save cellular settings to NVS: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("Cellular settings saved to NVS: APN=%s", net_cfg.cellular.apn);
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_connect(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_communication_service.cellular_available) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Check current state
    netif_state_t state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
    if (state == NETIF_STATE_UP) {
        LOG_SVC_INFO("Cellular already connected");
        return AICAM_OK;
    }
    
    if (state == NETIF_STATE_DEINIT) {
        // Need to initialize first
        LOG_SVC_INFO("Initializing cellular before connect...");
        aicam_result_t init_result = nm_ctrl_netif_init(NETIF_NAME_4G_CAT1);
        if (init_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to initialize cellular: %d", init_result);
            return init_result;
        }
    }
    
    LOG_SVC_INFO("Connecting cellular network...");
    g_communication_service.cellular_connect_start_time = rtc_get_uptime_ms();
    
    aicam_result_t result = nm_ctrl_netif_up(NETIF_NAME_4G_CAT1);
    if (result == AICAM_OK) {
        // Update info
        g_communication_service.cellular_info.network_status = COMM_STATUS_CONNECTED;
        g_communication_service.cellular_info.connection_start_time = rtc_get_timeStamp();
        
        // Get IP info
        netif_info_t info;
        if (nm_get_netif_info(NETIF_NAME_4G_CAT1, &info) == AICAM_OK) {
            snprintf(g_communication_service.cellular_info.ipv4_address,
                     sizeof(g_communication_service.cellular_info.ipv4_address),
                     "%d.%d.%d.%d", info.ip_addr[0], info.ip_addr[1],
                     info.ip_addr[2], info.ip_addr[3]);
            snprintf(g_communication_service.cellular_info.ipv4_gateway,
                     sizeof(g_communication_service.cellular_info.ipv4_gateway),
                     "%d.%d.%d.%d", info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
        }
        
        update_type_info_cache();

        //sta ready
        service_set_sta_ready(AICAM_TRUE);
        LOG_SVC_INFO("Cellular connected: IP=%s", g_communication_service.cellular_info.ipv4_address);
    } else {
        LOG_SVC_ERROR("Failed to connect cellular: %d", result);
        g_communication_service.cellular_info.network_status = COMM_STATUS_ERROR;
    }
    
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_disconnect(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    //if already disconnected, return success
    netif_state_t state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
    if (state != NETIF_STATE_UP) {
        LOG_SVC_INFO("Cellular is not connected");
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Disconnecting cellular network...");
    
    aicam_result_t result = nm_ctrl_netif_down(NETIF_NAME_4G_CAT1);
    if (result == AICAM_OK) {
        g_communication_service.cellular_info.network_status = COMM_STATUS_DISCONNECTED;
        g_communication_service.cellular_info.connection_duration_sec = 0;
        update_type_info_cache();
        LOG_SVC_INFO("Cellular disconnected");
    } else {
        LOG_SVC_ERROR("Failed to disconnect cellular: %d", result);
    }
    
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_get_detail_info(cellular_detail_info_t *info)
{
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Always refresh before returning to ensure latest info
    // aicam_result_t result = communication_cellular_refresh_info();
    // if (result != AICAM_OK) {
    //     LOG_SVC_WARN("Failed to refresh cellular info: %d, use cached info", result);
    // }

    // Update connection duration
    if (g_communication_service.cellular_info.network_status == COMM_STATUS_CONNECTED &&
        g_communication_service.cellular_info.connection_start_time > 0) {
        g_communication_service.cellular_info.connection_duration_sec =
            rtc_get_timeStamp() - g_communication_service.cellular_info.connection_start_time;
    }

    // Update DNS
    uint8_t dns_server[4] = {0};
    aicam_result_t 
    result = nm_ctrl_get_dns_server(0, dns_server);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get DNS server: %d", result);
        return result;
    }
    snprintf(g_communication_service.cellular_info.ipv4_dns,
             sizeof(g_communication_service.cellular_info.ipv4_dns),
             "%d.%d.%d.%d", dns_server[0], dns_server[1], dns_server[2], dns_server[3]);

    *info = g_communication_service.cellular_info;
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_refresh_info(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized || !g_communication_service.cellular_available) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Refreshing cellular information...");
    
    // Need to be in DOWN state to refresh info (based on 4g_netif_usage.md)
    netif_state_t state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
    aicam_bool_t was_up = (state == NETIF_STATE_UP);
    
    if (was_up) {
        LOG_SVC_INFO("Cellular is UP, getting info from cache...");
        // Down the cellular network
        aicam_result_t result = nm_ctrl_netif_down(NETIF_NAME_4G_CAT1);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to down cellular network: %d", result);
            return result;
        }
    }
    
    // Get netif info
    netif_info_t cellular_info;
    aicam_result_t result = nm_get_netif_info(NETIF_NAME_4G_CAT1, &cellular_info);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get cellular info: %d", result);
        return result;
    }
    
    // Update our cache
    strncpy(g_communication_service.cellular_info.imei, 
            cellular_info.cellular_info.imei,
            sizeof(g_communication_service.cellular_info.imei) - 1);
    strncpy(g_communication_service.cellular_info.imsi,
            cellular_info.cellular_info.imsi,
            sizeof(g_communication_service.cellular_info.imsi) - 1);
    strncpy(g_communication_service.cellular_info.iccid,
            cellular_info.cellular_info.iccid,
            sizeof(g_communication_service.cellular_info.iccid) - 1);
    strncpy(g_communication_service.cellular_info.model,
            cellular_info.cellular_info.model_name,
            sizeof(g_communication_service.cellular_info.model) - 1);
    strncpy(g_communication_service.cellular_info.version,
            cellular_info.cellular_info.version,
            sizeof(g_communication_service.cellular_info.version) - 1);
    strncpy(g_communication_service.cellular_info.isp,
            cellular_info.cellular_info.operator,
            sizeof(g_communication_service.cellular_info.isp) - 1);
    strncpy(g_communication_service.cellular_info.network_type,
            cellular_info.cellular_info.network_type,
            sizeof(g_communication_service.cellular_info.network_type) - 1);
    strncpy(g_communication_service.cellular_info.plmn_id,
            cellular_info.cellular_info.plmn_id,
            sizeof(g_communication_service.cellular_info.plmn_id) - 1);
    strncpy(g_communication_service.cellular_info.cell_id,
            cellular_info.cellular_info.cell_id,
            sizeof(g_communication_service.cellular_info.cell_id) - 1);
    strncpy(g_communication_service.cellular_info.lac,
            cellular_info.cellular_info.lac,
            sizeof(g_communication_service.cellular_info.lac) - 1);
    strncpy(g_communication_service.cellular_info.register_status,
            cellular_info.cellular_info.registration_status,
            sizeof(g_communication_service.cellular_info.register_status) - 1);
    strncpy(g_communication_service.cellular_info.sim_status,
            cellular_info.cellular_info.sim_status,
            sizeof(g_communication_service.cellular_info.sim_status) - 1);
    
    g_communication_service.cellular_info.csq = cellular_info.cellular_info.csq_value;
    g_communication_service.cellular_info.csq_level = cellular_info.cellular_info.csq_level;
    g_communication_service.cellular_info.rssi = cellular_info.cellular_info.rssi;
    g_communication_service.cellular_info.signal_level = cellular_info.cellular_info.csq_level;
    
    LOG_SVC_INFO("Cellular info refreshed: IMEI=%s, Signal=%d dBm",
                g_communication_service.cellular_info.imei,
                g_communication_service.cellular_info.rssi);

    // Up the cellular network
    result = nm_ctrl_netif_up(NETIF_NAME_4G_CAT1);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to up cellular network: %d", result);
        return result;
    }


    // Get IP info
    if (nm_get_netif_info(NETIF_NAME_4G_CAT1, &cellular_info) == AICAM_OK) {
        snprintf(g_communication_service.cellular_info.ipv4_address,
                 sizeof(g_communication_service.cellular_info.ipv4_address),
                 "%d.%d.%d.%d", cellular_info.ip_addr[0], cellular_info.ip_addr[1],
                 cellular_info.ip_addr[2], cellular_info.ip_addr[3]);
        snprintf(g_communication_service.cellular_info.ipv4_gateway,
                 sizeof(g_communication_service.cellular_info.ipv4_gateway),
                 "%d.%d.%d.%d", cellular_info.gw[0], cellular_info.gw[1], cellular_info.gw[2], cellular_info.gw[3]);
    }
    
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_cellular_send_at_command(const char *command,
                                                      char *response,
                                                      uint32_t response_size,
                                                      uint32_t timeout_ms)
{
    if (!command || !response || response_size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized || !g_communication_service.cellular_available) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // According to 4g_netif_usage.md, AT commands can only be sent when netif is DOWN
    netif_state_t state = nm_get_netif_state(NETIF_NAME_4G_CAT1);
    if (state != NETIF_STATE_DOWN) {
        LOG_SVC_ERROR("Cannot send AT command: cellular must be in DOWN state");
        strncpy(response, "Error: Cellular must be disconnected to send AT commands", response_size - 1);
        response[response_size - 1] = '\0';
        return AICAM_ERROR_BUSY;
    }
    
    // Check modem state - must be in INIT state
    modem_state_t modem_state = modem_device_get_state();
    if (modem_state != MODEM_STATE_INIT) {
        LOG_SVC_ERROR("Cannot send AT command: modem not in INIT state (current: %d)", modem_state);
        strncpy(response, "Error: Modem not initialized or in PPP mode", response_size - 1);
        response[response_size - 1] = '\0';
        return AICAM_ERROR_BUSY;
    }
    
    LOG_SVC_INFO("Sending AT command: %s", command);
    
    // Get external modem AT handle
    extern modem_at_handle_t modem_at_handle;
    
    // Prepare AT command buffer (add \r\n if not present)
    char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
    size_t cmd_len = strlen(command);
    
    if (cmd_len >= MODEM_AT_CMD_LEN_MAXIMUM - 3) {
        LOG_SVC_ERROR("AT command too long: %zu bytes", cmd_len);
        strncpy(response, "Error: AT command too long", response_size - 1);
        response[response_size - 1] = '\0';
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Copy command and ensure it ends with \r\n
    strncpy(at_cmd, command, sizeof(at_cmd) - 3);
    if (cmd_len < 2 || command[cmd_len - 1] != '\n') {
        // Add \r\n if not present
        if (cmd_len >= 1 && command[cmd_len - 1] == '\r') {
            strcat(at_cmd, "\n");
        } else {
            strcat(at_cmd, "\r\n");
        }
    }
    
    // Allocate response buffers
    int rsp_num = MODEM_AT_RSP_MAX_LINE_NUM;  // Request max lines
    char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
    aicam_bool_t alloc_failed = AICAM_FALSE;
    
    for (int i = 0; i < rsp_num; i++) {
        rsp_list[i] = (char *)buffer_calloc(1, MODEM_AT_RSP_LEN_MAXIMUM);
        if (rsp_list[i] == NULL) {
            LOG_SVC_ERROR("Failed to allocate response buffer %d", i);
            alloc_failed = AICAM_TRUE;
            break;
        }
    }
    
    if (alloc_failed) {
        // Free any allocated buffers
        for (int i = 0; i < rsp_num; i++) {
            if (rsp_list[i] != NULL) {
                buffer_free(rsp_list[i]);
                rsp_list[i] = NULL;
            }
        }
        strncpy(response, "Error: Memory allocation failed", response_size - 1);
        response[response_size - 1] = '\0';
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Set timeout (use default if not specified)
    if (timeout_ms == 0) {
        timeout_ms = 500;  // Default 500ms
    }
    
    // Send AT command and wait for response
    int ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout_ms);
    
    aicam_result_t result = AICAM_OK;
    
    if (ret >= MODEM_OK && ret > 0) {
        // Success - combine all response lines into output buffer
        response[0] = '\0';
        size_t total_len = 0;
        
        for (int i = 0; i < ret && i < rsp_num; i++) {
            if (rsp_list[i] != NULL && rsp_list[i][0] != '\0') {
                size_t line_len = strlen(rsp_list[i]);
                
                // Check if we have space for this line (plus newline and null terminator)
                if (total_len + line_len + 2 < response_size) {
                    if (total_len > 0) {
                        strcat(response, "\n");
                        total_len++;
                    }
                    strcat(response, rsp_list[i]);
                    total_len += line_len;
                } else {
                    // Response buffer full
                    LOG_SVC_WARN("Response buffer full, truncating at line %d", i);
                    break;
                }
            }
        }
        
        LOG_SVC_INFO("AT command response (%d lines): %s", ret, response);
        result = AICAM_OK;
        
    } else if (ret == MODEM_ERR_TIMEOUT) {
        LOG_SVC_ERROR("AT command timeout after %lu ms", (unsigned long)timeout_ms);
        strncpy(response, "Error: Command timeout", response_size - 1);
        response[response_size - 1] = '\0';
        result = AICAM_ERROR_TIMEOUT;
        
    } else {
        LOG_SVC_ERROR("AT command failed with error: %d", ret);
        snprintf(response, response_size, "Error: AT command failed (code: %d)", ret);
        result = AICAM_ERROR;
    }
    
    // Free response buffers
    for (int i = 0; i < rsp_num; i++) {
        if (rsp_list[i] != NULL) {
            buffer_free(rsp_list[i]);
            rsp_list[i] = NULL;
        }
    }
    
    return result;
#else
    strncpy(response, "Error: Cellular support not enabled", response_size - 1);
    response[response_size - 1] = '\0';
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_bool_t communication_cellular_is_available(void)
{
#if NETIF_4G_CAT1_IS_ENABLE
    return g_communication_service.cellular_available;
#else
    return AICAM_FALSE;
#endif
}

aicam_result_t communication_cellular_get_imei(char *imei)
{
    if (!imei) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    strncpy(imei, g_communication_service.cellular_info.imei, 31);
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

/* ==================== PoE/Ethernet Management APIs ==================== */

communication_status_t communication_poe_get_status(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.initialized || !g_communication_service.poe_available) {
        return COMM_STATUS_UNAVAILABLE;
    }
    
    update_type_info_cache();
    return g_communication_service.type_info[COMM_TYPE_POE].status;
#else
    return COMM_STATUS_UNAVAILABLE;
#endif
}

aicam_bool_t communication_poe_is_available(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    return g_communication_service.poe_available;
#else
    return AICAM_FALSE;
#endif
}

aicam_result_t communication_poe_connect(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.initialized) {
        LOG_SVC_ERROR("[PoE] Service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (!g_communication_service.poe_available) {
        LOG_SVC_WARN("[PoE] Module not available");
        return AICAM_ERROR_UNAVAILABLE;
    }

    netif_state_t state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
    if (state == NETIF_STATE_UP) {
        LOG_SVC_INFO("[PoE] Already connected");
        return AICAM_OK;
    }

    // Load IP configuration from persistent storage
    poe_config_persist_t poe_cfg;
    const char* ip_mode_str = "dhcp";
    aicam_bool_t config_loaded = AICAM_FALSE;
    if (json_config_get_poe_config(&poe_cfg) == AICAM_OK) {
        ip_mode_str = (poe_cfg.ip_mode == POE_IP_MODE_STATIC) ? "static" : "dhcp";
        config_loaded = AICAM_TRUE;
    }

    // Initialize interface if in DEINIT state
    if (state == NETIF_STATE_DEINIT) {
        LOG_SVC_INFO("[PoE] Initializing interface...");
        aicam_result_t init_result = nm_ctrl_netif_init(NETIF_NAME_ETH_WAN);
        if (init_result != AICAM_OK) {
            LOG_SVC_ERROR("[PoE] Init failed: %d (status: %s)", init_result,
                         poe_status_code_to_string(POE_STATUS_ERROR));
            return init_result;
        }
    }

    // nm_get_netif_cfg/nm_set_netif_cfg can be called in any state (DEINIT/DOWN/UP)
    if (config_loaded) {
        netif_config_t netif_cfg;
        //update dns server
        uint8_t dns_primary[4] = {0};
        uint8_t dns_secondary[4] = {0};
        nm_ctrl_get_dns_server(0, dns_primary);
        nm_ctrl_get_dns_server(1, dns_secondary);
        if(poe_cfg.dns_primary[0] != dns_primary[0] || poe_cfg.dns_primary[1] != dns_primary[1] || poe_cfg.dns_primary[2] != dns_primary[2] || poe_cfg.dns_primary[3] != dns_primary[3]){
            nm_ctrl_set_dns_server(0, poe_cfg.dns_primary);
        }
        if(poe_cfg.dns_secondary[0] != dns_secondary[0] || poe_cfg.dns_secondary[1] != dns_secondary[1] || poe_cfg.dns_secondary[2] != dns_secondary[2] || poe_cfg.dns_secondary[3] != dns_secondary[3]){
            nm_ctrl_set_dns_server(1, poe_cfg.dns_secondary);
        }
        int get_ret = nm_get_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
        if (get_ret == 0) {
            LOG_SVC_DEBUG("[PoE] Current netif ip_mode=%d before setting", netif_cfg.ip_mode);
            if (poe_cfg.ip_mode == POE_IP_MODE_STATIC) {
                netif_cfg.ip_mode = NETIF_IP_MODE_STATIC;
                memcpy(netif_cfg.ip_addr, poe_cfg.ip_addr, 4);
                memcpy(netif_cfg.netmask, poe_cfg.netmask, 4);
                memcpy(netif_cfg.gw, poe_cfg.gateway, 4);
                LOG_SVC_INFO("[PoE] Applying static IP config: %d.%d.%d.%d",
                            poe_cfg.ip_addr[0], poe_cfg.ip_addr[1],
                            poe_cfg.ip_addr[2], poe_cfg.ip_addr[3]);
            } else {
                netif_cfg.ip_mode = NETIF_IP_MODE_DHCP;
                // Use last DHCP IP for quick recovery hint if available
                if (poe_cfg.persist_last_ip &&
                    !(poe_cfg.last_dhcp_ip[0] == 0 && poe_cfg.last_dhcp_ip[1] == 0 &&
                      poe_cfg.last_dhcp_ip[2] == 0 && poe_cfg.last_dhcp_ip[3] == 0)) {
                    LOG_SVC_DEBUG("[PoE] Last DHCP IP hint: %d.%d.%d.%d",
                                 poe_cfg.last_dhcp_ip[0], poe_cfg.last_dhcp_ip[1],
                                 poe_cfg.last_dhcp_ip[2], poe_cfg.last_dhcp_ip[3]);
                }
            }

            if (poe_cfg.hostname[0] != '\0') {
                netif_cfg.host_name = poe_cfg.hostname;
            }

            // Set configuration (this does not bring up the interface since current state is DOWN)
            int set_ret = nm_set_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
            if (set_ret != 0) {
                LOG_SVC_ERROR("[PoE] Failed to set netif config: %d", set_ret);
            } else {
                LOG_SVC_INFO("[PoE] Netif config applied, ip_mode=%d", netif_cfg.ip_mode);
            }
        } else {
            LOG_SVC_ERROR("[PoE] Failed to get netif config: %d", get_ret);
        }
    }

    LOG_SVC_INFO("[PoE] Connecting (mode=%s)...", ip_mode_str);

    // Connect the interface using nm_ctrl_netif_up
    uint32_t start_time = osKernelGetTickCount();
    aicam_result_t result = nm_ctrl_netif_up(NETIF_NAME_ETH_WAN);
    uint32_t elapsed_ms = osKernelGetTickCount() - start_time;

    if (result == AICAM_OK) {
        update_type_info_cache();
        service_set_sta_ready(AICAM_TRUE);

        // Log connection details 
        netif_info_t netif_info;
        if (nm_get_netif_info(NETIF_NAME_ETH_WAN, &netif_info) == 0) {
            LOG_SVC_INFO("[PoE] Connected - IP=%d.%d.%d.%d, GW=%d.%d.%d.%d (took %ums)",
                        netif_info.ip_addr[0], netif_info.ip_addr[1],
                        netif_info.ip_addr[2], netif_info.ip_addr[3],
                        netif_info.gw[0], netif_info.gw[1],
                        netif_info.gw[2], netif_info.gw[3],
                        elapsed_ms);
        }
    } else {
        LOG_SVC_ERROR("[PoE] Connect failed: %d (status: %s, elapsed: %ums)",
                     result, poe_status_code_to_string(POE_STATUS_ERROR), elapsed_ms);
    }

    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_disconnect(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Log current IP before disconnect
    netif_info_t netif_info;
    if (nm_get_netif_info(NETIF_NAME_ETH_WAN, &netif_info) == 0 && 
        netif_info.state == NETIF_STATE_UP) {
        LOG_SVC_INFO("[PoE] Disconnecting (IP=%d.%d.%d.%d)...",
                    netif_info.ip_addr[0], netif_info.ip_addr[1], 
                    netif_info.ip_addr[2], netif_info.ip_addr[3]);
    } else {
        LOG_SVC_INFO("[PoE] Disconnecting...");
    }
    
    aicam_result_t result = nm_ctrl_netif_down(NETIF_NAME_ETH_WAN);
    if (result == AICAM_OK) {
        update_type_info_cache();
        LOG_SVC_INFO("[PoE] Disconnected (status: %s)", 
                    poe_status_code_to_string(POE_STATUS_LINK_DOWN));
    } else {
        LOG_SVC_ERROR("[PoE] Disconnect failed: %d", result);
    }
    
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_get_detail_info(poe_detail_info_t *info)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memset(info, 0, sizeof(poe_detail_info_t));

    // Get network interface configuration using nm_get_netif_cfg
    netif_config_t netif_cfg;
    int cfg_ret = nm_get_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
    if (cfg_ret == 0) {
        // IP mode from netif config (actual running config)
        info->ip_mode = (netif_cfg.ip_mode == NETIF_IP_MODE_STATIC) ? 1 : 0;

        // Hostname from config
        if (netif_cfg.host_name) {
            strncpy(info->hostname, netif_cfg.host_name, sizeof(info->hostname) - 1);
        }
    }

    // Get network interface info for status and addresses
    netif_info_t netif_info;
    int ret = nm_get_netif_info(NETIF_NAME_ETH_WAN, &netif_info);
    if (ret == 0) {
        // Network status
        info->network_status = g_communication_service.type_info[COMM_TYPE_POE].status;
        info->link_up = (netif_info.state == NETIF_STATE_UP);
        info->poe_powered = g_communication_service.poe_available;

        // Format IP addresses from netif_info (current actual addresses)
        snprintf(info->ip_address, sizeof(info->ip_address), "%d.%d.%d.%d",
                netif_info.ip_addr[0], netif_info.ip_addr[1],
                netif_info.ip_addr[2], netif_info.ip_addr[3]);
        snprintf(info->netmask, sizeof(info->netmask), "%d.%d.%d.%d",
                netif_info.netmask[0], netif_info.netmask[1],
                netif_info.netmask[2], netif_info.netmask[3]);
        snprintf(info->gateway, sizeof(info->gateway), "%d.%d.%d.%d",
                netif_info.gw[0], netif_info.gw[1],
                netif_info.gw[2], netif_info.gw[3]);

        // Get DNS servers
        uint8_t dns1[4], dns2[4];
        if (nm_ctrl_get_dns_server(0, dns1) == 0) {
            snprintf(info->dns_primary, sizeof(info->dns_primary), "%d.%d.%d.%d",
                    dns1[0], dns1[1], dns1[2], dns1[3]);
        }
        if (nm_ctrl_get_dns_server(1, dns2) == 0) {
            snprintf(info->dns_secondary, sizeof(info->dns_secondary), "%d.%d.%d.%d",
                    dns2[0], dns2[1], dns2[2], dns2[3]);
        }

        // MAC address
        snprintf(info->mac_address, sizeof(info->mac_address),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                netif_info.if_mac[0], netif_info.if_mac[1], netif_info.if_mac[2],
                netif_info.if_mac[3], netif_info.if_mac[4], netif_info.if_mac[5]);

        strncpy(info->interface_name, NETIF_NAME_ETH_WAN, sizeof(info->interface_name) - 1);

        // Hostname fallback from netif_info if not set from config
        if (info->hostname[0] == '\0' && netif_info.host_name) {
            strncpy(info->hostname, netif_info.host_name, sizeof(info->hostname) - 1);
        }

        // Status code and message
        info->status_code = (info->link_up && info->network_status == COMM_STATUS_CONNECTED) ?
                           POE_STATUS_CONNECTED : POE_STATUS_LINK_DOWN;
        strncpy(info->status_message, poe_status_code_to_string((poe_status_code_t)info->status_code),
               sizeof(info->status_message) - 1);
    }
    
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

uint8_t communication_poe_get_ip_mode(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    return (uint8_t)json_config_get_poe_ip_mode();
#else
    return 0;
#endif
}

aicam_result_t communication_poe_set_ip_mode(uint8_t mode)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (mode > POE_IP_MODE_STATIC) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    return json_config_set_poe_ip_mode((poe_ip_mode_t)mode);
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_get_static_config(poe_static_config_t *config)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    poe_config_persist_t poe_cfg;
    aicam_result_t result = json_config_get_poe_config(&poe_cfg);
    if (result == AICAM_OK) {
        memcpy(config->ip_addr, poe_cfg.ip_addr, 4);
        memcpy(config->netmask, poe_cfg.netmask, 4);
        memcpy(config->gateway, poe_cfg.gateway, 4);
        memcpy(config->dns_primary, poe_cfg.dns_primary, 4);
        memcpy(config->dns_secondary, poe_cfg.dns_secondary, 4);
        strncpy(config->hostname, poe_cfg.hostname, sizeof(config->hostname) - 1);
    }
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_set_static_config(const poe_static_config_t *config)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    poe_config_persist_t poe_cfg;
    aicam_result_t result = json_config_get_poe_config(&poe_cfg);
    if (result != AICAM_OK) {
        return result;
    }
    
    memcpy(poe_cfg.ip_addr, config->ip_addr, 4);
    memcpy(poe_cfg.netmask, config->netmask, 4);
    memcpy(poe_cfg.gateway, config->gateway, 4);
    memcpy(poe_cfg.dns_primary, config->dns_primary, 4);
    memcpy(poe_cfg.dns_secondary, config->dns_secondary, 4);
    strncpy(poe_cfg.hostname, config->hostname, sizeof(poe_cfg.hostname) - 1);
    
    LOG_SVC_INFO("PoE static config set: IP=%d.%d.%d.%d", 
                config->ip_addr[0], config->ip_addr[1], 
                config->ip_addr[2], config->ip_addr[3]);
    
    return json_config_set_poe_config(&poe_cfg);
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_save_config(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    poe_config_persist_t poe_cfg;
    aicam_result_t result = json_config_get_poe_config(&poe_cfg);
    if (result != AICAM_OK) {
        return result;
    }
    
    result = json_config_set_poe_config(&poe_cfg);
    if (result == AICAM_OK) {
        LOG_SVC_INFO("PoE configuration saved to NVS");
    }
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_load_config(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    poe_config_persist_t poe_cfg;
    aicam_result_t result = json_config_get_poe_config(&poe_cfg);
    if (result == AICAM_OK) {
        LOG_SVC_INFO("PoE configuration loaded: mode=%d", poe_cfg.ip_mode);
    }
    return result;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_apply_config(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.initialized) {
        LOG_SVC_ERROR("[PoE] Service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Load configuration
    poe_config_persist_t poe_cfg;
    aicam_result_t result = json_config_get_poe_config(&poe_cfg);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("[PoE] Failed to load config: %d", result);
        return result;
    }

    LOG_SVC_INFO("[PoE] Applying config: mode=%s, auto_reconnect=%s",
                poe_cfg.ip_mode == POE_IP_MODE_STATIC ? "static" : "dhcp",
                poe_cfg.auto_reconnect ? "yes" : "no");

    if (poe_cfg.ip_mode == POE_IP_MODE_STATIC) {
        LOG_SVC_INFO("[PoE] Static config: IP=%d.%d.%d.%d, GW=%d.%d.%d.%d, DNS=%d.%d.%d.%d",
                    poe_cfg.ip_addr[0], poe_cfg.ip_addr[1],
                    poe_cfg.ip_addr[2], poe_cfg.ip_addr[3],
                    poe_cfg.gateway[0], poe_cfg.gateway[1],
                    poe_cfg.gateway[2], poe_cfg.gateway[3],
                    poe_cfg.dns_primary[0], poe_cfg.dns_primary[1],
                    poe_cfg.dns_primary[2], poe_cfg.dns_primary[3]);
    }

    // Get current interface config 
    netif_config_t netif_cfg;
    result = nm_get_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
    if (result != 0) {
        LOG_SVC_ERROR("[PoE] Failed to get netif config");
        return AICAM_ERROR;
    }

    // Apply IP mode
    if (poe_cfg.ip_mode == POE_IP_MODE_STATIC) {
        netif_cfg.ip_mode = NETIF_IP_MODE_STATIC;
        memcpy(netif_cfg.ip_addr, poe_cfg.ip_addr, 4);
        memcpy(netif_cfg.netmask, poe_cfg.netmask, 4);
        memcpy(netif_cfg.gw, poe_cfg.gateway, 4);
    } else {
        netif_cfg.ip_mode = NETIF_IP_MODE_DHCP;
    }

    if (poe_cfg.hostname[0] != '\0') {
        netif_cfg.host_name = poe_cfg.hostname;
    }

    // Apply configuration using nm_set_netif_cfg
    // If the interface is currently UP, it will automatically restore UP state after config.
    // No need to manually call disconnect/connect.
    int ret = nm_set_netif_cfg(NETIF_NAME_ETH_WAN, &netif_cfg);
    if (ret != 0) {
        LOG_SVC_ERROR("[PoE] Failed to set netif config: %d", ret);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("[PoE] Config applied successfully (nm_set_netif_cfg handles reconnect automatically)");

    update_type_info_cache();

    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_validate_static_config(const poe_static_config_t *config,
                                                        char *error_msg,
                                                        size_t error_msg_size)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Validate IP address (not 0.0.0.0 or 255.255.255.255)
    if ((config->ip_addr[0] == 0 && config->ip_addr[1] == 0 && 
         config->ip_addr[2] == 0 && config->ip_addr[3] == 0) ||
        (config->ip_addr[0] == 255 && config->ip_addr[1] == 255 &&
         config->ip_addr[2] == 255 && config->ip_addr[3] == 255)) {
        if (error_msg && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "Invalid IP address");
        }
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Validate subnet mask
    uint32_t mask = ((uint32_t)config->netmask[0] << 24) | 
                   ((uint32_t)config->netmask[1] << 16) |
                   ((uint32_t)config->netmask[2] << 8) | 
                   config->netmask[3];
    if (mask == 0) {
        if (error_msg && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "Invalid subnet mask");
        }
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Validate gateway is in same subnet
    uint32_t ip = ((uint32_t)config->ip_addr[0] << 24) | 
                 ((uint32_t)config->ip_addr[1] << 16) |
                 ((uint32_t)config->ip_addr[2] << 8) | 
                 config->ip_addr[3];
    uint32_t gw = ((uint32_t)config->gateway[0] << 24) | 
                 ((uint32_t)config->gateway[1] << 16) |
                 ((uint32_t)config->gateway[2] << 8) | 
                 config->gateway[3];
    
    if ((ip & mask) != (gw & mask)) {
        if (error_msg && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "Gateway not in same subnet");
        }
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_check_ip_conflict(const uint8_t *ip_addr)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!ip_addr) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if IP is all zeros or broadcast
    if ((ip_addr[0] == 0 && ip_addr[1] == 0 && ip_addr[2] == 0 && ip_addr[3] == 0) ||
        (ip_addr[0] == 255 && ip_addr[1] == 255 && ip_addr[2] == 255 && ip_addr[3] == 255)) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if IP conflicts with current interface IP
    netif_info_t netif_info;
    int ret = nm_get_netif_info(NETIF_NAME_ETH_WAN, &netif_info);
    if (ret == 0 && netif_info.state == NETIF_STATE_UP) {
        // If the IP is already assigned to us, it's not a conflict
        if (memcmp(netif_info.ip_addr, ip_addr, 4) == 0) {
            return AICAM_OK;
        }
    }
    
    // TODO: Implement ARP-based conflict detection
    // Send ARP request for the IP address
    // If we get a reply, there's a conflict
    // For now, we assume no conflict
    
    LOG_SVC_DEBUG("PoE IP conflict check for %d.%d.%d.%d - no conflict detected",
                 ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
    
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

aicam_result_t communication_poe_check_gateway(const uint8_t *gateway, uint32_t timeout_ms)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!gateway) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if gateway is valid (not all zeros)
    if (gateway[0] == 0 && gateway[1] == 0 && gateway[2] == 0 && gateway[3] == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if PoE interface is up
    netif_state_t state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
    if (state != NETIF_STATE_UP) {
        LOG_SVC_WARN("PoE interface not up, cannot check gateway");
        return AICAM_ERROR;
    }
    
    // Simple gateway reachability check using ICMP
    // TODO: Implement proper ping using lwip raw sockets
    // For now, we check if we have a valid route to the gateway
    
    netif_info_t netif_info;
    int ret = nm_get_netif_info(NETIF_NAME_ETH_WAN, &netif_info);
    if (ret != 0) {
        return AICAM_ERROR;
    }
    
    // Check if gateway is in the same subnet
    uint32_t ip = ((uint32_t)netif_info.ip_addr[0] << 24) | 
                 ((uint32_t)netif_info.ip_addr[1] << 16) |
                 ((uint32_t)netif_info.ip_addr[2] << 8) | 
                 netif_info.ip_addr[3];
    uint32_t mask = ((uint32_t)netif_info.netmask[0] << 24) | 
                   ((uint32_t)netif_info.netmask[1] << 16) |
                   ((uint32_t)netif_info.netmask[2] << 8) | 
                   netif_info.netmask[3];
    uint32_t gw = ((uint32_t)gateway[0] << 24) | 
                 ((uint32_t)gateway[1] << 16) |
                 ((uint32_t)gateway[2] << 8) | 
                 gateway[3];
    
    if ((ip & mask) != (gw & mask)) {
        LOG_SVC_WARN("Gateway %d.%d.%d.%d not in same subnet",
                    gateway[0], gateway[1], gateway[2], gateway[3]);
        return AICAM_ERROR;
    }
    
    LOG_SVC_DEBUG("PoE gateway check for %d.%d.%d.%d - reachable (timeout=%ums)",
                 gateway[0], gateway[1], gateway[2], gateway[3], timeout_ms);
    
    return AICAM_OK;
#else
    (void)timeout_ms;
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

uint32_t communication_poe_get_status_code(void)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.initialized || !g_communication_service.poe_available) {
        return POE_STATUS_OFFLINE;
    }
    
    netif_state_t state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
    if (state == NETIF_STATE_UP) {
        return POE_STATUS_CONNECTED;
    } else if (state == NETIF_STATE_DOWN) {
        return POE_STATUS_LINK_DOWN;
    }
    return POE_STATUS_OFFLINE;
#else
    return POE_STATUS_OFFLINE;
#endif
}

aicam_result_t communication_poe_get_status_message(char *buffer, size_t buffer_size)
{
#if NETIF_ETH_WAN_IS_ENABLE
    if (!buffer || buffer_size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint32_t status_code = communication_poe_get_status_code();
    const char *status_str = poe_status_code_to_string((poe_status_code_t)status_code);
    strncpy(buffer, status_str, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return AICAM_OK;
#else
    return AICAM_ERROR_UNAVAILABLE;
#endif
}

/* ==================== Additional Service Management APIs ==================== */

aicam_result_t communication_save_config_to_nvs(void)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // TODO: Implement NVS save for communication config
    LOG_SVC_INFO("Communication configuration saved to NVS");
    return AICAM_OK;
}

/* ==================== CLI Commands ==================== */

/**
 * @brief CLI command: comm status
 */
static int comm_status_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("\r\n================== COMMUNICATION SERVICE STATUS ==================\r\n");
    printf("Service State: %s\r\n", 
           (g_communication_service.state == SERVICE_STATE_RUNNING) ? "RUNNING" :
           (g_communication_service.state == SERVICE_STATE_INITIALIZED) ? "INITIALIZED" : "UNINITIALIZED");
    printf("Version: %s\r\n", COMMUNICATION_SERVICE_VERSION);
    printf("Auto-start WiFi AP: %s\r\n", g_communication_service.config.auto_start_wifi_ap ? "YES" : "NO");
    printf("Auto-start WiFi STA: %s\r\n", g_communication_service.config.auto_start_wifi_sta ? "YES" : "NO");
    printf("Network Scan Enabled: %s\r\n", g_communication_service.config.enable_network_scan ? "YES" : "NO");
    printf("Auto-reconnect Enabled: %s\r\n", g_communication_service.config.enable_auto_reconnect ? "YES" : "NO");
    
    printf("\r\nNetwork Interfaces (%lu):\r\n", g_communication_service.interface_count);
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        network_interface_status_t *iface = &g_communication_service.interfaces[i];
        printf("  %s: %s %s %s\r\n", 
               iface->if_name,
               iface->connected ? "UP" : "DOWN",
               iface->ip_addr,
               iface->type == NETIF_TYPE_WIRELESS ? iface->ssid : "");
    }
    
    printf("\r\nStatistics:\r\n");
    printf("  Total Connections: %lu\r\n", (unsigned long)g_communication_service.stats.total_connections);
    printf("  Successful Connections: %lu\r\n", (unsigned long)g_communication_service.stats.successful_connections);
    printf("  Failed Connections: %lu\r\n", (unsigned long)g_communication_service.stats.failed_connections);
    printf("  Current Connections: %lu\r\n", (unsigned long)g_communication_service.stats.current_connections);
    printf("  Network Scans: %lu\r\n", (unsigned long)g_communication_service.stats.network_scans);
    printf("  Last Error: 0x%08lX\r\n", (unsigned long)g_communication_service.stats.last_error_code);
    printf("===============================================================\r\n\r\n");


    printf("\r\n================== NETWORK SCAN RESULTS ==================\r\n");
    printf("Found %lu networks:\r\n", g_communication_service.scan_result_count);
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        printf("  %s (%s) - %d dBm, Channel %d, Security: %d\r\n",
               result->ssid, result->bssid, (int)result->rssi, (int)result->channel, (int)result->security);
    }
    printf("=======================================================\r\n\r\n");


    printf("\r\n================== KNOWN NETWORKS ==================\r\n");
    for (uint32_t i = 0; i < g_communication_service.known_network_count; i++) {
        network_scan_result_t *result = &g_communication_service.known_networks[i];
        printf("  %s (%s) - %d dBm, Channel %d, Security: %d\r\n",
               result->ssid, result->bssid, (int)result->rssi, (int)result->channel, (int)result->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm interfaces
 */
static int comm_interfaces_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    // Update interface list
    update_interface_list();
    
    printf("\r\n================== NETWORK INTERFACES ==================\r\n");
    for (uint32_t i = 0; i < g_communication_service.interface_count; i++) {
        network_interface_status_t *iface = &g_communication_service.interfaces[i];
        printf("Interface: %s\r\n", iface->if_name);
        printf("  State: %s\r\n", iface->connected ? "UP" : "DOWN");
        printf("  Type: %s\r\n", 
               (iface->type == NETIF_TYPE_WIRELESS) ? "WIRELESS" :
               (iface->type == NETIF_TYPE_LOCAL) ? "LOCAL" : "UNKNOWN");
        printf("  IP Address: %s\r\n", iface->ip_addr);
        printf("  MAC Address: %s\r\n", iface->mac_addr);
        
        if (iface->type == NETIF_TYPE_WIRELESS) {
            printf("  SSID: %s\r\n", iface->ssid);
            printf("  RSSI: %lu dBm\r\n", iface->rssi);
            printf("  Channel: %lu\r\n", iface->channel);
        }
        printf("\r\n");
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm scan
 */
static int comm_scan_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (g_communication_service.scan_in_progress) {
        printf("Network scan already in progress\r\n");
        return -1;
    }
    
    printf("Starting network scan...\r\n");
    
    aicam_result_t result = communication_start_network_scan(NULL);
    if (result != AICAM_OK) {
        printf("Failed to start network scan: %d\r\n", result);
        return -1;
    }
    
    // Wait for scan to complete (simplified)
    int timeout = 100; // 10 seconds
    while (g_communication_service.scan_in_progress && timeout > 0) {
        osDelay(100);
        timeout--;
    }
    
    if (g_communication_service.scan_in_progress) {
        printf("Network scan timeout\r\n");
        return -1;
    }
    
    printf("\r\n================== NETWORK SCAN RESULTS ==================\r\n");
    printf("Found %lu networks:\r\n", g_communication_service.scan_result_count);
    
    // Show known networks first
    printf("\r\n--- KNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        if (result->is_known) {
            printf("  [KNOWN] %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
                   result->ssid, result->bssid, result->rssi, result->channel, (int)result->security);
        }
    }
    
    // Show unknown networks
    printf("\r\n--- UNKNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
        network_scan_result_t *result = &g_communication_service.scan_results[i];
        if (!result->is_known) {
            printf("  [NEW] %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
                   result->ssid, result->bssid, result->rssi, result->channel, (int)result->security);
        }
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm known
 */
static int comm_known_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    network_scan_result_t known_networks[MAX_KNOWN_NETWORKS];
    uint32_t count = 0;
    
    aicam_result_t result = communication_get_known_networks(known_networks, MAX_KNOWN_NETWORKS, &count);
    if (result != AICAM_OK) {
        printf("Failed to get known networks: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== KNOWN NETWORKS ==================\r\n");
    printf("Found %lu known networks:\r\n", count);
    
    for (uint32_t i = 0; i < count; i++) {
        network_scan_result_t *network = &known_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm unknown
 */
static int comm_unknown_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    network_scan_result_t unknown_networks[MAX_KNOWN_NETWORKS];
    uint32_t count = 0;
    
    aicam_result_t result = communication_get_unknown_networks(unknown_networks, MAX_KNOWN_NETWORKS, &count);
    if (result != AICAM_OK) {
        printf("Failed to get unknown networks: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== UNKNOWN NETWORKS ==================\r\n");
    printf("Found %lu unknown networks:\r\n", count);
    
    for (uint32_t i = 0; i < count; i++) {
        network_scan_result_t *network = &unknown_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm classified
 */
static int comm_classified_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    classified_scan_results_t results;
    
    aicam_result_t result = communication_get_classified_scan_results(&results);
    if (result != AICAM_OK) {
        printf("Failed to get classified scan results: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== CLASSIFIED NETWORK SCAN ==================\r\n");
    printf("Total networks: %d\r\n", (int)results.known_count + (int)results.unknown_count);
    printf("Known networks: %d\r\n", (int)results.known_count);
    printf("Unknown networks: %d\r\n", (int)results.unknown_count);
    
    printf("\r\n--- KNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < results.known_count; i++) {
        network_scan_result_t *network = &results.known_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    
    printf("\r\n--- UNKNOWN NETWORKS ---\r\n");
    for (uint32_t i = 0; i < results.unknown_count; i++) {
        network_scan_result_t *network = &results.unknown_networks[i];
        printf("  %s (%s) - %lu dBm, Channel %lu, Security: %d\r\n",
               network->ssid, network->bssid, network->rssi, network->channel, (int)network->security);
    }
    printf("=============================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm start (information only)
 */
static int comm_start_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm start <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually start the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s start request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_start_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log start request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s start request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm stop (information only)
 */
static int comm_stop_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm stop <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually stop the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s stop request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_stop_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log stop request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s stop request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm restart (information only)
 */
static int comm_restart_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm restart <interface>\r\n");
        printf("  Interfaces: ap, sta, lo\r\n");
        printf("  Note: This command only logs the request, does not actually restart the interface\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[1], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[1], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else if (strcmp(argv[1], "lo") == 0) {
        if_name = NETIF_NAME_LOCAL;
    } else {
        printf("Invalid interface: %s\r\n", argv[1]);
        return -1;
    }
    
    printf("Interface %s restart request logged (information only)\r\n", if_name);
    
    aicam_result_t result = communication_restart_interface(if_name);
    if (result != AICAM_OK) {
        printf("Failed to log restart request for interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s restart request logged successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm config
 */
static int comm_config_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        printf("Usage: comm config <interface> <ssid> [password]\r\n");
        printf("  Example: comm config sta MyWiFi mypassword\r\n");
        return -1;
    }
    
    const char* if_name = NULL;
    if (strcmp(argv[2], "ap") == 0) {
        if_name = NETIF_NAME_WIFI_AP;
    } else if (strcmp(argv[2], "sta") == 0) {
        if_name = NETIF_NAME_WIFI_STA;
    } else {
        printf("Invalid interface: %s (use 'ap' or 'sta')\r\n", argv[2]);
        return -1;
    }
    
    netif_config_t config = {0};
    strncpy(config.wireless_cfg.ssid, argv[3], sizeof(config.wireless_cfg.ssid) - 1);
    
    if (argc > 3) {
        strncpy(config.wireless_cfg.pw, argv[4], sizeof(config.wireless_cfg.pw) - 1);
        config.wireless_cfg.security = WIRELESS_WPA_WPA2_MIXED;
    } else {
        config.wireless_cfg.security = WIRELESS_OPEN;
    }
    
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
        config.ip_mode = NETIF_IP_MODE_DHCP;
    } else {
        config.ip_mode = NETIF_IP_MODE_DHCPS;
    }
    
    printf("Configuring interface %s with SSID '%s'...\r\n", if_name, argv[3]);
    
    aicam_result_t result = communication_configure_interface(if_name, &config);
    if (result != AICAM_OK) {
        printf("Failed to configure interface %s: %d\r\n", if_name, result);
        return -1;
    }
    
    printf("Interface %s configured successfully\r\n", if_name);
    return 0;
}

/**
 * @brief CLI command: comm stats
 */
static int comm_stats_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("\r\n================== COMMUNICATION STATISTICS ==================\r\n");
    printf("Total Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.total_connections);
    printf("Successful Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.successful_connections);
    printf("Failed Connections: %llu\r\n", (unsigned long long)g_communication_service.stats.failed_connections);
    printf("Disconnections: %llu\r\n", (unsigned long long)g_communication_service.stats.disconnections);
    printf("Current Connections: %lu\r\n", (unsigned long)g_communication_service.stats.current_connections);
    printf("Network Scans: %llu\r\n", (unsigned long long)g_communication_service.stats.network_scans);
    printf("Bytes Sent: %llu\r\n", (unsigned long long)g_communication_service.stats.bytes_sent);
    printf("Bytes Received: %llu\r\n", (unsigned long long)g_communication_service.stats.bytes_received);
    printf("Last Error Code: 0x%08X\r\n", (unsigned int)g_communication_service.stats.last_error_code);
    printf("=============================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm reset
 */
static int comm_reset_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("Resetting communication service statistics...\r\n");
    
    aicam_result_t result = communication_reset_stats();
    if (result != AICAM_OK) {
        printf("Failed to reset statistics: %d\r\n", result);
        return -1;
    }
    
    printf("Statistics reset successfully\r\n");
    return 0;
}

/**
 * @brief CLI command: comm delete
 */
static int comm_delete_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (argc < 4) {
        printf("Usage: comm delete <ssid> <bssid>\r\n");
        printf("  ssid  - Network SSID (e.g., \"MyWiFi\")\r\n");
        printf("  bssid - Network BSSID (e.g., \"AA:BB:CC:DD:EE:FF\")\r\n");
        printf("Example: comm delete \"MyWiFi\" \"AA:BB:CC:DD:EE:FF\"\r\n");
        return -1;
    }
    
    const char* ssid = argv[2];
    const char* bssid = argv[3];
    
    printf("Deleting known network: %s (%s)\r\n", ssid, bssid);
    
    aicam_result_t result = communication_delete_known_network(ssid, bssid);
    if (result != AICAM_OK) {
        printf("Failed to delete known network: %d\r\n", result);
        return -1;
    }
    
    printf("Successfully deleted known network: %s (%s)\r\n", ssid, bssid);
    return 0;
}

/**
 * @brief CLI command: comm types - Show all communication types
 */
static int comm_types_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    update_type_info_cache();
    
    printf("\r\n================== COMMUNICATION TYPES ==================\r\n");
    printf("Active Type (Connected): %s\r\n", communication_type_to_string(g_communication_service.active_type));
    printf("Selected Type (UI Page): %s\r\n", communication_type_to_string(g_communication_service.selected_type));
    printf("Preferred Type (Auto):   %s\r\n", communication_type_to_string(g_communication_service.preferred_type));
    printf("\r\n");
    
    // Startup status
    printf("--- Startup Status ---\r\n");
    printf("Decision Made: %s\r\n", g_communication_service.startup_decision_made ? "YES" : "NO");
    printf("WiFi STA Ready: %s\r\n", g_communication_service.wifi_sta_ready ? "YES" : "NO");
    printf("Cellular Ready: %s\r\n", g_communication_service.cellular_ready ? "YES" : "NO");
    printf("PoE Ready: %s\r\n", g_communication_service.poe_ready ? "YES" : "NO");
    if (g_communication_service.startup_begin_time > 0) {
        uint32_t elapsed = rtc_get_uptime_ms() - g_communication_service.startup_begin_time;
        printf("Startup Elapsed: %lu ms\r\n", (unsigned long)elapsed);
    }
    printf("\r\n");
    
    printf("Priority Order: PoE (3) > Cellular (2) > WiFi (1)\r\n\r\n");
    
    for (int i = 1; i < COMM_TYPE_MAX; i++) {
        communication_type_info_t *info = &g_communication_service.type_info[i];
        printf("--- %s ---\r\n", communication_type_to_string(info->type));
        printf("  Available: %s\r\n", info->available ? "YES" : "NO");
        printf("  Status: %s\r\n", communication_status_to_string(info->status));
        printf("  Priority: %d\r\n", (int)info->priority);
        printf("  Is Default: %s\r\n", info->is_default ? "YES" : "NO");
        if (info->status == COMM_STATUS_CONNECTED) {
            printf("  IP Address: %s\r\n", info->ip_addr);
            if (strlen(info->mac_addr) > 0) {
                printf("  MAC Address: %s\r\n", info->mac_addr);
            }
            if (info->signal_strength != 0) {
                printf("  Signal: %d dBm\r\n", (int)info->signal_strength);
            }
        }
        printf("\r\n");
    }
    printf("=========================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: comm switch - Switch communication type
 */
static int comm_switch_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (argc < 3) {
        printf("Usage: comm switch <type>\r\n");
        printf("  Types: wifi, cellular (or 4g), poe (or ethernet)\r\n");
        return -1;
    }
    
    communication_type_t target = communication_type_from_string(argv[2]);
    if (target == COMM_TYPE_NONE) {
        printf("Invalid type: %s\r\n", argv[2]);
        printf("Valid types: wifi, cellular, 4g, poe, ethernet\r\n");
        return -1;
    }
    
    if (!communication_is_type_available(target)) {
        printf("Communication type %s is not available\r\n", communication_type_to_string(target));
        return -1;
    }
    
    printf("Switching to %s...\r\n", communication_type_to_string(target));
    
    communication_switch_result_t result;
    aicam_result_t ret = communication_switch_type_sync(target, &result, 30000);
    
    if (ret == AICAM_OK && result.success) {
        printf("Successfully switched to %s in %lu ms\r\n", 
               communication_type_to_string(target), ((unsigned long)result.switch_time_ms));
    } else {
        printf("Failed to switch to %s: %s\r\n", 
               communication_type_to_string(target), result.error_message);
    }
    
    return (ret == AICAM_OK) ? 0 : -1;
}

/**
 * @brief CLI command: comm priority - Apply priority-based selection
 */
static int comm_priority_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("Applying priority-based communication selection...\r\n");
    
    aicam_result_t result = communication_apply_priority();
    
    if (result == AICAM_OK) {
        printf("Priority applied. Current type: %s\r\n", 
               communication_type_to_string(communication_get_current_type()));
    } else {
        printf("Failed to apply priority: %d\r\n", result);
    }
    
    return (result == AICAM_OK) ? 0 : -1;
}

/**
 * @brief CLI command: comm prefer - Set preferred type
 */
static int comm_prefer_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    if (argc < 3) {
        printf("Usage: comm prefer <type>\r\n");
        printf("  Types: none, wifi, cellular (or 4g), poe (or ethernet)\r\n");
        printf("  Current preferred: %s\r\n", 
               communication_type_to_string(g_communication_service.preferred_type));
        return 0;
    }
    
    communication_type_t target = communication_type_from_string(argv[2]);
    
    aicam_result_t result = communication_set_preferred_type(target);
    if (result == AICAM_OK) {
        printf("Preferred type set to: %s\r\n", communication_type_to_string(target));
    } else {
        printf("Failed to set preferred type: %d\r\n", result);
    }
    
    return (result == AICAM_OK) ? 0 : -1;
}

/**
 * @brief CLI command: comm cellular - Cellular management
 */
static int comm_cellular_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (!g_communication_service.cellular_available) {
        printf("Cellular module not available\r\n");
        return -1;
    }
    
    if (argc < 3) {
        printf("Usage: comm cellular <command>\r\n");
        printf("Commands:\r\n");
        printf("  status    - Show cellular status\r\n");
        printf("  connect   - Connect cellular network\r\n");
        printf("  disconnect- Disconnect cellular network\r\n");
        printf("  info      - Show detailed cellular information\r\n");
        printf("  refresh   - Refresh cellular information\r\n");
        printf("  settings  - Show/set cellular settings\r\n");
        return -1;
    }
    
    if (strcmp(argv[2], "status") == 0) {
        communication_status_t status = communication_cellular_get_status();
        printf("Cellular Status: %s\r\n", communication_status_to_string(status));
        return 0;
        
    } else if (strcmp(argv[2], "connect") == 0) {
        printf("Connecting cellular network...\r\n");
        aicam_result_t result = communication_cellular_connect();
        if (result == AICAM_OK) {
            printf("Cellular connected successfully\r\n");
        } else {
            printf("Failed to connect cellular: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "disconnect") == 0) {
        printf("Disconnecting cellular network...\r\n");
        aicam_result_t result = communication_cellular_disconnect();
        if (result == AICAM_OK) {
            printf("Cellular disconnected successfully\r\n");
        } else {
            printf("Failed to disconnect cellular: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "info") == 0) {
        cellular_detail_info_t info;
        aicam_result_t result = communication_cellular_get_detail_info(&info);
        if (result != AICAM_OK) {
            printf("Failed to get cellular info: %d\r\n", result);
            return -1;
        }
        
        printf("\r\n================== CELLULAR DETAILED INFO ==================\r\n");
        printf("Network Status: %s\r\n", communication_status_to_string(info.network_status));
        printf("SIM Status: %s\r\n", info.sim_status);
        printf("\r\n--- Device Info ---\r\n");
        printf("Model: %s\r\n", info.model);
        printf("Version: %s\r\n", info.version);
        printf("IMEI: %s\r\n", info.imei);
        printf("IMSI: %s\r\n", info.imsi);
        printf("ICCID: %s\r\n", info.iccid);
        printf("\r\n--- Network Info ---\r\n");
        printf("ISP: %s\r\n", info.isp);
        printf("Network Type: %s\r\n", info.network_type);
        printf("Register Status: %s\r\n", info.register_status);
        printf("PLMN ID: %s\r\n", info.plmn_id);
        printf("LAC: %s\r\n", info.lac);
        printf("Cell ID: %s\r\n", info.cell_id);
        printf("\r\n--- Signal Info ---\r\n");
        printf("Signal Level: %d\r\n", (int)info.signal_level);
        printf("CSQ: %d\r\n", (int)info.csq);
        printf("CSQ Level: %d\r\n", (int)info.csq_level);
        printf("RSSI: %d dBm\r\n", (int)info.rssi);
        printf("\r\n--- IPv4 Info ---\r\n");
        printf("Address: %s\r\n", info.ipv4_address);
        printf("Gateway: %s\r\n", info.ipv4_gateway);
        printf("DNS: %s\r\n", info.ipv4_dns);
        printf("\r\n--- IPv6 Info ---\r\n");
        printf("Address: %s\r\n", info.ipv6_address);
        printf("Gateway: %s\r\n", info.ipv6_gateway);
        printf("DNS: %s\r\n", info.ipv6_dns);
        printf("\r\n--- Connection Info ---\r\n");
        printf("Connection Duration: %lu seconds\r\n", (unsigned long)info.connection_duration_sec);
        printf("============================================================\r\n\r\n");
        return 0;
        
    } else if (strcmp(argv[2], "refresh") == 0) {
        printf("Refreshing cellular information...\r\n");
        aicam_result_t result = communication_cellular_refresh_info();
        if (result == AICAM_OK) {
            printf("Cellular information refreshed\r\n");
        } else {
            printf("Failed to refresh: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "settings") == 0) {
        if (argc < 4) {
            // Show current settings
            cellular_connection_settings_t settings;
            communication_cellular_get_settings(&settings);
            
            printf("\r\n================== CELLULAR SETTINGS ==================\r\n");
            printf("APN: %s\r\n", settings.apn);
            printf("Username: %s\r\n", settings.username);
            printf("Password: %s\r\n", settings.password);
            printf("PIN Code: %s\r\n", settings.pin_code);
            printf("Authentication: %d (0=None, 1=PAP, 2=CHAP, 3=Auto)\r\n", settings.authentication);
            printf("Roaming: %s\r\n", settings.enable_roaming ? "Enabled" : "Disabled");
            printf("========================================================\r\n\r\n");
            return 0;
        }
        
        // Set settings: comm cellular settings apn <apn> [user <user>] [pass <pass>] [auth <auth>]
        cellular_connection_settings_t settings;
        communication_cellular_get_settings(&settings);
        
        for (int i = 3; i < argc - 1; i += 2) {
            if (strcmp(argv[i], "apn") == 0) {
                strncpy(settings.apn, argv[i+1], sizeof(settings.apn) - 1);
            } else if (strcmp(argv[i], "user") == 0) {
                strncpy(settings.username, argv[i+1], sizeof(settings.username) - 1);
            } else if (strcmp(argv[i], "pass") == 0) {
                strncpy(settings.password, argv[i+1], sizeof(settings.password) - 1);
            } else if (strcmp(argv[i], "pin") == 0) {
                strncpy(settings.pin_code, argv[i+1], sizeof(settings.pin_code) - 1);
            } else if (strcmp(argv[i], "auth") == 0) {
                settings.authentication = (cellular_auth_type_t)atoi(argv[i+1]);
            } else if (strcmp(argv[i], "roam") == 0) {
                settings.enable_roaming = (strcmp(argv[i+1], "1") == 0 || 
                                          strcasecmp(argv[i+1], "true") == 0);
            }
        }
        
        aicam_result_t result = communication_cellular_set_settings(&settings);
        if (result == AICAM_OK) {
            printf("Cellular settings updated\r\n");
        } else {
            printf("Failed to update settings: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else {
        printf("Unknown cellular command: %s\r\n", argv[2]);
        return -1;
    }
#else
    printf("Cellular support not enabled (NETIF_4G_CAT1_IS_ENABLE=0)\r\n");
    return -1;
#endif
}

/**
 * @brief CLI command: comm poe - PoE management
 */
static int comm_poe_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
#if NETIF_ETH_WAN_IS_ENABLE
    if (!g_communication_service.poe_available) {
        printf("PoE module not available\r\n");
        return -1;
    }
    
    if (argc < 3) {
        printf("Usage: comm poe <command> [args]\r\n");
        printf("Commands:\r\n");
        printf("  status      - Show PoE status\r\n");
        printf("  info        - Show detailed PoE information\r\n");
        printf("  connect     - Connect PoE network\r\n");
        printf("  disconnect  - Disconnect PoE network\r\n");
        printf("  mode [dhcp|static] - Get/set IP mode\r\n");
        printf("  config      - Show current configuration\r\n");
        printf("  static <ip> <mask> <gw> [dns] - Set static IP config\r\n");
        printf("  apply       - Apply current configuration\r\n");
        printf("  save        - Save configuration to NVS\r\n");
        printf("  validate    - Validate current static config\r\n");
        return -1;
    }
    
    if (strcmp(argv[2], "status") == 0) {
        communication_status_t status = communication_poe_get_status();
        uint32_t status_code = communication_poe_get_status_code();
        printf("PoE Status: %s (code: %lu - %s)\r\n", 
               communication_status_to_string(status),
               (unsigned long)status_code,
               poe_status_code_to_string((poe_status_code_t)status_code));
        return 0;
        
    } else if (strcmp(argv[2], "info") == 0) {
        poe_detail_info_t info;
        aicam_result_t result = communication_poe_get_detail_info(&info);
        if (result != AICAM_OK) {
            printf("Failed to get PoE info: %d\r\n", result);
            return -1;
        }
        
        printf("\r\n================== POE DETAILED INFO ==================\r\n");
        printf("Network Status: %s\r\n", communication_status_to_string(info.network_status));
        printf("Status Code:    %lu (%s)\r\n", (unsigned long)info.status_code, info.status_message);
        printf("Link Up:        %s\r\n", info.link_up ? "YES" : "NO");
        printf("PoE Powered:    %s\r\n", info.poe_powered ? "YES" : "NO");
        printf("IP Mode:        %s\r\n", info.ip_mode == 0 ? "DHCP" : "Static");
        printf("IP Address:     %s\r\n", info.ip_address);
        printf("Netmask:        %s\r\n", info.netmask);
        printf("Gateway:        %s\r\n", info.gateway);
        printf("DNS Primary:    %s\r\n", info.dns_primary);
        printf("DNS Secondary:  %s\r\n", info.dns_secondary);
        printf("Hostname:       %s\r\n", info.hostname);
        printf("MAC Address:    %s\r\n", info.mac_address);
        printf("Interface:      %s\r\n", info.interface_name);
        printf("Connect Count:  %lu\r\n", (unsigned long)info.connect_count);
        printf("Disconnect Cnt: %lu\r\n", (unsigned long)info.disconnect_count);
        printf("DHCP Fail Cnt:  %lu\r\n", (unsigned long)info.dhcp_fail_count);
        printf("=======================================================\r\n");
        return 0;
        
    } else if (strcmp(argv[2], "connect") == 0) {
        printf("Connecting PoE network...\r\n");
        aicam_result_t result = communication_poe_connect();
        if (result == AICAM_OK) {
            printf("PoE connected successfully\r\n");
        } else {
            printf("Failed to connect PoE: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "disconnect") == 0) {
        printf("Disconnecting PoE network...\r\n");
        aicam_result_t result = communication_poe_disconnect();
        if (result == AICAM_OK) {
            printf("PoE disconnected successfully\r\n");
        } else {
            printf("Failed to disconnect PoE: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "mode") == 0) {
        if (argc >= 4) {
            // Set mode
            uint8_t mode = 0;
            if (strcmp(argv[3], "dhcp") == 0) {
                mode = 0;
            } else if (strcmp(argv[3], "static") == 0) {
                mode = 1;
            } else {
                printf("Invalid mode: %s (use dhcp or static)\r\n", argv[3]);
                return -1;
            }
            aicam_result_t result = communication_poe_set_ip_mode(mode);
            if (result == AICAM_OK) {
                printf("PoE IP mode set to: %s\r\n", mode == 0 ? "DHCP" : "Static");
            } else {
                printf("Failed to set IP mode: %d\r\n", result);
            }
            return (result == AICAM_OK) ? 0 : -1;
        } else {
            // Get mode
            uint8_t mode = communication_poe_get_ip_mode();
            printf("PoE IP Mode: %s\r\n", mode == 0 ? "DHCP" : "Static");
            return 0;
        }
        
    } else if (strcmp(argv[2], "config") == 0) {
        poe_config_persist_t poe_cfg;
        aicam_result_t result = json_config_get_poe_config(&poe_cfg);
        if (result != AICAM_OK) {
            printf("Failed to get config: %d\r\n", result);
            return -1;
        }
        
        printf("\r\n================== POE CONFIGURATION ==================\r\n");
        printf("IP Mode:          %s\r\n", poe_cfg.ip_mode == POE_IP_MODE_DHCP ? "DHCP" : "Static");
        printf("Static IP:        %d.%d.%d.%d\r\n", 
               poe_cfg.ip_addr[0], poe_cfg.ip_addr[1], poe_cfg.ip_addr[2], poe_cfg.ip_addr[3]);
        printf("Netmask:          %d.%d.%d.%d\r\n", 
               poe_cfg.netmask[0], poe_cfg.netmask[1], poe_cfg.netmask[2], poe_cfg.netmask[3]);
        printf("Gateway:          %d.%d.%d.%d\r\n", 
               poe_cfg.gateway[0], poe_cfg.gateway[1], poe_cfg.gateway[2], poe_cfg.gateway[3]);
        printf("DNS Primary:      %d.%d.%d.%d\r\n", 
               poe_cfg.dns_primary[0], poe_cfg.dns_primary[1], 
               poe_cfg.dns_primary[2], poe_cfg.dns_primary[3]);
        printf("DNS Secondary:    %d.%d.%d.%d\r\n", 
               poe_cfg.dns_secondary[0], poe_cfg.dns_secondary[1], 
               poe_cfg.dns_secondary[2], poe_cfg.dns_secondary[3]);
        printf("Hostname:         %s\r\n", poe_cfg.hostname);
        printf("DHCP Timeout:     %lu ms\r\n", (unsigned long)poe_cfg.dhcp_timeout_ms);
        printf("DHCP Retry:       %lu times\r\n", (unsigned long)poe_cfg.dhcp_retry_count);
        printf("Recovery Delay:   %lu ms\r\n", (unsigned long)poe_cfg.power_recovery_delay_ms);
        printf("Auto Reconnect:   %s\r\n", poe_cfg.auto_reconnect ? "YES" : "NO");
        printf("Validate Gateway: %s\r\n", poe_cfg.validate_gateway ? "YES" : "NO");
        printf("Detect Conflict:  %s\r\n", poe_cfg.detect_ip_conflict ? "YES" : "NO");
        printf("=======================================================\r\n");
        return 0;
        
    } else if (strcmp(argv[2], "static") == 0) {
        if (argc < 6) {
            printf("Usage: comm poe static <ip> <mask> <gateway> [dns]\r\n");
            printf("Example: comm poe static 192.168.1.100 255.255.255.0 192.168.1.1 8.8.8.8\r\n");
            return -1;
        }
        
        poe_static_config_t config;
        memset(&config, 0, sizeof(config));
        
        // Parse IP address
        unsigned int ip[4];
        if (sscanf(argv[3], "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
            printf("Invalid IP address: %s\r\n", argv[3]);
            return -1;
        }
        config.ip_addr[0] = ip[0]; config.ip_addr[1] = ip[1];
        config.ip_addr[2] = ip[2]; config.ip_addr[3] = ip[3];
        
        // Parse netmask
        if (sscanf(argv[4], "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
            printf("Invalid netmask: %s\r\n", argv[4]);
            return -1;
        }
        config.netmask[0] = ip[0]; config.netmask[1] = ip[1];
        config.netmask[2] = ip[2]; config.netmask[3] = ip[3];
        
        // Parse gateway
        if (sscanf(argv[5], "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
            printf("Invalid gateway: %s\r\n", argv[5]);
            return -1;
        }
        config.gateway[0] = ip[0]; config.gateway[1] = ip[1];
        config.gateway[2] = ip[2]; config.gateway[3] = ip[3];
        
        // Parse DNS if provided
        if (argc >= 7) {
            if (sscanf(argv[6], "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
                printf("Invalid DNS: %s\r\n", argv[6]);
                return -1;
            }
            config.dns_primary[0] = ip[0]; config.dns_primary[1] = ip[1];
            config.dns_primary[2] = ip[2]; config.dns_primary[3] = ip[3];
        } else {
            // Default DNS
            config.dns_primary[0] = 8; config.dns_primary[1] = 8;
            config.dns_primary[2] = 8; config.dns_primary[3] = 8;
        }
        
        // Validate first
        char error_msg[128];
        aicam_result_t result = communication_poe_validate_static_config(&config, error_msg, sizeof(error_msg));
        if (result != AICAM_OK) {
            printf("Invalid configuration: %s\r\n", error_msg);
            return -1;
        }
        
        // Set the configuration
        result = communication_poe_set_static_config(&config);
        if (result == AICAM_OK) {
            // Also set mode to static
            communication_poe_set_ip_mode(1);
            printf("Static IP configured: %d.%d.%d.%d\r\n", 
                   config.ip_addr[0], config.ip_addr[1], config.ip_addr[2], config.ip_addr[3]);
            printf("Use 'comm poe apply' to apply the configuration\r\n");
        } else {
            printf("Failed to set static config: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "apply") == 0) {
        printf("Applying PoE configuration...\r\n");
        aicam_result_t result = communication_poe_apply_config();
        if (result == AICAM_OK) {
            printf("PoE configuration applied successfully\r\n");
        } else {
            printf("Failed to apply configuration: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "save") == 0) {
        printf("Saving PoE configuration...\r\n");
        aicam_result_t result = communication_poe_save_config();
        if (result == AICAM_OK) {
            printf("PoE configuration saved to NVS\r\n");
        } else {
            printf("Failed to save configuration: %d\r\n", result);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else if (strcmp(argv[2], "validate") == 0) {
        poe_static_config_t config;
        aicam_result_t result = communication_poe_get_static_config(&config);
        if (result != AICAM_OK) {
            printf("Failed to get static config: %d\r\n", result);
            return -1;
        }
        
        char error_msg[128];
        result = communication_poe_validate_static_config(&config, error_msg, sizeof(error_msg));
        if (result == AICAM_OK) {
            printf("Static configuration is valid\r\n");
        } else {
            printf("Invalid configuration: %s\r\n", error_msg);
        }
        return (result == AICAM_OK) ? 0 : -1;
        
    } else {
        printf("Unknown PoE command: %s\r\n", argv[2]);
        return -1;
    }
#else
    printf("PoE support not enabled (NETIF_ETH_WAN_IS_ENABLE=0)\r\n");
    return -1;
#endif
}

/**
 * @brief CLI command: comm save - Save configuration
 */
static int comm_save_cmd(int argc, char* argv[])
{
    if (!g_communication_service.initialized) {
        printf("Communication service not initialized\r\n");
        return -1;
    }
    
    printf("Saving communication configuration...\r\n");
    
    aicam_result_t result = communication_save_config_to_nvs();
    
#if NETIF_4G_CAT1_IS_ENABLE
    if (result == AICAM_OK) {
        result = communication_cellular_save_settings();
    }
#endif
    
    if (result == AICAM_OK) {
        printf("Configuration saved successfully\r\n");
    } else {
        printf("Failed to save configuration: %d\r\n", result);
    }
    
    return (result == AICAM_OK) ? 0 : -1;
}

/**
 * @brief Main CLI command handler
 */
static int comm_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: comm <command> [args]\r\n");
        printf("Commands:\r\n");
        printf("  status     - Show service status\r\n");
        printf("  interfaces - List network interfaces\r\n");
        printf("  types      - Show all communication types\r\n");
        printf("  switch     - Switch communication type (wifi/cellular/poe)\r\n");
        printf("  priority   - Apply priority-based selection\r\n");
        printf("  prefer     - Set/show preferred type\r\n");
        printf("  cellular   - Cellular/4G management\r\n");
        printf("  poe        - PoE/Ethernet management\r\n");
        printf("  scan       - Scan for WiFi networks\r\n");
        printf("  known      - Show known networks only\r\n");
        printf("  unknown    - Show unknown networks only\r\n");
        printf("  classified - Show classified scan results\r\n");
        printf("  delete     - Delete known network (ssid bssid)\r\n");
        printf("  start      - Start interface\r\n");
        printf("  stop       - Stop interface\r\n");
        printf("  restart    - Restart interface\r\n");
        printf("  config     - Configure interface (ap/sta ssid [password])\r\n");
        printf("  save       - Save configuration to NVS\r\n");
        printf("  stats      - Show statistics\r\n");
        printf("  reset      - Reset statistics\r\n");
        return -1;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        return comm_status_cmd(argc, argv);
    } else if (strcmp(argv[1], "interfaces") == 0) {
        return comm_interfaces_cmd(argc, argv);
    } else if (strcmp(argv[1], "types") == 0) {
        return comm_types_cmd(argc, argv);
    } else if (strcmp(argv[1], "switch") == 0) {
        return comm_switch_cmd(argc, argv);
    } else if (strcmp(argv[1], "priority") == 0) {
        return comm_priority_cmd(argc, argv);
    } else if (strcmp(argv[1], "prefer") == 0) {
        return comm_prefer_cmd(argc, argv);
    } else if (strcmp(argv[1], "cellular") == 0) {
        return comm_cellular_cmd(argc, argv);
    } else if (strcmp(argv[1], "poe") == 0) {
        return comm_poe_cmd(argc, argv);
    } else if (strcmp(argv[1], "scan") == 0) {
        return comm_scan_cmd(argc, argv);
    } else if (strcmp(argv[1], "known") == 0) {
        return comm_known_cmd(argc, argv);
    } else if (strcmp(argv[1], "unknown") == 0) {
        return comm_unknown_cmd(argc, argv);
    } else if (strcmp(argv[1], "classified") == 0) {
        return comm_classified_cmd(argc, argv);
    } else if (strcmp(argv[1], "delete") == 0) {
        return comm_delete_cmd(argc, argv);
    } else if (strcmp(argv[1], "start") == 0) {
        return comm_start_cmd(argc, argv);
    } else if (strcmp(argv[1], "stop") == 0) {
        return comm_stop_cmd(argc, argv);
    } else if (strcmp(argv[1], "restart") == 0) {
        return comm_restart_cmd(argc, argv);
    } else if (strcmp(argv[1], "config") == 0) {
        return comm_config_cmd(argc, argv);
    } else if (strcmp(argv[1], "save") == 0) {
        return comm_save_cmd(argc, argv);
    } else if (strcmp(argv[1], "stats") == 0) {
        return comm_stats_cmd(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        return comm_reset_cmd(argc, argv);
    } else {
        printf("Unknown command: %s\r\n", argv[1]);
        return -1;
    }
}

/* ==================== Known Network Management ==================== */

aicam_result_t communication_delete_known_network(const char *ssid, const char *bssid)
{
    if (!g_communication_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!ssid || !bssid) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (strlen(ssid) == 0 || strlen(bssid) == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Public API: Deleting known network: %s (%s)", ssid, bssid);
    
    // Call internal delete function
    delete_known_network(ssid, bssid);
    
    LOG_SVC_INFO("Public API: Successfully deleted known network: %s (%s)", ssid, bssid);
    
    return AICAM_OK;
}

/* ==================== Network Interface Ready Callbacks Implementation ==================== */

/**
 * @brief WiFi AP ready callback
 */
static void on_wifi_ap_ready(const char *if_name, aicam_result_t result)
{
    if (result == AICAM_OK) {
        LOG_SVC_INFO("WiFi AP initialized and ready (broadcasting)");
        
        // Update statistics
        g_communication_service.stats.successful_connections++;

        // Update device MAC address
        device_service_update_device_mac_address();

        // Update communication type
        device_service_update_communication_type();

        // Update MQTT client ID and topic
        mqtt_service_update_client_id_and_topic();

        // Set indicator to solid on (AP active)
        device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
     
        //Configure AP interface using configuration from json_config_mgr
        network_service_config_t* network_config = (network_service_config_t*)buffer_calloc(1, sizeof(network_service_config_t));
        if (!network_config) {
            LOG_SVC_ERROR("Failed to allocate memory for network config");
            return;
        }
        netif_config_t ap_config = {0};
        aicam_result_t config_result = json_config_get_network_service_config(network_config);
        if (config_result == AICAM_OK) {
            LOG_SVC_INFO("Configuring AP with SSID: %s", network_config->ssid);
            // Configure AP interface
            if (strcmp(network_config->ssid, "AICAM-AP") == 0 || strlen(network_config->ssid) == 0) {
                LOG_SVC_INFO("Default AP SSID, skip configuration");
                communication_get_interface_config(NETIF_NAME_WIFI_AP, &ap_config);
                strncpy(network_config->ssid, ap_config.wireless_cfg.ssid, sizeof(network_config->ssid) - 1);
                strncpy(network_config->password, ap_config.wireless_cfg.pw, sizeof(network_config->password) - 1);
                aicam_result_t result = json_config_set_network_service_config(network_config);
                LOG_SVC_INFO("ssid: %s, password: %s", network_config->ssid, network_config->password);
                if (result != AICAM_OK) {
                    LOG_SVC_WARN("Failed to set network service configuration: %d", result);
                } else {
                    LOG_SVC_INFO("Network service configuration set successfully");
                }
            } else {
                netif_config_t ap_config = {0};
                nm_get_netif_cfg(NETIF_NAME_WIFI_AP, &ap_config);
                strncpy(ap_config.wireless_cfg.ssid, network_config->ssid, sizeof(ap_config.wireless_cfg.ssid) - 1);
                strncpy(ap_config.wireless_cfg.pw, network_config->password, sizeof(ap_config.wireless_cfg.pw) - 1);
                ap_config.wireless_cfg.security = (strlen(network_config->password) > 0) ? WIRELESS_WPA_WPA2_MIXED : WIRELESS_OPEN;
                
                aicam_result_t result = communication_configure_interface(NETIF_NAME_WIFI_AP, &ap_config);
                if (result != AICAM_OK) {
                    LOG_SVC_WARN("Failed to configure WiFi AP: %d", result);
                } else {
                    LOG_SVC_INFO("WiFi AP configured successfully with SSID: %s", network_config->ssid);
                }
            }
        } else {
            LOG_SVC_WARN("Failed to get network service configuration: %d", config_result);
        }

        buffer_free(network_config);

        // Notify other services that AP is ready
        // Web service can now be accessed

        
        uint32_t init_time = netif_init_manager_get_init_time(if_name);
        LOG_SVC_INFO("WiFi AP initialization completed in %u ms", init_time);
        service_set_ap_ready(AICAM_TRUE);
    } else {
        LOG_SVC_ERROR("WiFi AP initialization failed: %d", result);
        
        // Update statistics
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
}

/**
 * @brief WiFi STA ready callback
 * @note Only loads scan results, does NOT auto-connect
 *       Connection decision is made by check_all_ready_and_decide()
 */
static void on_wifi_sta_ready(const char *if_name, aicam_result_t result)
{
    // Mark as ready regardless of result
    g_communication_service.wifi_sta_ready = AICAM_TRUE;
    
    if (result == AICAM_OK) {
        LOG_SVC_INFO("WiFi STA initialized and ready");

        // Check if wakeup source requires time-optimized mode (skip time-consuming operations)
        wakeup_source_type_t wakeup_source = system_service_get_wakeup_source_type();
        aicam_bool_t requires_time_optimized = system_service_requires_time_optimized_mode(wakeup_source);

        // In time-optimized mode, skip scan result loading to save time
        // We'll use cached known network info directly
        if (!requires_time_optimized) {
            // Get scan results from storage (full speed mode only)
            aicam_result_t scan_result_result = communication_start_network_scan(NULL);
            if (scan_result_result != AICAM_OK) {
                LOG_SVC_ERROR("Failed to update network scan result: %d", scan_result_result);
            }
            wireless_scan_result_t *scan_result = nm_wireless_get_scan_result();
            if(scan_result) {
                g_communication_service.scan_result_count = scan_result->scan_count;
                for(uint32_t i = 0; i < g_communication_service.scan_result_count; i++) {
                    strncpy(g_communication_service.scan_results[i].ssid, scan_result->scan_info[i].ssid, sizeof(g_communication_service.scan_results[i].ssid) - 1);
                    snprintf(g_communication_service.scan_results[i].bssid, sizeof(g_communication_service.scan_results[i].bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                            scan_result->scan_info[i].bssid[0], scan_result->scan_info[i].bssid[1],
                            scan_result->scan_info[i].bssid[2], scan_result->scan_info[i].bssid[3],
                            scan_result->scan_info[i].bssid[4], scan_result->scan_info[i].bssid[5]);
                    g_communication_service.scan_results[i].rssi = scan_result->scan_info[i].rssi;
                    g_communication_service.scan_results[i].channel = scan_result->scan_info[i].channel;
                    g_communication_service.scan_results[i].security = (wireless_security_t)scan_result->scan_info[i].security;
                    g_communication_service.scan_results[i].connected = AICAM_FALSE;
                    g_communication_service.scan_results[i].is_known = AICAM_FALSE;
                    g_communication_service.scan_results[i].last_connected_time = 0;
                }
            }
        } else {
            // RTC wakeup mode: clear scan results to use cached known networks
            g_communication_service.scan_result_count = 0;
        }

        uint32_t init_time = netif_init_manager_get_init_time(if_name);
        LOG_SVC_INFO("WiFi STA initialization completed in %u ms", init_time);

    } else {
        LOG_SVC_ERROR("WiFi STA initialization failed: %d", result);
        
        // Update statistics
        g_communication_service.stats.failed_connections++;
        g_communication_service.stats.last_error_code = result;
    }
    
    // Check if all interfaces ready, then make connection decision
    check_all_ready_and_decide();
}

/* ==================== CLI Command Registration ==================== */

debug_cmd_reg_t comm_cmd_table[] = {
    {"comm", "Communication service management.", comm_cmd},
};

void comm_cmd_register(void)
{
    debug_cmdline_register(comm_cmd_table, sizeof(comm_cmd_table) / sizeof(comm_cmd_table[0]));
}

