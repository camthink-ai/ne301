#include <stdio.h>
#include <string.h>
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "lwip/ethip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/apps/mdns.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net_wifi_types.h"
#include "sl_wifi.h"
#include "sl_net.h"
#include "wifi.h"
#include "sl_rsi_utility.h"
#include "sli_net_utility.h"
#include "sli_net_constants.h"
#include "sli_wifi_constants.h"
#include "sl_net_dns.h"
#include "Log/debug.h"
#include "dhcpserver.h"
#include "sl_net_netif.h"

#define IS_TCP_IP_DUAL_MODE         1
#define IS_ENABLE_NWP_DEBUG_PRINTS  0
#ifdef SLI_SI91X_ENABLE_BLE
#define IS_ENABLE_BLE               1
#else
#define IS_ENABLE_BLE               0
#endif

#if IS_ENABLE_BLE
#include "ble_config.h"
#include "sl_rsi_ble.h"
#include "rsi_bt_common_apis.h"
#include "rsi_common_apis.h"
#endif

#if IS_TCP_IP_DUAL_MODE
extern sl_status_t sl_si91x_configure_ip_address(sl_net_ip_configuration_t *address, uint8_t virtual_ap_id);
#endif

/* Used by SDK event handler for dual-stack raw-data routing (sl_net_for_dual_stack.c not linked). */
bool bypass_mode_enabled = false;
bool dual_mode_enabled   = false;

/// @brief Default wireless network interface configuration
static const sl_wifi_device_configuration_t device_configuration = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = SL_WIFI_REGION_US,
  .boot_config = { .oper_mode = SL_SI91X_CONCURRENT_MODE,
#if IS_ENABLE_BLE
                   .coex_mode = SL_SI91X_WLAN_BLE_MODE,
#else
                   .coex_mode = SL_SI91X_WLAN_ONLY_MODE,
#endif
                   .feature_bit_map = (SL_SI91X_FEAT_SECURITY_OPEN | SL_SI91X_FEAT_AGGREGATION | SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE
#ifdef SLI_SI91X_MCU_INTERFACE
                      | SL_SI91X_FEAT_WPS_DISABLE
#endif
                      ),
                   .tcp_ip_feature_bit_map     = (
#if IS_TCP_IP_DUAL_MODE
                        SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_DHCPV4_SERVER | SL_SI91X_TCP_IP_FEAT_ICMP | SL_SI91X_TCP_IP_FEAT_SSL
                        | SL_SI91X_TCP_IP_FEAT_DNS_CLIENT |
#else
                        SL_SI91X_TCP_IP_FEAT_BYPASS | 
#endif
                        SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),
                   .custom_feature_bit_map     = SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID | SL_SI91X_CUSTOM_FEAT_DNS_SERVER_IN_DHCP_OFFER,
                   .ext_custom_feature_bit_map = (SL_SI91X_EXT_FEAT_LOW_POWER_MODE | SL_SI91X_EXT_FEAT_XTAL_CLK | MEMORY_CONFIG
#if IS_ENABLE_NWP_DEBUG_PRINTS
                    | SL_SI91X_EXT_FEAT_UART_SEL_FOR_DEBUG_PRINTS
#endif
#if defined(SLI_SI917) || defined(SLI_SI915)
                                                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif

#if IS_ENABLE_BLE
                                                  | SL_SI91X_EXT_FEAT_BT_CUSTOM_FEAT_ENABLE
#endif
                                                  ),
#if IS_ENABLE_BLE
                    .bt_feature_bit_map        = (SL_SI91X_BT_RF_TYPE | SL_SI91X_ENABLE_BLE_PROTOCOL),
#else
                   .bt_feature_bit_map         = 0,
#endif
                   .ext_tcp_ip_feature_bit_map = (
#if IS_TCP_IP_DUAL_MODE
                        SL_SI91X_EXT_TCP_IP_DUAL_MODE_ENABLE | SL_SI91X_EXT_EMB_MQTT_ENABLE
                        | SL_SI91X_EXT_TCP_IP_WINDOW_SCALING | SL_SI91X_EXT_TCP_IP_TOTAL_SELECTS(10) |
#endif
                        SL_SI91X_CONFIG_FEAT_EXTENTION_VALID),
#if IS_ENABLE_BLE
                    .ble_feature_bit_map =
                    ((SL_SI91X_BLE_MAX_NBR_PERIPHERALS(RSI_BLE_MAX_NBR_PERIPHERALS)
                    | SL_SI91X_BLE_MAX_NBR_CENTRALS(RSI_BLE_MAX_NBR_CENTRALS)
                    | SL_SI91X_BLE_MAX_NBR_ATT_SERV(RSI_BLE_MAX_NBR_ATT_SERV)
                    | SL_SI91X_BLE_MAX_NBR_ATT_REC(RSI_BLE_MAX_NBR_ATT_REC))
                    | SL_SI91X_FEAT_BLE_CUSTOM_FEAT_EXTENTION_VALID | SL_SI91X_BLE_PWR_INX(RSI_BLE_PWR_INX)
                    | SL_SI91X_BLE_PWR_SAVE_OPTIONS(RSI_BLE_PWR_SAVE_OPTIONS) | SL_SI91X_916_BLE_COMPATIBLE_FEAT_ENABLE
#if RSI_BLE_GATT_ASYNC_ENABLE
                    | SL_SI91X_BLE_GATT_ASYNC_ENABLE
#endif
                    ),
               .ble_ext_feature_bit_map =
                    ((SL_SI91X_BLE_NUM_CONN_EVENTS(RSI_BLE_NUM_CONN_EVENTS)
                    | SL_SI91X_BLE_NUM_REC_BYTES(RSI_BLE_NUM_REC_BYTES))
#if RSI_BLE_INDICATE_CONFIRMATION_FROM_HOST
                    | SL_SI91X_BLE_INDICATE_CONFIRMATION_FROM_HOST //indication response from app
#endif
#if RSI_BLE_MTU_EXCHANGE_FROM_HOST
                    | SL_SI91X_BLE_MTU_EXCHANGE_FROM_HOST //MTU Exchange request initiation from app
#endif
#if RSI_BLE_SET_SCAN_RESP_DATA_FROM_HOST
                    | (SL_SI91X_BLE_SET_SCAN_RESP_DATA_FROM_HOST) //Set SCAN Resp Data from app
#endif
#if RSI_BLE_DISABLE_CODED_PHY_FROM_HOST
                    | (SL_SI91X_BLE_DISABLE_CODED_PHY_FROM_HOST) //Disable Coded PHY from app
#endif
#if BLE_SIMPLE_GATT
                    | SL_SI91X_BLE_GATT_INIT
#endif
                    ),
#else
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
#endif
                   .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP | SL_SI91X_ENABLE_ENHANCED_MAX_PSP) }
};
/// @brief Remote wake-up configuration (wifi mode)
static sl_wifi_device_configuration_t remote_wake_up_wifi_cfg = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = SL_WIFI_REGION_US,
  .boot_config = { .oper_mode = SL_SI91X_CLIENT_MODE,
                   .coex_mode = SL_SI91X_WLAN_ONLY_MODE,
                   .feature_bit_map =
                     (SL_SI91X_FEAT_SECURITY_OPEN | SL_SI91X_FEAT_AGGREGATION | SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE
#ifdef SLI_SI91X_MCU_INTERFACE
                      | SL_SI91X_FEAT_WPS_DISABLE
#endif
                      ),
                   .tcp_ip_feature_bit_map = (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID
                                              | SL_SI91X_TCP_IP_FEAT_SSL | SL_SI91X_TCP_IP_FEAT_DNS_CLIENT),
                   .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID),
                   .ext_custom_feature_bit_map = (SL_SI91X_EXT_FEAT_LOW_POWER_MODE | SL_SI91X_EXT_FEAT_XTAL_CLK
                                                  | SL_SI91X_EXT_FEAT_DISABLE_DEBUG_PRINTS | MEMORY_CONFIG
#if defined(SLI_SI917) || defined(SLI_SI915)
                                                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif
                                                  ),
                   .bt_feature_bit_map         = 0,
                   .ext_tcp_ip_feature_bit_map = (SL_SI91X_CONFIG_FEAT_EXTENTION_VALID | SL_SI91X_EXT_EMB_MQTT_ENABLE),
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
                   .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP | SL_SI91X_ENABLE_ENHANCED_MAX_PSP) }
};
#if IS_ENABLE_BLE
/// @brief Remote wake-up configuration (ble mode)
static sl_wifi_device_configuration_t remote_wake_up_ble_cfg = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = SL_WIFI_REGION_US,
  .boot_config = { .oper_mode = SL_SI91X_CLIENT_MODE,
                   .coex_mode = SL_SI91X_WLAN_BLE_MODE,
                   .feature_bit_map        = (SL_SI91X_FEAT_WPS_DISABLE | SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE),
                   .tcp_ip_feature_bit_map = (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),
                   .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID),
                   .ext_custom_feature_bit_map = (SL_SI91X_EXT_FEAT_LOW_POWER_MODE | SL_SI91X_EXT_FEAT_XTAL_CLK
                                                  | SL_SI91X_EXT_FEAT_DISABLE_DEBUG_PRINTS | MEMORY_CONFIG
                                                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
                                                  | SL_SI91X_EXT_FEAT_BT_CUSTOM_FEAT_ENABLE),
                   .bt_feature_bit_map         = (SL_SI91X_BT_RF_TYPE | SL_SI91X_ENABLE_BLE_PROTOCOL),
                   .ext_tcp_ip_feature_bit_map = (SL_SI91X_CONFIG_FEAT_EXTENTION_VALID | SL_SI91X_EXT_EMB_MQTT_ENABLE),
                   .ble_feature_bit_map =
                    ((SL_SI91X_BLE_MAX_NBR_PERIPHERALS(RSI_BLE_MAX_NBR_PERIPHERALS)
                    | SL_SI91X_BLE_MAX_NBR_CENTRALS(RSI_BLE_MAX_NBR_CENTRALS)
                    | SL_SI91X_BLE_MAX_NBR_ATT_SERV(RSI_BLE_MAX_NBR_ATT_SERV)
                    | SL_SI91X_BLE_MAX_NBR_ATT_REC(RSI_BLE_MAX_NBR_ATT_REC))
                    | SL_SI91X_FEAT_BLE_CUSTOM_FEAT_EXTENTION_VALID | SL_SI91X_BLE_PWR_INX(RSI_BLE_PWR_INX)
                    | SL_SI91X_BLE_PWR_SAVE_OPTIONS(RSI_BLE_PWR_SAVE_OPTIONS) | SL_SI91X_916_BLE_COMPATIBLE_FEAT_ENABLE
#if RSI_BLE_GATT_ASYNC_ENABLE
                    | SL_SI91X_BLE_GATT_ASYNC_ENABLE
#endif
                    ),
                   .ble_ext_feature_bit_map =
                    ((SL_SI91X_BLE_NUM_CONN_EVENTS(RSI_BLE_NUM_CONN_EVENTS)
                    | SL_SI91X_BLE_NUM_REC_BYTES(RSI_BLE_NUM_REC_BYTES))
#if RSI_BLE_INDICATE_CONFIRMATION_FROM_HOST
                    | SL_SI91X_BLE_INDICATE_CONFIRMATION_FROM_HOST //indication response from app
#endif
#if RSI_BLE_MTU_EXCHANGE_FROM_HOST
                    | SL_SI91X_BLE_MTU_EXCHANGE_FROM_HOST //MTU Exchange request initiation from app
#endif
#if RSI_BLE_SET_SCAN_RESP_DATA_FROM_HOST
                    | (SL_SI91X_BLE_SET_SCAN_RESP_DATA_FROM_HOST) //Set SCAN Resp Data from app
#endif
#if RSI_BLE_DISABLE_CODED_PHY_FROM_HOST
                    | (SL_SI91X_BLE_DISABLE_CODED_PHY_FROM_HOST) //Disable Coded PHY from app
#endif
#if BLE_SIMPLE_GATT
                    | SL_SI91X_BLE_GATT_INIT
#endif
                    ),
                   .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP | SL_SI91X_ENABLE_ENHANCED_MAX_PSP) }
};
#endif
#define SL_NET_DEFAULT_DHCP_CONFIG_INIT \
    { .min_discover_retry_interval = 3, .max_discover_retry_interval = 30, \
      .min_request_retry_interval = 3, .max_request_retry_interval = 30, \
      .max_discover_retries = 5, .max_request_retries = 5 }

static void sl_net_ensure_dhcp_config(sl_net_ip_configuration_t *ip)
{
    if (ip == NULL || ip->mode != SL_IP_MANAGEMENT_DHCP) {
        return;
    }
    if (ip->dhcp_config.max_discover_retries != 0 || ip->dhcp_config.max_request_retries != 0) {
        return;
    }
    ip->dhcp_config = (sl_net_dhcp_configuration_t)SL_NET_DEFAULT_DHCP_CONFIG_INIT;
}

/// @brief Default wifi client profile
static sl_net_wifi_client_profile_t wifi_client_profile = {
    .config = {
        .ssid.value = NETIF_WIFI_STA_DEFAULT_SSID,
        .ssid.length = sizeof(NETIF_WIFI_STA_DEFAULT_SSID) - 1,
        .channel.channel = SL_WIFI_AUTO_CHANNEL,
        .channel.band = SL_WIFI_AUTO_BAND,
        .channel.bandwidth = SL_WIFI_AUTO_BANDWIDTH,
        .bssid = {{0}},
        .bss_type = SL_WIFI_BSS_TYPE_INFRASTRUCTURE,
        .security = ((sizeof(NETIF_WIFI_STA_DEFAULT_PW) > 8) ? SL_WIFI_WPA_WPA2_MIXED : SL_WIFI_OPEN),
        .encryption = WIRELESS_DEFAULT_ENCRYPTION,
        .client_options = 0,
        .credential_id = ((sizeof(NETIF_WIFI_STA_DEFAULT_PW) > 8) ? SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID : SL_WIFI_NO_CREDENTIAL_ID),
    },
    .ip = {
        .mode = SL_IP_MANAGEMENT_DHCP,
        .type = SL_IPV4,
        .host_name = NULL,
        .ip = {
            .v4.ip_address.value = NETIF_WIFI_STA_DEFAULT_IP,
            .v4.gateway.value = NETIF_WIFI_STA_DEFAULT_GW,
            .v4.netmask.value = NETIF_WIFI_STA_DEFAULT_MASK
        },
        .dhcp_config = SL_NET_DEFAULT_DHCP_CONFIG_INIT,
    }
};
/// @brief Default wifi AP profile
static sl_net_wifi_ap_profile_t wifi_ap_profile = {
    .config = {
        .ssid.value = NETIF_WIFI_AP_DEFAULT_SSID,
        .ssid.length = sizeof(NETIF_WIFI_AP_DEFAULT_SSID) - 1,
        .channel.channel = SL_WIFI_AUTO_CHANNEL,
        .channel.band = SL_WIFI_AUTO_BAND,
        .channel.bandwidth = SL_WIFI_AUTO_BANDWIDTH,
        .security = ((sizeof(NETIF_WIFI_AP_DEFAULT_PW) > 8) ? SL_WIFI_WPA_WPA2_MIXED : SL_WIFI_OPEN),
        .encryption = WIRELESS_DEFAULT_ENCRYPTION,
        .options = 0,
        .credential_id = ((sizeof(NETIF_WIFI_AP_DEFAULT_PW) > 8) ? SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID : SL_WIFI_NO_CREDENTIAL_ID),
        .keepalive_type = SL_SI91X_AP_NULL_BASED_KEEP_ALIVE,
        .beacon_interval = 100,
        .client_idle_timeout = 0xFF,
        .dtim_beacon_count = 3,
        .maximum_clients = NETIF_WIFI_AP_DEFAULT_CLIENT_NUM,
        .beacon_stop = 0,
        .is_11n_enabled = 0,
    },
    .ip = {
#if IS_TCP_IP_DUAL_MODE
        .mode = SL_IP_MANAGEMENT_STATIC_IP,
#else
        .mode = SL_IP_MANAGEMENT_LINK_LOCAL,
#endif
        .type = SL_IPV4,
        .host_name = NULL,
        .ip = {
            .v4.ip_address.value = NETIF_WIFI_AP_DEFAULT_IP,
            .v4.gateway.value = NETIF_WIFI_AP_DEFAULT_GW,
            .v4.netmask.value = NETIF_WIFI_AP_DEFAULT_MASK
        },
    }
};
/// @brief Default wifi client credential
static sl_net_wifi_psk_credential_entry_t wifi_client_credential = {    .type        = SL_NET_WIFI_PSK,
                                                                        .data_length = sizeof(NETIF_WIFI_STA_DEFAULT_PW) - 1,
                                                                        .data        = NETIF_WIFI_STA_DEFAULT_PW };
