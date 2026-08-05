/**
 * @file communication_service.h
 * @brief Communication Service Interface Header
 * @details communication service standard interface definition, support network interface information collection and configuration management
 */

#ifndef COMMUNICATION_SERVICE_H
#define COMMUNICATION_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "netif_manager.h"
#include "json_config_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Communication Type Definitions ==================== */
#define COMMUNICATION_SERVICE_VERSION "1.0.0"
#define MAX_NETWORK_INTERFACES 8
#define MAX_KNOWN_NETWORKS 16
#define MAX_UNKNOWN_NETWORKS 64
#define MAX_SCAN_RESULTS 64
#define STARTUP_TIMEOUT_MS  30000  // Startup connection decision timeout

/**
 * @brief Communication type enumeration
 * @note Priority: PoE > HaLow > Cellular > WiFi (higher value = higher priority)
 * @note NVS preferred_comm_type and qs_comm_pref_type (0–4) use the same numeric values.
 */
typedef enum {
    COMM_TYPE_NONE = 0,                     // No communication type
    COMM_TYPE_WIFI,                         // WiFi communication (lowest priority)
    COMM_TYPE_HALOW,                        // Wi-Fi HaLow communication
    COMM_TYPE_CELLULAR,                     // Cellular/4G communication (medium priority)
    COMM_TYPE_POE,                          // PoE/Ethernet communication (highest priority)
    COMM_TYPE_MAX
} communication_type_t;

/**
 * @brief Communication type status
 */
typedef enum {
    COMM_STATUS_UNAVAILABLE = 0,            // Communication type not available
    COMM_STATUS_DISCONNECTED,               // Available but not connected
    COMM_STATUS_CONNECTING,                 // Connection in progress
    COMM_STATUS_CONNECTED,                  // Connected and ready
    COMM_STATUS_ERROR,                      // Error state
    COMM_STATUS_MAX
} communication_status_t;

/**
 * @brief Cellular authentication type
 */
typedef enum {
    CELLULAR_AUTH_NONE = 0,                 // No authentication
    CELLULAR_AUTH_PAP,                      // PAP authentication
    CELLULAR_AUTH_CHAP,                     // CHAP authentication
    CELLULAR_AUTH_PAP_CHAP,                 // PAP/CHAP auto-select
    CELLULAR_AUTH_MAX
} cellular_auth_type_t;

/**
 * @brief Communication type information structure
 */
typedef struct {
    communication_type_t type;              // Communication type
    communication_status_t status;          // Current status
    aicam_bool_t available;                 // Whether this type is available (hardware present)
    aicam_bool_t is_default;                // Whether this is the default/active type
    int32_t priority;                       // Priority level (higher = more preferred)
    int32_t signal_strength;                // Signal strength (RSSI for WiFi/Cellular)
    char ip_addr[16];                       // IP address
    char mac_addr[18];                      // MAC address
    char interface_name[16];                // Interface name (e.g., "wl", "4g", "wn")
} communication_type_info_t;

/**
 * @brief Cellular connection settings structure
 */
typedef struct {
    char apn[32];                           // APN (Access Point Name)
    char username[64];                      // APN username
    char password[64];                      // APN password
    char pin_code[16];                      // SIM PIN code
    cellular_auth_type_t authentication;    // Authentication type
    aicam_bool_t enable_roaming;            // Enable roaming
    uint8_t operator;                       // Mobile operator (0: Auto, 1: China Mobile, 2: China Unicom, 3: China Telecom, 4: American Verizon)
    char plmn[8];                           // Manual PLMN code (MCC+MNC, 5-6 digits); empty = auto COPS=0
} cellular_connection_settings_t;

/**
 * @brief Cellular detailed information structure
 */
