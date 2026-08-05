#include "quick_network.h"
#include "quick_bootstrap.h"
#include "quick_storage.h"
#include "quick_trace.h"
#include "cmsis_os2.h"
#include "aicam_types.h"
#include "../Network/netif_manager/eg912u_gl_netif.h"
#include "../Network/netif_manager/sl_net_netif.h"
#include "../Network/netif_manager/netif_manager.h"
#include "../Network/mqtt_client/si91x_mqtt_client.h"
#include "common_utils.h"
#include "stm32n6xx_hal.h"
#include "tx_api.h"
#include <string.h>
#include <stdio.h>
/*
 * quick_network:
 * - init() only creates threads/sync objects (no NVS access here)
 * - network thread reads comm_pref + netif cfg from NVS and brings link up
 * - mqtt thread reads mqtt cfg from NVS, starts client, then drains publish queue.
 *   If init is skipped or fails, pending queue items are drained with an error callback
 *   so callers (e.g. quick_bootstrap) are not blocked waiting for QB_FLAG_MQTT_PUB_DONE.
 */

typedef struct {
    qs_mqtt_task_param_t param; /* shallow copy; caller owns param->data lifetime */
} qs_mqtt_task_item_t;

/* event flags */
#define QN_FLAG_CFG_READY   (1UL << 0)
#define QN_FLAG_NET_DONE    (1UL << 1)  /* network thread finished bring-up attempt */
#define QN_FLAG_NET_OK      (1UL << 2)  /* network usable */
#define QN_FLAG_MQTT_DONE   (1UL << 3)  /* mqtt thread finished init attempt (success or failure) */
#define QN_FLAG_MQTT_OK     (1UL << 4)  /* mqtt connected and ready */
#define QN_FLAG_MQTT_ERR    (1UL << 5)  /* mqtt error observed */
#define QN_FLAG_PUB_ACK     (1UL << 6)  /* publish ack observed (PUBLISHED event) */
#define QN_FLAG_PUB_ERR     (1UL << 7)  /* publish failed (error/disconnect) */
#define QN_FLAG_MQTT_THREAD_EXIT (1UL << 8)  /* qs_mqtt thread finished (stop/destroy done or early exit) */
#define QN_FLAG_AUTO_WIN    (1UL << 9)  /* AUTO: one interface won and became active */
#define QN_FLAG_INIT_ERR    (1UL << 24) /* init error */

#define QN_MQTT_THREAD_JOIN_MS (10000U)

#define QN_MQTT_TASK_QUEUE_SIZE   (8U)
#define QN_NET_CONNECT_TIMEOUT_MS (10000U)

/* AUTO: full parallel connect attempts (aligned with upper-layer app behavior).
 * First successful interface wins, becomes default netif, others will stop/exit best-effort.
 */
#define QN_AUTO_CONNECT_TIMEOUT_MS (60000U)
#define QN_AUTO_WORKER_STACK_SIZE  (4096u * 3u)

/* qn_wifi_try_connect_known: STA already INIT'd in AUTO parallel phase */
#define QN_WIFI_STA_INIT_UNKNOWN ((int)0x7FFFFFFFL)

static osEventFlagsId_t s_evt = NULL;
static osMessageQueueId_t s_mqtt_q = NULL;
static osThreadId_t s_net_tid = NULL;
static osThreadId_t s_mqtt_tid = NULL;
static aicam_bool_t s_inited = AICAM_FALSE;

/* Avoid MemAlloc() inside osMessageQueueNew (ThreadX CMSIS wrapper) by providing
 * both queue storage and control block statically.
 */
static TX_QUEUE s_mqtt_q_cb ALIGN_32 IN_PSRAM;
static uint8_t s_mqtt_q_mem[
    QN_MQTT_TASK_QUEUE_SIZE * (((sizeof(qs_mqtt_task_item_t) + (sizeof(ULONG) - 1U)) / sizeof(ULONG)) * sizeof(ULONG))
] ALIGN_32;
static const osMessageQueueAttr_t s_mqtt_q_attr = {
    .name = "qs_mqtt_q",
    .mq_mem = s_mqtt_q_mem,
    .mq_size = sizeof(s_mqtt_q_mem),
    .cb_mem = &s_mqtt_q_cb,
    .cb_size = sizeof(s_mqtt_q_cb),
};

static qs_comm_pref_type_t s_comm_pref_type = COMM_PREF_TYPE_DISABLE;
static char s_active_if_name[4] = {0}; /* "wl"/"4g"/"wn" + '\0' */
static int s_net_result = AICAM_ERROR;
static int s_mqtt_result = AICAM_ERROR;
static volatile aicam_bool_t s_net_stop_req = AICAM_FALSE;

static osMutexId_t s_mqtt_lock = NULL;
static ms_mqtt_client_handle_t s_ms_client = NULL;
static volatile aicam_bool_t s_ms_stop_req = AICAM_FALSE;
static qs_mqtt_all_config_t s_mqtt_cfg_cache;
static volatile aicam_bool_t s_mqtt_cfg_valid = AICAM_FALSE;
static volatile int s_wait_pub_msg_id = -1;
static volatile aicam_bool_t s_si91x_mqtt_ready = AICAM_FALSE;
static osMutexId_t s_auto_lock = NULL;

static void qn_network_thread(void *argument);
static void qn_mqtt_thread(void *argument);
static void qn_mqtt_event_handler(ms_mqtt_event_data_t *event_data, void *user_args);
static void qn_mqtt_drain_queue(int publish_err);
static void qn_mqtt_thread_exit_notify(void);
static int qn_auto_parallel_full_connect(void);
static int qn_auto_serial_connect(void);
static int qn_try_connect_one_ex(qs_comm_pref_type_t type, const char *if_name, int set_default);
static int qn_try_connect_one(qs_comm_pref_type_t type, const char *if_name);

static int qn_ensure_netif_inited(const char *if_name)
{
    if (!if_name) return AICAM_ERROR_INVALID_PARAM;
    netif_state_t state = nm_get_netif_state(if_name);
    if (state == NETIF_STATE_DEINIT) {
        return nm_ctrl_netif_init(if_name);
    }
    return 0;
}

