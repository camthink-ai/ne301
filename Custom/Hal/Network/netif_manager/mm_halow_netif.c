#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "mm_halow_netif.h"
#include "chip_id_mac.h"
#include "halow_platform_mac.h"

#if NETIF_WIFI_HALOW_IS_ENABLE

#include "stm32n6xx_hal.h"
#include "main.h"
#include "mem.h"
#include "Log/debug.h"
#include "pwr.h"
#include "cmsis_os2.h"
#include "mmhal.h"
#include "mmhal_wlan.h"
#include "mmwlan.h"
#include "mmipal.h"
#include "mmregdb.h"
#include "mbin.h"
#include "mmosal.h"
#include "mmutils.h"
#include "os.h"
#include "eloop.h"
#include "lwip/netif.h"
#include "mm_mbedtls_port.h"

#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
#include "spi.h"
#endif

struct netif *mmipal_get_lwip_netif(void);

/* Morse: set STA MAC after boot, before sta_enable (libmorse.a). */
enum mmwlan_status mmwlan_sta_set_mac_addr(const uint8_t *mac_addr);

#define HALOW_SCAN_RESULT_MAX           (64)
/** S1G Operation IE size (EID + len + 5 payload bytes). */
#define HALOW_S1G_OPERATION_IE_LEN      (MMWLAN_PRECONNECT_S1G_OP_IE_LEN)
/** Max age of scan-derived quick-join cache (ms). */
#define HALOW_PRECONNECT_TTL_MS         (60000U)
/** IEEE 802.11ah S1G Operation element ID. */
#define HALOW_DOT11_IE_S1G_OPERATION    (232U)
/** Cat1/HaLow rail settle after sleep power-on (PWR_HALOW shares PWR_CAT1). */
#define HALOW_PWR_SETTLE_MS             100U
#define HALOW_INIT_RETRY_DELAY_MS       10U
#define HALOW_INIT_POWER_CYCLE_MS       50U
#define HALOW_STACK_INIT_MAX_ATTEMPTS   3U
#define HALOW_LINK_WAIT_MS              (30000U)
#define HALOW_DPP_DEFAULT_TIMEOUT_MS    (120000U)

static netif_config_t halow_netif_cfg = {
    .wireless_cfg = {
        .ssid = NETIF_WIFI_HALOW_DEFAULT_SSID,
        .pw = NETIF_WIFI_HALOW_DEFAULT_PW,
        .security = WIRELESS_OPEN,
        .encryption = WIRELESS_DEFAULT_ENCRYPTION,
        .channel = 0,
    },
#if NETIF_WIFI_HALOW_IS_ENABLE
    .halow_cfg = {
        .country_code = NETIF_WIFI_HALOW_DEFAULT_COUNTRY,
        .tx_power_dbm = NETIF_WIFI_HALOW_DEFAULT_TX_PWR,
        .ps_mode = 0,
        .pmf_mode = 0,
        .scan_dwell_ms = NETIF_WIFI_HALOW_DEFAULT_SCAN_DWELL,
        .ndp_probe_enabled = 0,
        .bgscan_short_interval_s = 0,
        .bgscan_signal_threshold_dbm = 0,
        .bgscan_long_interval_s = 0,
        .rc_mcs = -1,
        .rc_bw_mhz = -1,
        .rc_gi = -1,
    },
#endif
    .ip_mode = NETIF_WIFI_HALOW_DEFAULT_IP_MODE,
    .ip_addr = NETIF_WIFI_HALOW_DEFAULT_IP,
    .netmask = NETIF_WIFI_HALOW_DEFAULT_MASK,
    .gw = NETIF_WIFI_HALOW_DEFAULT_GW,
};

static netif_state_t halow_state = NETIF_STATE_DEINIT;
static uint8_t halow_stack_ready = 0;
/** Set after mmwlan_boot(); cleared by shutdown so regdomain can be changed while DOWN. */
static uint8_t halow_mmwlan_booted = 0;
/** Set after mmwlan_init(); cleared by mmwlan_deinit(). */
static uint8_t halow_mmwlan_inited = 0;
static osMutexId_t halow_mutex = NULL;
static struct mmosal_semb *halow_link_sem = NULL;

static wireless_scan_result_t halow_storage_scan_result = {0};
static osSemaphoreId_t halow_scan_sem = NULL;
static wireless_scan_callback_t halow_scan_user_cb = NULL;
static uint8_t halow_scan_in_progress = 0;
static wireless_scan_info_t *halow_async_scan_buf = NULL;
static wireless_scan_result_t halow_async_scan_result = {0};
static enum mmwlan_scan_state halow_last_scan_state = MMWLAN_SCAN_SUCCESSFUL;
static PowerHandle halow_pwr_handle = 0;

static uint8_t halow_pwr_acquired = 0;

typedef struct {
    uint8_t  valid;
    uint8_t  bssid[6];
    uint16_t beacon_interval;
    uint8_t  s1g_operation_ie[HALOW_S1G_OPERATION_IE_LEN];
    uint32_t channel_freq_hz;
    int16_t  rssi;
    uint16_t probe_ies_len;
    uint8_t  probe_ies[MMWLAN_PRECONNECT_PROBE_IES_MAX];
    uint32_t cached_ms;
} halow_preconnect_entry_t;

static halow_preconnect_entry_t halow_preconnect_cache[HALOW_SCAN_RESULT_MAX];
static uint8_t halow_preconnect_cache_count = 0;

static uint8_t halow_preconnect_cache_valid_count(uint32_t now_ms)
{
    uint8_t n = 0;

    for (uint8_t i = 0; i < halow_preconnect_cache_count; i++) {
        halow_preconnect_entry_t *e = &halow_preconnect_cache[i];
        if (!e->valid) {
            continue;
        }
        if ((now_ms - e->cached_ms) > HALOW_PRECONNECT_TTL_MS) {
            e->valid = 0;
            continue;
        }
        n++;
    }
    return n;
}

static void halow_preconnect_cache_clear(void)
{
    memset(halow_preconnect_cache, 0, sizeof(halow_preconnect_cache));
    halow_preconnect_cache_count = 0;
}

static int halow_extract_s1g_operation_ie(const uint8_t *ies, uint16_t ies_len,
                                          uint8_t out[HALOW_S1G_OPERATION_IE_LEN])
{
    const uint8_t *p = ies;
    size_t left = ies_len;

    if (ies == NULL || out == NULL) {
        return -1;
    }

    while (left >= 2U) {
        uint8_t eid = p[0];
        uint8_t elen = p[1];

        p += 2U;
        left -= 2U;
        if ((size_t)elen > left) {
            break;
        }
        if (eid == HALOW_DOT11_IE_S1G_OPERATION && elen == 5U) {
            out[0] = eid;
            out[1] = elen;
            memcpy(out + 2U, p, elen);
            return 0;
        }
        p += elen;
        left -= elen;
    }

    return -1;
}

static halow_preconnect_entry_t *halow_preconnect_find_entry(const uint8_t bssid[6], uint32_t now_ms)
{
    for (uint8_t i = 0; i < halow_preconnect_cache_count; i++) {
        halow_preconnect_entry_t *e = &halow_preconnect_cache[i];
        if (!e->valid) {
            continue;
        }
        if ((now_ms - e->cached_ms) > HALOW_PRECONNECT_TTL_MS) {
            e->valid = 0;
            continue;
        }
        if (memcmp(e->bssid, bssid, 6) == 0) {
            return e;
        }
    }
    return NULL;
}

static void halow_preconnect_cache_store(const struct mmwlan_scan_result *result)
{
    uint8_t s1g_ie[HALOW_S1G_OPERATION_IE_LEN];
    halow_preconnect_entry_t *slot;
    uint32_t now_ms;

    if (result == NULL || result->bssid == NULL) {
        return;
    }
    if (result->s1g_operation_ie_valid) {
        memcpy(s1g_ie, result->s1g_operation_ie, sizeof(s1g_ie));
    } else if (result->ies == NULL || result->ies_len == 0U) {
        return;
    } else if (halow_extract_s1g_operation_ie(result->ies, result->ies_len, s1g_ie) != 0) {
        return;
    }

    now_ms = HAL_GetTick();
    slot = halow_preconnect_find_entry(result->bssid, now_ms);
    if (slot == NULL) {
        if (halow_preconnect_cache_count >= HALOW_SCAN_RESULT_MAX) {
            return;
        }
        slot = &halow_preconnect_cache[halow_preconnect_cache_count++];
    }

    slot->valid = 1;
    memcpy(slot->bssid, result->bssid, 6);
    slot->beacon_interval = result->beacon_interval;
    memcpy(slot->s1g_operation_ie, s1g_ie, sizeof(s1g_ie));
    slot->channel_freq_hz = result->channel_freq_hz;
    slot->rssi = result->rssi;
    slot->probe_ies_len = 0U;
    if (result->ies != NULL && result->ies_len > 0U) {
        uint16_t copy_len = result->ies_len;
        if (copy_len > MMWLAN_PRECONNECT_PROBE_IES_MAX) {
            copy_len = MMWLAN_PRECONNECT_PROBE_IES_MAX;
        }
        memcpy(slot->probe_ies, result->ies, copy_len);
        slot->probe_ies_len = copy_len;
    }
    slot->cached_ms = now_ms;
}

static void halow_preconnect_scan_rx_cb(const struct mmwlan_scan_result *result, void *arg)
{
    (void)arg;
    halow_preconnect_cache_store(result);
}

static int halow_preconnect_fill_sta_args(struct mmwlan_sta_args *sta_args)
{
    halow_preconnect_entry_t *entry;
    uint32_t now_ms;
    uint8_t cached_count;

    if (sta_args == NULL) {
        return 0;
    }
    if (!NETIF_MAC_IS_UNICAST(sta_args->bssid)) {
        return 0;
    }

    now_ms = HAL_GetTick();
    cached_count = halow_preconnect_cache_valid_count(now_ms);
    if (cached_count == 0U) {
        return 0;
    }

    entry = halow_preconnect_find_entry(sta_args->bssid, now_ms);
    if (entry == NULL) {
        return 0;
    }

    sta_args->preconnect_bss_valid = true;
    sta_args->preconnect_beacon_interval = entry->beacon_interval;
    memcpy(sta_args->preconnect_s1g_operation_ie,
           entry->s1g_operation_ie,
           sizeof(sta_args->preconnect_s1g_operation_ie));
    sta_args->preconnect_channel_freq_hz = entry->channel_freq_hz;
    sta_args->preconnect_rssi = entry->rssi;
    sta_args->preconnect_probe_ies_len = entry->probe_ies_len;
    if (entry->probe_ies_len > 0U) {
        memcpy(sta_args->preconnect_probe_ies,
               entry->probe_ies,
               entry->probe_ies_len);
    }
    return 1;
}

static void halow_resolve_sta_mac(uint8_t mac[6])
{
    if (NETIF_MAC_IS_UNICAST(halow_netif_cfg.diy_mac)) {
        memcpy(mac, halow_netif_cfg.diy_mac, 6U);
    } else {
        netif_chip_id_get_mac(mac, NETIF_CHIP_MAC_HALOW);
    }
}

static void halow_apply_sta_mac_policy(void)
{
    uint8_t mac[6];

    halow_resolve_sta_mac(mac);
    mmhal_wlan_use_platform_mac(mac);
}

static int halow_apply_halow_hw_config_locked(void);
static int halow_apply_rate_override_locked(void);
static int halow_apply_regdomain_locked(const char *country_code);
static int halow_regdomain_supported(const char *country_code);
static int halow_pick_fallback_regdomain(char *out, size_t out_len);
static int halow_mmwlan_boot_locked(void);
/** Last regdomain successfully applied to mmwlan (empty after teardown/deinit). */
static char halow_active_country_code[MM_HALOW_REGDOMAIN_CC_LEN] = "";