typedef struct {
    // Basic status
    communication_status_t network_status;  // Network status
    char sim_status[32];                    // SIM card status string
    
    // Device info
    char model[64];                         // Device model
    char version[64];                       // Firmware version
    char imei[32];                          // Device IMEI
    char imsi[32];                          // SIM card IMSI
    char iccid[32];                         // SIM card ICCID
    
    // Network info
    char isp[32];                           // Internet Service Provider
    char network_type[16];                  // Network type (e.g., "LTE", "3G")
    char register_status[32];               // Registration status
    char plmn_id[16];                       // PLMN ID
    char lac[16];                           // Location Area Code
    char cell_id[16];                       // Cell ID
    
    // Signal info
    int32_t signal_level;                   // Signal level (0-5)
    int32_t csq;                            // CSQ value (0-31, 99=unknown)
    int32_t csq_level;                      // CSQ level (0-5)
    int32_t rssi;                           // RSSI in dBm
    
    // IP info - IPv4
    char ipv4_address[16];                  // IPv4 address
    char ipv4_gateway[16];                  // IPv4 gateway
    char ipv4_dns[16];                      // IPv4 DNS
    
    // IP info - IPv6
    char ipv6_address[48];                  // IPv6 address
    char ipv6_gateway[48];                  // IPv6 gateway
    char ipv6_dns[48];                      // IPv6 DNS
    
    // Connection info
    uint32_t connection_duration_sec;       // Connection duration in seconds
    uint32_t connection_start_time;         // Connection start timestamp
} cellular_detail_info_t;

/**
 * @brief Communication switch result structure
 */
typedef struct {
    communication_type_t from_type;         // Original communication type
    communication_type_t to_type;           // Target communication type
    aicam_bool_t success;                   // Whether switch was successful
    uint32_t switch_time_ms;                // Time taken to switch in milliseconds
    char error_message[128];                // Error message if failed
} communication_switch_result_t;

/**
 * @brief Communication switch callback function type
 */
typedef void (*communication_switch_callback_t)(const communication_switch_result_t *result);

/* ==================== Communication Service Configuration ==================== */

/**
 * @brief Communication service configuration structure
 */
typedef struct {
    aicam_bool_t auto_start_wifi_ap;        // Auto start WiFi AP mode
    aicam_bool_t auto_start_wifi_sta;       // Auto start WiFi STA mode
    aicam_bool_t auto_start_halow;          // Auto start Wi-Fi HaLow
    aicam_bool_t auto_start_cellular;       // Auto start cellular/4G
    aicam_bool_t auto_start_poe;            // Auto start PoE/Ethernet
    aicam_bool_t enable_network_scan;       // Enable network scanning
    aicam_bool_t enable_auto_reconnect;     // Enable auto reconnection
    aicam_bool_t enable_auto_priority;      // Enable automatic priority-based switching
    uint32_t reconnect_interval_ms;         // Reconnection interval in milliseconds
    uint32_t scan_interval_ms;              // Network scan interval in milliseconds
    uint32_t connection_timeout_ms;         // Connection timeout in milliseconds
    aicam_bool_t enable_debug;              // Enable debug logging
    aicam_bool_t enable_stats;              // Enable statistics logging
    communication_type_t preferred_type;    // User preferred communication type
} communication_service_config_t;

/**
 * @brief Network interface status
 */
typedef struct {
    netif_state_t state;                    // Interface state
    netif_type_t type;                      // Interface type
    char if_name[16];                       // Interface name
    char ssid[32];                          // WiFi SSID (for wireless interfaces)
    char bssid[18];                         // WiFi BSSID (for wireless interfaces)
    char ip_addr[16];                       // IP address
    char mac_addr[18];                      // MAC address
    int32_t rssi;                           // Signal strength (for wireless)
    uint32_t channel;                       // WiFi channel (for wireless)
    wireless_security_t security;           // Wireless security type
    aicam_bool_t connected;                 // Connection status
} network_interface_status_t;

/**
 * @brief Communication service statistics
 */