/// @brief Default wifi AP credential
static sl_net_wifi_psk_credential_entry_t wifi_ap_credential = {    .type        = SL_NET_WIFI_PSK,
                                                                    .data_length = sizeof(NETIF_WIFI_AP_DEFAULT_PW) - 1,
                                                                    .data        = NETIF_WIFI_AP_DEFAULT_PW };
/// @brief Default client network interface
struct netif client_netif = {
    .name = {NETIF_NAME_WIFI_STA[0], NETIF_NAME_WIFI_STA[1]},
};
/// @brief Default AP network interface
struct netif ap_netif = {
    .name = {NETIF_NAME_WIFI_AP[0], NETIF_NAME_WIFI_AP[1]},
};

#define SL_NET_EVENT_FIRMWARE_ERROR         (1 << 24)
#define SL_NET_EVENT_STA_DISCONNECTED       (1 << 23)
#define SL_NET_EVENT_STA_RECONNECTED        (1 << 22)
#define SL_NET_EVENT_CMD(cmd)               (1 << cmd)
#define SL_NET_EVENT_ALL                    (0x7FFFFFFF)
static osMutexId_t sl_net_mutex = NULL;
static osEventFlagsId_t sl_net_events = NULL;
static osThreadId_t sl_net_thread_ID = NULL;
static const osThreadAttr_t attr = {
    .name       = "sl_net_thread",
    .priority   = osPriorityRealtime5,
    .stack_mem  = 0,
    .stack_size = 4096,
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};
static const sl_wifi_advanced_client_configuration_t default_client_configuration = {
    .max_retry_attempts     = 3,
    .scan_interval           = 3,
    .beacon_missed_count     = 40,
    .first_time_retry_enable = 1,
};
static const sl_wifi_scan_configuration_t ap_cnt_scan_cfg = { 
    .type = SL_WIFI_SCAN_TYPE_ADV_SCAN,
    .flags = 0,
    .periodic_scan_interval = 20,
    .channel_bitmap_2g4     = 0xFFFF,
    .channel_bitmap_5g      = { 
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF 
    }, 
    .lp_mode = 0
};
// #define DEFAULT_SCAN_TYPE SL_WIFI_SCAN_TYPE_ACTIVE
#define DEFAULT_SCAN_TYPE SL_WIFI_SCAN_TYPE_EXTENDED
#if DEFAULT_SCAN_TYPE == SL_WIFI_SCAN_TYPE_EXTENDED
#define DEFAULT_SCAN_RESULT_MAX 64
static sl_wifi_scan_type_t now_scan_type = SL_WIFI_SCAN_TYPE_EXTENDED;
static sl_wifi_extended_scan_result_t default_scan_results[DEFAULT_SCAN_RESULT_MAX] = {0};
static sl_wifi_extended_scan_result_parameters_t default_scan_result_parameters = { 
    .scan_results = default_scan_results,
    .array_length = DEFAULT_SCAN_RESULT_MAX,
    .result_count = NULL,
    .channel_filter = NULL,
    .security_mode_filter = NULL,
    .rssi_filter = NULL,
    .network_type_filter = NULL
};
#endif
static const sl_wifi_scan_configuration_t default_wifi_scan_cfg = { 
    .type  = DEFAULT_SCAN_TYPE,
    .flags = 0,
    .periodic_scan_interval = 0,
    .channel_bitmap_2g4     = 0xFFFF,
    .channel_bitmap_5g      = { 
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF 
    }, 
    .lp_mode = 0
};
static const sl_wifi_advanced_scan_configuration_t advanced_scan_configuration = {
    .active_channel_time = SL_WIFI_DEFAULT_ACTIVE_CHANNEL_SCAN_TIME,
    .passive_channel_time = SL_WIFI_DEFAULT_PASSIVE_CHANNEL_SCAN_TIME,
    .trigger_level = -40,
    .trigger_level_change = 5,
    .enable_multi_probe = 1,
    .enable_instant_scan = 0
};
static uint8_t sl_net_is_recovering = 0;
static sl_net_wakeup_mode_t remote_wakeup_mode = WAKEUP_MODE_NORMAL;
static int global_scan_result_count = 0;
static osSemaphoreId_t wifi_scan_sem = NULL;
static wireless_scan_result_t wifi_storage_scan_result = {0};
static uint8_t sl_net_async_scan_restore_sta = 0;
static uint8_t sl_net_async_scan_restore_ap = 0;
#define SL_NET_SCAN_DISCONNECT_SETTLE_MS  300
static uint8_t sl_net_get_channel_from_scan_result(const char *ssid, size_t length);

static sl_status_t sl_net_client_scan_callback_handler(sl_wifi_event_t event,
                                                       sl_wifi_scan_result_t *result,
                                                       uint32_t result_length,
                                                       void *arg);

static void sl_net_scan_async_restore_links(void)
{
    uint8_t try_times = 0;
    int tmp_ret = 0;

    if (!sl_net_async_scan_restore_sta && !sl_net_async_scan_restore_ap) {
        return;
    }
    if (sl_net_async_scan_restore_ap) {
        try_times = 0;
        do {
            tmp_ret = sl_net_ap_netif_up();
        } while (tmp_ret != SL_STATUS_OK && try_times++ < 3);
    }
    if (sl_net_async_scan_restore_sta) {
        try_times = 0;
        do {
            tmp_ret = sl_net_client_netif_up();
        } while (tmp_ret != SL_STATUS_OK && try_times++ < 3);
    }
    sl_net_async_scan_restore_sta = 0;
    sl_net_async_scan_restore_ap = 0;
}

static sl_status_t sl_net_prepare_extended_scan(void *scan_arg)
{
    sl_status_t status = sl_wifi_set_advanced_scan_configuration(&advanced_scan_configuration);
    if (status != SL_STATUS_OK) {
        return status;
    }
    sl_wifi_set_scan_callback(sl_net_client_scan_callback_handler, scan_arg);
    if (wifi_scan_sem != NULL) {
        (void)osSemaphoreAcquire(wifi_scan_sem, 0);
    }
    now_scan_type = SL_WIFI_SCAN_TYPE_EXTENDED;
    return sl_wifi_start_scan(SL_WIFI_CLIENT_INTERFACE, NULL, &default_wifi_scan_cfg);
}

static int sl_net_acquire_scan_results(uint32_t timeout_ms)
{
    if (wifi_scan_sem == NULL) {
        return SL_STATUS_FAIL;
    }
    if (osSemaphoreAcquire(wifi_scan_sem, timeout_ms) != osOK) {
        return SL_STATUS_TIMEOUT;
    }
    return (wifi_storage_scan_result.scan_count > 0) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

static void sl_net_pause_links_for_scan(bool sta_link_up, bool ap_link_up)
{
    sl_wifi_channel_t channel = { 0 };

    if (ap_link_up) {
        if (sl_wifi_get_channel(SL_WIFI_AP_2_4GHZ_INTERFACE, &channel) == SL_STATUS_OK) {
            wifi_ap_profile.config.channel.channel = channel.channel;
        }
        sl_net_ap_netif_down();
    }
    /* STA on one channel: full client netif down before multi-channel EXTENDED scan.
     * WC 4.x ADV_SCAN does not return results to host on SI917; use EXTENDED instead. */
    if (sta_link_up) {
        (void)sl_net_client_netif_down();
        osDelay(SL_NET_SCAN_DISCONNECT_SETTLE_MS);
    }
}

static void sl_net_restore_links_after_scan(bool sta_link_up, bool ap_link_up)
{
    uint8_t try_times = 0;
    int tmp_ret = 0;

    if (ap_link_up) {
        try_times = 0;
        do {
            tmp_ret = sl_net_ap_netif_up();
        } while (tmp_ret != SL_STATUS_OK && try_times++ < 3);
    }
    if (sta_link_up) {
        try_times = 0;
        do {
            tmp_ret = sl_net_client_netif_up();
        } while (tmp_ret != SL_STATUS_OK && try_times++ < 3);
    }
}

/// @brief Network interface low-level data input processing function
/// @param netif Network interface
/// @param b Data pointer
/// @param len Length
static void sl_net_low_level_input(struct netif *netif, uint8_t *b, uint16_t len)
{
    struct pbuf *p = NULL, *q = NULL;
    uint32_t bufferoffset = 0;
    err_t ret = ERR_OK;

    if (len <= 0) return;
    if (len < NETIF_LWIP_FRAME_ALIGNMENT) len = NETIF_LWIP_FRAME_ALIGNMENT;

    // Drop packets originated from the same interface and is not destined for the said interface
//    const uint8_t *src_mac = b + netif->hwaddr_len;
//    const uint8_t *dst_mac = b;

#if LWIP_IPV6
    if (!(ip6_addr_ispreferred(netif_ip6_addr_state(netif, 0)))
        && (memcmp(netif->hwaddr, src_mac, netif->hwaddr_len) == 0)
        && (memcmp(netif->hwaddr, dst_mac, netif->hwaddr_len) != 0)) {
            LOG_DRV_DEBUG(NETIF_NAME_STR_FMT ": DROP, [" NETIF_MAC_STR_FMT "]<-[" NETIF_MAC_STR_FMT "] type=%02x%02x\r\n", 
                            NETIF_NAME_PARAMETER(netif), NETIF_MAC_PARAMETER(dst_mac), NETIF_MAC_PARAMETER(src_mac), b[12], b[13]);
        return;
    }
#endif

    /* We allocate a pbuf chain of pbufs from the Lwip buffer pool
    * and copy the data to the pbuf chain
    */
    if ((p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL)) != ((struct pbuf *)0)) {
        for (q = p, bufferoffset = 0; q != NULL; q = q->next) {
            memcpy((uint8_t *)q->payload, (uint8_t *)b + bufferoffset, q->len);
            bufferoffset += q->len;
        }
        ret = netif->input(p, netif);
        if (ret != ERR_OK) {
            pbuf_free(p);
            osDelay(5);
            // LOG_DRV_ERROR(NETIF_NAME_STR_FMT ":Input failed(ret = %d)!\r\n", NETIF_NAME_PARAMETER(netif), ret);
        }
    } else {
        LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Failed to allocate pbuf!\r\n", NETIF_NAME_PARAMETER(netif));
    }
}
/// @brief Network interface low-level data output function
/// @param netif Network interface
/// @param p Data buffer
/// @return Error code
static err_t sl_net_low_level_output(struct netif *netif, struct pbuf *p)
{
    struct pbuf *q = NULL;
    uint8_t *out_buf = NULL, *out_ptr = NULL;
    sl_status_t status = SL_STATUS_OK;

    // printf("LWIP TX len : %d / %d\r\n", p->len, p->tot_len);
    if (p->len != p->tot_len) {
        out_buf = hal_mem_alloc_fast(p->tot_len);
        if (out_buf != NULL) {
            out_ptr = out_buf;
            for (q = p; q != NULL; q = q->next) {
                memcpy(out_ptr, q->payload, q->len);
                out_ptr += q->len;
            }
            if (netif == &client_netif) status = sl_wifi_send_raw_data_frame(SL_WIFI_CLIENT_INTERFACE, out_buf, p->tot_len);
            else status = sl_wifi_send_raw_data_frame(SL_WIFI_AP_INTERFACE, out_buf, p->tot_len);
            hal_mem_free(out_buf);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Failed to send data frame: 0x%0lX.\r\n", NETIF_NAME_PARAMETER(netif), status);
                return ERR_IF;
            }
            return ERR_OK;
        }
    }
    for (q = p; q != NULL; q = q->next) {
        // printf("LWIP TX len : %d\r\n", q->len);
        if (netif == &client_netif) status = sl_wifi_send_raw_data_frame(SL_WIFI_CLIENT_INTERFACE, (uint8_t *)q->payload, q->len);
        else status = sl_wifi_send_raw_data_frame(SL_WIFI_AP_INTERFACE, (uint8_t *)q->payload, q->len);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Failed to send data frame: 0x%0lX.\r\n", NETIF_NAME_PARAMETER(netif), status);
            return ERR_IF;
        }
    }
    return ERR_OK;
}
/// @brief Network interface low-level initialization function
/// @param netif Network interface to initialize
static err_t sl_net_ethernetif_init(struct netif *netif)
{
    sl_status_t status = SL_STATUS_OK;
    sl_mac_address_t mac_addr = {0};
    sl_wifi_interface_t interface = SL_WIFI_CLIENT_INTERFACE;

    if (netif == NULL) return ERR_ARG;
    if (netif == &ap_netif) interface = SL_WIFI_AP_INTERFACE;

    // set netif MAC hardware address length
    netif->hwaddr_len = ETH_HWADDR_LEN;
    // Request MAC address
    status = sl_wifi_get_mac_address(interface, &mac_addr);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Get MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER(netif), status);
        return ERR_IF;
    }
    // Config diy mac
    if (NETIF_MAC_IS_UNICAST(netif->hwaddr) && memcmp(netif->hwaddr, mac_addr.octet, netif->hwaddr_len)) {
        memcpy(mac_addr.octet, netif->hwaddr, netif->hwaddr_len);
        status = sl_wifi_set_mac_address(interface, &mac_addr);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Set MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER(netif), status);
            return ERR_IF;
        }
    } else memcpy(netif->hwaddr, mac_addr.octet, netif->hwaddr_len);
    LOG_DRV_DEBUG(NETIF_NAME_STR_FMT ": MAC Address: " NETIF_MAC_STR_FMT "\r\n", NETIF_NAME_PARAMETER(netif), NETIF_MAC_PARAMETER(netif->hwaddr));
    
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    if (interface == SL_WIFI_CLIENT_INTERFACE) netif->hostname = wifi_client_profile.ip.host_name;
    else if (interface == SL_WIFI_AP_INTERFACE) netif->hostname = wifi_ap_profile.ip.host_name;
    else netif->hostname = "LWIP_DEV";
#endif /* LWIP_NETIF_HOSTNAME */

    //! Assign handler/function for the interface
#if LWIP_IPV4 && LWIP_ARP
    netif->output = etharp_output;
#endif /* #if LWIP_IPV4 && LWIP_ARP */
#if LWIP_IPV6 && LWIP_ETHERNET
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 && LWIP_ETHERNET */
    netif->linkoutput = sl_net_low_level_output;

    // set netif maximum transfer unit
    netif->mtu = NETIF_MAX_TRANSFER_UNIT;

    // Accept broadcast address and ARP traffic
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

#if LWIP_IPV6_MLD
    netif->flags |= NETIF_FLAG_MLD6;