static void halow_normalize_country_code(const char *in, char *out, size_t out_len)
{
    size_t i;

    if (out == NULL || out_len == 0U) {
        return;
    }
    out[0] = '\0';
    if (in == NULL || in[0] == '\0') {
        return;
    }
    for (i = 0; in[i] != '\0' && i < out_len - 1U; i++) {
        out[i] = (char)toupper((unsigned char)in[i]);
    }
    out[i] = '\0';
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

static void halow_clear_active_country(void)
{
    halow_active_country_code[0] = '\0';
}
/* Select which embedded BCF the driver will use at next boot (mmhal_wlan_binaries.c). */
extern void mmhal_wlan_select_bcf_for_country(const char *country_code);
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
static void halow_dpp_timeout_eloop(void *eloop_ctx, void *timeout_ctx);
static void halow_dpp_cancel_timeout_timer(void);
static void halow_dpp_ensure_umac_idle_cli(void);
static void halow_dpp_finish(mm_halow_dpp_evt_t event);
#endif

static volatile uint8_t halow_dpp_active = 0;
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
static mm_halow_dpp_callback_t halow_dpp_user_cb = NULL;
static void *halow_dpp_user_arg = NULL;
static volatile uint8_t halow_dpp_deferred_cb_pending = 0;
static mm_halow_dpp_evt_t halow_dpp_deferred_evt;
#endif

/** @return 1 if rejected (DPP active); caller holds halow_mutex and should return -1. */
static int halow_dpp_reject_if_active_locked(void)
{
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
    if (halow_dpp_active) {
        LOG_DRV_ERROR("HaLow: not supported during DPP (ifconfig hw dpp_stop first)");
        return 1;
    }
#endif
    return 0;
}

static void halow_try_set_sta_mac_runtime(const uint8_t mac[6])
{
    uint8_t cur[6];
    enum mmwlan_status status;

    status = mmwlan_sta_set_mac_addr(mac);
    if (status == MMWLAN_SUCCESS) {
        return;
    }
    if (mmwlan_get_vif_mac_addr(MMWLAN_VIF_STA, cur) == MMWLAN_SUCCESS &&
        memcmp(cur, mac, 6) != 0) {
        LOG_DRV_WARN("HaLow STA MAC differs from cfg; down+up or deinit to apply");
    }
}

static int halow_power_acquire(void)
{
    if (halow_pwr_acquired) {
        return 0;
    }

    if (halow_pwr_handle == 0) {
        halow_pwr_handle = pwr_manager_get_handle(PWR_HALOW_NAME);
        if (halow_pwr_handle == 0) {
            LOG_DRV_ERROR("HaLow power handle not found (%s)", PWR_HALOW_NAME);
            return -1;
        }
    }

    if (pwr_manager_acquire(halow_pwr_handle) != 0) {
        LOG_DRV_ERROR("HaLow power acquire failed");
        return -1;
    }

    halow_pwr_acquired = 1;
    osDelay(HALOW_PWR_SETTLE_MS);
    return 0;
}

static void halow_power_release(void)
{
    if (!halow_pwr_acquired) {
        return;
    }

    pwr_manager_release(halow_pwr_handle);
    halow_pwr_acquired = 0;
}

static void mm_halow_gpios_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = MM_HALOW_RESET_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MM_HALOW_RESET_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = MM_HALOW_WAKE_Pin;
    HAL_GPIO_Init(MM_HALOW_WAKE_GPIO_Port, &GPIO_InitStruct);
    /*
     * Keep WAKE deasserted initially. The driver will assert it when required and
     * some HW/firmware combinations expect a low->high transition.
     */
    HAL_GPIO_WritePin(MM_HALOW_WAKE_GPIO_Port, MM_HALOW_WAKE_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = MM_HALOW_SPI_IRQ_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MM_HALOW_SPI_IRQ_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(MM_HALOW_SPI_IRQ_GPIO_Port, MM_HALOW_SPI_IRQ_Pin, GPIO_PIN_SET);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    GPIO_InitStruct.Pin = MM_HALOW_BUSY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(MM_HALOW_BUSY_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(MM_HALOW_BUSY_GPIO_Port, MM_HALOW_BUSY_Pin, GPIO_PIN_RESET);
    HAL_NVIC_SetPriority(EXTI15_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI15_IRQn);
}

static void mm_halow_gpios_deinit(void)
{
    HAL_GPIO_DeInit(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin);
    HAL_GPIO_DeInit(MM_HALOW_WAKE_GPIO_Port, MM_HALOW_WAKE_Pin);
    HAL_GPIO_DeInit(MM_HALOW_SPI_IRQ_GPIO_Port, MM_HALOW_SPI_IRQ_Pin);
    HAL_GPIO_DeInit(MM_HALOW_BUSY_GPIO_Port, MM_HALOW_BUSY_Pin);

    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
    HAL_NVIC_DisableIRQ(EXTI15_IRQn);
}

static void mm_halow_link_status_callback(const struct mmipal_link_status *link_status)
{
    if (link_status == NULL) {
        return;
    }

    if (link_status->link_state == MMIPAL_LINK_UP) {
        halow_state = NETIF_STATE_UP;
        if (halow_link_sem != NULL) {
            mmosal_semb_give(halow_link_sem);
        }
        LOG_DRV_INFO("HaLow link up, IP: %s", link_status->ip_addr);
    } else {
        if (halow_state == NETIF_STATE_UP) {
            halow_state = NETIF_STATE_DOWN;
        }
        LOG_DRV_INFO("HaLow link down");
    }
}

static void mm_halow_sta_status_callback(enum mmwlan_sta_state sta_state)
{
    switch (sta_state) {
    case MMWLAN_STA_DISABLED:
        LOG_DRV_DEBUG("HaLow STA disabled");
        break;
    case MMWLAN_STA_CONNECTING:
        LOG_DRV_DEBUG("HaLow STA connecting");
        break;
    case MMWLAN_STA_CONNECTED:
        LOG_DRV_DEBUG("HaLow STA connected (L2)");
        break;
    default:
        break;
    }
}

static int halow_link_is_up_locked(void)
{
    struct netif *nif;

    if (mmipal_get_link_state() != MMIPAL_LINK_UP) {
        return 0;
    }
    nif = mmipal_get_lwip_netif();
    return (nif != NULL && netif_is_link_up(nif));
}

static void halow_drain_link_sem(void)
{
    if (halow_link_sem == NULL) {
        return;
    }
    while (mmosal_semb_wait(halow_link_sem, 0)) {
    }
}

/** Wait for MMIPAL link-up; skip if already up (avoids stale sem after reconnect). */
static int halow_wait_for_link_locked(void)
{
    if (halow_link_is_up_locked()) {
        return 0;
    }
    halow_drain_link_sem();
    if (halow_link_sem == NULL) {
        return 0;
    }
    if (!mmosal_semb_wait(halow_link_sem, HALOW_LINK_WAIT_MS)) {
        return -1;
    }
    return halow_link_is_up_locked() ? 0 : -1;
}

static enum mmwlan_security_type halow_map_security(wireless_security_t security)
{
    switch (security) {
    case WIRELESS_OPEN:
        return MMWLAN_OPEN;
    case WIRELESS_OWE:
        return MMWLAN_OWE;
    case WIRELESS_SAE:
    case WIRELESS_WPA3:
        return MMWLAN_SAE;
    default:
        return MMWLAN_OPEN;
    }
}

static int halow_ip_u8_to_str(const uint8_t *ip, char *buf, size_t len)
{
    if (ip == NULL || buf == NULL || len < 16) {
        return -1;
    }
    snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return 0;
}

/** Stop scan/DPP/STA before stack deinit (not part of init; operational cleanup). */
static void halow_abort_operations_locked(void)
{
    if (halow_scan_in_progress) {
        (void)mmwlan_scan_abort();
        halow_scan_in_progress = 0;
    }

    if (halow_state == NETIF_STATE_UP) {
        (void)mmwlan_sta_disable();
        halow_state = NETIF_STATE_DOWN;
    }
}

/** Inverse of halow_mmwlan_boot_locked(). */
static void halow_mmwlan_unboot_locked(void)
{
    if (!halow_mmwlan_booted) {
        return;
    }

    if (mmwlan_shutdown() != MMWLAN_SUCCESS) {
        LOG_DRV_ERROR("HaLow mmwlan_shutdown failed");
    }
    halow_mmwlan_booted = 0;
}

/** Tear down mmHAL/mmwlan after a failed boot attempt (keeps GPIO/SPI/power). */
static void halow_stack_teardown_attempt_locked(void)
{
    if (halow_mmwlan_inited) {
        mmwlan_deinit();
        halow_mmwlan_inited = 0;
    }
    mmhal_wlan_deinit();
    halow_mmwlan_booted = 0;
}

/**
 * Morse/mmHAL bring-up — must match halow_stack_deinit_locked() in reverse order.
 * Caller holds halow_mutex; power and link_sem must already be ready.
 */
static int halow_stack_init_locked(void)
{
    int ret = 0;
    unsigned attempt;

    mm_halow_gpios_init();

#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
    MX_SPI6_Init();
#endif

    for (attempt = 0; attempt < HALOW_STACK_INIT_MAX_ATTEMPTS; attempt++) {
        if (attempt > 0U) {
            LOG_DRV_WARN("HaLow stack init retry %u/%u", attempt + 1U, HALOW_STACK_INIT_MAX_ATTEMPTS);
            halow_stack_teardown_attempt_locked();
            halow_power_release();
            osDelay(HALOW_INIT_POWER_CYCLE_MS);
            if (halow_power_acquire() != 0) {
                return -1;
            }
            osDelay(HALOW_INIT_RETRY_DELAY_MS);
        }

        mmhal_init();
        mmhal_wlan_init();
        mmhal_wlan_hard_reset();

        mmwlan_init();
        halow_mmwlan_inited = 1;

        if (halow_apply_regdomain_locked(halow_netif_cfg.halow_cfg.country_code) != 0) {
            char fallback[MM_HALOW_REGDOMAIN_CC_LEN];

            if (halow_pick_fallback_regdomain(fallback, sizeof(fallback)) != 0) {
                LOG_DRV_ERROR("HaLow: no regdomain in firmware BCF");
                halow_stack_teardown_attempt_locked();
                return -1;
            }
            LOG_DRV_WARN("HaLow regdomain '%s' not in firmware BCF, using %s",
                         halow_netif_cfg.halow_cfg.country_code, fallback);
            if (halow_apply_regdomain_locked(fallback) != 0) {
                halow_stack_teardown_attempt_locked();
                return -1;
            }
        }

        (void)halow_apply_halow_hw_config_locked();

        ret = halow_mmwlan_boot_locked();
        if (ret == 0) {
            halow_stack_ready = 0;
            return 0;
        } 
        if (ret == -333) return -333;

        halow_stack_teardown_attempt_locked();
    }

    LOG_DRV_ERROR("HaLow stack init failed after %u attempts", HALOW_STACK_INIT_MAX_ATTEMPTS);
    return -1;
}

/**
 * Inverse of halow_stack_init_locked() (same order, reversed).
 */
static void halow_stack_deinit_locked(void)
{
    halow_mmwlan_unboot_locked();       /* ↔ halow_mmwlan_boot_locked() */
    /* ↔ halow_apply_halow_hw_config_locked / regdomain: runtime config only */
    if (halow_mmwlan_inited) {
        mmwlan_deinit();                /* ↔ mmwlan_init() */
        halow_mmwlan_inited = 0;
    }
    mmhal_wlan_deinit();                /* ↔ mmhal_wlan_init() */
    /* ↔ mmhal_wlan_hard_reset: not undone */
    /* ↔ mmhal_init: no mmhal_deinit on this platform */
#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
    MX_SPI6_DeInit();                   /* ↔ MX_SPI6_Init() (HAL_SPI_DeInit + MspDeInit) */
#endif
    /* ↔ mm_halow_gpios_init: pins left as-is */
    mm_halow_gpios_deinit();

    halow_stack_ready = 0;
    halow_state = NETIF_STATE_DEINIT;
}

/** Inverse of init-time link_sem / power setup (after halow_stack_deinit_locked). */
static void halow_netif_resources_deinit_locked(void)
{
    if (halow_storage_scan_result.scan_info != NULL) {
        hal_mem_free(halow_storage_scan_result.scan_info);
        halow_storage_scan_result.scan_info = NULL;
    }
    halow_storage_scan_result.scan_count = 0;

    halow_clear_active_country();

    if (halow_link_sem != NULL) {
        mmosal_semb_delete(halow_link_sem);
        halow_link_sem = NULL;
    }

    halow_dpp_active = 0;
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
    halow_dpp_user_cb = NULL;
    halow_dpp_user_arg = NULL;
#endif

    halow_power_release();              /* ↔ halow_power_acquire() in init */
}

/** Stop STA/scan and mmwlan so channel list can be changed (Morse requires inactive UMAC). */
static int halow_mmwlan_teardown_locked(void)
{
    enum mmwlan_status status;

    if (halow_scan_in_progress) {
        (void)mmwlan_scan_abort();
        halow_scan_in_progress = 0;
        halow_last_scan_state = MMWLAN_SCAN_TERMINATED;
        if (halow_scan_sem != NULL) {
            (void)osSemaphoreAcquire(halow_scan_sem, 0);
            (void)osSemaphoreRelease(halow_scan_sem);
        }
    }

    if (halow_state == NETIF_STATE_UP) {
        (void)mmwlan_sta_disable();
        halow_state = NETIF_STATE_DOWN;
    }

    if (!halow_mmwlan_booted) {
        return 0;
    }

    status = mmwlan_shutdown();
    halow_mmwlan_booted = 0;
    halow_clear_active_country();
    if (status != MMWLAN_SUCCESS) {
        LOG_DRV_ERROR("HaLow mmwlan_shutdown failed");
        return -1;
    }
    return 0;
}

static int halow_mmwlan_boot_locked(void)
{
    int ret = 0;
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;

    /* Ensure the HAL BCF callback uses a region-appropriate BCF at next boot. */
    mmhal_wlan_select_bcf_for_country(halow_netif_cfg.halow_cfg.country_code);

    ret = mmwlan_boot(&boot_args);
    if (ret != MMWLAN_SUCCESS) {
        // LOG_DRV_ERROR("HaLow mmwlan_boot failed, ret=%d", ret);
        if (ret == MMWLAN_HW_DEVICE_UNAVAILABLE) return -333;
        return -1;
    }

    (void)mmwlan_set_power_save_mode(halow_netif_cfg.halow_cfg.ps_mode ? MMWLAN_PS_ENABLED : MMWLAN_PS_DISABLED);
    (void)halow_apply_halow_hw_config_locked();
    halow_apply_sta_mac_policy();
    halow_mmwlan_booted = 1;
    (void)halow_apply_rate_override_locked();
    return 0;
}

static int halow_ensure_mmwlan_booted_locked(void)
{
    if (halow_mmwlan_booted) {
        return 0;
    }
    return halow_mmwlan_boot_locked();
}

#if 0 /* legacy: parsing regdomains from embedded BCF */
static void halow_bcf_robuf_cleanup(struct mmhal_robuf *robuf)
{
    if (robuf != NULL && robuf->free_cb != NULL) {
        robuf->free_cb(robuf->free_arg);
    }
    if (robuf != NULL) {
        memset(robuf, 0, sizeof(*robuf));
    }
}

static uint16_t halow_bcf_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static int halow_bcf_read(uint32_t offset, uint32_t len, struct mmhal_robuf *robuf)
{
    memset(robuf, 0, sizeof(*robuf));
    mmhal_wlan_read_bcf_file(offset, len, robuf);
    if (robuf->buf == NULL || robuf->len < MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH) {
        halow_bcf_robuf_cleanup(robuf);
        return -1;
    }
    if (robuf->len > len) {
        robuf->len = len;
    }
    return 0;
}

static int halow_bcf_read_tlv_hdr(uint32_t *offset, struct mbin_tlv_hdr *tlv_hdr)
{
    struct mmhal_robuf robuf = {0};
    const struct mbin_tlv_hdr *hdr;

    if (halow_bcf_read(*offset, sizeof(*tlv_hdr), &robuf) != 0) {
        return -1;
    }
    hdr = (const struct mbin_tlv_hdr *)robuf.buf;
    tlv_hdr->type = halow_bcf_le16((const uint8_t *)&hdr->type);
    tlv_hdr->len = halow_bcf_le16((const uint8_t *)&hdr->len);
    *offset += robuf.len;
    halow_bcf_robuf_cleanup(&robuf);
    return 0;
}

static int halow_bcf_validate_magic(uint32_t *offset)
{
    struct mbin_tlv_hdr tlv_hdr;
    struct mmhal_robuf robuf = {0};
    uint32_t magic;

    if (halow_bcf_read_tlv_hdr(offset, &tlv_hdr) != 0) {
        return -1;
    }
    if (tlv_hdr.type != FIELD_TYPE_MAGIC || tlv_hdr.len != sizeof(magic)) {
        return -1;
    }
    if (halow_bcf_read(*offset, tlv_hdr.len, &robuf) != 0) {
        return -1;
    }
    magic = (uint32_t)robuf.buf[0] | ((uint32_t)robuf.buf[1] << 8U) |
            ((uint32_t)robuf.buf[2] << 16U) | ((uint32_t)robuf.buf[3] << 24U);
    *offset += robuf.len;
    halow_bcf_robuf_cleanup(&robuf);
    return (magic == MBIN_BCF_MAGIC_NUMBER) ? 0 : -1;
}

static int halow_bcf_regdom_add(const char *cc)
{
    unsigned i;

    if (cc == NULL || cc[0] == '\0' || cc[1] == '\0') {
        return -1;
    }
    for (i = 0; i < halow_bcf_regdoms.count; i++) {
        if (halow_bcf_regdoms.cc[i][0] == cc[0] && halow_bcf_regdoms.cc[i][1] == cc[1]) {
            return 0;
        }
    }
    if (halow_bcf_regdoms.count >= HALOW_BCF_REGDOM_MAX) {
        return -1;
    }
    halow_bcf_regdoms.cc[halow_bcf_regdoms.count][0] = cc[0];
    halow_bcf_regdoms.cc[halow_bcf_regdoms.count][1] = cc[1];
    halow_bcf_regdoms.cc[halow_bcf_regdoms.count][2] = '\0';
    halow_bcf_regdoms.count++;
    return 0;
}

/** Parse embedded BCF and collect FIELD_TYPE_BCF_REGDOM country codes. */
static int halow_bcf_load_regdomain_list(void)
{
    uint32_t offset = 0;
    struct mbin_tlv_hdr tlv_hdr;
    int ret = -1;

    if (halow_bcf_regdoms.parsed) {
        return (halow_bcf_regdoms.count > 0) ? 0 : -1;
    }
    halow_bcf_regdoms.parsed = 1;

    if (halow_bcf_validate_magic(&offset) != 0) {
        return -1;
    }
    if (halow_bcf_read_tlv_hdr(&offset, &tlv_hdr) != 0) {
        return -1;
    }
    if (tlv_hdr.type != FIELD_TYPE_BCF_BOARD_CONFIG) {
        return -1;
    }
    offset += tlv_hdr.len;

    while (halow_bcf_read_tlv_hdr(&offset, &tlv_hdr) == 0) {
        if (tlv_hdr.type == FIELD_TYPE_EOF || tlv_hdr.type == FIELD_TYPE_EOF_WITH_SIGNATURE) {
            ret = 0;
            break;
        }
        if (tlv_hdr.type == FIELD_TYPE_BCF_REGDOM) {
            struct mmhal_robuf robuf = {0};
            const struct mbin_regdom_hdr *hdr;
            char cc[MM_HALOW_REGDOMAIN_CC_LEN];

            if (tlv_hdr.len < sizeof(*hdr)) {
                ret = -1;
                break;
            }
            if (halow_bcf_read(offset, sizeof(*hdr), &robuf) != 0) {
                ret = -1;
                break;
            }
            hdr = (const struct mbin_regdom_hdr *)robuf.buf;
            cc[0] = (char)hdr->country_code[0];
            cc[1] = (char)hdr->country_code[1];
            cc[2] = '\0';
            halow_bcf_robuf_cleanup(&robuf);
            (void)halow_bcf_regdom_add(cc);
            offset += tlv_hdr.len;
            continue;
        }
        offset += tlv_hdr.len;
    }

    return (halow_bcf_regdoms.count > 0 && ret == 0) ? 0 : -1;
}

static int halow_bcf_has_regdom(const char *country_code)
{
    unsigned i;

    if (halow_bcf_load_regdomain_list() != 0) {
        return 0;
    }
    if (country_code == NULL || country_code[0] == '\0' || country_code[1] == '\0') {
        return 0;
    }
    for (i = 0; i < halow_bcf_regdoms.count; i++) {
        if (halow_bcf_regdoms.cc[i][0] == country_code[0] &&
            halow_bcf_regdoms.cc[i][1] == country_code[1]) {
            return 1;
        }
    }
    return 0;
}
#endif /* 0 */

static int halow_regdomain_supported(const char *country_code)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    char normalized[MM_HALOW_REGDOMAIN_CC_LEN];

    if (db == NULL || country_code == NULL || country_code[0] == '\0') {
        return 0;
    }
    halow_normalize_country_code(country_code, normalized, sizeof(normalized));
    if (mmwlan_lookup_regulatory_domain(db, normalized) == NULL) {
        return 0;
    }
    return 1;
}

static int halow_pick_fallback_regdomain(char *out, size_t out_len)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    unsigned i;

    if (out == NULL || out_len < MM_HALOW_REGDOMAIN_CC_LEN || db == NULL) {
        return -1;
    }
    if (halow_regdomain_supported(NETIF_WIFI_HALOW_DEFAULT_COUNTRY)) {
        strncpy(out, NETIF_WIFI_HALOW_DEFAULT_COUNTRY, out_len - 1U);
        out[out_len - 1U] = '\0';
        return 0;
    }
    for (i = 0; i < db->num_domains; i++) {
        const struct mmwlan_s1g_channel_list *domain = db->domains[i];

        if (domain == NULL) {
            continue;
        }
        strncpy(out, (const char *)domain->country_code, out_len - 1U);
        out[out_len - 1U] = '\0';
        return 0;
    }
    return -1;
}

