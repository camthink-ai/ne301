#ifndef __NETIF_MANAGER_H__
#define __NETIF_MANAGER_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/dns.h"

#define NETIF_LWIP_FRAME_ALIGNMENT          (60)
#define NETIF_MAX_TRANSFER_UNIT             (1500)
#define NETIF_DEFAULT_DNS_SERVER1           (0x08080808)    // 8.8.8.8
#define NETIF_DEFAULT_DNS_SERVER2           (0x050505DF)    // 223.5.5.5

#define NETIF_WIFI_STA_DEFAULT_SSID         "CAMTHINK_DEV"
#define NETIF_WIFI_STA_DEFAULT_PW           "12345678."
#define NETIF_WIFI_STA_DEFAULT_DHCP_TIMEOUT (30000)
#define NETIF_WIFI_STA_DEFAULT_IP           0xC86EA8C0      // 192.168.110.200
#define NETIF_WIFI_STA_DEFAULT_MASK         0x00FFFFFF      // 255.255.255.0
#define NETIF_WIFI_STA_DEFAULT_GW           0x016EA8C0      // 192.168.110.1

#define NETIF_WIFI_AP_DEFAULT_SSID          ""
#define NETIF_WIFI_AP_DEFAULT_PW            ""
#define NETIF_WIFI_AP_DEFAULT_IP            0x0A0AA8C0      // 192.168.10.10
#define NETIF_WIFI_AP_DEFAULT_MASK          0x00FFFFFF      // 255.255.255.0
#define NETIF_WIFI_AP_DEFAULT_GW            0x0A0AA8C0      // 192.168.10.10
#define NETIF_WIFI_AP_DEFAULT_CLIENT_NUM    (3)
#define NETIF_WIFI_AP_MAX_CLIENT_NUM        (5)

#define NETIF_ETH_WAN_IS_ENABLE             (1)
#define NETIF_ETH_WAN_DEFAULT_DHCP_TIMEOUT  (30000)
#define NETIF_ETH_WAN_DEFAULT_IP_MODE       (NETIF_IP_MODE_DHCP)
#define NETIF_ETH_WAN_DEFAULT_IP            {192, 168, 60, 232}
#define NETIF_ETH_WAN_DEFAULT_MASK          {255, 255, 255, 0}
#define NETIF_ETH_WAN_DEFAULT_GW            {192, 168, 60, 1}
#define NETIF_ETH_WAN_MACRAW_SEND_TIMEOUT   (20)
#define NETIF_ETH_WAN_WAIT_IR_TIMEOUT       (100)
#define NETIF_ETH_WAN_SBUF_CHANGE_IDLE_TIME (10)
#define NETIF_ETH_WAN_W5500_FW_VERSION      (0x04) ///< W5500 firmware version 0.4

#define NETIF_4G_CAT1_IS_ENABLE             (1)
#define NETIF_4G_CAT1_INIT_TIMEOUT_MS       (10000)
#define NETIF_4G_CAT1_CNT_TIMEOUT_MS        (30000)
#define NETIF_4G_CAT1_EXIT_TIMEOUT_MS       (10000)
#define NETIF_4G_CAT1_EXIT_DELAY_MS         (500)
#define NETIF_4G_CAT1_PPP_INTERVAL_MS       (1000)
#define NETIF_4G_CAT1_PPP_SEND_TIMEOUT      (50)
#define NETIF_4G_CAT1_TRY_CNT               (3)

#define NETIF_USB_ECM_IS_ENABLE             (1)
#define NETIF_USB_ECM_ACTIVATE_TIMEOUT_MS   (30000)
#define NETIF_USB_ECM_DHCP_TIMEOUT_MS       (30000)
#define NETIF_USB_ECM_UP_TIMEOUT_MS         (3000)
#define NETIF_USB_ECM_STABLE_TIME_MS        (3000)
#define NETIF_USB_ECM_STABLE_TIMEOUT_MS     (30000)
#define NETIF_USB_ECM_DEFAULT_IP_MODE       (NETIF_IP_MODE_DHCP)
#define NETIF_USB_ECM_DEFAULT_IP            {192, 168, 10, 100}
#define NETIF_USB_ECM_DEFAULT_MASK          {255, 255, 255, 0}
#define NETIF_USB_ECM_DEFAULT_GW            {192, 168, 10, 1}
#if defined(STM32N6_DK_BOARD)
    #define NETIF_USB_ECM_IS_CAT1_MODULE        (0)