typedef struct {
    uint64_t total_connections;             // Total connection attempts
    uint64_t successful_connections;        // Successful connections
    uint64_t failed_connections;            // Failed connections
    uint64_t disconnections;                // Total disconnections
    uint64_t network_scans;                 // Total network scans performed
    uint64_t bytes_sent;                    // Total bytes sent
    uint64_t bytes_received;                // Total bytes received
    uint32_t current_connections;           // Current active connections
    uint32_t last_error_code;               // Last error code
} communication_service_stats_t;

/**
 * @brief Network scan results with known/unknown classification
 */
typedef struct {
    network_scan_result_t known_networks[MAX_KNOWN_NETWORKS];       // Known networks
    network_scan_result_t unknown_networks[MAX_UNKNOWN_NETWORKS];   // Unknown networks
    uint32_t known_count;                                           // Number of known networks
    uint32_t unknown_count;                                         // Number of unknown networks
} classified_scan_results_t;

/* ==================== Communication Service Interface Functions ==================== */

/**
 * @brief Start network scan
 */
void start_network_scan(void);


/**
 * @brief Initialize communication service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_init(void *config);

/**
 * @brief Start communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_start(void);

/**
 * @brief Set the required netif for the wake-capture path.
 * @param type COMM_TYPE_NONE = default (init all per system comm-pref);
 *             otherwise bring up only that netif on the next start().
 * @note Must be called before communication_service_start(). Only takes effect
 *       on the wake-capture boot path; full-speed boots ignore it.
 */
void communication_service_set_required_type(communication_type_t type);

/**
 * @brief Stop communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_stop(void);

/**
 * @brief Deinitialize communication service
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_service_deinit(void);

/**
 * @brief Get communication service state
 * @return service_state_t Service state
 */
service_state_t communication_service_get_state(void);

/* ==================== Network Interface Management ==================== */

/**
 * @brief Get network interface list
 * @param interfaces Array to store interface information
 * @param max_count Maximum number of interfaces to return
 * @param actual_count Actual number of interfaces returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_network_interfaces(network_interface_status_t *interfaces, 
                                                   uint32_t max_count, 
                                                   uint32_t *actual_count);

/**
 * @brief Get specific network interface status
 * @param if_name Interface name
 * @param status Interface status structure
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_interface_status(const char *if_name, 
                                                 network_interface_status_t *status);

/**
 * @brief Check if network interface is connected
 * @param if_name Interface name
 * @return aicam_bool_t TRUE if connected, FALSE otherwise
 */
aicam_bool_t communication_is_interface_connected(const char *if_name);

/**
 * @brief Get network interface configuration
 * @param if_name Interface name
 * @param config Buffer to store interface configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_interface_config(const char *if_name, 
                                                 netif_config_t *config);

/**
 * @brief Configure network interface
 * @param if_name Interface name
 * @param config Interface configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_configure_interface(const char *if_name, 
                                                 netif_config_t *config);

/**
 * @brief Start network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_start_interface(const char *if_name);

/**
 * @brief Stop network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_stop_interface(const char *if_name);

/**
 * @brief Restart network interface
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_restart_interface(const char *if_name);

/**
 * @brief Disconnect network
 * @param if_name Interface name
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_disconnect_network(const char *if_name);

/* ==================== Network Scanning ==================== */

/**
 * @brief Start network scan
 * @param callback Callback function for scan results
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_start_network_scan(wireless_scan_callback_t callback);

/**
 * @brief Get last scan results
 * @param results Array to store scan results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_scan_results(network_scan_result_t *results, 
                                             uint32_t max_count, 
                                             uint32_t *actual_count);

/**
 * @brief Get classified network scan results (known/unknown)
 * @param results Buffer to store classified scan results
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_classified_scan_results(classified_scan_results_t *results);

/**
 * @brief Get known networks only
 * @param results Buffer to store known network results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_known_networks(network_scan_result_t *results, 
                                               uint32_t max_count, 
                                               uint32_t *actual_count);

/**
 * @brief Get unknown networks only
 * @param results Buffer to store unknown network results
 * @param max_count Maximum number of results to return
 * @param actual_count Actual number of results returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_unknown_networks(network_scan_result_t *results, 
                                                 uint32_t max_count, 
                                                 uint32_t *actual_count);

/**
 * @brief Delete a known network from the database
 * @param ssid Network SSID
 * @param bssid Network BSSID
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_delete_known_network(const char *ssid, const char *bssid);

/* ==================== Communication Type Management ==================== */