static int qn_apply_netif_cfg(qs_comm_pref_type_t type, const char *if_name)
{
    if (!if_name) return AICAM_ERROR_INVALID_PARAM;

    netif_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* Always start from current cfg to avoid clobbering unrelated fields. */
    if (nm_get_netif_cfg(if_name, &cfg) != 0) {
        return AICAM_ERROR_NOT_FOUND;
    }

    netif_config_t qs_cfg = {0};
    if (quick_storage_read_netif_config(type, &qs_cfg) != 0) {
        return AICAM_ERROR_NOT_FOUND;
    }

    /* Common */
    if (qs_cfg.host_name != NULL && qs_cfg.host_name[0] != '\0') {
        cfg.host_name = qs_cfg.host_name;
    }
    cfg.ip_mode = qs_cfg.ip_mode;
    memcpy(cfg.ip_addr, qs_cfg.ip_addr, sizeof(cfg.ip_addr));
    memcpy(cfg.netmask, qs_cfg.netmask, sizeof(cfg.netmask));
    memcpy(cfg.gw, qs_cfg.gw, sizeof(cfg.gw));

    /* Per-type blocks */
    if (type == COMM_PREF_TYPE_WIFI) {
        /* ssid/pw are the only must-have fields for STA quick connect */
        strncpy(cfg.wireless_cfg.ssid, qs_cfg.wireless_cfg.ssid, sizeof(cfg.wireless_cfg.ssid) - 1);
        strncpy(cfg.wireless_cfg.pw, qs_cfg.wireless_cfg.pw, sizeof(cfg.wireless_cfg.pw) - 1);
        cfg.wireless_cfg.channel = qs_cfg.wireless_cfg.channel;
        cfg.wireless_cfg.security = qs_cfg.wireless_cfg.security;
        cfg.wireless_cfg.encryption = qs_cfg.wireless_cfg.encryption;
    } else if (type == COMM_PREF_TYPE_CELLULAR) {
        memcpy(&cfg.cellular_cfg, &qs_cfg.cellular_cfg, sizeof(cfg.cellular_cfg));
    } else if (type == COMM_PREF_TYPE_POE) {
        /* already applied by common block */
#if NETIF_WIFI_HALOW_IS_ENABLE
    } else if (type == COMM_PREF_TYPE_HALOW) {
        strncpy(cfg.wireless_cfg.ssid, qs_cfg.wireless_cfg.ssid, sizeof(cfg.wireless_cfg.ssid) - 1);
        strncpy(cfg.wireless_cfg.pw, qs_cfg.wireless_cfg.pw, sizeof(cfg.wireless_cfg.pw) - 1);
        cfg.wireless_cfg.security = qs_cfg.wireless_cfg.security;
        strncpy(cfg.halow_cfg.country_code, qs_cfg.halow_cfg.country_code,
                sizeof(cfg.halow_cfg.country_code) - 1);
#endif
    }

    return nm_set_netif_cfg(if_name, &cfg);
}