#else
    #define NETIF_USB_ECM_IS_CAT1_MODULE        (1)
#endif

#ifndef NETIF_WIFI_HALOW_IS_ENABLE
#define NETIF_WIFI_HALOW_IS_ENABLE          (1)
#endif
#define NETIF_WIFI_HALOW_DEFAULT_TX_PWR       (0)
#define NETIF_WIFI_HALOW_DEFAULT_SCAN_DWELL   (30)
#if NETIF_WIFI_HALOW_IS_ENABLE
#define NETIF_WIFI_HALOW_DEFAULT_SSID         ""
#define NETIF_WIFI_HALOW_DEFAULT_PW           ""
#define NETIF_WIFI_HALOW_DEFAULT_COUNTRY      "US"
#define NETIF_WIFI_HALOW_DEFAULT_IP           {192, 168, 12, 199}
#define NETIF_WIFI_HALOW_DEFAULT_MASK         {255, 255, 255, 0}
#define NETIF_WIFI_HALOW_DEFAULT_GW           {192, 168, 12, 1}
#define NETIF_WIFI_HALOW_DEFAULT_IP_MODE      (NETIF_IP_MODE_DHCP)
#define NETIF_WIFI_HALOW_MAX_SCAN_DWELL       (300)
/** 1: scan de-dup by BSSID+SSID+freq+bw; 0 (default): BSSID+SSID only */
#ifndef NETIF_WIFI_HALOW_SCAN_DEDUP_BY_FREQ_BW
#define NETIF_WIFI_HALOW_SCAN_DEDUP_BY_FREQ_BW  (0)
#endif
#endif

#define NETIF_NAME_STR_FMT                  "%c%c%d"
#define NETIF_NAME_PARAMETER(netif)          (netif)->name[0], (netif)->name[1], (netif)->num
#define NETIF_NAME_LOCAL                    "lo"
#define NETIF_NAME_WIFI_STA                 "wl"
#define NETIF_NAME_WIFI_AP                  "ap"
#define NETIF_NAME_WIFI_HALOW               "hw"
#define NETIF_NAME_ETH_WAN                  "wn"
#define NETIF_NAME_4G_CAT1                  "4g"
#define NETIF_NAME_USB_ECM                  "ue"
#if NETIF_ETH_WAN_IS_ENABLE
#define NETIF_DEFAULT_NETIF_NAME            NETIF_NAME_ETH_WAN
#else
#define NETIF_DEFAULT_NETIF_NAME            NETIF_NAME_WIFI_STA
#endif

#define NETIF_MAC_STR_FMT                   "%02x:%02x:%02x:%02x:%02x:%02x"
#define NETIF_MAC_SCAN_STR_FMT              "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx"
#define NETIF_MAC_PARAMETER(mac)            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
#define NETIF_MAC_IS_MULTICAST(mac)         (mac[0] & 0x01)
#define NETIF_MAC_IS_BROADCAST(mac)         ((mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xFF)
#define NETIF_MAC_IS_ZERO(mac)              (!(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]))
#define NETIF_MAC_IS_UNICAST(mac)           ((!NETIF_MAC_IS_ZERO(mac)) && (!NETIF_MAC_IS_BROADCAST(mac)) && (!NETIF_MAC_IS_MULTICAST(mac)))

#define NETIF_IPV4_STR_FMT                  "%d.%d.%d.%d"
#define NETIF_IPV4_PARAMETER(ip)           	ip[0], ip[1], ip[2], ip[3]
#define NETIF_IPV4_IS_ZERO(ip)              (!(ip[0] | ip[1] | ip[2] | ip[3]))