/**
 * @brief Get list of available communication modules/types
 * @param types Array to store type information
 * @param max_count Maximum number of types to return
 * @param actual_count Actual number of types returned
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_available_modules(communication_type_info_t *types,
                                                   uint32_t max_count,
                                                   uint32_t *actual_count);

/**
 * @brief Get all communication types information
 * @param types Array to store type information (size should be COMM_TYPE_MAX)
 * @param max_count Maximum number of types
 * @param actual_count Actual count of types
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_all_types(communication_type_info_t *types,
                                           uint32_t max_count,
                                           uint32_t *actual_count);

/**
 * @brief Get specific communication type information
 * @param type Communication type
 * @param info Type information structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_type_info(communication_type_t type,
                                           communication_type_info_t *info);

/**
 * @brief Get current active (connected) communication type
 * @note This returns the type that is actually connected and used for data transmission
 * @return communication_type_t Active communication type (COMM_TYPE_NONE if no connection)
 */
communication_type_t communication_get_current_type(void);

/**
 * @brief Get user selected communication type (for UI page display)
 * @note This returns the type user has selected, which may not be connected yet
 * @return communication_type_t Selected communication type
 */
communication_type_t communication_get_selected_type(void);

/**
 * @brief Set user selected communication type (for UI page display)
 * @note This does not require connection, only checks if hardware is available
 * @param type Communication type to select
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_set_selected_type(communication_type_t type);

/**
 * @brief Get current communication type detailed information
 * @param info Type information structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_current_type_info(communication_type_info_t *info);

/**
 * @brief Get default communication type based on priority
 * @return communication_type_t Default type (highest priority available)
 */
communication_type_t communication_get_default_type(void);

/**
 * @brief Check if a communication type is available
 * @param type Communication type to check
 * @return aicam_bool_t TRUE if available
 */
aicam_bool_t communication_is_type_available(communication_type_t type);

/**
 * @brief Check if a communication type is connected
 * @param type Communication type to check
 * @return aicam_bool_t TRUE if connected
 */
aicam_bool_t communication_is_type_connected(communication_type_t type);

/**
 * @brief Switch to a different communication type (async)
 * @param type Target communication type
 * @param callback Callback function for result notification
 * @return aicam_result_t Operation result (AICAM_OK if switch initiated)
 */
aicam_result_t communication_switch_type(communication_type_t type,
                                         communication_switch_callback_t callback);

/**
 * @brief Switch to a different communication type (sync, blocking)
 * @param type Target communication type
 * @param result Switch result structure to fill
 * @param timeout_ms Timeout in milliseconds
 * @param scan_before_connect AICAM_TRUE on normal boot (scan then connect); AICAM_FALSE for web/low-power
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_switch_type_sync(communication_type_t type,
                                              communication_switch_result_t *result,
                                              uint32_t timeout_ms,
                                              aicam_bool_t scan_before_connect);

/** Mark an external switch session (e.g. Web API) so init callbacks do not duplicate work. */
void communication_switch_session_begin(void);
void communication_switch_session_end(void);

/**
 * @brief Set preferred communication type
 * @param type Preferred communication type
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_set_preferred_type(communication_type_t type);

/**
 * @brief Get preferred communication type
 * @return communication_type_t Preferred type
 */
communication_type_t communication_get_preferred_type(void);