#endif /* LWIP_IPV6_MLD */

    return ERR_OK;
}
/// @brief MACRAW data processing function provided for SL SDK calls
sl_status_t sl_si91x_host_process_data_frame(sl_wifi_interface_t interface, sl_wifi_buffer_t *buffer)
{
    void *packet;
    const uint8_t *dst_mac;
//    struct netif *ifp;
    sl_si91x_packet_t *rsi_pkt;

    packet = sl_si91x_host_get_buffer_data(buffer, 0, NULL);
    if (packet == NULL) return SL_STATUS_FAIL;
    rsi_pkt = (sl_si91x_packet_t *)packet;

    dst_mac = rsi_pkt->data;
    // printf("LWIP RX len : %d\r\n", rsi_pkt->length);
    // printf(NETIF_MAC_STR_FMT" -> "NETIF_MAC_STR_FMT"\r\n", NETIF_MAC_PARAMETER((dst_mac + 6)), NETIF_MAC_PARAMETER(dst_mac));
    if (NETIF_MAC_IS_MULTICAST(dst_mac) || NETIF_MAC_IS_BROADCAST(dst_mac)) {
        if (netif_is_link_up(&client_netif)) sl_net_low_level_input(&client_netif, rsi_pkt->data, rsi_pkt->length);
        if (netif_is_link_up(&ap_netif)) sl_net_low_level_input(&ap_netif, rsi_pkt->data, rsi_pkt->length);
    } else {
        if (memcmp(dst_mac, client_netif.hwaddr, client_netif.hwaddr_len) == 0 && netif_is_link_up(&client_netif)) sl_net_low_level_input(&client_netif, rsi_pkt->data, rsi_pkt->length);
        else if (memcmp(dst_mac, ap_netif.hwaddr, ap_netif.hwaddr_len) == 0 && netif_is_link_up(&ap_netif)) sl_net_low_level_input(&ap_netif, rsi_pkt->data, rsi_pkt->length);
    }
    
    return SL_STATUS_OK;
}

/****************************************************************************************/
/********************************** WIFI CLIENT NETIF ***********************************/
/****************************************************************************************/
/// @brief Client initialization function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_client_init(sl_net_interface_t interface,
                                    const void *configuration,
                                    void *context,
                                    sl_net_event_handler_t event_handler)
{
    sl_status_t status;
    UNUSED_PARAMETER(interface);
    UNUSED_PARAMETER(event_handler);
    UNUSED_PARAMETER(context);
    if (configuration != NULL) {
        const sl_wifi_device_configuration_t *cfg = (const sl_wifi_device_configuration_t *)configuration;
        dual_mode_enabled   = (cfg->boot_config.ext_tcp_ip_feature_bit_map & SL_SI91X_EXT_TCP_IP_DUAL_MODE_ENABLE) != 0;
        bypass_mode_enabled = (cfg->boot_config.tcp_ip_feature_bit_map & SL_SI91X_TCP_IP_FEAT_BYPASS) != 0;
    }
    status = sl_wifi_init(configuration, NULL, sl_wifi_default_event_handler);
    if (status == SL_STATUS_ALREADY_INITIALIZED
        && configuration != NULL
        && ((const sl_wifi_device_configuration_t *)configuration)->boot_config.oper_mode == SL_SI91X_CONCURRENT_MODE) {
        return SL_STATUS_OK;
    }
    return status;
}
/// @brief Client deinitialization function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_client_deinit(sl_net_interface_t interface)
{
    UNUSED_PARAMETER(interface);
    return sl_wifi_deinit();
}
/// @brief Activate client link
static int sl_net_set_client_link_up(sl_net_wifi_client_profile_t *profile)
{
    err_t err = ERR_OK;
#if !IS_TCP_IP_DUAL_MODE
    int event_flag = 0;
    uint32_t timeout_ms = 0;
#endif

    err = netifapi_netif_set_up(&client_netif);
    if (err != ERR_OK) return err;
    err = netifapi_netif_set_link_up(&client_netif);
    if (err != ERR_OK) {
        netifapi_netif_set_down(&client_netif);
        return err;
    }

#if IS_TCP_IP_DUAL_MODE
    ip_addr_t ipaddr  = { 0 };
    ip_addr_t gateway = { 0 };
    ip_addr_t netmask = { 0 };
    uint8_t *address = &(profile->ip.ip.v4.ip_address.bytes[0]);
    IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
    address = &(profile->ip.ip.v4.gateway.bytes[0]);
    IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
    address = &(profile->ip.ip.v4.netmask.bytes[0]);
    IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);
    err = netifapi_netif_set_addr(&client_netif,
                                    &ipaddr,
                                    &netmask,
                                    &gateway);
    LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " ip: %s\r\n", NETIF_NAME_PARAMETER(&client_netif), ip4addr_ntoa((const ip4_addr_t *)&client_netif.ip_addr));
#else
    if (SL_IP_MANAGEMENT_STATIC_IP == profile->ip.mode) {
#if LWIP_IPV4 && LWIP_IPV6
        ip_addr_t ipaddr  = { 0 };
        ip_addr_t gateway = { 0 };
        ip_addr_t netmask = { 0 };

        if ((profile->ip.type & SL_IPV4) == SL_IPV4) {
            uint8_t *address = &(profile->ip.ip.v4.ip_address.bytes[0]);
            IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
            address = &(profile->ip.ip.v4.gateway.bytes[0]);
            IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
            address = &(profile->ip.ip.v4.netmask.bytes[0]);
            IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

            err = netifapi_netif_set_addr(&client_netif,
                                    &ipaddr,
                                    &netmask,
                                    &gateway);
        }
        if ((profile->ip.type & SL_IPV6) == SL_IPV6) {
            uint32_t *address = &(profile->ip.ip.v6.link_local_address.value[0]);
            IP6_ADDR(&ipaddr.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(profile->ip.ip.v6.global_address.value[0]);
            IP6_ADDR(&gateway.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(profile->ip.ip.v6.gateway.value[0]);
            IP6_ADDR(&netmask.u_addr.ip6, address[0], address[1], address[2], address[3]);

            netif_ip6_addr_set(&client_netif, 0, &ipaddr.u_addr.ip6);
            netif_ip6_addr_set(&client_netif, 1, &gateway.u_addr.ip6);
            netif_ip6_addr_set(&client_netif, 2, &netmask.u_addr.ip6);
            
            netif_ip6_addr_set_state(&client_netif, 0, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&client_netif, 1, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&client_netif, 2, IP6_ADDR_PREFERRED);
        }
#elif LWIP_IPV4
        ip4_addr_t ipaddr  = { 0 };
        ip4_addr_t gateway = { 0 };
        ip4_addr_t netmask = { 0 };
        uint8_t *address   = &(profile->ip.ip.v4.ip_address.bytes[0]);

        IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.gateway.bytes[0]);
        IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v4.netmask.bytes[0]);
        IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

        err = netifapi_netif_set_addr(&client_netif, &ipaddr, &netmask, &gateway);
        LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " static ip: %s\r\n", NETIF_NAME_PARAMETER(&client_netif), ip4addr_ntoa((const ip4_addr_t *)&client_netif.ip_addr));
#elif LWIP_IPV6
        ip6_addr_t link_local_address = { 0 };
        ip6_addr_t global_address     = { 0 };
        ip6_addr_t gateway            = { 0 };
        uint32_t *address             = &(profile->ip.ip.v6.link_local_address.value[0]);

        IP6_ADDR(&link_local_address, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v6.global_address.value[0]);
        IP6_ADDR(&global_address, address[0], address[1], address[2], address[3]);
        address = &(profile->ip.ip.v6.gateway.value[0]);
        IP6_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        netif_ip6_addr_set(&client_netif, 0, &link_local_address);
        netif_ip6_addr_set(&client_netif, 1, &global_address);
        netif_ip6_addr_set(&client_netif, 2, &gateway);

        netif_ip6_addr_set_state(&client_netif, 0, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&client_netif, 1, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&client_netif, 2, IP6_ADDR_PREFERRED);
#endif /* LWIP_IPV6 */
    } else if (SL_IP_MANAGEMENT_DHCP == profile->ip.mode) {
#if LWIP_IPV4 && LWIP_DHCP
        ip_addr_set_zero_ip4(&(client_netif.ip_addr));
        ip_addr_set_zero_ip4(&(client_netif.netmask));
        ip_addr_set_zero_ip4(&(client_netif.gw));

        osEventFlagsClear(sl_net_events, SL_NET_EVENT_FIRMWARE_ERROR);
        err = netifapi_dhcp_start(&client_netif);
        if (err == ERR_OK) {
            timeout_ms = 0;
            do {
                if (dhcp_supplied_address(&client_netif)) {
                    LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " dhcp ip: %s\r\n", NETIF_NAME_PARAMETER(&client_netif), ip4addr_ntoa((const ip4_addr_t *)&client_netif.ip_addr));
                    break;
                }
                if (timeout_ms < NETIF_WIFI_STA_DEFAULT_DHCP_TIMEOUT) {
                    osDelay(100);
                    timeout_ms += 100;
                } else err = ERR_TIMEOUT;
                event_flag = (int)osEventFlagsWait(sl_net_events, SL_NET_EVENT_FIRMWARE_ERROR, osFlagsWaitAny | osFlagsNoClear, 0);
                if (event_flag > 0 && event_flag & SL_NET_EVENT_FIRMWARE_ERROR) err = ERR_IF;
            } while (err == ERR_OK);
        }
#endif /* LWIP_IPV4 && LWIP_DHCP */
        /*
        * Enable DHCPv6 with IPV6
        */

        // Stateless DHCPv6
#if LWIP_IPV6 && LWIP_IPV6_AUTOCONFIG
        // Automatically configure global addresses from Router Advertisements
        netif_set_ip6_autoconfig_enabled(&client_netif, 1);
        // Create and set the link-local address
        netif_create_ip6_linklocal_address(&client_netif, MAC_48_BIT_SET);
        LOG_DRV_DEBUG("IPv6 Address %s\r\n", ip6addr_ntoa(netif_ip6_addr(&client_netif, 0)));
        
        timeout_ms = 0;
        do {
            if (ip6_addr_ispreferred(netif_ip6_addr_state(&client_netif, 0))) {
                LOG_DRV_DEBUG("IPv6 Address %s\r\n", ip6addr_ntoa(netif_ip6_addr(&client_netif, 0)));
                break;
            }
            if (timeout_ms < NETIF_WIFI_STA_DEFAULT_DHCP_TIMEOUT) {
                osDelay(100);
                timeout_ms += 100;
            } else err = ERR_TIMEOUT;
        } while (err == ERR_OK)
#endif /* LWIP_IPV6 && LWIP_IPV6_AUTOCONFIG */
    }
#endif 

    if (err != ERR_OK) {
#if LWIP_IPV4 && LWIP_DHCP && !IS_TCP_IP_DUAL_MODE
        (void)netifapi_dhcp_stop(&client_netif);
#endif /* LWIP_IPV4 && LWIP_DHCP && !IS_TCP_IP_DUAL_MODE */
        netifapi_netif_set_link_down(&client_netif);
        netifapi_netif_set_down(&client_netif);
        return err;
    }
    return err;
}
/// @brief Client network interface activation provided for SL SDK calls
sl_status_t sl_net_wifi_client_up(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{
    // uint8_t cnt_try_times = 0;
    err_t err = ERR_OK;
    sl_status_t status = 0;

    // Load profile and connect here
    // sl_net_wifi_client_profile_t profile = { 0 };

    // status = sl_net_get_profile(SL_NET_WIFI_CLIENT_INTERFACE, profile_id, &profile);
    // if (status != SL_STATUS_OK) {
    //     LOG_DRV_DEBUG("Failed to get client profile: 0x%0lX\r\n", status);
    //     return status;
    // }

    status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, profile_id, &wifi_client_profile);
    if (status != SL_STATUS_OK) {
        // sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
        LOG_DRV_ERROR("Failed to set client profile: 0x%lX\r\n", status);
        return status;
    }

    if (wifi_client_profile.config.security != SL_WIFI_OPEN) {
        status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
                                        wifi_client_credential.type,
                                        &wifi_client_credential.data,
                                        wifi_client_credential.data_length);
        if (status != SL_STATUS_OK) {
            // sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
            LOG_DRV_ERROR("Failed to set client credentials: 0x%lX\r\n", status);
            return status;
        }
    }
    
    status = sl_wifi_set_advanced_client_configuration(SL_WIFI_CLIENT_INTERFACE, &default_client_configuration);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to set advanced client configuration: 0x%0lX\r\n", status);
        return status;
    }

    /* Set join feature bitmap */
    uint8_t join_feature_bitmap = 0;
    sl_si91x_get_join_configuration(SL_WIFI_CLIENT_INTERFACE, &join_feature_bitmap);
    join_feature_bitmap |= SL_WIFI_JOIN_FEAT_QUICK_JOIN;
    sl_si91x_set_join_configuration(SL_WIFI_CLIENT_INTERFACE, join_feature_bitmap);

    // do {
        status = sl_wifi_connect(SL_WIFI_CLIENT_INTERFACE, &wifi_client_profile.config, 18000);
    // } while (status != SL_STATUS_OK && cnt_try_times++ < 3);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to connect to Wi-Fi: 0x%0lX\r\n", status);
        return status;
    }

#if IS_TCP_IP_DUAL_MODE
    if (interface == SL_NET_WIFI_CLIENT_2_INTERFACE) {
        status = sl_si91x_configure_ip_address(&wifi_client_profile.ip, SL_WIFI_CLIENT_VAP_ID_1);
    } else {
        status = sl_si91x_configure_ip_address(&wifi_client_profile.ip, SL_WIFI_CLIENT_VAP_ID);
    }
    if (status != SL_STATUS_OK) {
        sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
        LOG_DRV_ERROR("Failed to configure client ip: 0x%0lX\r\n", status);
        return status;
    }
#endif

    err = sl_net_set_client_link_up(&wifi_client_profile);
    if (err != ERR_OK) {
        sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
        LOG_DRV_ERROR("Failed to set client link up: %d\r\n", err);
        return err;
    }
#if LWIP_IPV4 && LWIP_IPV6
    if ((wifi_client_profile.ip.type & SL_IPV4) == SL_IPV4) {
        ip_addr_t *addr;
        // Set the IP address of v4 interface
        addr = &client_netif.ip_addr;
        memcpy(wifi_client_profile.ip.ip.v4.ip_address.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));

        addr = &client_netif.gw;
        memcpy(wifi_client_profile.ip.ip.v4.gateway.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));

        addr = &client_netif.netmask;
        memcpy(wifi_client_profile.ip.ip.v4.netmask.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));
    }

    if ((wifi_client_profile.ip.type & SL_IPV6) == SL_IPV6) {
        // Set the IP address of v6 interface
        // Loop through the first 4 elements of the IPv6 address arrays to convert and assign them to the profile structure
        for (int i = 0; i < 4; i++) {
            wifi_client_profile.ip.ip.v6.link_local_address.value[i] = ntohl(client_netif.ip6_addr[0].u_addr.ip6.addr[i]);
            wifi_client_profile.ip.ip.v6.global_address.value[i]     = ntohl(client_netif.ip6_addr[1].u_addr.ip6.addr[i]);
            wifi_client_profile.ip.ip.v6.gateway.value[i]            = ntohl(client_netif.ip6_addr[2].u_addr.ip6.addr[i]);
        }
    }
#else /* LWIP_IPV4 && LWIP_IPV6 */
#if LWIP_IPV4
    u32_t *addr;

    addr = &client_netif.ip_addr.addr;
    memcpy(wifi_client_profile.ip.ip.v4.ip_address.bytes, addr, sizeof(*addr));

    addr = &client_netif.gw.addr;
    memcpy(wifi_client_profile.ip.ip.v4.gateway.bytes, addr, sizeof(*addr));

    addr = &client_netif.netmask.addr;
    memcpy(wifi_client_profile.ip.ip.v4.netmask.bytes, addr, sizeof(*addr));
#elif LWIP_IPV6
    // Loop through the first 4 elements of the IPv6 address arrays to convert and assign them to the profile structure
    for (int i = 0; i < 4; i++) {
        wifi_client_profile.ip.ip.v6.link_local_address.value[i] = ntohl(client_netif.ip6_addr[0].addr[i]);
        wifi_client_profile.ip.ip.v6.global_address.value[i]     = ntohl(client_netif.ip6_addr[1].addr[i]);
        wifi_client_profile.ip.ip.v6.gateway.value[i]            = ntohl(client_netif.ip6_addr[2].addr[i]);
    }