#define NETIF_HOST_NAME_SIZE                (33)
#define NETIF_SSID_VALUE_SIZE               (33)
#define NETIF_PW_VALUE_SIZE                 (65)
#define NETIF_FW_VERSION_SIZE               (65)

/// @brief Network interface type
typedef enum {
    NETIF_TYPE_LOCAL = 0,                   // Local
    NETIF_TYPE_WIRELESS,                    // Wireless
    NETIF_TYPE_ETH,                         // Ethernet
    NETIF_TYPE_4G,                          // 4G
    NETIF_TYPE_MAX,
} netif_type_t;

/// @brief Network interface state
typedef enum {
    NETIF_STATE_DEINIT = 0,                 // Not initialized
    NETIF_STATE_DOWN,                       // Disabled
    NETIF_STATE_UP,                         // Enabled
    NETIF_STATE_MAX,                        
} netif_state_t;

/// @brief Network interface IP mode
typedef enum {
    NETIF_IP_MODE_STATIC = 0,               // Static address
    NETIF_IP_MODE_DHCP,                     // DHCP client
    NETIF_IP_MODE_DHCPS,                    // DHCP server
    NETIF_IP_MODE_MAX,
} netif_ip_mode_t;

/// @brief Network interface operation command
typedef enum {
    NETIF_CMD_CFG = 0,                      // Configuration command (requires upper layer to DOWN/UP)
    NETIF_CMD_CFG_EX,                       // Configuration command (extended, automatically DOWN/UP)
    NETIF_CMD_INIT,                         // Initialization command
    NETIF_CMD_UP,                           // Enable command
    NETIF_CMD_INFO,                         // Get information command
    NETIF_CMD_STATE,                        // Get state command
    NETIF_CMD_DOWN,                         // Disable command
    NETIF_CMD_UNINIT,                       // Destroy command
    NETIF_CMD_MAX,
} netif_cmd_t;

/// @brief Wireless security type
typedef enum {
  WIRELESS_OPEN = 0,                        ///< Wi-Fi Open security type
  WIRELESS_WPA,                             ///< Wi-Fi WPA security type
  WIRELESS_WPA2,                            ///< Wi-Fi WPA2 security type
  WIRELESS_WEP,                             ///< Wi-Fi WEP security type
  WIRELESS_WPA_ENTERPRISE,                  ///< Wi-Fi WPA enterprise security type
  WIRELESS_WPA2_ENTERPRISE,                 ///< Wi-Fi WPA2 enterprise security type
  WIRELESS_WPA_WPA2_MIXED,                  ///< Wi-Fi WPA/WPA2 mixed security type that supports both WPA and WPA2
  WIRELESS_WPA3,                            ///< Wi-Fi WPA3 security type
  WIRELESS_WPA3_TRANSITION,                 ///< Wi-Fi WPA3 Transition security type (not currently supported in AP mode)
  WIRELESS_WPA3_ENTERPRISE,                 ///< Wi-Fi WPA3 enterprise security type
  WIRELESS_WPA3_TRANSITION_ENTERPRISE,      ///< Wi-Fi WPA3 Transition enterprise security type
  WIRELESS_OWE,                             ///< Wi-Fi OWE (HaLow / WPA3)
  WIRELESS_SAE,                             ///< Wi-Fi SAE (HaLow / WPA3)
  WIRELESS_SECURITY_MAX,
  WIRELESS_SECURITY_UNKNOWN = 0xFFFF,       ///< Wi-Fi Unknown Security type
} wireless_security_t;