static const struct mmwlan_s1g_channel_list *halow_regdomain_get_supported(unsigned index)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    unsigned i;
    unsigned n = 0;

    if (db == NULL) {
        return NULL;
    }
    for (i = 0; i < db->num_domains; i++) {
        const struct mmwlan_s1g_channel_list *domain = db->domains[i];

        if (domain == NULL) {
            continue;
        }
        if (n == index) {
            return domain;
        }
        n++;
    }
    return NULL;
}

static int halow_apply_regdomain_locked(const char *country_code)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    const struct mmwlan_s1g_channel_list *list;
    char normalized[MM_HALOW_REGDOMAIN_CC_LEN];
    uint8_t restart_radio = 0;

    if (country_code == NULL || country_code[0] == '\0') {
        return -1;
    }

    halow_normalize_country_code(country_code, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return -1;
    }

    if (halow_mmwlan_booted &&
        halow_active_country_code[0] != '\0' &&
        halow_country_code_same(normalized, halow_active_country_code)) {
        strncpy(halow_netif_cfg.halow_cfg.country_code, normalized,
                sizeof(halow_netif_cfg.halow_cfg.country_code) - 1);
        halow_netif_cfg.halow_cfg.country_code[sizeof(halow_netif_cfg.halow_cfg.country_code) - 1] = '\0';
        return 0;
    }

    list = mmwlan_lookup_regulatory_domain(db, normalized);
    if (list == NULL) {
        LOG_DRV_ERROR("HaLow regdomain not found: %s", normalized);
        return -1;
    }

    if (halow_mmwlan_booted) {
        restart_radio = 1;
        LOG_DRV_INFO("HaLow restarting radio for regdomain %s", normalized);
        if (halow_mmwlan_teardown_locked() != 0) {
            return -1;
        }
    }

    if (mmwlan_set_channel_list(list) != MMWLAN_SUCCESS) {
        LOG_DRV_ERROR("HaLow set_channel_list failed for %s", normalized);
        if (restart_radio) {
            (void)halow_mmwlan_boot_locked();
        }
        return -1;
    }

    strncpy(halow_netif_cfg.halow_cfg.country_code, normalized, sizeof(halow_netif_cfg.halow_cfg.country_code) - 1);
    halow_netif_cfg.halow_cfg.country_code[sizeof(halow_netif_cfg.halow_cfg.country_code) - 1] = '\0';

    if (restart_radio) {
        if (halow_mmwlan_boot_locked() != 0) {
            return -1;
        }
    }

    halow_storage_scan_result.scan_count = 0;

    strncpy(halow_active_country_code, normalized, sizeof(halow_active_country_code) - 1);
    halow_active_country_code[sizeof(halow_active_country_code) - 1] = '\0';

    return 0;
}