#endif /* LWIP_IPV6 */
#endif /* LWIP_IPV4 && LWIP_IPV6 */
    // Set the client profile
    status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, profile_id, &wifi_client_profile);

    return status;
}
/// @brief Deactivate client link
static void sl_net_set_client_link_down(void)
{
#if LWIP_IPV4 && LWIP_DHCP
    (void)netifapi_dhcp_stop(&client_netif);
#endif /* LWIP_IPV4 && LWIP_DHCP */
    netifapi_netif_set_link_down(&client_netif);
    netifapi_netif_set_down(&client_netif);
}
/// @brief Client network interface deactivation provided for SL SDK calls
sl_status_t sl_net_wifi_client_down(sl_net_interface_t interface)
{
    sl_status_t status;

    UNUSED_PARAMETER(interface);
    sl_net_set_client_link_down();

    status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
    if (status == SL_STATUS_SI91X_UNASSOCIATED || status == SL_STATUS_WIFI_INTERFACE_NOT_UP) {
        return SL_STATUS_OK;
    }
    return status;
}
/// @brief Client network interface initialization function provided externally
/// @param None
/// @return Error code
int sl_net_client_netif_init(void)
{
    // uint8_t init_try_times = 0;
    struct netif *wl0 = NULL;
#if LWIP_IPV4
    ip_addr_t sta_ipaddr;
    ip_addr_t sta_netmask;
    ip_addr_t sta_gw;

    /* Initialize the Station information */
    ip_addr_set_zero_ip4(&sta_ipaddr);
    ip_addr_set_zero_ip4(&sta_netmask);
    ip_addr_set_zero_ip4(&sta_gw);
#endif /* LWIP_IPV4 */
    sl_status_t status;

    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 != NULL && wl0 == &client_netif) return SL_STATUS_INVALID_STATE;

    // do {
        status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &device_configuration, NULL, NULL);
    // } while (status != SL_STATUS_OK && init_try_times++ < 3);
    if (status != SL_STATUS_OK && status != SL_STATUS_ALREADY_INITIALIZED) {
        if (netif_get_by_index(ap_netif.num + 1) != &ap_netif) {
            sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
        }
        LOG_DRV_ERROR("Failed to init Wi-Fi Client interface: 0x%lX\r\n", status);
        return status;
    }

    // status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID, &wifi_client_profile);
    // if (status != SL_STATUS_OK) {
    //     sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
    //     LOG_DRV_ERROR("Failed to set client profile: 0x%lX\r\n", status);
    //     return status;
    // }

    // if (wifi_client_profile.config.security != SL_WIFI_OPEN) {
    //     status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
    //                                     wifi_client_credential.type,
    //                                     &wifi_client_credential.data,
    //                                     wifi_client_credential.data_length);
    //     if (status != SL_STATUS_OK) {
    //         sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
    //         LOG_DRV_ERROR("Failed to set client credentials: 0x%lX\r\n", status);
    //         return status;
    //     }
    // }

    /* Add station interfaces (must run on tcpip_thread via netifapi_*) */
    err_t nif_err = netifapi_netif_add(&client_netif,
#if LWIP_IPV4
            (const ip4_addr_t *)&sta_ipaddr,
            (const ip4_addr_t *)&sta_netmask,
            (const ip4_addr_t *)&sta_gw,
#endif /* LWIP_IPV4 */
            NULL,
            &sl_net_ethernetif_init,
            &tcpip_input);
    if (nif_err != ERR_OK) {
        if (netif_get_by_index(ap_netif.num + 1) != &ap_netif) {
            sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
        }
        return SL_STATUS_FAIL;
    }
    
    /* Registers the default network interface */
    // netif_set_default(&client_netif);
    return 0;
}
/// @brief Network interface activation function provided externally
/// @param None
/// @return Error code
int sl_net_client_netif_up(void)
{
    struct netif *wl0 = NULL;
    sl_status_t status = SL_STATUS_OK;
    
    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 == NULL || wl0 != &client_netif) return SL_STATUS_INVALID_STATE;

    if (netif_is_link_up(wl0)) return SL_STATUS_OK;
    status = sl_net_up(SL_NET_WIFI_CLIENT_INTERFACE, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID);
    if (status != SL_STATUS_OK) {
        // LOG_DRV_ERROR("Failed to bring Wi-Fi client interface up: 0x%lX\r\n", status);
        return status;
    }
    return status;
}
/// @brief Extended version of network interface activation function (automatically switch AP channel before connecting STA)
/// @param None
/// @return Error code
static int sl_net_client_netif_up_ex(void)
{
    int ret = SL_STATUS_FAIL, tmp_ret = SL_STATUS_FAIL;
    uint8_t channel = 0, try_times = 0;
    netif_state_t ap_state = NETIF_STATE_DEINIT;

    ap_state = sl_net_ap_netif_state();
    channel = sl_net_get_channel_from_scan_result((const char *)wifi_client_profile.config.ssid.value, wifi_client_profile.config.ssid.length);
    if ((channel != 0 && channel == wifi_ap_profile.config.channel.channel) || ap_state != NETIF_STATE_UP) {
        // Same channel or AP not started, connect directly
        wifi_client_profile.config.channel.channel = channel;
        ret = sl_net_client_netif_up();
    } else {
        // Different channel and AP is started, need to switch channel or close AP first
        sl_net_ap_netif_down();
        if (channel != 0) {
            wifi_ap_profile.config.channel.channel = channel;
            do {
                tmp_ret = sl_net_ap_netif_up();
            } while (tmp_ret != 0 && try_times++ < 3);
        }
        ret = sl_net_client_netif_up();
        if (channel == 0) {
            do {
                tmp_ret = sl_net_ap_netif_up();
            } while (tmp_ret != 0 && try_times++ < 3);
        }
    }

    return ret;
}
/// @brief Network interface deactivation function provided externally
/// @param None
/// @return Error code
int sl_net_client_netif_down(void)
{
    struct netif *wl0 = NULL;
    sl_status_t status = SL_STATUS_OK;

    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 == NULL || wl0 != &client_netif) return SL_STATUS_INVALID_STATE;

    if (!netif_is_link_up(wl0)) return SL_STATUS_OK;
    status = sl_net_down(SL_NET_WIFI_CLIENT_INTERFACE);
    if (status != SL_STATUS_OK) {
        // LOG_DRV_ERROR("Failed to bring Wi-Fi client interface down: 0x%lX\r\n", status);
        return status;
    }
    return status;
}
/// @brief Network interface destruction function provided externally (client)
/// @param None
void sl_net_client_netif_deinit(void)
{
    struct netif *wl0 = NULL;

    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 == NULL || wl0 != &client_netif) return;

    if (netif_is_link_up(wl0)) sl_net_client_netif_down();
    // If AP network interface is not initialized, call SDK destruction function
    if (netif_get_by_index(ap_netif.num + 1) != &ap_netif) {
        sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
    }
    (void)netifapi_netif_remove(wl0);
}
/// @brief Network interface configuration function provided externally (client)
/// @param netif_cfg Specific configuration
/// @return Error code
int sl_net_client_netif_config(netif_config_t *netif_cfg)
{
    sl_status_t status = SL_STATUS_OK;
    sl_mac_address_t mac_addr = {0};
    if (netif_cfg == NULL) return SL_STATUS_INVALID_PARAMETER;
    if (netif_is_link_up(&client_netif) || netif_is_up(&client_netif)) return SL_STATUS_INVALID_STATE;
    
    if (NETIF_MAC_IS_UNICAST(netif_cfg->diy_mac)) {
        if (netif_get_by_index(client_netif.num + 1) == &client_netif) {
            status = sl_wifi_get_mac_address(SL_WIFI_CLIENT_INTERFACE, &mac_addr);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Get MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER((&client_netif)), status);
                return ERR_IF;
            }
            if (memcmp(netif_cfg->diy_mac, mac_addr.octet, sizeof(mac_addr.octet))) {
                memcpy(mac_addr.octet, netif_cfg->diy_mac, sizeof(mac_addr.octet));
                status = sl_wifi_set_mac_address(SL_WIFI_CLIENT_INTERFACE, &mac_addr);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Set MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER((&client_netif)), status);
                    return ERR_IF;
                }
            }
        }
        memcpy(client_netif.hwaddr, netif_cfg->diy_mac, sizeof(netif_cfg->diy_mac));
        LOG_DRV_DEBUG(NETIF_NAME_STR_FMT ": MAC Address: " NETIF_MAC_STR_FMT "\r\n", NETIF_NAME_PARAMETER((&client_netif)), NETIF_MAC_PARAMETER(netif_cfg->diy_mac));
    }

    if (netif_cfg->host_name != NULL) {
        wifi_client_profile.ip.host_name = netif_cfg->host_name;
    #if LWIP_NETIF_HOSTNAME
        client_netif.hostname = wifi_client_profile.ip.host_name;
    #endif
    }

    if (NETIF_MAC_IS_UNICAST(netif_cfg->wireless_cfg.bssid)) {
        memcpy(wifi_client_profile.config.bssid.octet, netif_cfg->wireless_cfg.bssid, sizeof(netif_cfg->wireless_cfg.bssid));
    }
    wifi_client_profile.config.ssid.length = strlen(netif_cfg->wireless_cfg.ssid);
    memcpy(wifi_client_profile.config.ssid.value, netif_cfg->wireless_cfg.ssid, wifi_client_profile.config.ssid.length);
    wifi_client_credential.data_length = strlen(netif_cfg->wireless_cfg.pw);
    if (wifi_client_credential.data_length < 8) {
        wifi_client_profile.config.security = SL_WIFI_OPEN;
        wifi_client_profile.config.credential_id = SL_WIFI_NO_CREDENTIAL_ID;
    } else {
        memcpy(wifi_client_credential.data, netif_cfg->wireless_cfg.pw, wifi_client_credential.data_length);
        wifi_client_profile.config.security = (sl_wifi_security_t)netif_cfg->wireless_cfg.security;
        wifi_client_profile.config.credential_id = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID;
    }
    wifi_client_profile.config.encryption = (sl_wifi_encryption_t)netif_cfg->wireless_cfg.encryption;
    wifi_client_profile.config.channel.channel = netif_cfg->wireless_cfg.channel;
    
    if (netif_cfg->ip_mode == NETIF_IP_MODE_STATIC) wifi_client_profile.ip.mode = SL_IP_MANAGEMENT_STATIC_IP;
    else if (netif_cfg->ip_mode == NETIF_IP_MODE_DHCP) wifi_client_profile.ip.mode = SL_IP_MANAGEMENT_DHCP;
    else if (netif_cfg->ip_mode == NETIF_IP_MODE_DHCPS) wifi_client_profile.ip.mode = SL_IP_MANAGEMENT_LINK_LOCAL;
    sl_net_ensure_dhcp_config(&wifi_client_profile.ip);
    
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->ip_addr)) memcpy(wifi_client_profile.ip.ip.v4.ip_address.bytes, netif_cfg->ip_addr, sizeof(netif_cfg->ip_addr));
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->gw)) memcpy(wifi_client_profile.ip.ip.v4.gateway.bytes, netif_cfg->gw, sizeof(netif_cfg->gw));
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->netmask)) memcpy(wifi_client_profile.ip.ip.v4.netmask.bytes, netif_cfg->netmask, sizeof(netif_cfg->netmask));

    return SL_STATUS_OK;
}
/// @brief Network interface information retrieval function provided externally (client)
/// @param netif_info Specific information
/// @return Error code
int sl_net_client_netif_info(netif_info_t *netif_info)
{
    sl_wifi_firmware_version_t firmware_version = { 0 };
    sl_wifi_channel_t channel = {0};
    sl_status_t status = SL_STATUS_OK;
    struct netif *wl0 = NULL;
    if (netif_info == NULL) return SL_STATUS_INVALID_PARAMETER;

    netif_info->host_name = wifi_client_profile.ip.host_name;
    netif_info->if_name = NETIF_NAME_WIFI_STA;
    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 == NULL || wl0 != &client_netif) netif_info->state = NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&client_netif) || !netif_is_up(&client_netif)) netif_info->state = NETIF_STATE_DOWN;
    else netif_info->state = NETIF_STATE_UP;
    netif_info->type = NETIF_TYPE_WIRELESS;
    netif_info->rssi = 0;

    memset(netif_info->fw_version, 0, sizeof(netif_info->fw_version));
    if (netif_info->state != NETIF_STATE_DEINIT) {
        status = sl_wifi_get_firmware_version(&firmware_version);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("Failed to wifi firmware version: 0x%lx\r\n", status);
            return status;
        }
        snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "%x%x.%d.%d.%d.%d.%d.%d",
                firmware_version.chip_id,
                firmware_version.rom_id,
                firmware_version.major,
                firmware_version.minor,
                firmware_version.security_version,
                firmware_version.patch_num,
                firmware_version.customer_id,
                firmware_version.build_num);
        
        if (netif_info->state == NETIF_STATE_UP) {
            status = sl_wifi_get_channel(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &channel);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("Failed to get client channel: 0x%lx\r\n", status);
                return status;
            }
            wifi_client_profile.config.channel.channel = channel.channel;
            status = sl_wifi_get_signal_strength(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, (int32_t *)&netif_info->rssi);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("Failed to get client rssi: 0x%lx\r\n", status);
                return status;
            }
        }
    }
    memcpy(netif_info->if_mac, client_netif.hwaddr, sizeof(netif_info->if_mac));

    if (wifi_client_profile.ip.mode == SL_IP_MANAGEMENT_STATIC_IP) netif_info->ip_mode = NETIF_IP_MODE_STATIC;
    else if (wifi_client_profile.ip.mode == SL_IP_MANAGEMENT_DHCP) netif_info->ip_mode = NETIF_IP_MODE_DHCP;
    else if (wifi_client_profile.ip.mode == SL_IP_MANAGEMENT_LINK_LOCAL) netif_info->ip_mode = NETIF_IP_MODE_DHCPS;
    
    memcpy(netif_info->ip_addr, &client_netif.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->gw, &client_netif.gw, sizeof(netif_info->gw));
    memcpy(netif_info->netmask, &client_netif.netmask, sizeof(netif_info->netmask));

    memcpy(netif_info->wireless_cfg.bssid, wifi_client_profile.config.bssid.octet, sizeof(netif_info->wireless_cfg.bssid));
    memset(netif_info->wireless_cfg.ssid, 0x00, sizeof(netif_info->wireless_cfg.ssid));
    memcpy(netif_info->wireless_cfg.ssid, wifi_client_profile.config.ssid.value, wifi_client_profile.config.ssid.length);
    memset(netif_info->wireless_cfg.pw, 0x00, sizeof(netif_info->wireless_cfg.pw));
    if (wifi_client_credential.data_length >= 8) memcpy(netif_info->wireless_cfg.pw, wifi_client_credential.data, wifi_client_credential.data_length);
    netif_info->wireless_cfg.security = (wireless_security_t)wifi_client_profile.config.security;
    netif_info->wireless_cfg.encryption = (wireless_encryption_t)wifi_client_profile.config.encryption;
    netif_info->wireless_cfg.channel = wifi_client_profile.config.channel.channel;

    return status;
}

netif_state_t sl_net_client_netif_state(void)
{
    struct netif *wl0 = NULL;

    wl0 = netif_get_by_index(client_netif.num + 1);
    if (wl0 == NULL || wl0 != &client_netif) return NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&client_netif) || !netif_is_up(&client_netif)) return NETIF_STATE_DOWN;
    else return NETIF_STATE_UP;
}

struct netif *sl_net_client_netif_ptr(void)
{
    return &client_netif;
}

static uint8_t sl_net_get_channel_from_scan_result(const char *ssid, size_t length)
{
    uint8_t channel = 0;
    int i = 0;
    for (i = 0; i < wifi_storage_scan_result.scan_count; i++) {
        if (strncmp(wifi_storage_scan_result.scan_info[i].ssid, ssid, length) == 0) {
            channel = wifi_storage_scan_result.scan_info[i].channel;
            break;
        }
    }
    return channel;
}