/// @brief Wireless encryption method
typedef enum {
  WIRELESS_DEFAULT_ENCRYPTION = 0,          ///< Default Wi-Fi encryption
  WIRELESS_NO_ENCRYPTION,                   ///< Wi-Fi with no Encryption (not currently supported in STA mode)
  WIRELESS_WEP_ENCRYPTION,                  ///< Wi-Fi with WEP Encryption (not currently supported in STA mode)
  WIRELESS_TKIP_ENCRYPTION,                 ///< Wi-Fi with TKIP Encryption (not currently supported in STA mode)
  WIRELESS_CCMP_ENCRYPTION,                 ///< Wi-Fi with CCMP Encryption
  WIRELESS_EAP_TLS_ENCRYPTION,              ///< Wi-Fi with Enterprise TLS Encryption
  WIRELESS_EAP_TTLS_ENCRYPTION,             ///< Wi-Fi with Enterprise TTLS Encryption
  WIRELESS_EAP_FAST_ENCRYPTION,             ///< Wi-Fi with Enterprise FAST Encryption
  WIRELESS_PEAP_MSCHAPV2_ENCRYPTION,        ///< Wi-Fi with Enterprise PEAP Encryption
  WIRELESS_EAP_LEAP_ENCRYPTION,             ///< Wi-Fi with Enterprise LEAP Encryption
  WIRELESS_ENCRYPTION_MAX
} wireless_encryption_t;

#pragma pack(push, 1)
/// @brief Wireless configuration
typedef struct
{
    uint8_t bssid[6];                       // BSSID (only for wireless network cards)
    char ssid[NETIF_SSID_VALUE_SIZE];       // SSID
    char pw[NETIF_PW_VALUE_SIZE];           // PASSWORD
    wireless_security_t security;           // Wireless security type
    wireless_encryption_t encryption;       // Wireless encryption
    uint8_t channel;                        // Channel (0: auto, others: specified)
    uint8_t max_client_num;                 // Maximum client count (only for AP network cards)
} wireless_config_t;

/** HaLow regdomain code buffer size (matches @ref MMWLAN_COUNTRY_CODE_LEN). */
#define NETIF_HALOW_COUNTRY_CODE_LEN        (16U)

#if NETIF_WIFI_HALOW_IS_ENABLE
/// @brief HaLow (mmx108) extended wireless configuration
typedef struct {
    char country_code[NETIF_HALOW_COUNTRY_CODE_LEN];  ///< Regdomain code (e.g. "US", "AU_2020")
    uint16_t tx_power_dbm;                  ///< TX power cap (0 = use regdomain max only)
    uint8_t ps_mode;                        ///< MMWLAN_PS_DISABLED / MMWLAN_PS_ENABLED
    uint8_t pmf_mode;                       ///< MMWLAN_PMF_REQUIRED / MMWLAN_PMF_DISABLED
    uint32_t scan_dwell_ms;                 ///< Foreground scan dwell time per channel
    uint8_t ndp_probe_enabled;              ///< NDP probe for internal/scan config
    uint16_t bgscan_short_interval_s;
    int8_t bgscan_signal_threshold_dbm;
    uint16_t bgscan_long_interval_s;
    int8_t rc_mcs;                          ///< TX MCS 0..9, or -1 (rate control default)
    int8_t rc_bw_mhz;                       ///< TX BW 1/2/4/8 MHz, or -1 (default)
    int8_t rc_gi;                           ///< GI: 0 short, 1 long, or -1 (default)
} halow_wireless_config_t;
#endif

/// @brief Cellular configuration
typedef struct {
    char apn[32];                           // APN (Access Point Name)
    char user[64];                          // APN username
    char passwd[64];                        // APN password
    uint8_t apn_context_id;                 // APN context ID (read only) (0: Auto, others: specified)
    uint8_t authentication;                 // APN authentication
    uint8_t is_enable_roam;                 // Enable roaming
    uint8_t isp_selected;                   // ISP selected (0: Auto, 1: China Mobile, 2: China Unicom, 3: China Telecom, 4: American Verizon)
    char plmn[8];                           // Manual PLMN code (MCC+MNC, 5-6 digits); empty = auto COPS=0
    char pin[32];                           // SIM PIN
    char puk[32];                           // SIM PUK
    uint8_t ppp_context_id;                 // PPP context ID (0: Auto, others: specified)
    char ppp_pre_at_cmds[8][64];           // PPP pre-AT command list (8 commands, 64 characters each)
} cellular_config_t;