static aicam_bool_t qn_parse_bssid_str(const char *bssid_str, uint8_t out[6])
{
    if (!bssid_str || !out) return AICAM_FALSE;
    unsigned int b[6] = {0};
    if (sscanf(bssid_str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return AICAM_FALSE;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)(b[i] & 0xFF);
    return AICAM_TRUE;
}

/**
 * @param parallel_sta_init_hint QN_WIFI_STA_INIT_UNKNOWN: run qn_ensure_netif_inited;
 *                               0: AUTO parallel INIT already succeeded for STA;
 *                               other: return this code (parallel INIT failed).
 */
static int qn_wifi_try_connect_known(uint32_t connect_timeout_ms, int parallel_sta_init_hint)
{
    qs_wifi_network_info_t known[MAX_KNOWN_WIFI_NETWORKS];
    uint32_t known_count = 0;
    memset(known, 0, sizeof(known));

    if (quick_storage_read_known_wifi_networks(known, &known_count) != 0) {
        QT_TRACE("[QN] ", "wifi known: read nvs fail");
        return AICAM_ERROR_NOT_FOUND;
    }
    if (known_count == 0U) {
        QT_TRACE("[QN] ", "wifi known: list empty");
        return AICAM_ERROR_NOT_FOUND;
    }

    int ret;
    if (parallel_sta_init_hint == QN_WIFI_STA_INIT_UNKNOWN) {
        ret = qn_ensure_netif_inited(NETIF_NAME_WIFI_STA);
        if (ret != 0) {
            return ret;
        }
    } else if (parallel_sta_init_hint != 0) {
        return parallel_sta_init_hint;
    }
    /* parallel_sta_init_hint == 0: STA INIT already done in AUTO parallel workers */

    QT_TRACE("[QN] ", "wifi known: n=%u parallel_sta=%d", (unsigned)known_count, parallel_sta_init_hint);

    /* Refresh scan cache (communication_service uses this path). */
    int scan_rc = nm_wireless_update_scan_result(3000);
    if (scan_rc != 0) {
        QT_TRACE("[QN] ", "wifi scan update rc=%d", scan_rc);
    }
    wireless_scan_result_t *scan = nm_wireless_get_scan_result();

    /* Sort indices by last_connected_time desc (simple bubble sort). */
    uint32_t idx[MAX_KNOWN_WIFI_NETWORKS];
    for (uint32_t i = 0; i < known_count; i++) idx[i] = i;
    for (uint32_t i = 0; i + 1 < known_count; i++) {
        for (uint32_t j = 0; j + 1 < known_count - i; j++) {
            if (known[idx[j]].last_connected_time < known[idx[j + 1]].last_connected_time) {
                uint32_t t = idx[j];
                idx[j] = idx[j + 1];
                idx[j + 1] = t;
            }
        }
    }

    /* If scan list has zero matches against known SSIDs, only try the most recent one. */
    aicam_bool_t scan_has_any_match = AICAM_FALSE;
    if (scan && scan->scan_info && scan->scan_count > 0) {
        for (uint32_t si = 0; si < scan->scan_count; si++) {
            wireless_scan_info_t *sinfo = &scan->scan_info[si];
            if (!sinfo) continue;
            for (uint32_t k = 0; k < known_count; k++) {
                qs_wifi_network_info_t *kn0 = &known[idx[k]];
                if (kn0->ssid[0] == '\0' || kn0->password[0] == '\0') continue;
                if (strncmp(sinfo->ssid, kn0->ssid, sizeof(sinfo->ssid)) == 0) {
                    scan_has_any_match = AICAM_TRUE;
                    break;
                }
            }
            if (scan_has_any_match) break;
        }
    }
    const aicam_bool_t try_last_connected_only = (scan_has_any_match == AICAM_FALSE) ? AICAM_TRUE : AICAM_FALSE;
    if (try_last_connected_only) {
        QT_TRACE("[QN] ", "wifi scan: no known ssid match -> try last_connected only");
    }

    uint32_t tried = 0U;
#if QB_TRACE_ENABLE
    int last_fail_rc = 0;
#endif
    for (uint32_t k = 0; k < known_count; k++) {
        qs_wifi_network_info_t *kn = &known[idx[k]];
        if (kn->ssid[0] == '\0' || kn->password[0] == '\0') {
            continue;
        }

        netif_config_t sta_cfg;
        memset(&sta_cfg, 0, sizeof(sta_cfg));
        (void)nm_get_netif_cfg(NETIF_NAME_WIFI_STA, &sta_cfg);

        strncpy(sta_cfg.wireless_cfg.ssid, kn->ssid, sizeof(sta_cfg.wireless_cfg.ssid) - 1);
        strncpy(sta_cfg.wireless_cfg.pw, kn->password, sizeof(sta_cfg.wireless_cfg.pw) - 1);
        sta_cfg.ip_mode = NETIF_IP_MODE_DHCP;

        /* Prefer scan result BSSID/channel/security; fallback to stored bssid string if valid. */
        aicam_bool_t bssid_set = AICAM_FALSE;
        if (scan && scan->scan_info && scan->scan_count > 0) {
            for (uint32_t i = 0; i < scan->scan_count; i++) {
                wireless_scan_info_t *si = &scan->scan_info[i];
                if (strncmp(si->ssid, kn->ssid, sizeof(si->ssid)) == 0) {
                    memcpy(sta_cfg.wireless_cfg.bssid, si->bssid, sizeof(sta_cfg.wireless_cfg.bssid));
                    sta_cfg.wireless_cfg.channel = si->channel;
                    sta_cfg.wireless_cfg.security = (wireless_security_t)si->security;
                    bssid_set = AICAM_TRUE;
                    break;
                }
            }
        }
        if (!bssid_set && kn->bssid[0] != '\0') {
            (void)qn_parse_bssid_str(kn->bssid, sta_cfg.wireless_cfg.bssid);
        }

        tried++;
        ret = nm_set_netif_cfg(NETIF_NAME_WIFI_STA, &sta_cfg);
        if (ret != 0) {
        #if QB_TRACE_ENABLE
            last_fail_rc = ret;
        #endif
            QT_TRACE("[QN] ", "wifi cfg fail ssid=%.16s rc=%d", kn->ssid, ret);
            if (try_last_connected_only) break;
            continue;
        }

        ret = nm_ctrl_netif_up(NETIF_NAME_WIFI_STA);
        if (ret != 0) {
        #if QB_TRACE_ENABLE
            last_fail_rc = ret;
        #endif
            QT_TRACE("[QN] ", "wifi up fail ssid=%.16s rc=%d", kn->ssid, ret);
            (void)nm_ctrl_netif_down(NETIF_NAME_WIFI_STA);
            if (try_last_connected_only) break;
            continue;
        }
        (void)connect_timeout_ms;
        QT_TRACE("[QN] ", "wifi ok ssid=%.16s", kn->ssid);
        return 0;
    }

    /* AICAM_ERROR_TIMEOUT (-5): parallel INIT ok but no known AP got link (or no valid ssid/pw entries). */
    QT_TRACE("[QN] ", "wifi known exhausted tried=%u last_rc=%d scan_rc=%d -> timeout",
             (unsigned)tried,
             last_fail_rc,
             scan_rc);
    return AICAM_ERROR_TIMEOUT;
}

static int qn_try_connect_one_ex(qs_comm_pref_type_t type, const char *if_name, int set_default)
{
    int ret = 0;
    if (!if_name) return AICAM_ERROR_INVALID_PARAM;

    if (type == COMM_PREF_TYPE_CELLULAR) eg912u_netif_enable_fast_mode();
    /* Application-layer order: init(if DEINIT) -> config -> up */
    QT_TRACE("[QN] ", "try %s type=%u: init...", if_name, (unsigned)type);
    ret = qn_ensure_netif_inited(if_name);
    QT_TRACE("[QN] ", "try %s: init rc=%d", if_name, ret);
    if (ret != 0) return ret;

    QT_TRACE("[QN] ", "try %s type=%u: cfg...", if_name, (unsigned)type);
    ret = qn_apply_netif_cfg(type, if_name);
    QT_TRACE("[QN] ", "try %s: cfg rc=%d", if_name, ret);
    if (ret != 0) return ret;

    QT_TRACE("[QN] ", "try %s: up...", if_name);
    ret = nm_ctrl_netif_up(if_name);
    QT_TRACE("[QN] ", "try %s: up rc=%d", if_name, ret);
    if (ret != 0) return ret;

    if (set_default) {
        /* Ensure it becomes default for sockets/DNS path. */
        (void)nm_ctrl_set_default_netif(if_name);
    }
    QT_TRACE("[QN] ", "try %s: ok", if_name);
    return 0;
}

static int qn_try_connect_one(qs_comm_pref_type_t type, const char *if_name)
{
    return qn_try_connect_one_ex(type, if_name, 1);
}

#if NETIF_WIFI_HALOW_IS_ENABLE
static void qn_deinit_halow_if_needed(void)
{
    netif_state_t st = nm_get_netif_state(NETIF_NAME_WIFI_HALOW);
    if (st != NETIF_STATE_DEINIT) {
        (void)nm_ctrl_netif_down(NETIF_NAME_WIFI_HALOW);
        (void)nm_ctrl_netif_deinit(NETIF_NAME_WIFI_HALOW);
    }
}

static int qn_halow_try_connect(void)
{
    netif_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (quick_storage_read_netif_config(COMM_PREF_TYPE_HALOW, &cfg) != 0) {
        return AICAM_ERROR_NOT_FOUND;
    }
    if (cfg.wireless_cfg.ssid[0] == '\0') {
        return AICAM_ERROR_NOT_FOUND;
    }

    int ret = qn_ensure_netif_inited(NETIF_NAME_WIFI_HALOW);
    if (ret != 0) {
        return ret;
    }

    ret = qn_apply_netif_cfg(COMM_PREF_TYPE_HALOW, NETIF_NAME_WIFI_HALOW);
    if (ret != 0) {
        return ret;
    }

    if (nm_wireless_update_scan_result_ex(NETIF_NAME_WIFI_HALOW, 3000) != 0) {
        return AICAM_ERROR_NOT_FOUND;
    }

    wireless_scan_result_t *scan = nm_wireless_get_scan_result_ex(NETIF_NAME_WIFI_HALOW);
    aicam_bool_t found = AICAM_FALSE;
    if (scan != NULL && scan->scan_info != NULL) {
        aicam_bool_t have_bssid = NETIF_MAC_IS_UNICAST(cfg.wireless_cfg.bssid) ? AICAM_TRUE : AICAM_FALSE;

        for (uint32_t i = 0; i < (uint32_t)scan->scan_count; i++) {
            if (strncmp(scan->scan_info[i].ssid, cfg.wireless_cfg.ssid,
                        sizeof(scan->scan_info[i].ssid)) != 0) {
                continue;
            }
            if (have_bssid &&
                memcmp(cfg.wireless_cfg.bssid, scan->scan_info[i].bssid, 6) != 0) {
                continue;
            }
            memcpy(cfg.wireless_cfg.bssid, scan->scan_info[i].bssid,
                   sizeof(cfg.wireless_cfg.bssid));
            (void)nm_set_netif_cfg(NETIF_NAME_WIFI_HALOW, &cfg);
            found = AICAM_TRUE;
            break;
        }
    }
    if (!found) {
        return AICAM_ERROR_NOT_FOUND;
    }

    ret = nm_ctrl_netif_up(NETIF_NAME_WIFI_HALOW);
    if (ret != 0) {
        return ret;
    }

    (void)nm_ctrl_set_default_netif(NETIF_NAME_WIFI_HALOW);
    return 0;
}
#endif

static int qn_auto_serial_connect(void)
{
    int ret;

#if NETIF_ETH_WAN_IS_ENABLE
    ret = qn_try_connect_one(COMM_PREF_TYPE_POE, NETIF_NAME_ETH_WAN);
    if (ret == 0) {
        strncpy(s_active_if_name, NETIF_NAME_ETH_WAN, sizeof(s_active_if_name) - 1);
        s_comm_pref_type = COMM_PREF_TYPE_POE;
        return 0;
    }
#endif

#if NETIF_WIFI_HALOW_IS_ENABLE
    ret = qn_halow_try_connect();
    if (ret == 0) {
        strncpy(s_active_if_name, NETIF_NAME_WIFI_HALOW, sizeof(s_active_if_name) - 1);
        s_comm_pref_type = COMM_PREF_TYPE_HALOW;
        return 0;
    }
    qn_deinit_halow_if_needed();
#endif

#if NETIF_4G_CAT1_IS_ENABLE
    ret = qn_try_connect_one(COMM_PREF_TYPE_CELLULAR, NETIF_NAME_4G_CAT1);
    if (ret == 0) {
        strncpy(s_active_if_name, NETIF_NAME_4G_CAT1, sizeof(s_active_if_name) - 1);
        s_comm_pref_type = COMM_PREF_TYPE_CELLULAR;
        return 0;
    }
#endif

    ret = qn_wifi_try_connect_known(3000, QN_WIFI_STA_INIT_UNKNOWN);
    if (ret == 0) {
        strncpy(s_active_if_name, NETIF_NAME_WIFI_STA, sizeof(s_active_if_name) - 1);
        s_comm_pref_type = COMM_PREF_TYPE_WIFI;
        return 0;
    }

    return ret;
}

typedef struct {
    qs_comm_pref_type_t type;
    const char *if_name;
    int result;
    volatile uint32_t done;
} qn_auto_job_t;

static void qn_auto_worker(void *arg)
{
    qn_auto_job_t *j = (qn_auto_job_t *)arg;
    int rc = AICAM_ERROR;

    if (j == NULL || j->if_name == NULL || s_evt == NULL) {
        return;
    }

    /* Winner already selected -> exit quickly. */
    if ((osEventFlagsGet(s_evt) & QN_FLAG_AUTO_WIN) != 0U) {
        j->result = AICAM_ERROR_CANCELLED;
        j->done = 1U;
        return;
    }

    if (j->type == COMM_PREF_TYPE_WIFI) {
        rc = qn_wifi_try_connect_known(3000, QN_WIFI_STA_INIT_UNKNOWN);
    } else {
        /* Do full init+cfg+up, but don't set default route until we win. */
        rc = qn_try_connect_one_ex(j->type, j->if_name, 0);
    }

    j->result = rc;

    if (rc == 0) {
        /* Try to become winner (single-writer). */
        if (s_auto_lock) (void)osMutexAcquire(s_auto_lock, osWaitForever);
        if ((osEventFlagsGet(s_evt) & QN_FLAG_AUTO_WIN) == 0U) {
            s_comm_pref_type = j->type;
            strncpy(s_active_if_name, j->if_name, sizeof(s_active_if_name) - 1);
            s_active_if_name[sizeof(s_active_if_name) - 1] = '\0';
            (void)nm_ctrl_set_default_netif(j->if_name);
            (void)osEventFlagsSet(s_evt, QN_FLAG_AUTO_WIN);
            QT_TRACE("[QN] ", "auto: win %s type=%u", j->if_name, (unsigned)j->type);
        } else {
            /* Another winner exists; best-effort back off. */
            (void)nm_ctrl_netif_down(j->if_name);
        }
        if (s_auto_lock) (void)osMutexRelease(s_auto_lock);
    }

    j->done = 1U;
}

static int qn_auto_parallel_full_connect(void)
{
    qn_auto_job_t jobs[3];
    osThreadId_t tids[3];
    static uint8_t stacks[3][QN_AUTO_WORKER_STACK_SIZE] ALIGN_32;
    uint32_t n = 0U;

    memset(jobs, 0, sizeof(jobs));
    memset(tids, 0, sizeof(tids));

    if (s_evt != NULL) {
        (void)osEventFlagsClear(s_evt, QN_FLAG_AUTO_WIN);
    }

#if NETIF_ETH_WAN_IS_ENABLE
    jobs[n].type = COMM_PREF_TYPE_POE;
    jobs[n].if_name = NETIF_NAME_ETH_WAN;
    n++;
#endif
#if NETIF_4G_CAT1_IS_ENABLE
    jobs[n].type = COMM_PREF_TYPE_CELLULAR;
    jobs[n].if_name = NETIF_NAME_4G_CAT1;
    n++;
#endif
    jobs[n].type = COMM_PREF_TYPE_WIFI;
    jobs[n].if_name = NETIF_NAME_WIFI_STA;
    n++;

    for (uint32_t i = 0U; i < n; i++) {
        const osThreadAttr_t attr = {
            .name = (jobs[i].type == COMM_PREF_TYPE_WIFI) ? "qn_awl" :
                    (jobs[i].type == COMM_PREF_TYPE_CELLULAR) ? "qn_a4g" : "qn_awn",
            .stack_mem = stacks[i],
            .stack_size = sizeof(stacks[i]),
            .priority = osPriorityNormal,
        };
        tids[i] = osThreadNew(qn_auto_worker, &jobs[i], &attr);
        if (tids[i] == NULL) {
            jobs[i].result = AICAM_ERROR_NO_MEMORY;
            jobs[i].done = 1U;
            QT_TRACE("[QN] ", "auto: worker create fail if=%s", jobs[i].if_name);
        }
    }

    const uint32_t t0 = osKernelGetTickCount();
    const uint32_t hz = osKernelGetTickFreq();
    int last_rc = AICAM_ERROR_TIMEOUT;

    for (;;) {
        if ((osEventFlagsGet(s_evt) & QN_FLAG_AUTO_WIN) != 0U) {
            return 0;
        }

        uint32_t done_cnt = 0U;
        for (uint32_t i = 0U; i < n; i++) {
            if (jobs[i].done != 0U) {
                done_cnt++;
                if (jobs[i].result != 0) {
                    last_rc = jobs[i].result;
                }
            }
        }
        if (done_cnt == n) {
            break;
        }
        if (s_net_stop_req) {
            return AICAM_ERROR_CANCELLED;
        }

        uint32_t elapsed_ms = 0U;
        if (hz != 0U) {
            elapsed_ms = ((osKernelGetTickCount() - t0) * 1000U) / hz;
        }
        if (elapsed_ms >= QN_AUTO_CONNECT_TIMEOUT_MS) {
            QT_TRACE("[QN] ", "auto: full connect timeout (cap=%ums)", (unsigned)QN_AUTO_CONNECT_TIMEOUT_MS);
            break;
        }
        (void)osDelay(10U);
    }

    return last_rc;
}

static void qn_network_thread(void *argument)
{
    (void)argument;

    qt_prof_t prof;
    qt_prof_init(&prof);
    QT_TRACE("[QN] ", "net start");
    qt_prof_step(&prof, "[QN] net:start ");

    netif_manager_register();

    qs_comm_pref_type_t pref = COMM_PREF_TYPE_DISABLE;
    if (quick_storage_read_comm_pref_type(&pref) != 0) {
        pref = COMM_PREF_TYPE_AUTO;
    }

    s_comm_pref_type = pref;
    (void)osEventFlagsSet(s_evt, QN_FLAG_CFG_READY);
    QT_TRACE("[QN] ", "pref=%u", (unsigned)pref);
    qt_prof_step(&prof, "[QN] net:cfg ");

    /* Reset results for this run */
    s_net_result = AICAM_ERROR;
    s_net_stop_req = AICAM_FALSE;
    (void)osEventFlagsClear(s_evt, QN_FLAG_NET_DONE | QN_FLAG_NET_OK);
    (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_DONE | QN_FLAG_MQTT_OK | QN_FLAG_MQTT_ERR);
    s_mqtt_cfg_valid = AICAM_FALSE;

    if (pref == COMM_PREF_TYPE_DISABLE) {
        s_active_if_name[0] = '\0';
        s_net_result = AICAM_ERROR_UNAVAILABLE;
        (void)osEventFlagsSet(s_evt, QN_FLAG_NET_DONE);
        QT_TRACE("[QN] ", "disabled");
        return;
    }

    int ret = -1;
    if (s_net_stop_req) {
        s_net_result = AICAM_ERROR_CANCELLED;
        (void)osEventFlagsSet(s_evt, QN_FLAG_NET_DONE);
        return;
    }
    if (pref == COMM_PREF_TYPE_AUTO) {
        /* Serial connect: PoE -> HaLow -> 4G -> WiFi (HaLow/4G mutually exclusive). */
        s_active_if_name[0] = '\0';
        ret = qn_auto_serial_connect();
    } else if (pref == COMM_PREF_TYPE_POE) {
        ret = qn_try_connect_one(COMM_PREF_TYPE_POE, NETIF_NAME_ETH_WAN);
        if (ret == 0) strncpy(s_active_if_name, NETIF_NAME_ETH_WAN, sizeof(s_active_if_name) - 1);
#if NETIF_WIFI_HALOW_IS_ENABLE
    } else if (pref == COMM_PREF_TYPE_HALOW) {
        ret = qn_halow_try_connect();
        if (ret == 0) strncpy(s_active_if_name, NETIF_NAME_WIFI_HALOW, sizeof(s_active_if_name) - 1);
#endif
    } else if (pref == COMM_PREF_TYPE_CELLULAR) {
        ret = qn_try_connect_one(COMM_PREF_TYPE_CELLULAR, NETIF_NAME_4G_CAT1);
        if (ret == 0) strncpy(s_active_if_name, NETIF_NAME_4G_CAT1, sizeof(s_active_if_name) - 1);
    } else if (pref == COMM_PREF_TYPE_WIFI) {
        ret = qn_wifi_try_connect_known(3000, QN_WIFI_STA_INIT_UNKNOWN);
        if (ret == 0) strncpy(s_active_if_name, NETIF_NAME_WIFI_STA, sizeof(s_active_if_name) - 1);
    } else {
        ret = AICAM_ERROR_INVALID_PARAM;
    }

    s_net_result = ret;

    /* Always unblock waiters, but only set OK flag on success. */
    if (ret == 0) {
        (void)osEventFlagsSet(s_evt, QN_FLAG_NET_OK);
    } else {
        s_comm_pref_type = COMM_PREF_TYPE_DISABLE;
        s_active_if_name[0] = '\0';
    }
    (void)osEventFlagsSet(s_evt, QN_FLAG_NET_DONE);

    ret = s_net_result;
    QT_TRACE("[QN] ", "net %s rc=%d",
             (ret == 0) ? "ok" : "fail",
             ret);
    if (ret == 0) {
        QT_TRACE("[QN] ", "active=%s type=%u", s_active_if_name, (unsigned)s_comm_pref_type);
    }
    qt_prof_step(&prof, "[QN] net:done ");
}

static void qn_mqtt_event_handler(ms_mqtt_event_data_t *event_data, void *user_args)
{
    (void)user_args;
    if (!event_data || s_evt == NULL) return;

    if (event_data->event_id == MQTT_EVENT_CONNECTED) {
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_OK);
        QT_TRACE("[QN] ", "mqtt connected");
    } else if (event_data->event_id == MQTT_EVENT_ERROR) {
        s_mqtt_result = (event_data->error_code != 0) ? event_data->error_code : AICAM_ERROR;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_ERR);
        QT_TRACE("[QN] ", "mqtt error %d", s_mqtt_result);
    } else if (event_data->event_id == MQTT_EVENT_DISCONNECTED) {
        /* Allow queue to continue, but mark not-ok until reconnected (if auto reconnect enabled). */
        (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_OK);
        QT_TRACE("[QN] ", "mqtt disconnected");
    } else if (event_data->event_id == MQTT_EVENT_PUBLISHED) {
        /* PUBLISHED event is treated as PUBACK (QoS1) completion for current message id. */
        if (s_wait_pub_msg_id >= 0 && (int)event_data->msg_id == s_wait_pub_msg_id) {
            (void)osEventFlagsSet(s_evt, QN_FLAG_PUB_ACK);
        }
    } else if (event_data->event_id == MQTT_EVENT_DELETED) {
        /* For ms_mqtt_client, publish failures are typically surfaced as DELETED. */
        if (s_wait_pub_msg_id >= 0 && (int)event_data->msg_id == s_wait_pub_msg_id) {
            (void)osEventFlagsSet(s_evt, QN_FLAG_PUB_ERR);
        }
    }
}