static sl_status_t sl_net_client_scan_callback_handler(sl_wifi_event_t event, sl_wifi_scan_result_t *result, uint32_t result_length, void *arg)
{
    int i = 0;
#if DEFAULT_SCAN_TYPE == SL_WIFI_SCAN_TYPE_EXTENDED
    uint16_t scan_result_num = 0;
    sl_status_t status = SL_STATUS_OK;
#endif
    wireless_scan_result_t scan_result = {0};
    wireless_scan_callback_t callback = NULL;

    if (arg != (void *)&wifi_storage_scan_result) callback = (wireless_scan_callback_t)arg;
    if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
        global_scan_result_count = -1;
        LOG_DRV_ERROR("scan failed: 0x%X, 0x%X, %p\r\n", (int)event, (int)(*(sl_status_t *)result), callback);
        if (callback != NULL) callback(-1, NULL);
        else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
        sl_net_scan_async_restore_links();
        return SL_STATUS_FAIL;
    }

#if DEFAULT_SCAN_TYPE == SL_WIFI_SCAN_TYPE_EXTENDED
    /* WiseConnect 4.x: extended scan completion has no inline payload (result == NULL)
     * but result_length may be set to count * sizeof(sl_wifi_extended_scan_result_t).
     * Old SDK always passed result_length == 0 for this case. */
    if (now_scan_type == SL_WIFI_SCAN_TYPE_EXTENDED) {
        default_scan_result_parameters.result_count = &scan_result_num;
        status = sl_wifi_get_stored_scan_results(SL_WIFI_CLIENT_INTERFACE, &default_scan_result_parameters);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("get scan result failed: 0x%X\r\n", status);
            if (callback != NULL) callback(-1, NULL);
            else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
            sl_net_scan_async_restore_links();
            return SL_STATUS_FAIL;
        }
        global_scan_result_count = scan_result_num;
        if (scan_result_num > 0) {
            LOG_DRV_INFO("extended scan result count: %d\r\n", global_scan_result_count);
            scan_result.scan_count = scan_result_num;
            scan_result.scan_info = hal_mem_alloc_large(scan_result_num * sizeof(wireless_scan_info_t));
            if (scan_result.scan_info == NULL) {
                if (callback != NULL) callback(-2, NULL);
                else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
                sl_net_scan_async_restore_links();
                return SL_STATUS_FAIL;
            }
            for (i = 0; i < scan_result_num; i++) {
                scan_result.scan_info[i].rssi = -default_scan_results[i].rssi;
                memcpy(scan_result.scan_info[i].ssid, default_scan_results[i].ssid, sizeof(scan_result.scan_info[i].ssid));
                memcpy(scan_result.scan_info[i].bssid, default_scan_results[i].bssid, sizeof(scan_result.scan_info[i].bssid));
                scan_result.scan_info[i].channel = default_scan_results[i].rf_channel;
                scan_result.scan_info[i].security = default_scan_results[i].security_mode;
            }
            if (callback != NULL) callback(0, &scan_result);
            else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) {
                if (wifi_storage_scan_result.scan_info != NULL) {
                    wifi_storage_scan_result.scan_count = scan_result.scan_count;
                    memcpy(wifi_storage_scan_result.scan_info, scan_result.scan_info, scan_result.scan_count * sizeof(wireless_scan_info_t));
                }
                osSemaphoreRelease(wifi_scan_sem);
            }
            hal_mem_free(scan_result.scan_info);
            sl_net_scan_async_restore_links();
            return SL_STATUS_OK;
        }
        global_scan_result_count = 0;
        LOG_DRV_ERROR("extended scan result count is 0\r\n");
        if (callback != NULL) callback(0, &scan_result);
        else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
        sl_net_scan_async_restore_links();
        return SL_STATUS_OK;
    }
#endif

    if (result_length == 0) {
        LOG_DRV_ERROR("scan result length is 0\r\n");
        if (callback != NULL) callback(0, &scan_result);
        else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
    } else if (result != NULL) {
        LOG_DRV_INFO("normal scan result count: %d\r\n", result->scan_count);
        global_scan_result_count = result->scan_count;
        scan_result.scan_count = result->scan_count;
        scan_result.scan_info = hal_mem_alloc_large(result->scan_count * sizeof(wireless_scan_info_t));
        if (scan_result.scan_info == NULL) {
            if (callback != NULL) callback(-2, NULL);
            return SL_STATUS_FAIL;
        }
        for (i = 0; i < result->scan_count; i++) {
            scan_result.scan_info[i].rssi = -result->scan_info[i].rssi_val;
            memcpy(scan_result.scan_info[i].ssid, result->scan_info[i].ssid, sizeof(scan_result.scan_info[i].ssid));
            memcpy(scan_result.scan_info[i].bssid, result->scan_info[i].bssid, sizeof(scan_result.scan_info[i].bssid));
            scan_result.scan_info[i].channel = result->scan_info[i].rf_channel;
            scan_result.scan_info[i].security = result->scan_info[i].security_mode;
        }
        if (callback != NULL) callback(0, &scan_result);
        else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) {
            if (wifi_storage_scan_result.scan_info != NULL) {
                wifi_storage_scan_result.scan_count = scan_result.scan_count;
                memcpy(wifi_storage_scan_result.scan_info, scan_result.scan_info, scan_result.scan_count * sizeof(wireless_scan_info_t));
            }
            osSemaphoreRelease(wifi_scan_sem);
        }
        hal_mem_free(scan_result.scan_info);
    } else {
        global_scan_result_count = 0;
        LOG_DRV_ERROR("scan result pointer is NULL (len=%lu)\r\n", (unsigned long)result_length);
        if (callback != NULL) callback(-1, NULL);
        else if (arg == (void *)&wifi_storage_scan_result && wifi_scan_sem != NULL) osSemaphoreRelease(wifi_scan_sem);
    }
    sl_net_scan_async_restore_links();
    return SL_STATUS_OK;
}

int sl_net_start_scan(wireless_scan_callback_t callback)
{
    struct netif *_if_ = NULL;
    sl_status_t status = SL_STATUS_OK;
    bool sta_link_up = false;
    bool ap_link_up = false;

    osMutexAcquire(sl_net_mutex, osWaitForever);
    _if_ = netif_get_by_index(client_netif.num + 1);
    if (_if_ == &client_netif) {
        sta_link_up = netif_is_link_up(&client_netif);
    } else {
        _if_ = netif_get_by_index(ap_netif.num + 1);
        if (_if_ != &ap_netif || !netif_is_link_up(_if_)) {
            status = SL_STATUS_INVALID_STATE;
            goto sl_net_start_scan_end;
        }
    }
    ap_link_up = (sl_net_ap_netif_state() == NETIF_STATE_UP);

    if (sta_link_up || ap_link_up) {
        sl_net_pause_links_for_scan(sta_link_up, ap_link_up);
        sl_net_async_scan_restore_sta = sta_link_up;
        sl_net_async_scan_restore_ap = ap_link_up;
    }

    status = sl_net_prepare_extended_scan(callback);
    if (status != SL_STATUS_OK && status != SL_STATUS_IN_PROGRESS) {
        sl_net_async_scan_restore_sta = 0;
        sl_net_async_scan_restore_ap = 0;
        if (sta_link_up || ap_link_up) {
            sl_net_restore_links_after_scan(sta_link_up, ap_link_up);
        }
        LOG_DRV_ERROR("Failed to start scan: 0x%lX\r\n", status);
        goto sl_net_start_scan_end;
    }
    status = SL_STATUS_OK;

sl_net_start_scan_end:
    osMutexRelease(sl_net_mutex);
    return status;
}

wireless_scan_result_t *sl_net_get_strorage_scan_result(void)
{
    return &wifi_storage_scan_result;
}

int sl_net_update_strorage_scan_result(uint32_t timeout_ms)
{
    int ret = 0;
    netif_state_t ap_state = NETIF_STATE_DEINIT;
    netif_state_t client_state = NETIF_STATE_DEINIT;
    bool sta_link_up = false;
    bool ap_link_up = false;

    if (wifi_scan_sem == NULL) {
        wifi_scan_sem = osSemaphoreNew(1, 0, NULL);
        if (wifi_scan_sem == NULL) return SL_STATUS_ALLOCATION_FAILED;
    }
    if (wifi_storage_scan_result.scan_info == NULL) {
#if DEFAULT_SCAN_TYPE == SL_WIFI_SCAN_TYPE_EXTENDED
        wifi_storage_scan_result.scan_info = hal_mem_alloc_large(DEFAULT_SCAN_RESULT_MAX * sizeof(wireless_scan_info_t));
#else
        wifi_storage_scan_result.scan_info = hal_mem_alloc_large(SL_WIFI_MAX_SCANNED_AP * sizeof(wireless_scan_info_t));
#endif
        if (wifi_storage_scan_result.scan_info == NULL) return SL_STATUS_ALLOCATION_FAILED;
    }

    ap_state = sl_net_ap_netif_state();
    client_state = sl_net_client_netif_state();
    if (client_state == NETIF_STATE_DEINIT) return SL_STATUS_INVALID_STATE;

    sta_link_up = (client_state == NETIF_STATE_UP);
    ap_link_up = (ap_state == NETIF_STATE_UP);

    osMutexAcquire(sl_net_mutex, osWaitForever);
    if (sta_link_up || ap_link_up) {
        sl_net_pause_links_for_scan(sta_link_up, ap_link_up);
    }

    ret = sl_net_prepare_extended_scan(&wifi_storage_scan_result);
    if (ret == SL_STATUS_OK || ret == SL_STATUS_IN_PROGRESS) {
        ret = sl_net_acquire_scan_results(timeout_ms);
    }

    if (sta_link_up || ap_link_up) {
        sl_net_restore_links_after_scan(sta_link_up, ap_link_up);
    }

    osMutexRelease(sl_net_mutex);
    return ret;
}

/****************************************************************************************/
/************************************ WIFI AP NETIF *************************************/
/****************************************************************************************/
/// @brief AP initialization function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_ap_init(sl_net_interface_t interface,
                                const void *configuration,
                                const void *workspace,
                                sl_net_event_handler_t event_handler)
{
    sl_status_t status;
    const sl_wifi_device_configuration_t *cfg = configuration;
    UNUSED_PARAMETER(interface);
    UNUSED_PARAMETER(workspace);
    UNUSED_PARAMETER(event_handler);
    if (cfg == NULL) {
        cfg = &device_configuration;
    }
    status = sl_wifi_init(cfg, NULL, sl_wifi_default_event_handler);
    if (status == SL_STATUS_ALREADY_INITIALIZED
        && cfg->boot_config.oper_mode == SL_SI91X_CONCURRENT_MODE) {
        return SL_STATUS_OK;
    }
    return status;
}
/// @brief AP destruction function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_ap_deinit(sl_net_interface_t interface)
{
    UNUSED_PARAMETER(interface);
    return sl_wifi_deinit();
}
/// @brief AP activation function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_ap_up(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{
    UNUSED_PARAMETER(interface);
    err_t err = ERR_OK;
    sl_status_t status;
    sl_wifi_channel_t channel = {
        .channel = SL_WIFI_AUTO_CHANNEL,
        .band = SL_WIFI_AUTO_BAND,
        .bandwidth = SL_WIFI_AUTO_BANDWIDTH,
    };

    if (netif_get_by_index(client_netif.num + 1) == &client_netif && netif_is_link_up(&client_netif)) {
        status = sl_wifi_get_channel(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, &channel);
        if (status != SL_STATUS_OK) LOG_DRV_WARN("Failed to get client channel: 0x%lx\r\n", status);
        else if (channel.channel != 0) {
            LOG_DRV_DEBUG("AP channel: %d -> %d\r\n", wifi_ap_profile.config.channel.channel, channel.channel);
            wifi_ap_profile.config.channel.channel = channel.channel;
        }
    }

    if (wifi_ap_profile.config.ssid.length < 1) {
        snprintf((char *)wifi_ap_profile.config.ssid.value, sizeof(wifi_ap_profile.config.ssid.value), "NE301_%02X%02X%02X", ap_netif.hwaddr[3], ap_netif.hwaddr[4], ap_netif.hwaddr[5]);
        wifi_ap_profile.config.ssid.length = strlen((char *)wifi_ap_profile.config.ssid.value);
        LOG_DRV_INFO("Use default ap name: %s\r\n", wifi_ap_profile.config.ssid.value);
    }
    // Set the AP profile
    status = sl_net_set_profile(SL_NET_WIFI_AP_INTERFACE, profile_id, &wifi_ap_profile);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to set ap profile: 0x%0lX\r\n", status);
        return status;
    }

    if (wifi_ap_profile.config.security != SL_WIFI_OPEN) {
        status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
                                        wifi_ap_credential.type,
                                        &wifi_ap_credential.data,
                                        wifi_ap_credential.data_length);
        if (status != SL_STATUS_OK) {
            // sl_net_deinit(SL_NET_WIFI_AP_INTERFACE);
            LOG_DRV_ERROR("Failed to set ap credentials: 0x%lX\r\n", status);
            return status;
        }
    }

#if IS_TCP_IP_DUAL_MODE
    status = SL_STATUS_NOT_SUPPORTED;
    if (interface == SL_NET_WIFI_AP_1_INTERFACE) {
        status = sl_si91x_configure_ip_address(&wifi_ap_profile.ip, SL_WIFI_AP_VAP_ID);
    } else if (interface == SL_NET_WIFI_AP_2_INTERFACE) {
        status = sl_si91x_configure_ip_address(&wifi_ap_profile.ip, SL_WIFI_AP_VAP_ID_1);
    }
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to configure ap ip: 0x%0lX\r\n", status);
        return status;
    }

    // status = sl_net_get_profile(SL_NET_WIFI_AP_INTERFACE, profile_id, &wifi_ap_profile);
    // if (status != SL_STATUS_OK) {
    //     printf("Failed to get ap profile: 0x%lx\r\n", status);
    //     return status;
    // }
#endif
    status = sl_wifi_start_ap(SL_WIFI_AP_2_4GHZ_INTERFACE, &wifi_ap_profile.config);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to start ap: 0x%0lX\r\n", status);
        return status;
    }

    err = netifapi_netif_set_up(&ap_netif);
    if (err != ERR_OK) goto ap_netif_up_end;
    err = netifapi_netif_set_link_up(&ap_netif);
    if (err != ERR_OK) goto ap_netif_up_end;

#if IS_TCP_IP_DUAL_MODE
    ip_addr_t ipaddr  = { 0 };
    ip_addr_t gateway = { 0 };
    ip_addr_t netmask = { 0 };
    uint8_t *address = &(wifi_ap_profile.ip.ip.v4.ip_address.bytes[0]);
    IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
    address = &(wifi_ap_profile.ip.ip.v4.gateway.bytes[0]);
    IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
    address = &(wifi_ap_profile.ip.ip.v4.netmask.bytes[0]);
    IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);
    err = netifapi_netif_set_addr(&ap_netif,
                                    &ipaddr,
                                    &netmask,
                                    &gateway);
    LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " ip: %s\r\n", NETIF_NAME_PARAMETER(&ap_netif), ip4addr_ntoa((const ip4_addr_t *)&ap_netif.ip_addr));
