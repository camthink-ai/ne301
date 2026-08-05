#ifndef __MM_HALOW_NETIF_H__
#define __MM_HALOW_NETIF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "netif_manager.h"

/** Country code buffer size, matches @ref NETIF_HALOW_COUNTRY_CODE_LEN. */
#define MM_HALOW_REGDOMAIN_CC_LEN           NETIF_HALOW_COUNTRY_CODE_LEN

/** HaLow DPP (Wi-Fi Easy Connect) completion events. */
typedef enum {
    MM_HALOW_DPP_EVT_SUCCESS = 0,
    MM_HALOW_DPP_EVT_FAILED,
    MM_HALOW_DPP_EVT_SESSION_OVERLAP,
    MM_HALOW_DPP_EVT_TIMEOUT,
    MM_HALOW_DPP_EVT_STOPPED,
} mm_halow_dpp_evt_t;

/** Payload passed to @ref mm_halow_dpp_callback_t (valid only for the duration of the call). */
typedef struct {
    mm_halow_dpp_evt_t event;
    /** Set on @ref MM_HALOW_DPP_EVT_SUCCESS; points at saved netif credentials. */
    const char *ssid;
    /** @ref WIRELESS_OPEN or @ref WIRELESS_SAE on success. */
    uint8_t security;
} mm_halow_dpp_evt_info_t;

typedef void (*mm_halow_dpp_callback_t)(const mm_halow_dpp_evt_info_t *info, void *user_arg);

#if NETIF_WIFI_HALOW_IS_ENABLE

int mm_halow_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param);
struct netif *mm_halow_netif_ptr(void);
netif_state_t mm_halow_netif_state(void);

int mm_halow_start_scan(wireless_scan_callback_t callback);
wireless_scan_result_t *mm_halow_get_storage_scan_result(void);
int mm_halow_update_storage_scan_result(uint32_t timeout_ms);
/** Abort any in-progress scan and wait for idle (up to @p wait_ms). */
int mm_halow_ensure_scan_idle(uint32_t wait_ms);
/** Return 1 if a foreground @c mmwlan_scan_request scan is in progress. */
int mm_halow_is_scan_in_progress(void);

/**
 * Select a scanned AP for quick join (sets @c wireless_cfg.bssid).
 * Requires a prior scan that captured S1G Operation IE for @p bssid (within 60s).
 * @return 0 on success, -1 if @p bssid is not in the quick-join cache.
 */
int mm_halow_set_preconnect_target(const uint8_t bssid[6]);

int mm_halow_set_regdomain(const char *country_code);

unsigned mm_halow_regdomain_count(void);
/** 1 if @p country_code is in mmregdb and the embedded firmware BCF. */
int mm_halow_regdomain_is_supported(const char *country_code);
int mm_halow_regdomain_get_code(unsigned index, char *country_code, size_t len);
/** Print regdomains present in firmware BCF (no HaLow init required). */
int mm_halow_list_regdomains(void);
int mm_halow_set_tx_power(uint16_t tx_power_dbm);
/** Max EIRP (dBm) for @p country_code in mmregdb; 0 if lookup fails. */
int mm_halow_get_regdomain_max_tx_dbm(const char *country_code);
/**
 * Override HaLow TX rate control (mmwlan_ate_override_rate_control).
 * @param mcs MCS 0..9, or -1 to use rate control default.
 * @param bw_mhz Bandwidth 1/2/4/8, or -1 for default.
 * @param gi Guard interval: 0 short, 1 long, or -1 for default.
 */
int mm_halow_set_rate_override(int8_t mcs, int8_t bw_mhz, int8_t gi);
int mm_halow_print_rate_override(void);
int mm_halow_set_power_save(uint8_t enable);
int mm_halow_set_scan_config(uint32_t dwell_ms, uint8_t ndp_probe_enabled);
int mm_halow_print_version(void);
/**
 * Print current BCF metadata.
 *
 * @param country_code Optional regdomain code. If provided, selects the matching BCF first
 *                     and then prints metadata. If NULL, uses the current HaLow config country code.
 */