static int halow_apply_rate_override_locked(void)
{
    const halow_wireless_config_t *hc = &halow_netif_cfg.halow_cfg;
    enum mmwlan_status status;

    if (!halow_mmwlan_booted) {
        return 0;
    }

    status = mmwlan_ate_override_rate_control((enum mmwlan_mcs)hc->rc_mcs,
                                            (enum mmwlan_bw)hc->rc_bw_mhz,
                                            (enum mmwlan_gi)hc->rc_gi);
    if (status != MMWLAN_SUCCESS) {
        LOG_DRV_WARN("HaLow rate override failed: %d", (int)status);
        return -1;
    }
    return 0;
}

static int halow_apply_halow_hw_config_locked(void)
{
    struct mmwlan_scan_config scan_cfg = MMWLAN_SCAN_CONFIG_INIT;
    enum mmwlan_status status;

    if (halow_netif_cfg.halow_cfg.tx_power_dbm > 0) {
        status = mmwlan_override_max_tx_power(halow_netif_cfg.halow_cfg.tx_power_dbm);
        if (status != MMWLAN_SUCCESS) {
            LOG_DRV_WARN("HaLow tx power override failed: %d", (int)status);
        }
    } else {
        (void)mmwlan_override_max_tx_power(0);
    }

    scan_cfg.dwell_time_ms = halow_netif_cfg.halow_cfg.scan_dwell_ms;
    scan_cfg.ndp_probe_enabled = (halow_netif_cfg.halow_cfg.ndp_probe_enabled != 0);
    scan_cfg.home_channel_dwell_time_ms = MMWLAN_SCAN_DEFAULT_DWELL_ON_HOME_MS;
    (void)mmwlan_set_scan_config(&scan_cfg);

    (void)halow_apply_rate_override_locked();

    return 0;
}

static void halow_set_lwip_netif_name(void)
{
    struct netif *nif = mmipal_get_lwip_netif();

    if (nif != NULL) {
        nif->name[0] = NETIF_NAME_WIFI_HALOW[0];
        nif->name[1] = NETIF_NAME_WIFI_HALOW[1];
    }
}

static int halow_mmipal_init_from_cfg(void)
{
    struct mmipal_init_args ip_init_args;
    char ip_str[16];
    char mask_str[16];
    char gw_str[16];

    memset(&ip_init_args, 0, sizeof(ip_init_args));

    if (halow_netif_cfg.ip_mode == NETIF_IP_MODE_DHCP) {
        ip_init_args.mode = MMIPAL_DHCP;
    } else {
        ip_init_args.mode = MMIPAL_STATIC;
        halow_ip_u8_to_str(halow_netif_cfg.ip_addr, ip_str, sizeof(ip_str));
        halow_ip_u8_to_str(halow_netif_cfg.netmask, mask_str, sizeof(mask_str));
        halow_ip_u8_to_str(halow_netif_cfg.gw, gw_str, sizeof(gw_str));
        strncpy(ip_init_args.ip_addr, ip_str, sizeof(ip_init_args.ip_addr) - 1);
        strncpy(ip_init_args.netmask, mask_str, sizeof(ip_init_args.netmask) - 1);
        strncpy(ip_init_args.gateway_addr, gw_str, sizeof(ip_init_args.gateway_addr) - 1);
    }

    if (mmipal_init(&ip_init_args) != MMIPAL_SUCCESS) {
        LOG_DRV_ERROR("HaLow mmipal_init failed");
        return -1;
    }

    mmipal_set_link_status_callback(mm_halow_link_status_callback);
    halow_set_lwip_netif_name();
    halow_stack_ready = 1;
    return 0;
}

static int halow_wireless_cfg_valid_for_sta(const wireless_config_t *wc)
{
    size_t ssid_len;
    size_t pass_len;

    if (wc == NULL) {
        return 0;
    }

    ssid_len = strlen(wc->ssid);
    if (ssid_len == 0 || ssid_len > MMWLAN_SSID_MAXLEN) {
        return 0;
    }

    if (wc->security == WIRELESS_SAE || wc->security == WIRELESS_WPA3) {
        pass_len = strlen(wc->pw);
        if (pass_len == 0 || pass_len > MMWLAN_PASSPHRASE_MAXLEN) {
            return 0;
        }
    }

    return 1;
}

static void halow_fill_sta_args(struct mmwlan_sta_args *sta_args)
{
    const wireless_config_t *wc = &halow_netif_cfg.wireless_cfg;
    const halow_wireless_config_t *hc = &halow_netif_cfg.halow_cfg;
    size_t ssid_len;
    size_t pass_len;

    struct mmwlan_sta_args defaults = MMWLAN_STA_ARGS_INIT;
    memcpy(sta_args, &defaults, sizeof(*sta_args));

    ssid_len = strlen(wc->ssid);
    if (ssid_len > MMWLAN_SSID_MAXLEN) {
        ssid_len = MMWLAN_SSID_MAXLEN;
    }
    memcpy(sta_args->ssid, wc->ssid, ssid_len);
    sta_args->ssid[ssid_len] = '\0';
    sta_args->ssid_len = (uint16_t)ssid_len;

    pass_len = strlen(wc->pw);
    if (pass_len > MMWLAN_PASSPHRASE_MAXLEN) {
        pass_len = MMWLAN_PASSPHRASE_MAXLEN;
    }
    memcpy(sta_args->passphrase, wc->pw, pass_len);
    sta_args->passphrase[pass_len] = '\0';
    sta_args->passphrase_len = (uint16_t)pass_len;

    sta_args->security_type = halow_map_security(wc->security);
    sta_args->pmf_mode = (hc->pmf_mode != 0) ? MMWLAN_PMF_DISABLED : MMWLAN_PMF_REQUIRED;

    if (NETIF_MAC_IS_UNICAST(wc->bssid)) {
        memcpy(sta_args->bssid, wc->bssid, sizeof(sta_args->bssid));
    }

    sta_args->bgscan_short_interval_s = hc->bgscan_short_interval_s;
    sta_args->bgscan_signal_threshold_dbm = hc->bgscan_signal_threshold_dbm;
    sta_args->bgscan_long_interval_s = hc->bgscan_long_interval_s;

    /* Feed preconnect cache from wpas/connect-time scans as well as mmwlan_scan_request(). */
    sta_args->scan_rx_cb = halow_preconnect_scan_rx_cb;
    sta_args->scan_rx_cb_arg = NULL;

    sta_args->sae_owe_ec_groups[0]=19;
    sta_args->sae_owe_ec_groups[1]=0;
    sta_args->sae_owe_ec_groups[2]=0;

    (void)halow_preconnect_fill_sta_args(sta_args);
}

static void halow_scan_result_to_info(const struct mmwlan_scan_result *src, wireless_scan_info_t *dst)
{
    size_t ssid_len;

    memset(dst, 0, sizeof(*dst));
    if (src == NULL) {
        return;
    }

    dst->rssi = src->rssi;
    dst->channel_freq_hz = src->channel_freq_hz;
    dst->bw_mhz = src->bw_mhz;
    /* wireless_scan_info_t.security is uint8_t; use MAX as unknown marker */
    dst->security = (uint8_t)WIRELESS_SECURITY_MAX;

    if (src->bssid != NULL) {
        memcpy(dst->bssid, src->bssid, 6);
    }

    if (src->ssid != NULL && src->ssid_len > 0) {
        ssid_len = src->ssid_len;
        if (ssid_len >= NETIF_SSID_VALUE_SIZE) {
            ssid_len = NETIF_SSID_VALUE_SIZE - 1;
        }
        memcpy(dst->ssid, src->ssid, ssid_len);
        dst->ssid[ssid_len] = '\0';
    }

    /* Security detection (best-effort): parse RSN IE and look for OWE/SAE AKMs. */
    if (src->ies != NULL && src->ies_len >= 2) {
        const uint8_t *p = src->ies;
        size_t left = src->ies_len;
        uint8_t saw_rsn = 0;

        while (left >= 2) {
            uint8_t eid = p[0];
            uint8_t elen = p[1];
            p += 2;
            left -= 2;
            if (elen > left) {
                break;
            }

            if (eid == 48 /* RSN */) {
                /* RSN IE: version(2) + group cipher(4) + pairwise_count(2) + pairwise_list + akm_count(2) + akm_list */
                const uint8_t *rsn = p;
                size_t rsn_len = elen;
                size_t off = 0;
                uint16_t pairwise_cnt = 0;
                uint16_t akm_cnt = 0;

                saw_rsn = 1;

                if (rsn_len < 2 + 4 + 2) {
                    goto rsn_done;
                }
                off += 2; /* version */
                off += 4; /* group cipher */
                pairwise_cnt = (uint16_t)(rsn[off] | (rsn[off + 1] << 8));
                off += 2;
                if (off + (size_t)pairwise_cnt * 4 > rsn_len) {
                    goto rsn_done;
                }
                off += (size_t)pairwise_cnt * 4;
                if (off + 2 > rsn_len) {
                    goto rsn_done;
                }
                akm_cnt = (uint16_t)(rsn[off] | (rsn[off + 1] << 8));
                off += 2;
                if (off + (size_t)akm_cnt * 4 > rsn_len) {
                    goto rsn_done;
                }

                for (uint16_t i = 0; i < akm_cnt; i++) {
                    const uint8_t *suite = &rsn[off + (size_t)i * 4];
                    /* 00-0f-ac is the IEEE OUI used in RSN suites */
                    if (suite[0] == 0x00 && suite[1] == 0x0f && suite[2] == 0xac) {
                        uint8_t type = suite[3];
                        if (type == 8 /* SAE */) {
                            dst->security = (uint8_t)WIRELESS_SAE;
                            goto rsn_done;
                        }
                        if (type == 18 /* OWE */) {
                            dst->security = (uint8_t)WIRELESS_OWE;
                            goto rsn_done;
                        }
                    }
                }

rsn_done:
                /* If we saw RSN but no explicit OWE/SAE, mark as protected generic. */
                if (dst->security == (uint8_t)WIRELESS_SECURITY_MAX) {
                    dst->security = (uint8_t)WIRELESS_WPA2;
                }
            }

            p += elen;
            left -= elen;
        }

        if (!saw_rsn && dst->security == (uint8_t)WIRELESS_SECURITY_MAX) {
            /* If privacy bit not set, treat as open. */
            if ((src->capability_info & 0x0010) == 0) {
                dst->security = (uint8_t)WIRELESS_OPEN;
            }
        }
    } else {
        if ((src->capability_info & 0x0010) == 0) {
            dst->security = (uint8_t)WIRELESS_OPEN;
        }
    }
}