#else
    if (SL_IP_MANAGEMENT_STATIC_IP == wifi_ap_profile.ip.mode) {
#if LWIP_IPV4 && LWIP_IPV6
        ip_addr_t ipaddr  = { 0 };
        ip_addr_t gateway = { 0 };
        ip_addr_t netmask = { 0 };

        if ((wifi_ap_profile.ip.type & SL_IPV4) == SL_IPV4) {
            uint8_t *address = &(wifi_ap_profile.ip.ip.v4.ip_address.bytes[0]);
            IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v4.gateway.bytes[0]);
            IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v4.netmask.bytes[0]);
            IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

            err = netifapi_netif_set_addr(&ap_netif,
                                    &ipaddr,
                                    &netmask,
                                    &gateway);
            LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " static ip: %s\r\n", NETIF_NAME_PARAMETER(&ap_netif), ip4addr_ntoa((const ip4_addr_t *)&ap_netif.ip_addr));
        }
        if ((wifi_ap_profile.ip.type & SL_IPV6) == SL_IPV6) {
            uint32_t *address = &(wifi_ap_profile.ip.ip.v6.link_local_address.value[0]);
            IP6_ADDR(&ipaddr.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v6.global_address.value[0]);
            IP6_ADDR(&gateway.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v6.gateway.value[0]);
            IP6_ADDR(&netmask.u_addr.ip6, address[0], address[1], address[2], address[3]);

            netif_ip6_addr_set(&ap_netif, 0, &ipaddr.u_addr.ip6);
            netif_ip6_addr_set(&ap_netif, 1, &gateway.u_addr.ip6);
            netif_ip6_addr_set(&ap_netif, 2, &netmask.u_addr.ip6);

            netif_ip6_addr_set_state(&ap_netif, 0, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&ap_netif, 1, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&ap_netif, 2, IP6_ADDR_PREFERRED);
        }
#elif LWIP_IPV4
        ip4_addr_t ipaddr  = { 0 };
        ip4_addr_t gateway = { 0 };
        ip4_addr_t netmask = { 0 };
        uint8_t *address   = &(wifi_ap_profile.ip.ip.v4.ip_address.bytes[0]);

        IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v4.gateway.bytes[0]);
        IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v4.netmask.bytes[0]);
        IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

        err = netifapi_netif_set_addr(&ap_netif, &ipaddr, &netmask, &gateway);
        LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " static ip: %s\r\n", NETIF_NAME_PARAMETER(&ap_netif), ip4addr_ntoa((const ip4_addr_t *)&ap_netif.ip_addr));
#elif LWIP_IPV6
        ip6_addr_t link_local_address = { 0 };
        ip6_addr_t global_address     = { 0 };
        ip6_addr_t gateway            = { 0 };
        uint32_t *address             = &(wifi_ap_profile.ip.ip.v6.link_local_address.value[0]);

        IP6_ADDR(&link_local_address, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v6.global_address.value[0]);
        IP6_ADDR(&global_address, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v6.gateway.value[0]);
        IP6_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        netif_ip6_addr_set(&ap_netif, 0, &link_local_address);
        netif_ip6_addr_set(&ap_netif, 1, &global_address);
        netif_ip6_addr_set(&ap_netif, 2, &gateway);

        netif_ip6_addr_set_state(&ap_netif, 0, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&ap_netif, 1, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&ap_netif, 2, IP6_ADDR_PREFERRED);
#endif /* LWIP_IPV6 */
    } else if (SL_IP_MANAGEMENT_DHCP == wifi_ap_profile.ip.mode) {
        err = ERR_IF;
    } else if (SL_IP_MANAGEMENT_LINK_LOCAL == wifi_ap_profile.ip.mode) {
#if LWIP_IPV4 && LWIP_IPV6
        ip_addr_t ipaddr  = { 0 };
        ip_addr_t gateway = { 0 };
        ip_addr_t netmask = { 0 };

        if ((wifi_ap_profile.ip.type & SL_IPV4) == SL_IPV4) {
            uint8_t *address = &(wifi_ap_profile.ip.ip.v4.ip_address.bytes[0]);
            IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v4.gateway.bytes[0]);
            IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v4.netmask.bytes[0]);
            IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

            err = netifapi_netif_set_addr(&ap_netif,
                                    &ipaddr,
                                    &netmask,
                                    &gateway);
            LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " dhcp server ip: %s\r\n", NETIF_NAME_PARAMETER(&ap_netif), ip4addr_ntoa((const ip4_addr_t *)&ap_netif.ip_addr));
            if (err == ERR_OK) dhcps_start(&ap_netif);
        }
        if ((wifi_ap_profile.ip.type & SL_IPV6) == SL_IPV6) {
            uint32_t *address = &(wifi_ap_profile.ip.ip.v6.link_local_address.value[0]);
            IP6_ADDR(&ipaddr.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v6.global_address.value[0]);
            IP6_ADDR(&gateway.u_addr.ip6, address[0], address[1], address[2], address[3]);
            address = &(wifi_ap_profile.ip.ip.v6.gateway.value[0]);
            IP6_ADDR(&netmask.u_addr.ip6, address[0], address[1], address[2], address[3]);

            netif_ip6_addr_set(&ap_netif, 0, &ipaddr.u_addr.ip6);
            netif_ip6_addr_set(&ap_netif, 1, &gateway.u_addr.ip6);
            netif_ip6_addr_set(&ap_netif, 2, &netmask.u_addr.ip6);

            netif_ip6_addr_set_state(&ap_netif, 0, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&ap_netif, 1, IP6_ADDR_PREFERRED);
            netif_ip6_addr_set_state(&ap_netif, 2, IP6_ADDR_PREFERRED);
        }
#elif LWIP_IPV4
        ip4_addr_t ipaddr  = { 0 };
        ip4_addr_t gateway = { 0 };
        ip4_addr_t netmask = { 0 };
        uint8_t *address   = &(wifi_ap_profile.ip.ip.v4.ip_address.bytes[0]);

        IP4_ADDR(&ipaddr, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v4.gateway.bytes[0]);
        IP4_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v4.netmask.bytes[0]);
        IP4_ADDR(&netmask, address[0], address[1], address[2], address[3]);

        err = netifapi_netif_set_addr(&ap_netif, &ipaddr, &netmask, &gateway);
        LOG_DRV_DEBUG(NETIF_NAME_STR_FMT " dhcp server ip: %s\r\n", NETIF_NAME_PARAMETER(&ap_netif), ip4addr_ntoa((const ip4_addr_t *)&ap_netif.ip_addr));
        if (err == ERR_OK) dhcps_start(&ap_netif);
#elif LWIP_IPV6
        ip6_addr_t link_local_address = { 0 };
        ip6_addr_t global_address     = { 0 };
        ip6_addr_t gateway            = { 0 };
        uint32_t *address             = &(wifi_ap_profile.ip.ip.v6.link_local_address.value[0]);

        IP6_ADDR(&link_local_address, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v6.global_address.value[0]);
        IP6_ADDR(&global_address, address[0], address[1], address[2], address[3]);
        address = &(wifi_ap_profile.ip.ip.v6.gateway.value[0]);
        IP6_ADDR(&gateway, address[0], address[1], address[2], address[3]);
        netif_ip6_addr_set(&ap_netif, 0, &link_local_address);
        netif_ip6_addr_set(&ap_netif, 1, &global_address);
        netif_ip6_addr_set(&ap_netif, 2, &gateway);

        netif_ip6_addr_set_state(&ap_netif, 0, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&ap_netif, 1, IP6_ADDR_PREFERRED);
        netif_ip6_addr_set_state(&ap_netif, 2, IP6_ADDR_PREFERRED);
#endif /* LWIP_IPV6 */
    }
#endif

ap_netif_up_end:
    if (err != ERR_OK) {
        if (netif_is_link_up(&ap_netif)) netifapi_netif_set_link_down(&ap_netif);
        if (netif_is_up(&ap_netif)) netifapi_netif_set_down(&ap_netif);
        sl_wifi_stop_ap(SL_WIFI_AP_2_4GHZ_INTERFACE);
    }
    return err;
}
/// @brief AP deactivation function implementation provided for SL SDK calls
sl_status_t sl_net_wifi_ap_down(sl_net_interface_t interface)
{
    UNUSED_PARAMETER(interface);
    netifapi_netif_set_link_down(&ap_netif);
    netifapi_netif_set_down(&ap_netif);
    
    dhcps_stop(&ap_netif);
    return sl_wifi_stop_ap(SL_WIFI_AP_2_4GHZ_INTERFACE);
}

static sl_status_t ap_connected_event_handler(sl_wifi_event_t event, void *data, uint32_t data_length, void *arg)
{
    sl_mac_address_t *mac_address = (sl_mac_address_t *)data;
    UNUSED_PARAMETER(data_length);
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(event);

    printf("Remote Client connected: ");
    print_mac_address((sl_mac_address_t *)mac_address);
    printf("\r\n");
    if (wifi_ap_profile.ip.mode == SL_IP_MANAGEMENT_LINK_LOCAL) {
        dhcps_add_client_by_mac(mac_address->octet);
    }

    return SL_STATUS_OK;
}

static sl_status_t ap_disconnected_event_handler(sl_wifi_event_t event, void *data, uint32_t data_length, void *arg)
{
    sl_mac_address_t *mac_address = (sl_mac_address_t *)data;
    UNUSED_PARAMETER(data_length);
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(event);

    printf("Remote Client disconnected: ");
    print_mac_address(mac_address);
    printf("\r\n");
    if (wifi_ap_profile.ip.mode == SL_IP_MANAGEMENT_LINK_LOCAL) {
        dhcps_del_client_by_mac(mac_address->octet);
    }

    return SL_STATUS_OK;
}

/// @brief AP network interface initialization function provided externally
/// @param None
/// @return Error code
int sl_net_ap_netif_init(void)
{
    struct netif *ap0 = NULL;
    // uint8_t init_try_times = 0;
#if LWIP_IPV4
    ip_addr_t ap_ipaddr;
    ip_addr_t ap_netmask;
    ip_addr_t ap_gw;

    /* Initialize the Station information */
    ip_addr_set_zero_ip4(&ap_ipaddr);
    ip_addr_set_zero_ip4(&ap_netmask);
    ip_addr_set_zero_ip4(&ap_gw);
#endif /* LWIP_IPV4 */
    sl_status_t status;

    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 != NULL && ap0 == &ap_netif) return SL_STATUS_INVALID_STATE;

    // do {
        status = sl_net_init(SL_NET_WIFI_AP_INTERFACE, &device_configuration, NULL, NULL);
    // } while (status != SL_STATUS_OK && init_try_times++ < 3);
    if (status != SL_STATUS_OK && status != SL_STATUS_ALREADY_INITIALIZED) {
        if (netif_get_by_index(client_netif.num + 1) != &client_netif) {
            sl_net_deinit(SL_NET_WIFI_AP_INTERFACE);
        }
        LOG_DRV_ERROR("Failed to init Wi-Fi AP interface: 0x%lX\r\n", status);
        return status;
    }

#if IS_ENABLE_NWP_DEBUG_PRINTS
    sl_si91x_assertion_t assertion = {
        .assert_type = SL_SI91X_ASSERTION_TYPE_ALL,
        .assert_level = SL_SI91X_ASSERTION_LEVEL_MAX
    };
    sl_si91x_debug_log(&assertion);
#endif
    sl_wifi_set_callback(SL_WIFI_CLIENT_CONNECTED_EVENTS, ap_connected_event_handler, NULL);
    sl_wifi_set_callback(SL_WIFI_CLIENT_DISCONNECTED_EVENTS, ap_disconnected_event_handler, NULL);
    
    // status = sl_net_set_profile(SL_NET_WIFI_AP_INTERFACE, SL_NET_DEFAULT_WIFI_AP_PROFILE_ID, &wifi_ap_profile);
    // if (status != SL_STATUS_OK) {
    //     LOG_DRV_DEBUG("Failed to set ap profile: 0x%0lX\r\n", status);
    //     return status;
    // }

    // if (wifi_ap_profile.config.security != SL_WIFI_OPEN) {
    //     status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
    //                                     wifi_ap_credential.type,
    //                                     &wifi_ap_credential.data,
    //                                     wifi_ap_credential.data_length);
    //     if (status != SL_STATUS_OK) {
    //         sl_net_deinit(SL_NET_WIFI_AP_INTERFACE);
    //         LOG_DRV_ERROR("Failed to set ap credentials: 0x%lX\r\n", status);
    //         return status;
    //     }
    // }

    /* Add station interfaces */
    err_t nif_err = netifapi_netif_add(&ap_netif,
#if LWIP_IPV4
            (const ip4_addr_t *)&ap_ipaddr,
            (const ip4_addr_t *)&ap_netmask,
            (const ip4_addr_t *)&ap_gw,
#endif /* LWIP_IPV4 */
            NULL,
            &sl_net_ethernetif_init,
            &tcpip_input);
    if (nif_err != ERR_OK) {
        if (netif_get_by_index(client_netif.num + 1) != &client_netif) {
            sl_net_deinit(SL_NET_WIFI_AP_INTERFACE);
        }
        return SL_STATUS_FAIL;
    }
    
    /* Registers the default network interface */
    // netif_set_default(&ap_netif);
    return 0;
}
/// @brief Network interface activation function provided externally (AP)
/// @param None
/// @return Error code
int sl_net_ap_netif_up(void)
{
    struct netif *ap0 = NULL;
    sl_status_t status;

    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 == NULL || ap0 != &ap_netif) return SL_STATUS_INVALID_STATE;
    
    if (netif_is_link_up(ap0)) return SL_STATUS_OK;
    status = sl_net_up(SL_NET_WIFI_AP_INTERFACE, SL_NET_DEFAULT_WIFI_AP_PROFILE_ID);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to bring Wi-Fi AP interface up: 0x%lX\r\n", status);
        return status;
    } else {
#if LWIP_MDNS_RESPONDER
        /* Set mDNS host name */
        mdns_resp_add_netif(&ap_netif, "ne301");
        mdns_resp_add_service(&ap_netif, "Web Server", "_http", 
                            DNSSD_PROTO_TCP, 80, NULL, NULL);
        mdns_resp_add_service(&ap_netif, "WebSocket Server", "_ws", 
                            DNSSD_PROTO_TCP, 8081, NULL, NULL);
#endif /* LWIP_MDNS_RESPONDER */
    }
    return status;
}
/// @brief Network interface deactivation function provided externally (AP)
/// @param None
/// @return Error code
int sl_net_ap_netif_down(void)
{
    struct netif *ap0 = NULL;
    sl_status_t status;

    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 == NULL || ap0 != &ap_netif) return SL_STATUS_INVALID_STATE;

    if (!netif_is_link_up(ap0)) return SL_STATUS_OK;
    status = sl_net_down(SL_NET_WIFI_AP_INTERFACE);
    if (status != SL_STATUS_OK) {
        LOG_DRV_ERROR("Failed to bring Wi-Fi AP interface down: 0x%lX\r\n", status);
        return status;
    }
#if LWIP_MDNS_RESPONDER
    /* Remove mDNS host name */
    mdns_resp_remove_netif(&ap_netif);