/// @brief Cellular information
typedef struct {
    int csq_value;                       // Signal strength value (0~31, 99: no signal)
    int ber_value;                       // Bit error rate value
    int csq_level;                       // Signal strength level (0~5)
    int rssi;                            // Received Signal Strength Indicator
    char model_name[64];                 // Device model name
    char imei[32];                       // Device IMEI
    char imsi[32];                       // SIM card IMSI
    char iccid[32];                      // SIM card ICCID
    char sim_status[32];                 // SIM card status
    char operator[32];                   // Current network operator name
    char version[32];                    // Firmware version
    char plmn_id[8];                     // Current network PLMN ID
    char cell_id[32];                    // Current cell ID
    char lac[32];                        // Location area code
    char network_type[16];               // Network type
    char registration_status[64];        // Network registration status
} cellular_info_t;

/// @brief Wireless scan information
typedef struct {
    int rssi;                               ///< RSSI value of the AP
    char ssid[NETIF_SSID_VALUE_SIZE];       ///< SSID of the AP
    uint8_t bssid[6];                       ///< BSSID of the AP
    uint8_t channel;                        ///< Channel number of the AP (legacy Wi-Fi)
    uint8_t security;                       ///< Security mode of the AP
    uint32_t channel_freq_hz;               ///< Center frequency (Hz), for HaLow S1G
    uint8_t bw_mhz;                         ///< Channel bandwidth (MHz), for HaLow S1G
} wireless_scan_info_t;

/// @brief Wireless scan result
typedef struct {
    uint8_t scan_count;                     ///< Number of available scan results
    wireless_scan_info_t *scan_info;        ///< Scan infos
} wireless_scan_result_t;
#pragma pack(pop)

/// @brief Wireless scan callback
typedef void (*wireless_scan_callback_t)(int recode, wireless_scan_result_t *scan_result);

/// @brief Network interface status
typedef struct {
    const char *if_name;                    // Network interface name
    char *host_name;                        // Host name
    netif_state_t state;                    // Network interface state
    netif_type_t type;                      // Network interface type
    
    int rssi;                               // RSSI (only for wireless/4G network interface)
    cellular_info_t cellular_info;          // Cellular information (only for 4G network interface)
    cellular_config_t cellular_cfg;         // Cellular configuration (only for 4G network interface)
    wireless_config_t wireless_cfg;         // Wireless configuration (only for wireless network interface)
#if NETIF_WIFI_HALOW_IS_ENABLE
    halow_wireless_config_t halow_cfg;      // HaLow extended config (only for hw netif)
#endif

    char fw_version[NETIF_FW_VERSION_SIZE]; // Firmware version
    uint8_t if_mac[6];                      // Network interface MAC address
    netif_ip_mode_t ip_mode;                // IP mode
    uint8_t ip_addr[4];                     // IP address
    uint8_t netmask[4];                     // Netmask address
    uint8_t gw[4];                          // Gateway address
} netif_info_t;

/// @brief Network interface configuration
typedef struct {
    char *host_name;                        // Host name

    cellular_config_t cellular_cfg;         // Cellular configuration (only for 4G network interface)
    wireless_config_t wireless_cfg;         // Wireless configuration (only for wireless network interface)
#if NETIF_WIFI_HALOW_IS_ENABLE
    halow_wireless_config_t halow_cfg;      // HaLow extended config (only for hw netif)
#endif

    uint8_t diy_mac[6];                     // Custom MAC address (all zeros means use default MAC address)
    netif_ip_mode_t ip_mode;                // IP mode
    uint8_t ip_addr[4];                     // IP address
    uint8_t netmask[4];                     // Netmask address
    uint8_t gw[4];                          // Gateway address
} netif_config_t;

/// @brief Network interface control interface
/// @param if_name Network interface name
/// @param cmd Command
/// @param param Parameter
/// @return Error code
int netif_manager_ctrl(const char *if_name, netif_cmd_t cmd, void *param);

/// @brief Register network interface manager commands
/// @param None
void netif_manager_register_commands(void);