static int halow_scan_is_duplicate_entry(const wireless_scan_info_t *cur,
                                         const wireless_scan_info_t *tmp)
{
    if (memcmp(cur->bssid, tmp->bssid, 6) != 0) {
        return 0;
    }
    if (strncmp(cur->ssid, tmp->ssid, NETIF_SSID_VALUE_SIZE) != 0) {
        return 0;
    }
#if NETIF_WIFI_HALOW_SCAN_DEDUP_BY_FREQ_BW
    if (cur->channel_freq_hz != tmp->channel_freq_hz ||
        cur->bw_mhz != tmp->bw_mhz) {
        return 0;
    }
#endif
    return 1;
}

static void halow_scan_merge_duplicate_entry(wireless_scan_info_t *cur,
                                             const wireless_scan_info_t *tmp)
{
    if (tmp->rssi > cur->rssi) {
        cur->rssi = tmp->rssi;
#if !NETIF_WIFI_HALOW_SCAN_DEDUP_BY_FREQ_BW
        cur->channel_freq_hz = tmp->channel_freq_hz;
        cur->bw_mhz = tmp->bw_mhz;
#endif
    }
    if (cur->security >= WIRELESS_SECURITY_MAX && tmp->security < WIRELESS_SECURITY_MAX) {
        cur->security = tmp->security;
    }
}

static void halow_scan_rx_handler(const struct mmwlan_scan_result *result, void *arg)
{
    wireless_scan_result_t *target = (wireless_scan_result_t *)arg;
    wireless_scan_info_t *info;
    wireless_scan_info_t tmp;

    if (result == NULL || target == NULL) {
        return;
    }

    /* Convert once, then de-duplicate (see NETIF_WIFI_HALOW_SCAN_DEDUP_BY_FREQ_BW). */
    halow_scan_result_to_info(result, &tmp);
    halow_preconnect_cache_store(result);

    if (target->scan_info != NULL) {
        for (uint8_t i = 0; i < target->scan_count; i++) {
            wireless_scan_info_t *cur = &target->scan_info[i];
            if (halow_scan_is_duplicate_entry(cur, &tmp)) {
                halow_scan_merge_duplicate_entry(cur, &tmp);
                return;
            }
        }
    }

    if (target->scan_count >= HALOW_SCAN_RESULT_MAX) {
        return;
    }

    if (target->scan_info == NULL) {
        return;
    }

    info = &target->scan_info[target->scan_count];
    memcpy(info, &tmp, sizeof(*info));
    target->scan_count++;
}

static void halow_scan_complete_handler(enum mmwlan_scan_state scan_state, void *arg)
{
    wireless_scan_result_t *target = (wireless_scan_result_t *)arg;
    wireless_scan_result_t snapshot = {0};
    wireless_scan_callback_t user_cb = halow_scan_user_cb;
    int cb_ret = (scan_state == MMWLAN_SCAN_SUCCESSFUL) ? 0 : -1;

    halow_scan_in_progress = 0;
    halow_scan_user_cb = NULL;
    halow_last_scan_state = scan_state;

    if (scan_state != MMWLAN_SCAN_SUCCESSFUL) {
        LOG_DRV_WARN("HaLow scan complete state=%d", (int)scan_state);
    }

    if (target != NULL && target->scan_info != NULL && user_cb != NULL) {
        snapshot.scan_count = target->scan_count;
        snapshot.scan_info = target->scan_info;
        user_cb(cb_ret, &snapshot);
    }

    if (target == &halow_async_scan_result && halow_async_scan_buf != NULL) {
        hal_mem_free(halow_async_scan_buf);
        halow_async_scan_buf = NULL;
        halow_async_scan_result.scan_info = NULL;
        halow_async_scan_result.scan_count = 0;
    }

    if (halow_scan_sem != NULL) {
        osSemaphoreRelease(halow_scan_sem);
    }
}

static int halow_run_scan_locked(wireless_scan_result_t *storage, wireless_scan_callback_t callback, uint32_t wait_ms)
{
    struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
    enum mmwlan_status status;
    wireless_scan_result_t *scan_target = storage;
    int ret = 0;

    if (halow_state == NETIF_STATE_DEINIT) {
        return -1;
    }

    if (halow_scan_in_progress) {
        if (wait_ms == 0) {
            /* Async caller: scan already running */
            return 0;
        }
        if (halow_scan_sem != NULL &&
            osSemaphoreAcquire(halow_scan_sem, wait_ms) != osOK) {
            (void)mmwlan_scan_abort();
            halow_scan_in_progress = 0;
            return -1;
        }
        halow_scan_in_progress = 0;
        /* Previous scan finished; sync caller always starts a fresh scan below */
    }

    if (halow_dpp_reject_if_active_locked()) {
        return -1;
    }

    if (halow_ensure_mmwlan_booted_locked() != 0) {
        return -1;
    }

    halow_preconnect_cache_clear();

    if (halow_scan_sem == NULL) {
        halow_scan_sem = osSemaphoreNew(1, 0, NULL);
        if (halow_scan_sem == NULL) {
            return -1;
        }
    }

    if (scan_target == NULL && callback != NULL) {
        halow_async_scan_buf = hal_mem_alloc_large(HALOW_SCAN_RESULT_MAX * sizeof(wireless_scan_info_t));
        if (halow_async_scan_buf == NULL) {
            return -1;
        }
        halow_async_scan_result.scan_info = halow_async_scan_buf;
        halow_async_scan_result.scan_count = 0;
        scan_target = &halow_async_scan_result;
    }

    if (scan_target != NULL && scan_target->scan_info == NULL) {
        scan_target->scan_info = hal_mem_alloc_large(HALOW_SCAN_RESULT_MAX * sizeof(wireless_scan_info_t));
        if (scan_target->scan_info == NULL) {
            if (halow_async_scan_buf != NULL) {
                hal_mem_free(halow_async_scan_buf);
                halow_async_scan_buf = NULL;
            }
            return -1;
        }
    }

    if (scan_target != NULL) {
        scan_target->scan_count = 0;
    }

    /*
     * Morse supports hw scan while STA is connected (dwell_on_home_ms). Do not
     * mmwlan_sta_disable() here — that drops the link and the minimal restore in
     * the scan-complete path left the netif DOWN / second UP stuck on link_sem.
     */

    halow_scan_user_cb = callback;
    halow_scan_in_progress = 1;
    halow_last_scan_state = MMWLAN_SCAN_SUCCESSFUL;

    uint32_t dwell_ms = halow_netif_cfg.halow_cfg.scan_dwell_ms;

    if (dwell_ms < MMWLAN_SCAN_MIN_DWELL_TIME_MS) {
        dwell_ms = MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS;
    }

    scan_req.scan_rx_cb = halow_scan_rx_handler;
    scan_req.scan_complete_cb = halow_scan_complete_handler;
    scan_req.scan_cb_arg = scan_target;
    scan_req.args.dwell_time_ms = dwell_ms;
    scan_req.args.dwell_on_home_ms = MMWLAN_SCAN_DEFAULT_DWELL_ON_HOME_MS;

    osSemaphoreAcquire(halow_scan_sem, 0);

    status = mmwlan_scan_request(&scan_req);
    if (status != MMWLAN_SUCCESS) {
        halow_scan_in_progress = 0;
        halow_scan_user_cb = NULL;
        LOG_DRV_ERROR("HaLow scan_request failed: %d", (int)status);
        if (halow_async_scan_buf != NULL) {
            hal_mem_free(halow_async_scan_buf);
            halow_async_scan_buf = NULL;
        }
        return -1;
    }

    if (wait_ms > 0) {
        if (osSemaphoreAcquire(halow_scan_sem, wait_ms) != osOK) {
            (void)mmwlan_scan_abort();
            ret = -1;
        } else if (halow_last_scan_state != MMWLAN_SCAN_SUCCESSFUL) {
            ret = -1;
        }
        /* scan_count==0 with SUCCESSFUL is valid; caller checks AP list */
    }

    return ret;
}

int mm_halow_netif_init(void)
{
    if (halow_mutex == NULL) {
        halow_mutex = osMutexNew(NULL);
        if (halow_mutex == NULL) {
            return -1;
        }
    }

    osMutexAcquire(halow_mutex, osWaitForever);

    mm_mbedtls_port_init();

    if (halow_state != NETIF_STATE_DEINIT) {
        if (halow_dpp_reject_if_active_locked()) {
            osMutexRelease(halow_mutex);
            return -1;
        }
        LOG_DRV_INFO("HaLow re-init: resetting previous session");
        halow_abort_operations_locked();
        halow_stack_deinit_locked();
    }

    if (halow_link_sem == NULL) {
        halow_link_sem = mmosal_semb_create("halow_link");
        if (halow_link_sem == NULL) {
            osMutexRelease(halow_mutex);
            return -1;
        }
    }

    if (!halow_pwr_acquired) {
        if (halow_power_acquire() != 0) {
            osMutexRelease(halow_mutex);
            return -1;
        }
    }

    if (halow_stack_init_locked() != 0) {
        goto init_fail;
    }

    halow_state = NETIF_STATE_DOWN;
    osMutexRelease(halow_mutex);
    LOG_DRV_INFO("HaLow netif initialized");
    return 0;

init_fail:
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
    halow_dpp_cancel_timeout_timer();
#endif
    halow_dpp_active = 0;
#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
    halow_dpp_user_cb = NULL;
    halow_dpp_user_arg = NULL;
#endif
    halow_stack_deinit_locked();
    halow_netif_resources_deinit_locked();
    osMutexRelease(halow_mutex);
    return -1;
}

int mm_halow_netif_up(void)
{
    struct mmwlan_sta_args sta_args;
    enum mmwlan_status status;
    enum mmwlan_sta_state sta_state;
    int ip_init_ret = 0;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);

    if (!halow_stack_ready) {
        ip_init_ret = halow_mmipal_init_from_cfg();
        if (ip_init_ret != 0) {
            LOG_DRV_ERROR("HaLow mmipal_init failed (ret=%d)", ip_init_ret);
            osMutexRelease(halow_mutex);
            return -1;
        }
    } else {
        /*
         * Stack already up from a prior session: mmipal keeps old IP until refreshed.
         * Sync halow_netif_cfg (may have changed while disconnected) before STA enable.
         */
        ip_init_ret = mm_halow_apply_ip_config();
        if (ip_init_ret != 0) {
            LOG_DRV_ERROR("HaLow apply IP from cfg failed (ret=%d)", ip_init_ret);
            osMutexRelease(halow_mutex);
            return -1;
        }
    }

    if (halow_scan_in_progress) {
        LOG_DRV_ERROR("HaLow UP: wait for scan to finish");
        osMutexRelease(halow_mutex);
        return -1;
    }

    if (halow_state == NETIF_STATE_UP && halow_link_is_up_locked()) {
        osMutexRelease(halow_mutex);
        return 0;
    }

    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }

    if (halow_ensure_mmwlan_booted_locked() != 0) {
        osMutexRelease(halow_mutex);
        return -1;
    }

#if !defined(MMWLAN_DPP_DISABLED) || !MMWLAN_DPP_DISABLED
    /*
     * DPP success leaves UMAC in listen mode until mmwlan_dpp_stop(); active is
     * already cleared — release mutex before blocking stop.
     */
    osMutexRelease(halow_mutex);
    halow_dpp_ensure_umac_idle_cli();
    osMutexAcquire(halow_mutex, osWaitForever);