/**
 * @brief Apply priority-based communication type selection
 * @note Automatically switches to highest priority available type
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_apply_priority(void);

/**
 * @brief Set auto priority enable state
 * @param enable Enable/disable auto priority
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_set_auto_priority(aicam_bool_t enable);

/**
 * @brief Get auto priority enable state
 * @return aicam_bool_t Auto priority state
 */
aicam_bool_t communication_get_auto_priority(void);

/**
 * @brief Convert communication type to string
 * @param type Communication type
 * @return const char* Type string
 */
const char* communication_type_to_string(communication_type_t type);

/**
 * @brief Convert communication status to string
 * @param status Communication status
 * @return const char* Status string
 */
const char* communication_status_to_string(communication_status_t status);

/**
 * @brief Convert string to communication type
 * @param str Type string
 * @return communication_type_t Communication type
 */
communication_type_t communication_type_from_string(const char *str);

/* ==================== Cellular/4G Management ==================== */

/**
 * @brief Get cellular connection status
 * @return communication_status_t Cellular status
 */
communication_status_t communication_cellular_get_status(void);

/**
 * @brief Get cellular connection settings
 * @param settings Settings structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_get_settings(cellular_connection_settings_t *settings);

/**
 * @brief Set cellular connection settings
 * @param settings Settings structure
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_set_settings(const cellular_connection_settings_t *settings);

/**
 * @brief Save cellular settings to NVS
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_save_settings(void);

/**
 * @brief Connect cellular network
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_connect(void);

/**
 * @brief Disconnect cellular network
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_disconnect(void);

/**
 * @brief Get cellular detailed information
 * @param info Detail information structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_get_detail_info(cellular_detail_info_t *info);

/**
 * @brief Refresh cellular information (update from modem)
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_refresh_info(void);

/**
 * @brief Send AT command to cellular modem
 * @param command AT command string (without AT prefix)
 * @param response Response buffer
 * @param response_size Response buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_send_at_command(const char *command,
                                                      char *response,
                                                      uint32_t response_size,
                                                      uint32_t timeout_ms);

/**
 * @brief Check if cellular module is available (hardware present)
 * @return aicam_bool_t TRUE if available
 */
aicam_bool_t communication_cellular_is_available(void);

/**
 * @brief Get cellular IMEI
 * @param imei IMEI buffer (at least 32 bytes)
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_cellular_get_imei(char *imei);

/* ==================== PoE/Ethernet Management ==================== */

/**
 * @brief PoE/Ethernet detailed information structure
 */
typedef struct {
    // Connection status
    communication_status_t network_status;  // Network status
    uint32_t status_code;                   // Detailed status code (poe_status_code_t)
    char status_message[64];                // Status message
    
    // IP configuration
    uint8_t ip_mode;                        // 0=DHCP, 1=Static
    char ip_address[16];                    // IPv4 address
    char netmask[16];                       // Subnet mask
    char gateway[16];                       // Default gateway
    char dns_primary[16];                   // Primary DNS
    char dns_secondary[16];                 // Secondary DNS
    char hostname[32];                      // Hostname
    
    // Hardware info
    char mac_address[18];                   // MAC address
    char interface_name[16];                // Interface name (wn)
    aicam_bool_t link_up;                   // Physical link status
    aicam_bool_t poe_powered;               // PoE power status
    
    // Connection statistics
    uint32_t connection_duration_sec;       // Connection duration in seconds
    uint32_t connection_start_time;         // Connection start timestamp
    uint32_t dhcp_lease_time;               // DHCP lease time (seconds)
    uint32_t dhcp_lease_remaining;          // DHCP lease remaining (seconds)
    
    // Event counters
    uint32_t connect_count;                 // Total connect attempts
    uint32_t disconnect_count;              // Total disconnections
    uint32_t dhcp_fail_count;               // DHCP failure count
    uint32_t last_error_code;               // Last error code
} poe_detail_info_t;

/**
 * @brief PoE static IP configuration structure
 */