#endif /* LWIP_MDNS_RESPONDER */
    return status;
}
/// @brief Network interface destruction function provided externally (AP)
/// @param None
void sl_net_ap_netif_deinit(void)
{
    struct netif *ap0 = NULL;

    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 == NULL || ap0 != &ap_netif) return;

    if (netif_is_link_up(ap0)) sl_net_ap_netif_down();
    // If STA network interface is not initialized, call SDK destruction function
    if (netif_get_by_index(client_netif.num + 1) != &client_netif) {
        sl_net_deinit(SL_NET_WIFI_AP_INTERFACE);
    }
    (void)netifapi_netif_remove(ap0);
}
/// @brief Network interface configuration function provided externally (AP)
/// @param netif_cfg Specific configuration
/// @return Error code
int sl_net_ap_netif_config(netif_config_t *netif_cfg)
{
    sl_status_t status = SL_STATUS_OK;
    sl_mac_address_t mac_addr = {0};
    if (netif_cfg == NULL) return SL_STATUS_INVALID_PARAMETER;
    if (netif_is_link_up(&ap_netif) || netif_is_up(&ap_netif)) return SL_STATUS_INVALID_STATE;
    
    if (NETIF_MAC_IS_UNICAST(netif_cfg->diy_mac)) {
        if (netif_get_by_index(ap_netif.num + 1) == &ap_netif) {
            status = sl_wifi_get_mac_address(SL_WIFI_AP_INTERFACE, &mac_addr);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Get MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER((&ap_netif)), status);
                return ERR_IF;
            }
            if (memcmp(netif_cfg->diy_mac, mac_addr.octet, sizeof(mac_addr.octet))) {
                memcpy(mac_addr.octet, netif_cfg->diy_mac, sizeof(mac_addr.octet));
                status = sl_wifi_set_mac_address(SL_WIFI_AP_INTERFACE, &mac_addr);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Set MAC address failed(status = 0x%lX)!\r\n", NETIF_NAME_PARAMETER((&ap_netif)), status);
                    return ERR_IF;
                }
            }
        }
        memcpy(ap_netif.hwaddr, netif_cfg->diy_mac, sizeof(netif_cfg->diy_mac));
        LOG_DRV_DEBUG(NETIF_NAME_STR_FMT ": MAC Address: " NETIF_MAC_STR_FMT "\r\n", NETIF_NAME_PARAMETER((&ap_netif)), NETIF_MAC_PARAMETER(netif_cfg->diy_mac));
    }

    if (netif_cfg->host_name != NULL) {
        wifi_ap_profile.ip.host_name = netif_cfg->host_name;
    #if LWIP_NETIF_HOSTNAME
        ap_netif.hostname = wifi_ap_profile.ip.host_name;
    #endif
    }

    if (netif_cfg->wireless_cfg.max_client_num > NETIF_WIFI_AP_MAX_CLIENT_NUM) netif_cfg->wireless_cfg.max_client_num = NETIF_WIFI_AP_MAX_CLIENT_NUM;
    wifi_ap_profile.config.maximum_clients = netif_cfg->wireless_cfg.max_client_num;
    wifi_ap_profile.config.ssid.length = strlen(netif_cfg->wireless_cfg.ssid);
    memcpy(wifi_ap_profile.config.ssid.value, netif_cfg->wireless_cfg.ssid, wifi_ap_profile.config.ssid.length);
    wifi_ap_credential.data_length = strlen(netif_cfg->wireless_cfg.pw);
    if (wifi_ap_credential.data_length < 8) {
        wifi_ap_profile.config.security = SL_WIFI_OPEN;
        wifi_ap_profile.config.credential_id = SL_WIFI_NO_CREDENTIAL_ID;
    } else {
        memcpy(wifi_ap_credential.data, netif_cfg->wireless_cfg.pw, wifi_ap_credential.data_length);
        wifi_ap_profile.config.security = (sl_wifi_security_t)netif_cfg->wireless_cfg.security;
        wifi_ap_profile.config.credential_id = SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID;
    }
    wifi_ap_profile.config.encryption = (sl_wifi_encryption_t)netif_cfg->wireless_cfg.encryption;
    wifi_ap_profile.config.channel.channel = netif_cfg->wireless_cfg.channel;
    
    if (netif_cfg->ip_mode == NETIF_IP_MODE_STATIC) wifi_ap_profile.ip.mode = SL_IP_MANAGEMENT_STATIC_IP;
    else if (netif_cfg->ip_mode == NETIF_IP_MODE_DHCP) wifi_ap_profile.ip.mode = SL_IP_MANAGEMENT_DHCP;
    else if (netif_cfg->ip_mode == NETIF_IP_MODE_DHCPS) wifi_ap_profile.ip.mode = SL_IP_MANAGEMENT_LINK_LOCAL;
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->ip_addr)) memcpy(wifi_ap_profile.ip.ip.v4.ip_address.bytes, netif_cfg->ip_addr, sizeof(netif_cfg->ip_addr));
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->gw)) memcpy(wifi_ap_profile.ip.ip.v4.gateway.bytes, netif_cfg->gw, sizeof(netif_cfg->gw));
    if (!NETIF_IPV4_IS_ZERO(netif_cfg->netmask)) memcpy(wifi_ap_profile.ip.ip.v4.netmask.bytes, netif_cfg->netmask, sizeof(netif_cfg->netmask));
    
    return SL_STATUS_OK;
}
/// @brief Network interface information retrieval function provided externally (AP)
/// @param netif_info Specific information
/// @return Error code
int sl_net_ap_netif_info(netif_info_t *netif_info)
{
    sl_wifi_firmware_version_t firmware_version = { 0 };
    sl_wifi_channel_t channel = {0};
    sl_status_t status = SL_STATUS_OK;
    struct netif *ap0 = NULL;
    if (netif_info == NULL) return SL_STATUS_INVALID_PARAMETER;

    netif_info->host_name = wifi_ap_profile.ip.host_name;
    netif_info->if_name = NETIF_NAME_WIFI_AP;
    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 == NULL || ap0 != &ap_netif) netif_info->state = NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&ap_netif) || !netif_is_up(&ap_netif)) netif_info->state = NETIF_STATE_DOWN;
    else netif_info->state = NETIF_STATE_UP;
    netif_info->type = NETIF_TYPE_WIRELESS;
    netif_info->rssi = 0;

    memset(netif_info->fw_version, 0, sizeof(netif_info->fw_version));
    if (netif_info->state != NETIF_STATE_DEINIT) {
        status = sl_wifi_get_firmware_version(&firmware_version);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("Failed to wifi firmware version: 0x%lx\r\n", status);
            return status;
        }
        snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "%x%x.%d.%d.%d.%d.%d.%d",
                firmware_version.chip_id,
                firmware_version.rom_id,
                firmware_version.major,
                firmware_version.minor,
                firmware_version.security_version,
                firmware_version.patch_num,
                firmware_version.customer_id,
                firmware_version.build_num);
        
        if (netif_info->state == NETIF_STATE_UP) {
            status = sl_wifi_get_channel(SL_WIFI_AP_2_4GHZ_INTERFACE, &channel);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("Failed to get ap channel: 0x%lx\r\n", status);
                return status;
            }
            wifi_ap_profile.config.channel.channel = channel.channel;
        }
    }
    memcpy(netif_info->if_mac, ap_netif.hwaddr, sizeof(netif_info->if_mac));

    if (wifi_ap_profile.ip.mode == SL_IP_MANAGEMENT_STATIC_IP) netif_info->ip_mode = NETIF_IP_MODE_STATIC;
    else if (wifi_ap_profile.ip.mode == SL_IP_MANAGEMENT_DHCP) netif_info->ip_mode = NETIF_IP_MODE_DHCP;
    else if (wifi_ap_profile.ip.mode == SL_IP_MANAGEMENT_LINK_LOCAL) netif_info->ip_mode = NETIF_IP_MODE_DHCPS;

    memcpy(netif_info->ip_addr, &ap_netif.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->gw, &ap_netif.gw, sizeof(netif_info->gw));
    memcpy(netif_info->netmask, &ap_netif.netmask, sizeof(netif_info->netmask));

    memset(netif_info->wireless_cfg.ssid, 0x00, sizeof(netif_info->wireless_cfg.ssid));
    memcpy(netif_info->wireless_cfg.ssid, wifi_ap_profile.config.ssid.value, wifi_ap_profile.config.ssid.length);
    memset(netif_info->wireless_cfg.pw, 0x00, sizeof(netif_info->wireless_cfg.pw));
    if (wifi_ap_credential.data_length >= 8) memcpy(netif_info->wireless_cfg.pw, wifi_ap_credential.data, wifi_ap_credential.data_length);
    netif_info->wireless_cfg.max_client_num = wifi_ap_profile.config.maximum_clients;
    netif_info->wireless_cfg.security = (wireless_security_t)wifi_ap_profile.config.security;
    netif_info->wireless_cfg.encryption = (wireless_encryption_t)wifi_ap_profile.config.encryption;
    netif_info->wireless_cfg.channel = wifi_ap_profile.config.channel.channel;

    return status;
}

netif_state_t sl_net_ap_netif_state(void)
{
    struct netif *ap0 = NULL;

    ap0 = netif_get_by_index(ap_netif.num + 1);
    if (ap0 == NULL || ap0 != &ap_netif) return NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&ap_netif) || !netif_is_up(&ap_netif)) return NETIF_STATE_DOWN;
    else return NETIF_STATE_UP;
}

struct netif *sl_net_ap_netif_ptr(void)
{
    return &ap_netif;
}

extern void netif_manager_change_default_if(void);
#ifdef SLI_SI91X_SIMULATION_C1C2_ERROR
extern void sli_generate_c1c2_error(void);
extern void sli_reset_c1c2_error(void);
#endif
void sl_net_thread(void *arg)
{
    int event_flag = 0, ret = 0;
    uint8_t try_times = 0;
    sl_status_t status = SL_STATUS_OK;
    netif_state_t ap_state = NETIF_STATE_DEINIT, client_state = NETIF_STATE_DEINIT;

    while (1) {
        event_flag = (int)osEventFlagsWait(sl_net_events, SL_NET_EVENT_ALL, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
        if (event_flag > 0) {
            if (event_flag & SL_NET_EVENT_FIRMWARE_ERROR) {
                sl_net_is_recovering = 1;
                LOG_DRV_INFO("WIFI firmware abnormal, attempting to recover...");
                osMutexAcquire(sl_net_mutex, osWaitForever);
                ap_state = sl_net_ap_netif_state();
                client_state = sl_net_client_netif_state();
                osMutexRelease(sl_net_mutex);
                LOG_DRV_INFO("Save the current network card status: ap_state = %d, client_state = %d\r\n", ap_state, client_state);
                try_times = 0;
                do {
                    ret = 0;
                    osMutexAcquire(sl_net_mutex, osWaitForever);
                    sl_net_ap_netif_deinit();
                    sl_net_client_netif_deinit(); 
                    osMutexRelease(sl_net_mutex);
                    osDelay(try_times * 500 + 500);
                    osMutexAcquire(sl_net_mutex, osWaitForever);
                #ifdef SLI_SI91X_SIMULATION_C1C2_ERROR
                    sli_reset_c1c2_error();
                #endif
                    if (client_state > NETIF_STATE_DEINIT) {
                        ret = sl_net_client_netif_init();
                        if (ret) goto recover_end;
                        if (client_state == NETIF_STATE_UP) {
                            ret = sl_net_client_netif_up();
                            if (ret) goto recover_end;
                        }
                    }
                    if (ap_state > NETIF_STATE_DEINIT) {
                        ret = sl_net_ap_netif_init();
                        if (ret) goto recover_end;
                        if (ap_state == NETIF_STATE_UP) {
                            ret = sl_net_ap_netif_up();
                            if (ret) goto recover_end;
                        }
                    }
                recover_end:
                    osMutexRelease(sl_net_mutex);
                    LOG_DRV_INFO("%dth recovery result: 0x%X", (try_times + 1), ret);
                } while (++try_times < 10 && ret != 0);
                sl_net_is_recovering = 0;
                if (ret == 0) {
                    netif_manager_change_default_if();
                    osEventFlagsClear(sl_net_events, SL_NET_EVENT_FIRMWARE_ERROR);
                	LOG_DRV_INFO("WIFI firmware recovery successful!");
                } else LOG_DRV_INFO("WIFI firmware recovery failed!");
            } else if (event_flag & SL_NET_EVENT_STA_DISCONNECTED) {
                osMutexAcquire(sl_net_mutex, osWaitForever);
                client_state = sl_net_client_netif_state();
                if (client_state == NETIF_STATE_UP) {
                    sl_net_client_netif_down();
                    status = sli_si91x_driver_send_command(SLI_WIFI_REQ_INIT,
                                                              SLI_WIFI_WLAN_CMD,
                                                              NULL,
                                                              0,
                                                              SLI_WIFI_WAIT_FOR_COMMAND_SUCCESS,
                                                              NULL,
                                                              NULL);
                    if (status != SL_STATUS_OK) {
                        LOG_DRV_ERROR("Failed to re-initialize Wi-Fi driver: 0x%lX\r\n", status);
                    }
                }
                osEventFlagsClear(sl_net_events, SL_NET_EVENT_STA_DISCONNECTED);
                osMutexRelease(sl_net_mutex);
            }
        }
    }
}

/// @brief WiFi network interface error callback
/// @param error_code Error code
void sli_firmware_error_callback(int error_code)
{
    if (remote_wakeup_mode != WAKEUP_MODE_NORMAL) return;
    if (error_code == (SL_WIFI_EVENT_FAIL_INDICATION_EVENTS | SL_WIFI_JOIN_EVENTS)) {
        LOG_DRV_WARN("WIFI STA Disconnected.");
        osEventFlagsSet(sl_net_events, SL_NET_EVENT_STA_DISCONNECTED);
    } else {
        LOG_DRV_ERROR("WIFI firmware error: 0x%X", error_code);
    #ifdef SLI_SI91X_SIMULATION_C1C2_ERROR
        if (error_code == 0x1234) sli_generate_c1c2_error();
        else {
    #endif
            osEventFlagsSet(sl_net_events, SL_NET_EVENT_FIRMWARE_ERROR);
    #ifdef SLI_SI91X_SIMULATION_C1C2_ERROR
        }
    #endif
    }
}

/// @brief Initialize WiFi network interface
/// @param None
/// @return Error code 
int sl_net_netif_init(void)
{
    if (is_wifi_ant()) return SL_STATUS_INVALID_STATE;
    if (sl_net_mutex == NULL) {
        sl_net_mutex = osMutexNew(NULL);
        if (sl_net_mutex == NULL) return -1;
    }
    if (sl_net_events == NULL) {
        sl_net_events = osEventFlagsNew(NULL);
        if (sl_net_events == NULL) return -1;
    }
    if (sl_net_thread_ID == NULL) {
        sl_net_thread_ID = osThreadNew(sl_net_thread, NULL, &attr);
        if (sl_net_thread_ID == NULL) return -1;
    }
    return 0;
}

sl_net_wakeup_mode_t sl_net_netif_get_wakeup_mode(void)
{
    return remote_wakeup_mode;
}
int sl_net_netif_romote_wakeup_mode_ctrl(sl_net_wakeup_mode_t wakeup_mode)
{
    int ret = 0;
    uint8_t cnt_try_times = 0;
    sl_status_t status = SL_STATUS_OK;
    netif_state_t ap_state = NETIF_STATE_DEINIT, client_state = NETIF_STATE_DEINIT;
    if (sl_net_thread_ID == NULL) return SL_STATUS_INVALID_STATE;
    osMutexAcquire(sl_net_mutex, osWaitForever);

    do {
        if (remote_wakeup_mode == WAKEUP_MODE_NORMAL && wakeup_mode != WAKEUP_MODE_NORMAL) {
            ap_state = sl_net_ap_netif_state();
            client_state = sl_net_client_netif_state();
            if (ap_state != NETIF_STATE_DEINIT) sl_net_ap_netif_deinit();
            if (client_state != NETIF_STATE_DEINIT) sl_net_client_netif_deinit();
            if (ap_state != NETIF_STATE_DEINIT || client_state != NETIF_STATE_DEINIT) osDelay(500);

            if (wakeup_mode == WAKEUP_MODE_WIFI) {
                status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &remote_wake_up_wifi_cfg, NULL, NULL);
                if (status != SL_STATUS_OK) {
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to init Wi-Fi Client interface: 0x%lX\r\n", status);
                    break;
                }

                status = sl_si91x_configure_timeout(SL_SI91X_KEEP_ALIVE_TIMEOUT, 60);
                if (status != SL_STATUS_OK) {
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to set keep-alive timeout: 0x%lX\r\n", status);
                    break;
                }

                status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID, &wifi_client_profile);
                if (status != SL_STATUS_OK) {
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to set client profile: 0x%lX\r\n", status);
                    break;
                }

                if (wifi_client_profile.config.security != SL_WIFI_OPEN) {
                    status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
                                                    wifi_client_credential.type,
                                                    &wifi_client_credential.data,
                                                    wifi_client_credential.data_length);
                    if (status != SL_STATUS_OK) {
                        sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                        LOG_DRV_ERROR("Failed to set client credentials: 0x%lX\r\n", status);
                        break;
                    }
                }
                
                do {
                    status = sl_wifi_connect(SL_WIFI_CLIENT_INTERFACE, &wifi_client_profile.config, 18000);
                } while (status != SL_STATUS_OK && cnt_try_times++ < 3);
                if (status != SL_STATUS_OK) {
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to connect to Wi-Fi: 0x%0lX\r\n", status);
                    break;
                }

                // Configure the IP address settings
                status = SL_STATUS_NOT_SUPPORTED;
                if (SL_NET_INTERFACE_TYPE(SL_NET_WIFI_CLIENT_INTERFACE) == SL_NET_WIFI_CLIENT_1_INTERFACE) {
                    status = sl_si91x_configure_ip_address(&wifi_client_profile.ip, SL_WIFI_CLIENT_VAP_ID);
                } else if (SL_NET_INTERFACE_TYPE(SL_NET_WIFI_CLIENT_INTERFACE) == SL_NET_WIFI_CLIENT_2_INTERFACE) {
                    status = sl_si91x_configure_ip_address(&wifi_client_profile.ip, SL_WIFI_CLIENT_VAP_ID_1);
                }
                if (status != SL_STATUS_OK) {
                    sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to configure client ip: 0x%0lX\r\n", status);
                    break;
                }

                status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID, &wifi_client_profile);
                if (status != SL_STATUS_OK) {
                    sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
                    LOG_DRV_ERROR("Failed to set client profile: 0x%lX\r\n", status);
                    break;
                }
            } else if (wakeup_mode == WAKEUP_MODE_BLE) {
#if IS_ENABLE_BLE
                status = sl_wifi_init(&remote_wake_up_ble_cfg, NULL, sl_wifi_default_event_handler);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR("Failed to init BLE: 0x%lX\r\n", status);
                    break;
                }