#endif

    if (!halow_wireless_cfg_valid_for_sta(&halow_netif_cfg.wireless_cfg)) {
        LOG_DRV_ERROR("HaLow UP: invalid wireless cfg (ssid='%s' sec=%d) - run DPP or set ssid/pass",
                      halow_netif_cfg.wireless_cfg.ssid,
                      (int)halow_netif_cfg.wireless_cfg.security);
        osMutexRelease(halow_mutex);
        return -1;
    }

    halow_fill_sta_args(&sta_args);

    {
        uint8_t mac[6];
        halow_apply_sta_mac_policy();
        halow_resolve_sta_mac(mac);
        (void)mmwlan_sta_set_mac_addr(mac);
        LOG_DRV_INFO("HaLow STA MAC " NETIF_MAC_STR_FMT, NETIF_MAC_PARAMETER(mac));
    }

    LOG_DRV_INFO("HaLow UP: ssid='%s' sec=%d ps=%u pmf=%u reg=%s txpwr=%u",
                 halow_netif_cfg.wireless_cfg.ssid,
                 (int)halow_netif_cfg.wireless_cfg.security,
                 halow_netif_cfg.halow_cfg.ps_mode,
                 halow_netif_cfg.halow_cfg.pmf_mode,
                 halow_netif_cfg.halow_cfg.country_code,
                 halow_netif_cfg.halow_cfg.tx_power_dbm);

    (void)mmwlan_set_power_save_mode(halow_netif_cfg.halow_cfg.ps_mode ? MMWLAN_PS_ENABLED : MMWLAN_PS_DISABLED);
    (void)mmwlan_set_dynamic_ps_timeout(halow_netif_cfg.halow_cfg.ps_mode ? 100 : 3600000U);
    (void)halow_apply_halow_hw_config_locked();

    /*
     * Ensure HAL preconnect scan is fully torn down before STA enable; otherwise
     * mmdrv_set_channel during SAE auth often times out (rx page too short / -116).
     */
    (void)mmwlan_scan_abort();
    if (sta_args.preconnect_bss_valid) {
        osDelay(100);
    }

    status = mmwlan_sta_enable(&sta_args, mm_halow_sta_status_callback);
    if (status != MMWLAN_SUCCESS) {
        if (status == MMWLAN_UNAVAILABLE) {
            LOG_DRV_ERROR("mmwlan_sta_enable failed: %d (UMAC busy, e.g. DPP still active)",
                          (int)status);
        } else if (status == MMWLAN_INVALID_ARGUMENT) {
            LOG_DRV_ERROR("mmwlan_sta_enable failed: %d (bad ssid/pass/security)",
                          (int)status);
        } else {
            LOG_DRV_ERROR("mmwlan_sta_enable failed: %d", (int)status);
        }
        osMutexRelease(halow_mutex);
        return -1;
    }

    for (int i = 0; i < 10; i++) {
        uint8_t mac[6];
        sta_state = mmwlan_get_sta_state();
        halow_resolve_sta_mac(mac);
        halow_try_set_sta_mac_runtime(mac);
        if (sta_state == MMWLAN_STA_CONNECTED || sta_state == MMWLAN_STA_CONNECTING) {
            break;
        }
        osDelay(200);
    }

    if (halow_wait_for_link_locked() != 0) {
        LOG_DRV_ERROR("HaLow link wait timeout");
        (void)mmwlan_sta_disable();
        osMutexRelease(halow_mutex);
        return -1;
    }

    halow_state = NETIF_STATE_UP;
    osMutexRelease(halow_mutex);
    return 0;
}

int mm_halow_netif_down(void)
{
    enum mmwlan_status status;

    if (halow_mutex == NULL || halow_state == NETIF_STATE_DEINIT) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);

    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }

    if (halow_state == NETIF_STATE_UP) {
        status = mmwlan_sta_disable();
        if (status != MMWLAN_SUCCESS) {
            osMutexRelease(halow_mutex);
            return -1;
        }
        halow_state = NETIF_STATE_DOWN;
    }

    osMutexRelease(halow_mutex);
    return 0;
}

void mm_halow_netif_deinit(void)
{
    if (halow_mutex == NULL) {
        return;
    }

    osMutexAcquire(halow_mutex, osWaitForever);

    if (halow_state == NETIF_STATE_DEINIT) {
        osMutexRelease(halow_mutex);
        return;
    }

    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return;
    }

    halow_abort_operations_locked();
    halow_stack_deinit_locked();
    halow_netif_resources_deinit_locked();

    osMutexRelease(halow_mutex);
}

int mm_halow_netif_config(netif_config_t *netif_cfg)
{
    if (netif_cfg == NULL) {
        return -1;
    }
    if (halow_state == NETIF_STATE_UP) {
        return -1;
    }

    if (halow_state != NETIF_STATE_DEINIT && halow_mutex != NULL) {
        osMutexAcquire(halow_mutex, osWaitForever);
        if (halow_dpp_reject_if_active_locked()) {
            osMutexRelease(halow_mutex);
            return -1;
        }
        osMutexRelease(halow_mutex);
    }

    if (netif_cfg->host_name != NULL) {
        /* hostname stored elsewhere if needed */
    }

    {
        char prev_country[MM_HALOW_REGDOMAIN_CC_LEN];
        int region_changed;

        strncpy(prev_country, halow_netif_cfg.halow_cfg.country_code, sizeof(prev_country) - 1);
        prev_country[sizeof(prev_country) - 1] = '\0';

        memcpy(&halow_netif_cfg.wireless_cfg, &netif_cfg->wireless_cfg, sizeof(halow_netif_cfg.wireless_cfg));
        memcpy(&halow_netif_cfg.halow_cfg, &netif_cfg->halow_cfg, sizeof(halow_netif_cfg.halow_cfg));
        memcpy(halow_netif_cfg.diy_mac, netif_cfg->diy_mac, sizeof(halow_netif_cfg.diy_mac));
        halow_apply_sta_mac_policy();
        halow_netif_cfg.ip_mode = netif_cfg->ip_mode;
        memcpy(halow_netif_cfg.ip_addr, netif_cfg->ip_addr, sizeof(halow_netif_cfg.ip_addr));
        memcpy(halow_netif_cfg.netmask, netif_cfg->netmask, sizeof(halow_netif_cfg.netmask));
        memcpy(halow_netif_cfg.gw, netif_cfg->gw, sizeof(halow_netif_cfg.gw));

        /*
         * Before init: country_code is stored and applied in mm_halow_netif_init().
         * After init (DOWN): apply regdomain dynamically (restart radio if needed).
         */
        if (halow_state == NETIF_STATE_DEINIT) {
            return 0;
        }

        if (halow_mutex == NULL) {
            return -1;
        }

        region_changed = !halow_country_code_same(prev_country, halow_netif_cfg.halow_cfg.country_code);

        osMutexAcquire(halow_mutex, osWaitForever);
        if (region_changed) {
            if (halow_apply_regdomain_locked(halow_netif_cfg.halow_cfg.country_code) != 0) {
                osMutexRelease(halow_mutex);
                return -1;
            }
        }
        if (halow_apply_halow_hw_config_locked() != 0) {
            osMutexRelease(halow_mutex);
            return -1;
        }
        osMutexRelease(halow_mutex);
        return 0;
    }
}

static void halow_u8_from_ip_str(const char *str, uint8_t *ip)
{
    unsigned int a, b, c, d;
    if (str == NULL || ip == NULL) {
        return;
    }
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        ip[0] = (uint8_t)a;
        ip[1] = (uint8_t)b;
        ip[2] = (uint8_t)c;
        ip[3] = (uint8_t)d;
    }
}

int mm_halow_apply_ip_config(void)
{
    struct mmipal_ip_config ip_cfg;
    char ip_str[16];
    char mask_str[16];
    char gw_str[16];

    if (!halow_stack_ready) {
        return 0;
    }

    memset(&ip_cfg, 0, sizeof(ip_cfg));
    if (halow_netif_cfg.ip_mode == NETIF_IP_MODE_DHCP) {
        ip_cfg.mode = MMIPAL_DHCP;
    } else {
        ip_cfg.mode = MMIPAL_STATIC;
        halow_ip_u8_to_str(halow_netif_cfg.ip_addr, ip_str, sizeof(ip_str));
        halow_ip_u8_to_str(halow_netif_cfg.netmask, mask_str, sizeof(mask_str));
        halow_ip_u8_to_str(halow_netif_cfg.gw, gw_str, sizeof(gw_str));
        strncpy(ip_cfg.ip_addr, ip_str, sizeof(ip_cfg.ip_addr) - 1U);
        strncpy(ip_cfg.netmask, mask_str, sizeof(ip_cfg.netmask) - 1U);
        strncpy(ip_cfg.gateway_addr, gw_str, sizeof(ip_cfg.gateway_addr) - 1U);
    }

    return (mmipal_set_ip_config(&ip_cfg) == MMIPAL_SUCCESS) ? 0 : -1;
}

int mm_halow_netif_info(netif_info_t *netif_info)
{
    struct netif *nif;
    struct mmipal_ip_config ip_cfg;
    struct mmwlan_version version;
    uint8_t mac[MMWLAN_MAC_ADDR_LEN];

    if (netif_info == NULL) {
        return -1;
    }

    memset(netif_info, 0, sizeof(*netif_info));
    netif_info->if_name = NETIF_NAME_WIFI_HALOW;
    netif_info->type = NETIF_TYPE_WIRELESS;
    netif_info->state = mm_halow_netif_state();
    memcpy(&netif_info->wireless_cfg, &halow_netif_cfg.wireless_cfg, sizeof(netif_info->wireless_cfg));
    memcpy(&netif_info->halow_cfg, &halow_netif_cfg.halow_cfg, sizeof(netif_info->halow_cfg));
    netif_info->ip_mode = halow_netif_cfg.ip_mode;
    memcpy(netif_info->ip_addr, halow_netif_cfg.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->netmask, halow_netif_cfg.netmask, sizeof(netif_info->netmask));
    memcpy(netif_info->gw, halow_netif_cfg.gw, sizeof(netif_info->gw));

    if (halow_stack_ready && mmipal_get_ip_config(&ip_cfg) == MMIPAL_SUCCESS) {
        halow_u8_from_ip_str(ip_cfg.ip_addr, netif_info->ip_addr);
        halow_u8_from_ip_str(ip_cfg.netmask, netif_info->netmask);
        halow_u8_from_ip_str(ip_cfg.gateway_addr, netif_info->gw);
        if (ip_cfg.mode == MMIPAL_DHCP) {
            netif_info->ip_mode = NETIF_IP_MODE_DHCP;
        } else {
            netif_info->ip_mode = NETIF_IP_MODE_STATIC;
        }
    }

    nif = mm_halow_netif_ptr();
    if (nif != NULL) {
        memcpy(netif_info->if_mac, nif->hwaddr, 6);
    } else if (mmwlan_get_mac_addr(mac) == MMWLAN_SUCCESS) {
        memcpy(netif_info->if_mac, mac, 6);
    }

    if (mmwlan_get_version(&version) == MMWLAN_SUCCESS) {
        snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "%s/%s",
                 version.morse_fw_version, version.morselib_version);
    }

    if (netif_info->state == NETIF_STATE_UP) {
        netif_info->rssi = (int)mmwlan_get_rssi();
    }

    return 0;
}

netif_state_t mm_halow_netif_state(void)
{
    if (halow_state == NETIF_STATE_DEINIT) {
        return NETIF_STATE_DEINIT;
    }
    if (mmwlan_get_sta_state() == MMWLAN_STA_CONNECTED) {
        struct netif *nif = mm_halow_netif_ptr();
        if (nif != NULL && netif_is_link_up(nif)) {
            return NETIF_STATE_UP;
        }
    }
    if (halow_stack_ready) {
        return NETIF_STATE_DOWN;
    }
    return halow_state;
}

struct netif *mm_halow_netif_ptr(void)
{
    if (!halow_stack_ready) {
        return NULL;
    }
    return mmipal_get_lwip_netif();
}

int mm_halow_start_scan(wireless_scan_callback_t callback)
{
    int ret;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    ret = halow_run_scan_locked(NULL, callback, 0);
    osMutexRelease(halow_mutex);
    return ret;
}

wireless_scan_result_t *mm_halow_get_storage_scan_result(void)
{
    return &halow_storage_scan_result;
}

int mm_halow_update_storage_scan_result(uint32_t timeout_ms)
{
    int ret;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    ret = halow_run_scan_locked(&halow_storage_scan_result, NULL, timeout_ms);
    osMutexRelease(halow_mutex);
    return ret;
}