typedef struct {
    uint8_t ip_addr[4];                     // IPv4 address
    uint8_t netmask[4];                     // Subnet mask
    uint8_t gateway[4];                     // Default gateway
    uint8_t dns_primary[4];                 // Primary DNS server
    uint8_t dns_secondary[4];               // Secondary DNS server
    char hostname[32];                      // Hostname
} poe_static_config_t;

/**
 * @brief Get PoE connection status
 * @return communication_status_t PoE status
 */
communication_status_t communication_poe_get_status(void);

/**
 * @brief Check if PoE is available (hardware detected)
 * @return aicam_bool_t TRUE if available
 */
aicam_bool_t communication_poe_is_available(void);

/**
 * @brief Connect PoE network
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_connect(void);

/**
 * @brief Disconnect PoE network
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_disconnect(void);

/**
 * @brief Get PoE detailed information
 * @param info Detail information structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_get_detail_info(poe_detail_info_t *info);

/**
 * @brief Get PoE IP mode (DHCP or Static)
 * @return uint8_t 0=DHCP, 1=Static
 */
uint8_t communication_poe_get_ip_mode(void);

/**
 * @brief Set PoE IP mode
 * @param mode 0=DHCP, 1=Static
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_set_ip_mode(uint8_t mode);

/**
 * @brief Get PoE static IP configuration
 * @param config Static IP configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_get_static_config(poe_static_config_t *config);

/**
 * @brief Set PoE static IP configuration
 * @param config Static IP configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_set_static_config(const poe_static_config_t *config);

/**
 * @brief Save PoE configuration to persistent storage
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_save_config(void);

/**
 * @brief Load PoE configuration from persistent storage
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_load_config(void);

/**
 * @brief Apply PoE configuration and reconnect
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_apply_config(void);

/**
 * @brief Validate static IP configuration
 * @param config Static IP configuration to validate
 * @param error_msg Buffer to store error message (optional)
 * @param error_msg_size Error message buffer size
 * @return aicam_result_t AICAM_OK if valid
 */
aicam_result_t communication_poe_validate_static_config(const poe_static_config_t *config,
                                                        char *error_msg,
                                                        size_t error_msg_size);

/**
 * @brief Check if IP address is valid and not conflicting
 * @param ip_addr IP address to check (4 bytes)
 * @return aicam_result_t AICAM_OK if valid and no conflict
 */
aicam_result_t communication_poe_check_ip_conflict(const uint8_t *ip_addr);

/**
 * @brief Check gateway reachability
 * @param gateway Gateway address (4 bytes)
 * @param timeout_ms Timeout in milliseconds
 * @return aicam_result_t AICAM_OK if reachable
 */
aicam_result_t communication_poe_check_gateway(const uint8_t *gateway, uint32_t timeout_ms);

/**
 * @brief Get PoE status code
 * @return uint32_t Status code (poe_status_code_t)
 */
uint32_t communication_poe_get_status_code(void);

/**
 * @brief Get PoE status message
 * @param buffer Buffer to store status message
 * @param buffer_size Buffer size
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_poe_get_status_message(char *buffer, size_t buffer_size);

/* ==================== Service Management ==================== */

/**
 * @brief Get communication service configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_config(communication_service_config_t *config);

/**
 * @brief Set communication service configuration
 * @param config Configuration structure
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_set_config(const communication_service_config_t *config);

/**
 * @brief Save communication service configuration to NVS
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_save_config_to_nvs(void);

/**
 * @brief Get communication service statistics
 * @param stats Statistics structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_get_stats(communication_service_stats_t *stats);

/**
 * @brief Reset communication service statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t communication_reset_stats(void);

/**
 * @brief Check if communication service is running
 * @return aicam_bool_t TRUE if running, FALSE otherwise
 */
aicam_bool_t communication_is_running(void);

/**
 * @brief Get communication service version
 * @return const char* Version string
 */
const char* communication_get_version(void);

/**
 * @brief Register CLI commands
 */
void comm_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_SERVICE_H */