#else
                status = SL_STATUS_NOT_SUPPORTED;
#endif
            }

            if (status == SL_STATUS_OK) remote_wakeup_mode = wakeup_mode;
        } else if (remote_wakeup_mode != WAKEUP_MODE_NORMAL && wakeup_mode == WAKEUP_MODE_NORMAL) {
            if (remote_wakeup_mode == WAKEUP_MODE_WIFI) {
                sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
                status = sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
            } else if (remote_wakeup_mode == WAKEUP_MODE_BLE) {
#if IS_ENABLE_BLE
                status = sl_wifi_deinit();
#else
                status = SL_STATUS_NOT_SUPPORTED;
#endif
            }
            
            if (status == SL_STATUS_OK) remote_wakeup_mode = wakeup_mode;
        }
    } while (0);

    if (remote_wakeup_mode == WAKEUP_MODE_NORMAL && wakeup_mode != WAKEUP_MODE_NORMAL && status != SL_STATUS_OK) {
        if (ap_state > NETIF_STATE_DEINIT) {
            ret = sl_net_ap_netif_init();
            if (ap_state == NETIF_STATE_UP && ret == SL_STATUS_OK) {
                ret = sl_net_ap_netif_up();
            }
        }
        if (client_state > NETIF_STATE_DEINIT) {
            ret = sl_net_client_netif_init();
            if (client_state == NETIF_STATE_UP && ret == SL_STATUS_OK) {
                ret = sl_net_client_netif_up();
            }
        }
    }

    osMutexRelease(sl_net_mutex);
    if (status == SL_STATUS_OK && remote_wakeup_mode != wakeup_mode) status = SL_STATUS_INVALID_STATE;
    return status;
}

int sl_net_netif_filter_broadcast_ctrl(uint8_t enable)
{
    sl_status_t status = SL_STATUS_OK;
    if (sl_net_thread_ID == NULL) return SL_STATUS_INVALID_STATE;
    osMutexAcquire(sl_net_mutex, osWaitForever);

    do {
        if (sl_net_ap_netif_state() == NETIF_STATE_DEINIT && sl_net_client_netif_state() == NETIF_STATE_DEINIT && remote_wakeup_mode == WAKEUP_MODE_NORMAL) {
            status = SL_STATUS_INVALID_STATE;
            break;
        }

        status = sl_wifi_filter_broadcast(5000, enable, 1);
        if (status != SL_STATUS_OK) {
            LOG_DRV_ERROR("Failed to enable/disable broadcast filter: 0x%lX\r\n", status);
            break;
        }
    } while (0);

    osMutexRelease(sl_net_mutex);
    return status;
}

extern sl_wifi_system_performance_profile_t current_performance_profile;
int sl_net_netif_low_power_mode_ctrl(uint8_t enable){
    sl_status_t status = SL_STATUS_OK;
    sl_wifi_performance_profile_v2_t performance_profile = {0};
    if (sl_net_thread_ID == NULL) return SL_STATUS_INVALID_STATE;
    osMutexAcquire(sl_net_mutex, osWaitForever);

    do {
        if (sl_net_ap_netif_state() == NETIF_STATE_DEINIT && sl_net_client_netif_state() == NETIF_STATE_DEINIT && remote_wakeup_mode == WAKEUP_MODE_NORMAL) {
            status = SL_STATUS_INVALID_STATE;
            break;
        }

        if (enable && current_performance_profile == HIGH_PERFORMANCE) {
#if IS_ENABLE_BLE
            // If BLE wakeup mode and not scanning / connecting, return not supported
            if (remote_wakeup_mode == WAKEUP_MODE_BLE && !sl_ble_is_scanning() && sl_ble_connected_num() == 0) {
                status = SL_STATUS_NOT_SUPPORTED;
                break;
            }
#endif
            
            performance_profile.profile = ASSOCIATED_POWER_SAVE_LOW_LATENCY;
            if (remote_wakeup_mode == WAKEUP_MODE_WIFI || remote_wakeup_mode == WAKEUP_MODE_NORMAL) {
                status = sl_wifi_filter_broadcast(5000, 1, 1);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR("Failed to enable/disable broadcast filter: 0x%lX\r\n", status);
                    break;
                }
            }
#if IS_ENABLE_BLE
            if (remote_wakeup_mode == WAKEUP_MODE_BLE) {
                status = rsi_bt_power_save_profile(RSI_SLEEP_MODE_2, RSI_MAX_PSP);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR("Failed to set power save profile: 0x%lX\r\n", status);
                    break;
                }
            }
#endif
            status = sl_wifi_set_performance_profile_v2(&performance_profile);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("Failed to set performance profile: 0x%lX\r\n", status);
                break;
            }
        } else if (current_performance_profile != HIGH_PERFORMANCE) {
            performance_profile.profile = HIGH_PERFORMANCE;
#if IS_ENABLE_BLE
            if (remote_wakeup_mode == WAKEUP_MODE_BLE) {
                status = rsi_bt_power_save_profile(RSI_ACTIVE, RSI_MAX_PSP);
                if (status != SL_STATUS_OK) {
                    LOG_DRV_ERROR("Failed to set power save profile: 0x%lX\r\n", status);
                    break;
                }
            }
#endif
            status = sl_wifi_set_performance_profile_v2(&performance_profile);
            if (status != SL_STATUS_OK) {
                LOG_DRV_ERROR("Failed to set performance profile: 0x%lX\r\n", status);
                break;
            }
            if (status == SL_STATUS_OK && current_performance_profile != performance_profile.profile) {
                status = sl_wifi_set_performance_profile_v2(&performance_profile);
            }
        }
    } while (0);
    
    osMutexRelease(sl_net_mutex);
    return status;
}

// Resolve a host name to an IP address using DNS
sl_status_t sl_net_dns_resolve_hostname(const char *host_name,
    const uint32_t timeout,
    const sl_net_dns_resolution_ip_type_t dns_resolution_ip,
    sl_ip_address_t *sl_ip_address)
{
    // Check for a NULL pointer for sl_ip_address
    SL_WIFI_ARGS_CHECK_NULL_POINTER(sl_ip_address);

    sl_status_t status;
    sl_wifi_system_packet_t *packet;
    sl_wifi_buffer_t *buffer                        = NULL;
    const sli_si91x_dns_response_t *dns_response    = { 0 };
    sli_si91x_dns_query_request_t dns_query_request = { 0 };

    // Determine the wait period based on the timeout value
    sli_wifi_wait_period_t wait_period = timeout == 0 ? SLI_WIFI_RETURN_IMMEDIATELY
                    : SLI_WIFI_WAIT_FOR_RESPONSE(timeout);
    // Determine the IP version to be used (IPv4 or IPv6)
    dns_query_request.ip_version[0] = (dns_resolution_ip == SL_NET_DNS_TYPE_IPV4) ? 4 : 6;
    memcpy(dns_query_request.url_name, host_name, sizeof(dns_query_request.url_name));

    status = sli_si91x_driver_send_command(SLI_WLAN_REQ_DNS_QUERY,
        SLI_SI91X_NETWORK_CMD,
        &dns_query_request,
        sizeof(dns_query_request),
        wait_period,
        NULL,
        &buffer);

    // Check if the command failed and free the buffer if it was allocated
    if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_si91x_host_free_buffer(buffer);
    }
    VERIFY_STATUS_AND_RETURN(status);

    // Extract the DNS response from the SI91X packet buffer
    packet       = sl_si91x_host_get_buffer_data(buffer, 0, NULL);
    dns_response = (sli_si91x_dns_response_t *)packet->data;

    // Convert the SI91X DNS response to the sl_ip_address format
    sli_convert_si91x_dns_response(sl_ip_address, dns_response);
    sli_si91x_host_free_buffer(buffer);
    return SL_STATUS_OK;
}

// sl_status_t sl_net_set_dns_server(sl_net_interface_t interface, const sl_net_dns_address_t *address)
// {
//     UNUSED_PARAMETER(interface);
//     sl_status_t status                                  = 0;
//     sli_dns_server_add_request_t dns_server_add_request = { 0 };

//     //! Check for invalid parameters
//     if ((address->primary_server_address && address->primary_server_address->type != SL_IPV4
//         && address->primary_server_address->type != SL_IPV6)
//         || (address->secondary_server_address && address->secondary_server_address->type != SL_IPV4
//             && address->secondary_server_address->type != SL_IPV6)) {
//         //! Throw error in case of invalid parameters
//         return SL_STATUS_INVALID_PARAMETER;
//     }

//     dns_server_add_request.dns_mode[0] = SLI_NET_STATIC_IP;

//     if (address->primary_server_address && address->primary_server_address->type == SL_IPV4) {
//         dns_server_add_request.ip_version[0] = SL_IPV4_VERSION;
//         //! Fill Primary IP address
//         memcpy(dns_server_add_request.sli_ip_address1.primary_dns_ipv4,
//             address->primary_server_address->ip.v4.bytes,
//             SL_IPV4_ADDRESS_LENGTH);
//     } else if (address->primary_server_address && address->primary_server_address->type == SL_IPV6) {
//         dns_server_add_request.ip_version[0] = SL_IPV6_VERSION;
//         //! Fill Primary IP address
//         memcpy(dns_server_add_request.sli_ip_address1.primary_dns_ipv6,
//             address->primary_server_address->ip.v6.bytes,
//             SL_IPV6_ADDRESS_LENGTH);
//     }

//     if (address->secondary_server_address && address->secondary_server_address->type == SL_IPV4) {
//         dns_server_add_request.ip_version[0] = SL_IPV4_VERSION;
//         //! Fill Secondary IP address
//         memcpy(dns_server_add_request.sli_ip_address2.secondary_dns_ipv4,
//             address->secondary_server_address->ip.v4.bytes,
//             SL_IPV4_ADDRESS_LENGTH);
//     } else if (address->secondary_server_address && address->secondary_server_address->type == SL_IPV6) {
//         dns_server_add_request.ip_version[0] = SL_IPV6_VERSION;
//         //! Fill Secondary IP address
//         memcpy(dns_server_add_request.sli_ip_address2.secondary_dns_ipv6,
//             address->secondary_server_address->ip.v6.bytes,
//             SL_IPV6_ADDRESS_LENGTH);
//     }

//     status = sli_si91x_driver_send_command(SLI_WLAN_REQ_DNS_SERVER_ADD,
//                                             SLI_SI91X_NETWORK_CMD,
//                                             &dns_server_add_request,
//                                             sizeof(dns_server_add_request),
//                                             SLI_SI91X_WAIT_FOR_COMMAND_SUCCESS,
//                                             NULL,
//                                             NULL);

//     return status;
// }

/// @brief WiFi network interface external control interface
/// @param if_name Network interface name
/// @param cmd Command
/// @param param Parameter
/// @return Error code
int sl_net_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param) 
{
    int ret = SL_STATUS_NOT_FOUND;
    netif_state_t if_state = NETIF_STATE_DEINIT;
    if (sl_net_thread_ID == NULL) return SL_STATUS_INVALID_STATE;
    if (sl_net_is_recovering == 1) return SL_STATUS_BUSY;

    osMutexAcquire(sl_net_mutex, osWaitForever);
    if (remote_wakeup_mode == WAKEUP_MODE_NORMAL || cmd == NETIF_CMD_INFO || cmd == NETIF_CMD_STATE) {
        switch (cmd) {
            case NETIF_CMD_CFG:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) ret = sl_net_client_netif_config((netif_config_t *)param);
                else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) ret = sl_net_ap_netif_config((netif_config_t *)param);
                break;
            case NETIF_CMD_INIT:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) ret = sl_net_client_netif_init();
                else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) ret = sl_net_ap_netif_init();
                break;
            case NETIF_CMD_UP:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) ret = sl_net_client_netif_up_ex();
                else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) ret = sl_net_ap_netif_up();
                break;
            case NETIF_CMD_INFO:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) ret = sl_net_client_netif_info((netif_info_t *)param);
                else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) ret = sl_net_ap_netif_info((netif_info_t *)param);
                break;
            case NETIF_CMD_DOWN:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) ret = sl_net_client_netif_down();
                else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) ret = sl_net_ap_netif_down();
                break;
            case NETIF_CMD_UNINIT:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
                    sl_net_client_netif_deinit();
                    ret = SL_STATUS_OK;
                } else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
                    sl_net_ap_netif_deinit();
                    ret = SL_STATUS_OK;
                }
                break;
            case NETIF_CMD_STATE:
                if (param == NULL) ret = SL_STATUS_INVALID_PARAMETER;
                else {
                    ret = SL_STATUS_OK;
                    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) *((netif_state_t *)param) = sl_net_client_netif_state();
                    else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) *((netif_state_t *)param) = sl_net_ap_netif_state();
                    else ret = SL_STATUS_NOT_FOUND;
                }
                break;
            case NETIF_CMD_CFG_EX:
                if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) {
                    if_state = sl_net_client_netif_state();
                    if (if_state == NETIF_STATE_UP) {
                        ret = sl_net_client_netif_down();
                        if (ret) break;
                    }

                    ret = sl_net_client_netif_config((netif_config_t *)param);
                    if (ret) break;

                    if (if_state == NETIF_STATE_UP) ret = sl_net_client_netif_up_ex();
                } else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
                    if_state = sl_net_ap_netif_state();
                    if (if_state == NETIF_STATE_UP) {
                        ret = sl_net_ap_netif_down();
                        if (ret) break; 
                    }

                    ret = sl_net_ap_netif_config((netif_config_t *)param);
                    if (ret) break;

                    if (if_state == NETIF_STATE_UP) ret = sl_net_ap_netif_up();
                }
                break;
            default:
                break;
        }
    } else ret = SL_STATUS_NOT_SUPPORTED;
    
    osMutexRelease(sl_net_mutex);
    // if (ret == SL_STATUS_TIMEOUT) {
    // 	sli_firmware_error_callback(ret);
    // } else 
    // if (ret == SL_STATUS_OK && wifi_storage_scan_result.scan_count == 0) {
    //     sl_net_update_strorage_scan_result(3000);
    // }
    // If the firmware is not present and never updated, enter the update mode
    if (ret == SL_STATUS_VALID_FIRMWARE_NOT_PRESENT && get_wifi_update_times() < 3) {
        wifi_enter_update_mode();
    }
    return ret;
}

