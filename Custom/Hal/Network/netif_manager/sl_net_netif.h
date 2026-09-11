#ifndef __SL_NET_NETIF_H__
#define __SL_NET_NETIF_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "netif_manager.h"
#include "sl_status.h"  /* SL_STATUS_* codes returned by the region helpers below */

/**************************************** WiFi Network Interface Related Interfaces *******************************************/
int sl_net_client_netif_init(void);
int sl_net_client_netif_up(void);
int sl_net_client_netif_down(void);
void sl_net_client_netif_deinit(void);
int sl_net_client_netif_config(netif_config_t *netif_cfg);
int sl_net_client_netif_info(netif_info_t *netif_info);
netif_state_t sl_net_client_netif_state(void);
struct netif *sl_net_client_netif_ptr(void);

int sl_net_ap_netif_init(void);
int sl_net_ap_netif_up(void);
int sl_net_ap_netif_down(void);
void sl_net_ap_netif_deinit(void);
int sl_net_ap_netif_config(netif_config_t *netif_cfg);
int sl_net_ap_netif_info(netif_info_t *netif_info);
netif_state_t sl_net_ap_netif_state(void);
struct netif *sl_net_ap_netif_ptr(void);

typedef enum {
    WAKEUP_MODE_NORMAL = 0,
    WAKEUP_MODE_WIFI = 1,
    WAKEUP_MODE_BLE = 2,
    WAKEUP_MODE_MAX,
} sl_net_wakeup_mode_t;

int sl_net_netif_init(void);
/// @brief Skip the fw < 2.16.5 DHCP-server compatibility check for STA-only
///        boots (fast wakeup path). Call before netif init; the check still
///        runs before the AP is initialized. No-op in non-self-DHCP builds.
void sl_net_netif_skip_fw_dhcps_compat_check(void);
sl_net_wakeup_mode_t sl_net_netif_get_wakeup_mode(void);
int sl_net_netif_romote_wakeup_mode_ctrl(sl_net_wakeup_mode_t wakeup_mode);
int sl_net_netif_filter_broadcast_ctrl(uint8_t enable);
/// @brief Low-power-mode tuning knobs for connected-sleep power testing. When
///        cfg is NULL the function keeps the SDK's existing defaults. NOTE:
///        listen_interval is intentionally omitted — it only takes effect when
///        SL_WIFI_JOIN_FEAT_PS_CMD_LISTEN_INTERVAL_VALID is set at join time, so
///        it is not a runtime knob. Use num_of_dtim_skip to stretch sleep instead.
typedef struct {
    uint8_t  profile;                  ///< 0 = LOW_LATENCY / FAST PSP (default), 1 = ASSOCIATED_POWER_SAVE / MAX PSP.
    uint8_t  num_of_dtim_skip;         ///< DTIM beacons to skip between wakes (0 = none).
    uint16_t monitor_interval;         ///< Monitor interval, ms; LOW_LATENCY only (0 = SDK default 50).
    uint8_t  beacon_miss_ignore_limit; ///< Missed beacons tolerated in sleep (1-10; 0 = default 1).
} sl_net_lpwr_config_t;

int sl_net_netif_low_power_mode_ctrl(uint8_t enable, const sl_net_lpwr_config_t *cfg);

/// @brief TWT (Target Wake Time) auto-selection config. Only the throughput /
///        latency hints below are effective; the SDK derives the wake interval
///        and service-period duration from them. Mirrors the v2 SDK struct.
typedef struct {
    uint16_t average_tx_throughput; ///< Expected avg Tx throughput, Kbps (0-10000 = 0-10 Mbps).
    uint32_t tx_latency;            ///< Allowed Tx latency, ms (0 = same as rx_latency; else 200 ms - 6 h).
    uint32_t rx_latency;            ///< Max RX latency, ms (0 = SDK default 2 s; else 2 s - 6 h).
} sl_net_twt_config_t;