int mm_halow_ensure_scan_idle(uint32_t wait_ms)
{
    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_scan_in_progress) {
        (void)mmwlan_scan_abort();
        if (halow_scan_sem != NULL) {
            (void)osSemaphoreAcquire(halow_scan_sem, wait_ms);
        }
        halow_scan_in_progress = 0;
        halow_scan_user_cb = NULL;
    }
    osMutexRelease(halow_mutex);
    return 0;
}

int mm_halow_is_scan_in_progress(void)
{
    int in_progress = 0;

    if (halow_mutex == NULL) {
        return 0;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    in_progress = (halow_scan_in_progress != 0) ? 1 : 0;
    osMutexRelease(halow_mutex);
    return in_progress;
}

int mm_halow_set_preconnect_target(const uint8_t bssid[6])
{
    halow_preconnect_entry_t *entry;
    uint32_t now_ms;

    if (halow_mutex == NULL || bssid == NULL || !NETIF_MAC_IS_UNICAST(bssid)) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    now_ms = HAL_GetTick();
    entry = halow_preconnect_find_entry(bssid, now_ms);
    if (entry == NULL) {
        osMutexRelease(halow_mutex);
        return -1;
    }

    memcpy(halow_netif_cfg.wireless_cfg.bssid, bssid, 6);
    osMutexRelease(halow_mutex);
    return 0;
}

int mm_halow_set_regdomain(const char *country_code)
{
    int ret;

    if (halow_mutex == NULL || country_code == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_state == NETIF_STATE_UP) {
        LOG_DRV_ERROR("HaLow regdomain: run 'ifconfig hw down' first");
        osMutexRelease(halow_mutex);
        return -1;
    }
    if (halow_scan_in_progress) {
        LOG_DRV_ERROR("HaLow regdomain: wait for scan to finish");
        osMutexRelease(halow_mutex);
        return -1;
    }
    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }
    ret = halow_apply_regdomain_locked(country_code);
    osMutexRelease(halow_mutex);
    return ret;
}

int mm_halow_get_regdomain_max_tx_dbm(const char *country_code)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    const struct mmwlan_s1g_channel_list *list;
    char normalized[MM_HALOW_REGDOMAIN_CC_LEN];
    unsigned i;
    int8_t max_pwr = 0;

    if (country_code == NULL || country_code[0] == '\0') {
        country_code = halow_netif_cfg.halow_cfg.country_code;
    }

    halow_normalize_country_code(country_code, normalized, sizeof(normalized));
    if (normalized[0] == '\0' || db == NULL) {
        return 0;
    }

    list = mmwlan_lookup_regulatory_domain(db, normalized);
    if (list == NULL || list->channels == NULL || list->num_channels == 0U) {
        return 0;
    }

    for (i = 0; i < list->num_channels; i++) {
        if (list->channels[i].max_tx_eirp_dbm > max_pwr) {
            max_pwr = list->channels[i].max_tx_eirp_dbm;
        }
    }

    return (int)max_pwr;
}

int mm_halow_set_tx_power(uint16_t tx_power_dbm)
{
    enum mmwlan_status status;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }
    halow_netif_cfg.halow_cfg.tx_power_dbm = tx_power_dbm;
    status = mmwlan_override_max_tx_power(tx_power_dbm);
    osMutexRelease(halow_mutex);

    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

static const char *halow_rc_gi_str(int8_t gi)
{
    if (gi < 0) {
        return "auto";
    }
    if (gi == (int8_t)MMWLAN_GI_SHORT) {
        return "short";
    }
    if (gi == (int8_t)MMWLAN_GI_LONG) {
        return "long";
    }
    return "?";
}

int mm_halow_print_rate_override(void)
{
    const halow_wireless_config_t *hc = &halow_netif_cfg.halow_cfg;

    printf("HaLow rate override:\r\n");
    if (hc->rc_mcs < 0) {
        printf("  mcs: auto\r\n");
    } else {
        printf("  mcs: %d\r\n", (int)hc->rc_mcs);
    }
    if (hc->rc_bw_mhz < 0) {
        printf("  bw:  auto\r\n");
    } else {
        printf("  bw:  %d MHz\r\n", (int)hc->rc_bw_mhz);
    }
    printf("  gi:  %s\r\n", halow_rc_gi_str(hc->rc_gi));
    printf("  applied: %s\r\n", halow_mmwlan_booted ? "yes" : "pending (boot hw first)");
    return 0;
}

int mm_halow_set_rate_override(int8_t mcs, int8_t bw_mhz, int8_t gi)
{
    int ret = 0;

    if (mcs < -1 || mcs > (int8_t)MMWLAN_MCS_MAX) {
        return -1;
    }
    if (bw_mhz != -1 && bw_mhz != 1 && bw_mhz != 2 && bw_mhz != 4 && bw_mhz != 8) {
        return -1;
    }
    if (gi < -1 || gi > (int8_t)MMWLAN_GI_MAX) {
        return -1;
    }

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }

    halow_netif_cfg.halow_cfg.rc_mcs = mcs;
    halow_netif_cfg.halow_cfg.rc_bw_mhz = bw_mhz;
    halow_netif_cfg.halow_cfg.rc_gi = gi;

    if (halow_mmwlan_booted) {
        ret = halow_apply_rate_override_locked();
    }

    osMutexRelease(halow_mutex);
    return ret;
}

int mm_halow_set_power_save(uint8_t enable)
{
    enum mmwlan_status status;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }
    halow_netif_cfg.halow_cfg.ps_mode = enable ? 1 : 0;
    status = mmwlan_set_power_save_mode(enable ? MMWLAN_PS_ENABLED : MMWLAN_PS_DISABLED);
    osMutexRelease(halow_mutex);

    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

int mm_halow_set_scan_config(uint32_t dwell_ms, uint8_t ndp_probe_enabled)
{
    struct mmwlan_scan_config scan_cfg = MMWLAN_SCAN_CONFIG_INIT;
    enum mmwlan_status status;

    if (halow_mutex == NULL) {
        return -1;
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_dpp_reject_if_active_locked()) {
        osMutexRelease(halow_mutex);
        return -1;
    }
    halow_netif_cfg.halow_cfg.scan_dwell_ms = dwell_ms;
    halow_netif_cfg.halow_cfg.ndp_probe_enabled = ndp_probe_enabled;
    scan_cfg.dwell_time_ms = dwell_ms;
    scan_cfg.ndp_probe_enabled = (ndp_probe_enabled != 0);
    scan_cfg.home_channel_dwell_time_ms = MMWLAN_SCAN_DEFAULT_DWELL_ON_HOME_MS;
    status = mmwlan_set_scan_config(&scan_cfg);
    osMutexRelease(halow_mutex);

    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

unsigned mm_halow_regdomain_count(void)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    unsigned i;
    unsigned n = 0;

    if (db == NULL) {
        return 0;
    }
    for (i = 0; i < db->num_domains; i++) {
        const struct mmwlan_s1g_channel_list *domain = db->domains[i];

        if (domain == NULL) {
            continue;
        }
        n++;
    }
    return n;
}

int mm_halow_regdomain_is_supported(const char *country_code)
{
    return halow_regdomain_supported(country_code) ? 1 : 0;
}

int mm_halow_regdomain_get_code(unsigned index, char *country_code, size_t len)
{
    const struct mmwlan_s1g_channel_list *domain;

    if (country_code == NULL || len < MM_HALOW_REGDOMAIN_CC_LEN) {
        return -1;
    }

    domain = halow_regdomain_get_supported(index);
    if (domain == NULL) {
        return -1;
    }

    strncpy(country_code, (const char *)domain->country_code, len - 1U);
    country_code[len - 1U] = '\0';
    return 0;
}

int mm_halow_list_regdomains(void)
{
    const struct mmwlan_regulatory_db *db = get_regulatory_db();
    const char *current = halow_netif_cfg.halow_cfg.country_code;
    unsigned i;
    unsigned listed = 0;

    if (db == NULL) {
        printf("HaLow regdomain DB unavailable\r\n");
        return -1;
    }

    printf("HaLow regdomains (regulatory_db_domains, %u):\r\n", mm_halow_regdomain_count());
    for (i = 0; i < db->num_domains; i++) {
        const struct mmwlan_s1g_channel_list *domain = db->domains[i];

        if (domain == NULL) {
            continue;
        }

        printf("  %s (%u ch)", (const char *)domain->country_code, domain->num_channels);
        listed++;
        if (current[0] != '\0' && halow_country_code_same(current, (const char *)domain->country_code)) {
            printf(" *");
        }
        printf("\r\n");
    }

    if (listed == 0U) {
        printf("  (none)\r\n");
    }
    if (current[0] != '\0') {
        printf("Current cfg: %s", current);
        if (!halow_regdomain_supported(current)) {
            printf(" (not in regulatory_db)");
        }
        printf("\r\n");
    }
    return 0;
}

int mm_halow_print_version(void)
{
    struct mmwlan_version version;

    if (mmwlan_get_version(&version) != MMWLAN_SUCCESS) {
        printf("HaLow version query failed\r\n");
        return -1;
    }

    printf("HaLow FW: %s\r\n", version.morse_fw_version);
    printf("HaLow Lib: %s\r\n", version.morselib_version);
    printf("HaLow Chip: %s (0x%08lX)\r\n", version.morse_chip_id_string, (unsigned long)version.morse_chip_id);
    return 0;
}

int mm_halow_print_bcf_info(const char *country_code)
{
    struct mmwlan_bcf_metadata meta;
    const char *cc = country_code;

    if (cc == NULL) {
        cc = halow_netif_cfg.halow_cfg.country_code;
    }
    if (cc == NULL || cc[0] == '\0' || cc[1] == '\0') {
        printf("HaLow BCF: invalid country code\r\n");
        return -1;
    }

    mmhal_wlan_select_bcf_for_country(cc);

    if (mmwlan_get_bcf_metadata(&meta) != MMWLAN_SUCCESS) {
        printf("HaLow BCF metadata query failed\r\n");
        return -1;
    }

    printf("HaLow BCF (for %c%c):\r\n", cc[0], cc[1]);
    printf("  Version: %u.%u.%u\r\n",
           (unsigned)meta.version.major,
           (unsigned)meta.version.minor,
           (unsigned)meta.version.patch);
    printf("  Board: %s\r\n", meta.board_desc);
    printf("  Build: %s\r\n", meta.build_version);
    return 0;
}

#if defined(MMWLAN_DPP_DISABLED) && MMWLAN_DPP_DISABLED

int mm_halow_dpp_start(uint32_t timeout_ms, mm_halow_dpp_callback_t cb, void *user_arg)
{
    MM_UNUSED(timeout_ms);
    MM_UNUSED(cb);
    MM_UNUSED(user_arg);
    LOG_DRV_ERROR("HaLow DPP not available (MMWLAN_DPP_DISABLED in libmorse)");
    return -1;
}

int mm_halow_dpp_stop(void)
{
    return -1;
}

uint8_t mm_halow_dpp_is_active(void)
{
    return 0;
}

#else /* MMWLAN_DPP_DISABLED */

static int halow_dpp_apply_credentials(const struct mmwlan_dpp_cb_args *ev)
{
    const uint8_t *ssid;
    uint16_t ssid_len;
    const char *pass;
    size_t copy_len;

    if (ev == NULL) {
        return -1;
    }

    ssid = ev->args.pb_result.ssid;
    ssid_len = ev->args.pb_result.ssid_len;
    pass = ev->args.pb_result.passphrase;

    if (ssid == NULL || ssid_len == 0) {
        LOG_DRV_ERROR("HaLow DPP: no SSID in PB result");
        return -1;
    }

    copy_len = ssid_len;
    if (copy_len >= NETIF_SSID_VALUE_SIZE) {
        copy_len = NETIF_SSID_VALUE_SIZE - 1;
    }
    memcpy(halow_netif_cfg.wireless_cfg.ssid, ssid, copy_len);
    halow_netif_cfg.wireless_cfg.ssid[copy_len] = '\0';

    if (pass != NULL && pass[0] != '\0') {
        strncpy(halow_netif_cfg.wireless_cfg.pw, pass, NETIF_PW_VALUE_SIZE - 1);
        halow_netif_cfg.wireless_cfg.pw[NETIF_PW_VALUE_SIZE - 1] = '\0';
        halow_netif_cfg.wireless_cfg.security = WIRELESS_SAE;
    } else {
        memset(halow_netif_cfg.wireless_cfg.pw, 0, sizeof(halow_netif_cfg.wireless_cfg.pw));
        halow_netif_cfg.wireless_cfg.security = WIRELESS_OPEN;
        LOG_DRV_WARN("HaLow DPP: no passphrase in PB result, using open");
    }

    return 0;
}