/* If the mqtt thread exits before the main publish loop, queued tasks would never run;
 * invoke each callback with @p publish_err so quick_bootstrap releases payload and sets done flags.
 */
static void qn_mqtt_drain_queue(int publish_err)
{
    unsigned n = 0;

    if (s_mqtt_q == NULL) {
        return;
    }
    for (;;) {
        qs_mqtt_task_item_t item;
        memset(&item, 0, sizeof(item));
        if (osMessageQueueGet(s_mqtt_q, &item, NULL, 0) != osOK) {
            break;
        }
        n++;
        if (item.param.cmd == 0xFFU) {
            if (item.param.callback != NULL) {
                item.param.callback(0, item.param.callback_param);
            }
            continue;
        }
        if (item.param.callback != NULL) {
            item.param.callback(publish_err, item.param.callback_param);
        }
    }
    if (n != 0U) {
        QT_TRACE("[QN] ", "mqtt drain tasks=%u err=%d", (unsigned)n, publish_err);
    }
}

static void qn_mqtt_thread_exit_notify(void)
{
    if (s_evt != NULL) {
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_THREAD_EXIT);
    }
}

static void qn_mqtt_thread(void *argument)
{
    (void)argument;

    qt_prof_t prof;
    qt_prof_init(&prof);
    QT_TRACE("[QN] ", "mqtt start");
    qt_prof_step(&prof, "[QN] mqtt:start ");

    s_mqtt_result = AICAM_ERROR;
    s_ms_stop_req = AICAM_FALSE;

    /* 1) Wait config ready, decide whether MQTT should run. */
    (void)osEventFlagsWait(s_evt, QN_FLAG_CFG_READY, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    if (s_comm_pref_type == COMM_PREF_TYPE_DISABLE) {
        s_mqtt_result = AICAM_ERROR_UNAVAILABLE;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
        QT_TRACE("[QN] ", "mqtt skip(disabled)");
        qn_mqtt_drain_queue(s_mqtt_result);
        qn_mqtt_thread_exit_notify();
        return;
    }

    /* 2) Read MQTT config (NVS) inside thread, after network is ready. */
    qs_mqtt_all_config_t mqtt_cfg;
    if (quick_storage_read_mqtt_all_config(&mqtt_cfg) != 0) {
        s_mqtt_result = AICAM_ERROR_NOT_FOUND;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
        QT_TRACE("[QN] ", "mqtt cfg missing");
        qn_mqtt_drain_queue(s_mqtt_result);
        qn_mqtt_thread_exit_notify();
        return;
    }
    s_mqtt_cfg_cache = mqtt_cfg;
    s_mqtt_cfg_valid = AICAM_TRUE;
    qt_prof_step(&prof, "[QN] mqtt:cfg ");

    /* 3) Wait network bring-up attempt to finish; proceed only if network is OK. */
    (void)osEventFlagsWait(s_evt, QN_FLAG_NET_DONE | QN_FLAG_NET_OK, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    if ((osEventFlagsGet(s_evt) & QN_FLAG_NET_OK) == 0) {
        s_mqtt_result = AICAM_ERROR_UNAVAILABLE;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
        QT_TRACE("[QN] ", "mqtt skip(net not ok)");
        qn_mqtt_drain_queue(s_mqtt_result);
        qn_mqtt_thread_exit_notify();
        return;
    }
    printf("net ok, %lu ms\r\n", HAL_GetTick());
    qt_prof_step(&prof, "[QN] mqtt:net ");

    ms_mqtt_client_handle_t client = ms_mqtt_client_init(&mqtt_cfg.ms_mqtt_config);
    if (!client) {
        s_mqtt_result = AICAM_ERROR_NO_MEMORY;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
        QT_TRACE("[QN] ", "ms init fail");
        qn_mqtt_drain_queue(s_mqtt_result);
        qn_mqtt_thread_exit_notify();
        return;
    }
    if (s_mqtt_lock) osMutexAcquire(s_mqtt_lock, osWaitForever);
    s_ms_client = client;
    if (s_mqtt_lock) osMutexRelease(s_mqtt_lock);

    (void)ms_mqtt_client_register_event(client, qn_mqtt_event_handler, NULL);
    if (ms_mqtt_client_start(client) != 0) {
        (void)ms_mqtt_client_destroy(client);
        if (s_mqtt_lock) osMutexAcquire(s_mqtt_lock, osWaitForever);
        if (s_ms_client == client) s_ms_client = NULL;
        if (s_mqtt_lock) osMutexRelease(s_mqtt_lock);
        s_mqtt_result = AICAM_ERROR;
        (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
        QT_TRACE("[QN] ", "ms start fail");
        qn_mqtt_drain_queue(s_mqtt_result);
        qn_mqtt_thread_exit_notify();
        return;
    }

    /* Wait for CONNECTED (event-driven) or ERROR, bounded. */
    uint32_t wait = osEventFlagsWait(
        s_evt,
        QN_FLAG_MQTT_OK | QN_FLAG_MQTT_ERR,
        osFlagsWaitAny | osFlagsNoClear,
        QN_NET_CONNECT_TIMEOUT_MS);

    if ((wait & QN_FLAG_MQTT_OK) != 0) {
        s_mqtt_result = 0;
    } else {
        if (s_mqtt_result == 0) s_mqtt_result = AICAM_ERROR_TIMEOUT;
        /* keep running to allow auto-reconnect, but not marked OK */
    }
    (void)osEventFlagsSet(s_evt, QN_FLAG_MQTT_DONE);
    QT_TRACE("[QN] ", "mqtt init rc=%d", s_mqtt_result);
    qt_prof_step(&prof, "[QN] mqtt:ready ");

    for (;;) {
        qs_mqtt_task_item_t item;
        memset(&item, 0, sizeof(item));
        if (osMessageQueueGet(s_mqtt_q, &item, NULL, osWaitForever) != osOK) {
            continue;
        }

        if (item.param.cmd == 0xFF) {
            /* stop command wakes the thread and exits cleanly */
            s_ms_stop_req = AICAM_TRUE;
            if (item.param.callback) {
                item.param.callback(0, item.param.callback_param);
            }
            QT_TRACE("[QN] ", "mqtt stop");
            break;
        }

        /* If not connected, fail fast (caller callback gets error). */
        if ((osEventFlagsGet(s_evt) & QN_FLAG_MQTT_OK) == 0) {
            if (item.param.callback) {
                item.param.callback(AICAM_ERROR_TIMEOUT, item.param.callback_param);
            }
            QT_TRACE("[QN] ", "pub drop(not conn)");
            continue;
        }

        int pub_ret = ms_mqtt_client_publish(
            client,
            mqtt_cfg.data_report_topic,
            (uint8_t *)item.param.data,
            (int)item.param.data_len,
            (int)mqtt_cfg.data_report_qos,
            0);

        if (pub_ret < 0) {
            if (item.param.callback) {
                item.param.callback(pub_ret, item.param.callback_param);
            }
            QT_TRACE("[QN] ", "pub fail %d", pub_ret);
            continue;
        }
        QT_TRACE("[QN] ", "pub id=%d len=%lu qos=%u ack=%u",
                 pub_ret,
                 (unsigned long)item.param.data_len,
                 (unsigned)mqtt_cfg.data_report_qos,
                 (unsigned)item.param.is_wait_pub_ack);

        if (item.param.is_wait_pub_ack) {
            if (mqtt_cfg.data_report_qos > 0) {
                /* wait PUBACK via MQTT_EVENT_PUBLISHED (or fail via MQTT_EVENT_DELETED) */
                s_wait_pub_msg_id = pub_ret;
                (void)osEventFlagsClear(s_evt, QN_FLAG_PUB_ACK | QN_FLAG_PUB_ERR);
                uint32_t w = osEventFlagsWait(
                    s_evt,
                    QN_FLAG_PUB_ACK | QN_FLAG_PUB_ERR,
                    osFlagsWaitAny | osFlagsNoClear,
                    5000);
                s_wait_pub_msg_id = -1;

                if ((w & QN_FLAG_PUB_ACK) != 0) {
                    if (item.param.callback) item.param.callback(0, item.param.callback_param);
                } else if ((w & QN_FLAG_PUB_ERR) != 0) {
                    if (item.param.callback) item.param.callback(AICAM_ERROR, item.param.callback_param);
                } else {
                    if (item.param.callback) item.param.callback(AICAM_ERROR_TIMEOUT, item.param.callback_param);
                }
            } else {
                osDelay(1000);
                if (item.param.callback) item.param.callback(0, item.param.callback_param);
            }
        } else {
            if (item.param.callback) item.param.callback(0, item.param.callback_param);
        }
    }

    /* requested stop: shutdown client */
    (void)ms_mqtt_client_stop(client);
    (void)ms_mqtt_client_destroy(client);
    if (s_mqtt_lock) osMutexAcquire(s_mqtt_lock, osWaitForever);
    if (s_ms_client == client) s_ms_client = NULL;
    if (s_mqtt_lock) osMutexRelease(s_mqtt_lock);
    qn_mqtt_thread_exit_notify();
}

int quick_network_init(void)
{
    if (s_inited) return 0;

    quick_log_mutex_init();

    if (s_evt == NULL) {
        s_evt = osEventFlagsNew(NULL);
        if (s_evt == NULL) return AICAM_ERROR_NO_MEMORY;
    }
    if (s_mqtt_q == NULL) {
        s_mqtt_q = osMessageQueueNew(QN_MQTT_TASK_QUEUE_SIZE, sizeof(qs_mqtt_task_item_t), &s_mqtt_q_attr);
        if (s_mqtt_q == NULL) return AICAM_ERROR_NO_MEMORY;
    }
    if (s_mqtt_lock == NULL) {
        s_mqtt_lock = osMutexNew(NULL);
        if (s_mqtt_lock == NULL) return AICAM_ERROR_NO_MEMORY;
    }
    if (s_auto_lock == NULL) {
        s_auto_lock = osMutexNew(NULL);
        if (s_auto_lock == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    /* Place stacks in PSRAM to reduce internal SRAM pressure. */
    static uint8_t s_net_stack[4096 * 4] ALIGN_32;
    static uint8_t s_mqtt_stack[4096 * 2] ALIGN_32;

    if (s_net_tid == NULL) {
        static const osThreadAttr_t attr = {
            .name = "qs_net",
            .stack_mem = s_net_stack,
            .stack_size = sizeof(s_net_stack),
            .priority = osPriorityNormal,
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_net_tid = osThreadNew(qn_network_thread, NULL, &attr);
        if (s_net_tid == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    if (s_mqtt_tid == NULL) {
        if (s_evt != NULL) {
            (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_THREAD_EXIT);
        }
        static const osThreadAttr_t attr = {
            .name = "qs_mqtt",
            .stack_mem = s_mqtt_stack,
            .stack_size = sizeof(s_mqtt_stack),
            .priority = osPriorityBelowNormal,
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_mqtt_tid = osThreadNew(qn_mqtt_thread, NULL, &attr);
        if (s_mqtt_tid == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    s_inited = AICAM_TRUE;
    QT_TRACE("[QN] ", "init ok");
    return 0;
}

void quick_network_deinit(void)
{
    if (!s_inited) return;

    /* Stop network thread if still running. */
    s_net_stop_req = AICAM_TRUE;
    if (s_evt) {
        uint32_t flags = osEventFlagsGet(s_evt);
        if ((flags & QN_FLAG_NET_DONE) == 0 && s_net_tid) {
            /* Network bring-up may block inside driver; terminate to guarantee exit. */
            (void)osThreadTerminate(s_net_tid);
            s_net_tid = NULL;
            (void)osEventFlagsSet(s_evt, QN_FLAG_NET_DONE);
            (void)osEventFlagsClear(s_evt, QN_FLAG_NET_OK);
        }
    }

    /* 1) Stop qs_mqtt thread gracefully: it must run ms_mqtt_client_stop/destroy before WiFi down. */
    if (s_evt != NULL) {
        (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_THREAD_EXIT);
    }
    if (s_mqtt_q != NULL && s_mqtt_tid != NULL) {
        qs_mqtt_task_param_t stop_task = {0};
        stop_task.cmd = 0xFF;
        (void)quick_network_add_mqtt_task(&stop_task);
    }
    s_ms_stop_req = AICAM_TRUE;

    if (s_mqtt_tid != NULL) {
        const uint32_t t0 = osKernelGetTickCount();
        const uint32_t hz = osKernelGetTickFreq();
        aicam_bool_t joined = AICAM_FALSE;

        for (;;) {
            if ((osEventFlagsGet(s_evt) & QN_FLAG_MQTT_THREAD_EXIT) != 0U) {
                joined = AICAM_TRUE;
                break;
            }
            uint32_t elapsed_ms = 0U;
            if (hz != 0U) {
                elapsed_ms = ((osKernelGetTickCount() - t0) * 1000U) / hz;
            }
            if (elapsed_ms >= QN_MQTT_THREAD_JOIN_MS) {
                QT_TRACE("[QN] ", "mqtt thread join timeout");
                break;
            }
            (void)osDelay(10U);
        }

        if (joined == AICAM_FALSE) {
            if (s_mqtt_lock != NULL) {
                (void)osMutexAcquire(s_mqtt_lock, osWaitForever);
            }
            ms_mqtt_client_handle_t c = s_ms_client;
            if (s_mqtt_lock != NULL) {
                (void)osMutexRelease(s_mqtt_lock);
            }
            if (c != NULL) {
                (void)ms_mqtt_client_stop(c);
                (void)ms_mqtt_client_destroy(c);
                if (s_mqtt_lock != NULL) {
                    (void)osMutexAcquire(s_mqtt_lock, osWaitForever);
                }
                if (s_ms_client == c) {
                    s_ms_client = NULL;
                }
                if (s_mqtt_lock != NULL) {
                    (void)osMutexRelease(s_mqtt_lock);
                }
            }
            (void)osThreadTerminate(s_mqtt_tid);
            QT_TRACE("[QN] ", "mqtt thread terminate (forced)");
        }
        s_mqtt_tid = NULL;
        if (s_evt != NULL) {
            (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_THREAD_EXIT);
        }
    }

    /* 2) Network interface handling depends on SI91X remote-wakeup readiness. */
    if (!s_si91x_mqtt_ready) {
        /* Not in remote-wakeup keep-alive mode: bring down and deinit active interface. */
        const char *if_name = NULL;
        if (s_comm_pref_type == COMM_PREF_TYPE_WIFI) if_name = NETIF_NAME_WIFI_STA;
#if NETIF_WIFI_HALOW_IS_ENABLE
        else if (s_comm_pref_type == COMM_PREF_TYPE_HALOW) if_name = NETIF_NAME_WIFI_HALOW;
#endif
        else if (s_comm_pref_type == COMM_PREF_TYPE_CELLULAR) if_name = NETIF_NAME_4G_CAT1;
        else if (s_comm_pref_type == COMM_PREF_TYPE_POE) if_name = NETIF_NAME_ETH_WAN;

        if (if_name) {
            (void)nm_ctrl_netif_down(if_name);
            (void)nm_ctrl_netif_deinit(if_name);
        }
    }

    /* Keep SI91X MQTT running; do NOT deinit or change wakeup/low-power modes. */

    /* Minimal reset: allow re-init; keep RTOS objects allocated to avoid use-after-free. */
    s_inited = AICAM_FALSE;
}

int quick_network_wait_config(qs_comm_pref_type_t *comm_pref_type)
{
    if (!comm_pref_type) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    (void)osEventFlagsWait(s_evt, QN_FLAG_CFG_READY, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    *comm_pref_type = s_comm_pref_type;
    return s_net_result;
}

int quick_network_add_mqtt_task(qs_mqtt_task_param_t *mqtt_task_param)
{
    if (!mqtt_task_param) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_mqtt_q == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    if (mqtt_task_param->cmd == 0xFF) {
        /* stop command: data is optional */
    } else {
        /* publish */
        if (!mqtt_task_param->data || mqtt_task_param->data_len == 0) return AICAM_ERROR_INVALID_PARAM;
    }

    qs_mqtt_task_item_t item;
    memset(&item, 0, sizeof(item));
    item.param = *mqtt_task_param; /* shallow copy */

    if (osMessageQueuePut(s_mqtt_q, &item, 0, 0) != osOK) {
        return AICAM_ERROR_FULL;
    }
    if (mqtt_task_param->cmd == 0xFF) {
        QT_TRACE("[QN] ", "q stop");
    } else {
        QT_TRACE("[QN] ", "q pub %luB", (unsigned long)mqtt_task_param->data_len);
    }
    return 0;
}

int quick_network_get_mqtt_result(void)
{
    return s_mqtt_result;
}

int quick_network_switch_remote_wakeup_mode(void)
{
    /* Only meaningful for WiFi communication. */
    if (s_comm_pref_type != COMM_PREF_TYPE_WIFI) {
        return AICAM_ERROR_NOT_SUPPORTED;
    }

    /* 1) Stop and destroy existing MS MQTT client (if any). */
    /* Wake mqtt thread to exit (avoid blocking in queue get). */
    qs_mqtt_task_param_t stop_task = {0};
    stop_task.cmd = 0xFF;
    (void)quick_network_add_mqtt_task(&stop_task);
    s_ms_stop_req = AICAM_TRUE;
    (void)osEventFlagsClear(s_evt, QN_FLAG_MQTT_OK | QN_FLAG_MQTT_ERR);

    /* 2) Switch network to remote wake-up mode. */
    QT_TRACE("[QN] ", "remote wakeup switch");
    int ret = sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_WIFI);
    if (ret != 0) {
        QT_TRACE("[QN] ", "remote mode fail %d", ret);
        return ret;
    }

    /* 3) Use cached MQTT config if available (avoid re-reading NVS). */
    (void)osEventFlagsWait(s_evt, QN_FLAG_CFG_READY, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    /* Prefer waiting mqtt thread attempted to read config; if it never runs, cache may stay invalid. */
    (void)osEventFlagsWait(s_evt, QN_FLAG_MQTT_DONE, osFlagsWaitAny | osFlagsNoClear, 0);

    qs_mqtt_all_config_t mqtt_cfg;
    if (s_mqtt_cfg_valid) {
        mqtt_cfg = s_mqtt_cfg_cache;
    } else {
        /* Fallback: read once here if mqtt thread never populated cache. */
        if (quick_storage_read_mqtt_all_config(&mqtt_cfg) != 0) {
            (void)sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_NORMAL);
            return AICAM_ERROR_NOT_FOUND;
        }
        s_mqtt_cfg_cache = mqtt_cfg;
        s_mqtt_cfg_valid = AICAM_TRUE;
    }

    s_si91x_mqtt_ready = AICAM_FALSE;

    ret = si91x_mqtt_client_init(&mqtt_cfg.ms_mqtt_config);
    if (ret != MQTT_ERR_OK) {
        (void)sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_NORMAL);
        QT_TRACE("[QN] ", "si91x init fail %d", ret);
        return ret;
    }

    ret = si91x_mqtt_client_connnect_sync(10000);
    if (ret != MQTT_ERR_OK) {
        /* don't deinit si91x here; keep state minimal, caller may retry */
        (void)sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_NORMAL);
        QT_TRACE("[QN] ", "si91x conn fail %d", ret);
        return ret;
    }

    /* Subscribe wake-up subject; use receive topic/qos from config. subscribe_sync returns only after ACK. */
    ret = si91x_mqtt_client_subscribe_sync(
        mqtt_cfg.data_receive_topic,
        (int)mqtt_cfg.data_receive_qos,
        5000);
    if (ret != MQTT_ERR_OK) {
        (void)si91x_mqtt_client_disconnect_sync(3000);
        (void)sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_NORMAL);
        QT_TRACE("[QN] ", "si91x sub fail %d", ret);
        return ret;
    }

    /* 4) Enter WiFi low power mode (required before sleep). */
    ret = sl_net_netif_low_power_mode_ctrl(1);
    if (ret != 0) {
        (void)si91x_mqtt_client_disconnect_sync(3000);
        (void)sl_net_netif_romote_wakeup_mode_ctrl(WAKEUP_MODE_NORMAL);
        QT_TRACE("[QN] ", "wifi lp fail %d", ret);
        return ret;
    }

    s_si91x_mqtt_ready = AICAM_TRUE;
    QT_TRACE("[QN] ", "remote ready");
    return 0;
}