int mm_halow_print_bcf_info(const char *country_code);

/**
 * Apply @c halow_netif_cfg IP settings at runtime (DHCP or static).
 * @return 0 on success, -1 if stack not ready or mmipal rejected the change.
 */
int mm_halow_apply_ip_config(void);

/**
 * Start DPP push-button provisioning (non-blocking).
 * @p timeout_ms 0 uses @ref HALOW_DPP_DEFAULT_TIMEOUT_MS.
 * @p cb          Optional; invoked on success, failure, overlap, timeout, or stop.
 * @return 0 if started, -1 on precondition or start failure (no callback).
 */
int mm_halow_dpp_start(uint32_t timeout_ms, mm_halow_dpp_callback_t cb, void *user_arg);
/** Stop an in-progress DPP session; invokes @p cb with @ref MM_HALOW_DPP_EVT_STOPPED if active. */
int mm_halow_dpp_stop(void);
uint8_t mm_halow_dpp_is_active(void);

#else  /* !NETIF_WIFI_HALOW_IS_ENABLE — stubs that return error/unavailable */

static inline int mm_halow_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{ (void)if_name; (void)cmd; (void)param; return -1; }

static inline struct netif *mm_halow_netif_ptr(void) { return NULL; }
static inline netif_state_t mm_halow_netif_state(void) { return NETIF_STATE_DEINIT; }

static inline int mm_halow_start_scan(wireless_scan_callback_t callback)
{ (void)callback; return -1; }

static inline wireless_scan_result_t *mm_halow_get_storage_scan_result(void) { return NULL; }

static inline int mm_halow_update_storage_scan_result(uint32_t timeout_ms)
{ (void)timeout_ms; return -1; }

static inline int mm_halow_ensure_scan_idle(uint32_t wait_ms)
{ (void)wait_ms; return -1; }

static inline int mm_halow_is_scan_in_progress(void) { return 0; }

static inline int mm_halow_set_preconnect_target(const uint8_t bssid[6])
{ (void)bssid; return -1; }

static inline int mm_halow_set_regdomain(const char *country_code)
{ (void)country_code; return -1; }

static inline unsigned mm_halow_regdomain_count(void) { return 0; }

static inline int mm_halow_regdomain_is_supported(const char *country_code)
{ (void)country_code; return 0; }

static inline int mm_halow_regdomain_get_code(unsigned index, char *country_code, size_t len)
{ (void)index; (void)country_code; (void)len; return -1; }

static inline int mm_halow_list_regdomains(void) { return -1; }

static inline int mm_halow_set_tx_power(uint16_t tx_power_dbm)
{ (void)tx_power_dbm; return -1; }

static inline int mm_halow_get_regdomain_max_tx_dbm(const char *country_code)
{ (void)country_code; return 0; }

static inline int mm_halow_set_rate_override(int8_t mcs, int8_t bw_mhz, int8_t gi)
{ (void)mcs; (void)bw_mhz; (void)gi; return -1; }

static inline int mm_halow_print_rate_override(void) { return -1; }

static inline int mm_halow_set_power_save(uint8_t enable)
{ (void)enable; return -1; }

static inline int mm_halow_set_scan_config(uint32_t dwell_ms, uint8_t ndp_probe_enabled)
{ (void)dwell_ms; (void)ndp_probe_enabled; return -1; }

static inline int mm_halow_print_version(void) { return -1; }

static inline int mm_halow_print_bcf_info(const char *country_code)
{ (void)country_code; return -1; }

static inline int mm_halow_apply_ip_config(void) { return -1; }

static inline int mm_halow_dpp_start(uint32_t timeout_ms, mm_halow_dpp_callback_t cb, void *user_arg)
{ (void)timeout_ms; (void)cb; (void)user_arg; return -1; }

static inline int mm_halow_dpp_stop(void) { return -1; }

static inline uint8_t mm_halow_dpp_is_active(void) { return 0; }

#endif /* NETIF_WIFI_HALOW_IS_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* __MM_HALOW_NETIF_H__ */