/// @brief TWT call mode. ASYNC returns as soon as the firmware accepts the config
///        command (the real AP-negotiation outcome arrives later via the TWT
///        response callback); SYNC blocks until that callback fires (or timeout).
typedef enum {
    SL_NET_TWT_ASYNC = 0,
    SL_NET_TWT_SYNC  = 1,
} sl_net_twt_mode_t;

/// @brief Enable/disable TWT. enable=1 sets up a session using cfg (SDK defaults
///        when NULL); enable=0 tears down. mode=SYNC blocks up to timeout_ms for
///        the real AP-negotiation result instead of the meaningless
///        command-accepted status; mode=ASYNC returns immediately. Only valid in
///        remote wakeup mode (SL_STATUS_INVALID_STATE in WAKEUP_MODE_NORMAL);
///        requires an active Wi-Fi client connection.
int sl_net_netif_twt_ctrl(uint8_t enable, const sl_net_twt_config_t *cfg,
                          sl_net_twt_mode_t mode, uint32_t timeout_ms);

int sl_net_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param);
int sl_net_start_scan(wireless_scan_callback_t callback);
wireless_scan_result_t *sl_net_get_strorage_scan_result(void);
int sl_net_update_strorage_scan_result(uint32_t timeout_ms);

/**************************************** WiFi Region (Country) Code *******************************************/
/// @brief Configure WiFi region code. Only effective when both WiFi netifs are DEINIT
///        (applied at the next init). Strings: "us","eu","jp","world","kr","cn".
int sl_net_wifi_set_region_code(const char *country_code);
/// @brief Get the currently active WiFi region string.
int sl_net_wifi_get_region_code(char *buf, size_t len);
uint32_t sl_net_wifi_get_supported_region_count(void);
int sl_net_wifi_get_region_code_by_index(uint32_t idx, char *buf, size_t len);

/************************************** NWP Debug (RAM Dump) **********************************************/
/// @brief Trigger an NWP RAM dump. The content streams out of the Si91x NWP UART pin
///        (not the bus): capture with Docklight at 460800 8N1 saving HEX; a valid dump
///        shows the repeating pattern 00 04 04 04 00 04 04 04. Requires a build with
///        IS_ENABLE_NWP_DEBUG_PRINTS 1 in sl_net_netif.c (debug UART routed at init).
/// @param address Start address — offset from NWP RAM base (0 = RAM start); absolute
///        global-map addresses in [0x22000000, +672K) are accepted and converted
///        (fw 2.16.5 NACKs raw absolute addresses with 0x1003E)
/// @param length  Bytes to dump (0 = full NWP RAM, 672 KB)
/// @return SL_STATUS_OK when triggered, SL_STATUS_NOT_SUPPORTED in non-debug builds
int sl_net_nwp_ram_dump(uint32_t address, uint32_t length);
/// @brief Read the program counters of NWP firmware threads 0~3 (optionally all 16
///        registers, R15 = SP). Each 4-byte little-endian value streams out of the
///        NWP UART port in call order — the console only maps addresses to chunks.
///        Thread PC/register addresses (0x22000420 / 0x22000440 + 0x80 * n) are
///        converted to NWP RAM offsets internally (the fw validates them as offsets).
/// @param with_regs 0 = PC only, 1 = PC + R0~R15
/// @return SL_STATUS_OK, SL_STATUS_NOT_SUPPORTED in non-debug builds
int sl_net_nwp_print_thread_pc(uint8_t with_regs);

void sli_firmware_error_callback(int error_code);

/// @brief Real bus-write result for LWIP/transceiver RAW data packets, called by
///        the SDK DATA-packet TX status handler (hal thread) with the outcome of
///        sli_hal_si91x_send_packet_to_bus(). Feeds the consecutive bus-write
///        failure detector in sl_net_netif.c (reset by any real write success,
///        no time window — must not starve on idle traffic); non-OK means the
///        chip never received the frame (wakeup failed / BUS_WRITE_ERROR).
void sl_net_notify_bus_tx_failure(sl_status_t status);
/***************************************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* __SL_NET_NETIF_H__ */