/// @brief Register network interface manager to system
/// @param None
void netif_manager_register(void);

/// @brief Unregister network interface manager
/// @param None
void netif_manager_unregister(void);

/// @brief Get network interface information list
/// @param netif_info_list Pointer to pointer of information list (needs external release)
/// @return Less than 0: failure, greater than or equal to 0: number of network interfaces
int nm_get_netif_list(netif_info_t **netif_info_list);

/// @brief Free network interface information list
/// @param netif_info_list Information list pointer
void nm_free_netif_list(netif_info_t *netif_info_list);

/// @brief Get network interface state
/// @param if_name Network interface name
/// @return Network interface state
netif_state_t nm_get_netif_state(const char *if_name);

/// @brief Get network interface information
/// @param if_name Network interface name
/// @param netif_info Network interface information pointer
/// @return Error code
int nm_get_netif_info(const char *if_name, netif_info_t *netif_info);

/// @brief Print network interface information
/// @param if_name Network interface name (if not empty, use it first)
/// @param netif_info Network interface information pointer
void nm_print_netif_info(const char *if_name, netif_info_t *netif_info);

/// @brief Get network interface configuration
/// @param if_name Network interface name
/// @param netif_cfg Network interface configuration pointer
/// @return Error code
int nm_get_netif_cfg(const char *if_name, netif_config_t *netif_cfg);

/// @brief Set network interface configuration (better call nm_get_netif_cfg before setting)
/// @param if_name Network interface name
/// @param netif_cfg Network interface configuration pointer
/// @return Error code
int nm_set_netif_cfg(const char *if_name, netif_config_t *netif_cfg);

/// @brief Initialize network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_init(const char *if_name);

/// @brief Start network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_up(const char *if_name);

/// @brief Stop network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_down(const char *if_name);

/// @brief Deinitialize network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_deinit(const char *if_name);

/// @brief Get default network interface name
/// @param None
/// @return Default network interface name
const char *nm_get_default_netif_name(void);

/// @brief Get default network interface information
/// @param netif_info Default network interface information
/// @return Error code
int nm_ctrl_get_default_netif_info(netif_info_t *netif_info);

/// @brief Set default network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_set_default_netif(const char *if_name);

/// @brief Get configured default network interface name (but may not be the one in use)
/// @param None
/// @return Default network interface name
const char *nm_get_set_default_netif_name(void);

/// @brief Set DNS server (maximum number is DNS_MAX_SERVERS)
/// @param idx DNS server index
/// @param dns_server DNS server address
/// @return Error code
int nm_ctrl_set_dns_server(int idx, uint8_t *dns_server);

/// @brief Get DNS server (maximum number is DNS_MAX_SERVERS)
/// @param idx DNS server index
/// @param dns_server DNS server address
/// @return Error code
int nm_ctrl_get_dns_server(int idx, uint8_t *dns_server);

/// @brief Wireless scan (default interface: Wi-Fi STA wl)
/// @param callback Callback function
/// @return Error code
int nm_wireless_start_scan(wireless_scan_callback_t callback);

/// @brief Wireless scan on specified interface (wl/ap/hw)
int nm_wireless_start_scan_ex(const char *if_name, wireless_scan_callback_t callback);

/// @brief Get wireless scan result (default: wl)
wireless_scan_result_t *nm_wireless_get_scan_result(void);

/// @brief Get wireless scan result for specified interface
wireless_scan_result_t *nm_wireless_get_scan_result_ex(const char *if_name);

/// @brief Update wireless scan result (default: wl)
int nm_wireless_update_scan_result(uint32_t timeout);

/// @brief Update wireless scan result for specified interface
int nm_wireless_update_scan_result_ex(const char *if_name, uint32_t timeout);

/// @brief Print wireless scan result
/// @param if_name Interface name (NULL for legacy Wi-Fi channel print)
/// @param scan_result Scan result
void nm_print_wireless_scan_result(const char *if_name, wireless_scan_result_t *scan_result);

#ifdef __cplusplus
}
#endif

#endif /* __NETIF_MANAGER_H__ */