static void halow_dpp_cancel_timeout_timer(void)
{
    (void)eloop_cancel_timeout(halow_dpp_timeout_eloop, NULL, NULL);
}

/**
 * Tear down UMAC DPP listen mode from a normal task (never from UMAC/eloop).
 * No-op when the radio was never booted or UMAC evtloop is not running.
 */
static void halow_dpp_ensure_umac_idle_cli(void)
{
    enum mmwlan_status status;
    int i;

    if (!halow_mmwlan_booted && !halow_dpp_active) {
        return;
    }

    osDelay(20);

    for (i = 0; i < 10; i++) {
        status = mmwlan_dpp_stop();
        if (status == MMWLAN_SUCCESS) {
            return;
        }
        if (status == MMWLAN_UNAVAILABLE || status == MMWLAN_NOT_RUNNING ||
            status == MMWLAN_NOT_INITIALIZED) {
            return;
        }
        osDelay(10);
    }

    LOG_DRV_WARN("HaLow mmwlan_dpp_stop failed (last ret=%d)", (int)status);
}

static void halow_dpp_invoke_user_cb(mm_halow_dpp_evt_t event)
{
    mm_halow_dpp_evt_info_t info = { .event = event };
    mm_halow_dpp_callback_t cb = halow_dpp_user_cb;
    void *arg = halow_dpp_user_arg;

    halow_dpp_user_cb = NULL;
    halow_dpp_user_arg = NULL;

    if (event == MM_HALOW_DPP_EVT_SUCCESS) {
        info.ssid = halow_netif_cfg.wireless_cfg.ssid;
        info.security = halow_netif_cfg.wireless_cfg.security;
    }

    if (cb != NULL) {
        cb(&info, arg);
    }
}

static void halow_dpp_deferred_user_cb_eloop(void *eloop_ctx, void *timeout_ctx)
{
    mm_halow_dpp_evt_t evt;

    MM_UNUSED(eloop_ctx);
    MM_UNUSED(timeout_ctx);

    if (!halow_dpp_deferred_cb_pending) {
        return;
    }
    halow_dpp_deferred_cb_pending = 0;
    evt = halow_dpp_deferred_evt;
    halow_dpp_invoke_user_cb(evt);
}

static void halow_dpp_schedule_user_cb(mm_halow_dpp_evt_t event)
{
    int ret;

    halow_dpp_deferred_evt = event;
    halow_dpp_deferred_cb_pending = 1;
    (void)eloop_cancel_timeout(halow_dpp_deferred_user_cb_eloop, NULL, NULL);
    ret = eloop_register_timeout(0, 0, halow_dpp_deferred_user_cb_eloop, NULL, NULL);
    if (ret <= 0) {
        halow_dpp_deferred_cb_pending = 0;
        LOG_DRV_WARN("HaLow DPP: deferred user cb failed (ret=%d), invoke inline", ret);
        halow_dpp_invoke_user_cb(event);
    }
}

static void halow_dpp_finish(mm_halow_dpp_evt_t event)
{
    if (!halow_dpp_active) {
        return;
    }

    halow_dpp_cancel_timeout_timer();
    halow_dpp_active = 0;
    /*
     * User cb runs on eloop (not UMAC). mmwlan_dpp_stop() only from CLI via
     * halow_dpp_ensure_umac_idle_cli() (e.g. after DPP OK, before ifconfig hw up).
     */
    halow_dpp_schedule_user_cb(event);
}

static void halow_dpp_timeout_eloop(void *eloop_ctx, void *timeout_ctx)
{
    MM_UNUSED(eloop_ctx);
    MM_UNUSED(timeout_ctx);

    if (!halow_dpp_active) {
        return;
    }

    LOG_DRV_ERROR("HaLow DPP wait timeout");
    halow_dpp_finish(MM_HALOW_DPP_EVT_TIMEOUT);
}

static int halow_dpp_arm_timeout(uint32_t timeout_ms)
{
    unsigned int secs = timeout_ms / 1000U;
    unsigned int usecs = (timeout_ms % 1000U) * 1000U;
    int ret;

    halow_dpp_cancel_timeout_timer();
    /*
     * Morse eloop shim returns 1 on success (bool), not hostap's 0.
     * Treat non-positive as failure.
     */
    ret = eloop_register_timeout(secs, usecs, halow_dpp_timeout_eloop, NULL, NULL);
    if (ret <= 0) {
        LOG_DRV_ERROR("HaLow DPP: eloop_register_timeout failed (ret=%d)", ret);
        return -1;
    }
    return 0;
}

static void mm_halow_dpp_event_cb(const struct mmwlan_dpp_cb_args *dpp_event, void *arg)
{
    enum mmwlan_dpp_pb_result result;
    mm_halow_dpp_evt_t evt = MM_HALOW_DPP_EVT_FAILED;

    MM_UNUSED(arg);

    if (dpp_event == NULL || dpp_event->event != MMWLAN_DPP_EVT_PB_RESULT) {
        return;
    }

    if (!halow_dpp_active) {
        return;
    }

    result = dpp_event->args.pb_result.result;

    if (result == MMWLAN_DPP_PB_RESULT_SUCCESS) {
        if (halow_dpp_apply_credentials(dpp_event) == 0) {
            evt = MM_HALOW_DPP_EVT_SUCCESS;
            LOG_DRV_INFO("HaLow DPP success: ssid='%s' sec=%d",
                         halow_netif_cfg.wireless_cfg.ssid,
                         (int)halow_netif_cfg.wireless_cfg.security);
        } else {
            LOG_DRV_ERROR("HaLow DPP: PB success but credentials missing");
        }
    } else if (result == MMWLAN_DPP_PB_RESULT_SESSION_OVERLAP) {
        evt = MM_HALOW_DPP_EVT_SESSION_OVERLAP;
        LOG_DRV_WARN("HaLow DPP session overlap (multiple configurators?)");
    } else {
        LOG_DRV_ERROR("HaLow DPP failed (mmwlan result=%d)", (int)result);
    }

    halow_dpp_finish(evt);
}

uint8_t mm_halow_dpp_is_active(void)
{
    return halow_dpp_active;
}

int mm_halow_dpp_stop(void)
{
    if (halow_mutex != NULL) {
        osMutexAcquire(halow_mutex, osWaitForever);
    }
    if (!halow_dpp_active) {
        if (halow_mutex != NULL) {
            osMutexRelease(halow_mutex);
        }
        halow_dpp_ensure_umac_idle_cli();
        if (halow_mutex != NULL) {
            osMutexAcquire(halow_mutex, osWaitForever);
        }
        return 0;
    }

    LOG_DRV_INFO("HaLow DPP stop requested");
    halow_dpp_finish(MM_HALOW_DPP_EVT_STOPPED);
    if (halow_mutex != NULL) {
        osMutexRelease(halow_mutex);
    }
    halow_dpp_ensure_umac_idle_cli();
    if (halow_mutex != NULL) {
        osMutexAcquire(halow_mutex, osWaitForever);
    }
    return 0;
}

int mm_halow_dpp_start(uint32_t timeout_ms, mm_halow_dpp_callback_t cb, void *user_arg)
{
    struct mmwlan_dpp_args dpp_args = {0};
    enum mmwlan_status status;

    if (timeout_ms == 0) {
        timeout_ms = HALOW_DPP_DEFAULT_TIMEOUT_MS;
    }

    if (halow_mutex == NULL || halow_state == NETIF_STATE_DEINIT) {
        LOG_DRV_ERROR("HaLow DPP: call 'ifconfig hw init' first");
        return -1;
    }

    LOG_SIMPLE("HaLow DPP: preparing (may take a few seconds)...\r\n");

    if (halow_dpp_active) {
        LOG_DRV_WARN("HaLow DPP: replacing previous session");
        halow_dpp_cancel_timeout_timer();
        halow_dpp_active = 0;
        halow_dpp_user_cb = NULL;
        halow_dpp_user_arg = NULL;
        halow_dpp_ensure_umac_idle_cli();
    }

    osMutexAcquire(halow_mutex, osWaitForever);
    if (halow_state == NETIF_STATE_UP) {
        LOG_DRV_ERROR("HaLow DPP: interface is up, run 'ifconfig hw down' first");
        osMutexRelease(halow_mutex);
        return -1;
    }
    if (halow_scan_in_progress) {
        LOG_DRV_ERROR("HaLow DPP: scan in progress");
        osMutexRelease(halow_mutex);
        return -1;
    }
    if (halow_ensure_mmwlan_booted_locked() != 0) {
        osMutexRelease(halow_mutex);
        return -1;
    }
    osMutexRelease(halow_mutex);

    halow_dpp_user_cb = cb;
    halow_dpp_user_arg = user_arg;

    dpp_args.dpp_event_cb = mm_halow_dpp_event_cb;
    dpp_args.dpp_event_cb_arg = NULL;

    status = mmwlan_dpp_start(&dpp_args);
    if (status != MMWLAN_SUCCESS) {
        halow_dpp_user_cb = NULL;
        halow_dpp_user_arg = NULL;
        halow_dpp_ensure_umac_idle_cli();
        LOG_DRV_ERROR("mmwlan_dpp_start failed: %d (set regdomain?)", (int)status);
        return -1;
    }

    if (halow_dpp_arm_timeout(timeout_ms) != 0) {
        halow_dpp_user_cb = NULL;
        halow_dpp_user_arg = NULL;
        halow_dpp_ensure_umac_idle_cli();
        LOG_DRV_ERROR("HaLow DPP: timeout timer failed");
        return -1;
    }

    halow_dpp_active = 1;
    LOG_SIMPLE("HaLow DPP: listening - press AP/configurator button (%lu s)\r\n",
               (unsigned long)(timeout_ms / 1000U));
    return 0;
}

#endif /* MMWLAN_DPP_DISABLED */

int mm_halow_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{
    int ret = -1;
    netif_state_t if_state = NETIF_STATE_MAX;

    if (if_name == NULL || strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
        return -1;
    }

    switch (cmd) {
    case NETIF_CMD_CFG:
        ret = mm_halow_netif_config((netif_config_t *)param);
        break;
    case NETIF_CMD_INIT:
        ret = mm_halow_netif_init();
        break;
    case NETIF_CMD_UP:
        ret = mm_halow_netif_up();
        break;
    case NETIF_CMD_INFO:
        ret = mm_halow_netif_info((netif_info_t *)param);
        break;
    case NETIF_CMD_DOWN:
        ret = mm_halow_netif_down();
        break;
    case NETIF_CMD_UNINIT:
        mm_halow_netif_deinit();
        ret = 0;
        break;
    case NETIF_CMD_STATE:
        if (param == NULL) {
            ret = -1;
        } else {
            *(netif_state_t *)param = mm_halow_netif_state();
            ret = 0;
        }
        break;
    case NETIF_CMD_CFG_EX:
        if_state = mm_halow_netif_state();
        if (if_state == NETIF_STATE_UP) {
            ret = mm_halow_netif_down();
            if (ret != 0) {
                break;
            }
        }
        ret = mm_halow_netif_config((netif_config_t *)param);
        if (ret != 0) {
            break;
        }
        if (if_state == NETIF_STATE_UP) {
            ret = mm_halow_netif_up();
        }
        break;
    default:
        break;
    }

    return ret;
}

#endif /* NETIF_WIFI_HALOW_IS_ENABLE */
