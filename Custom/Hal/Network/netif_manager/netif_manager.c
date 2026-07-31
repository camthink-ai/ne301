#include <string.h>
#include "lwip/tcpip.h"
#include "lwip/init.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/mdns.h"
#include "lwip/netifapi.h"
#if IP_NAT
#include "lwip/ip4_nat.h"
#endif
#include MBEDTLS_CONFIG_FILE
#include "threading_alt.h"
#include "dev_manager.h"
#include "Log/debug.h"
#include "aicam_types.h"
#include "sl_net_netif.h"
#include "mm_halow_netif.h"
#include "w5500_netif.h"
#include "eg912u_gl_netif.h"
#include "usb_ecm_netif.h"
#include "cat1.h"
#include "ms_modem.h"
#include "drtc.h"
#include "iperf_test.h"
#include "ms_mqtt_client_test.h"
#include "ms_network_test.h"
#include "http_client_test.h"
#include "aws_capture.h"
#include "icmp_client.h"
#include "netif_manager.h"
#include "wifi.h"
#include "sl_rsi_ble.h"
#include "rtmp_push_test.h"

static ip_addr_t default_dns_server[DNS_MAX_SERVERS] = {
    IPADDR4_INIT(NETIF_DEFAULT_DNS_SERVER1),
    IPADDR4_INIT(NETIF_DEFAULT_DNS_SERVER2),
};
static const char *default_sntp_server[SNTP_MAX_SERVERS] = {
    "time.windows.com",
    "pool.ntp.org",
    "time1.google.com",
};
static const char *default_if_name = NETIF_DEFAULT_NETIF_NAME;
// static device_t *if_manager_dev = NULL;
const osThreadAttr_t ifTask_attributes = {
    .name = "ifTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 4 * 1024
};

typedef struct {
    char *if_name;
    netif_type_t if_type;
} if_name_type_t;

static const if_name_type_t if_name_type_list[] = {
    {NETIF_NAME_LOCAL, NETIF_TYPE_LOCAL},
    {NETIF_NAME_WIFI_AP, NETIF_TYPE_WIRELESS},
    {NETIF_NAME_WIFI_STA, NETIF_TYPE_WIRELESS},
#if NETIF_WIFI_HALOW_IS_ENABLE
    {NETIF_NAME_WIFI_HALOW, NETIF_TYPE_WIRELESS},
#endif
#if NETIF_4G_CAT1_IS_ENABLE
    {NETIF_NAME_4G_CAT1, NETIF_TYPE_4G},
#endif
#if NETIF_USB_ECM_IS_ENABLE
#if NETIF_USB_ECM_IS_CAT1_MODULE
    {NETIF_NAME_USB_ECM, NETIF_TYPE_4G},
#else
    {NETIF_NAME_USB_ECM, NETIF_TYPE_ETH},
#endif
#endif
#if NETIF_ETH_WAN_IS_ENABLE
    {NETIF_NAME_ETH_WAN, NETIF_TYPE_ETH},
#endif
};

static osMutexId_t netif_manager_mutex = NULL;
static const char *netif_type_str[] = {"local", "wireless", "ethernet", "4g", "unknown"};
static const char *netif_state_str[] = {"deinit", "down", "up", "unknown"};
static const char *netif_security_str[] = {"open", "wpa", "wpa2", "wep", "wpa_enterprise", "wpa2_enterprise", "wpa_wpa2_mixed", "wpa3", "wpa3_transition", "wpa3_enterprise", "wpa3_transition_enterprise", "owe", "sae", "unknown"};
static const char *netif_encryption_str[] = {"default", "no_encryption", "wep", "tkip", "ccmp", "eap_tls", "eap_ttls", "eap_fast", "peap_mschapv2", "eap_leap", "unknown"};

void sntp_set_system_time(uint32_t sec)
{
    rtc_set_timeStamp(sec);
    LOG_SIMPLE("NTP: %d s\r\n", sec);
}

void sntp_get_system_time(uint32_t *sec, uint32_t *us)
{
    *sec = (uint32_t)rtc_get_timeStamp();
    *us = 0;
}

static const char *wireless_scan_cb_if_name = NETIF_NAME_WIFI_STA;

static void wireless_scan_callback_func(int recode, wireless_scan_result_t *scan_result)
{
    if (recode == 0) {
        nm_print_wireless_scan_result(wireless_scan_cb_if_name, scan_result);
    } else {
        LOG_SIMPLE("wireless scan failed: %d\r\n", recode);
    }
}

#if NETIF_WIFI_HALOW_IS_ENABLE
static void netif_cli_halow_dpp_cb(const mm_halow_dpp_evt_info_t *info, void *user_arg)
{
    (void)user_arg;

    if (info == NULL) {
        return;
    }

    switch (info->event) {
    case MM_HALOW_DPP_EVT_SUCCESS:
        LOG_SIMPLE("HaLow DPP OK: ssid=%s sec=%s -> 'ifconfig hw up'\r\n",
                   info->ssid != NULL ? info->ssid : "",
                   info->security == WIRELESS_SAE ? "sae" : "open");
        break;
    case MM_HALOW_DPP_EVT_SESSION_OVERLAP:
        LOG_SIMPLE("HaLow DPP: session overlap\r\n");
        break;
    case MM_HALOW_DPP_EVT_TIMEOUT:
        LOG_SIMPLE("HaLow DPP: timeout\r\n");
        break;
    case MM_HALOW_DPP_EVT_STOPPED:
        LOG_SIMPLE("HaLow DPP: stopped\r\n");
        break;
    case MM_HALOW_DPP_EVT_FAILED:
    default:
        LOG_SIMPLE("HaLow DPP: failed\r\n");
        break;
    }
}
#endif

#if NETIF_WIFI_HALOW_IS_ENABLE
static int halow_cli_parse_mcs(const char *s, int8_t *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(s, "none") == 0 || strcmp(s, "auto") == 0 || strcmp(s, "off") == 0) {
        *out = -1;
        return 0;
    }
    {
        int v = atoi(s);
        if (v < 0 || v > 9) {
            return -1;
        }
        *out = (int8_t)v;
    }
    return 0;
}

static int halow_cli_parse_bw(const char *s, int8_t *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(s, "none") == 0 || strcmp(s, "auto") == 0 || strcmp(s, "off") == 0) {
        *out = -1;
        return 0;
    }
    {
        int v = atoi(s);
        if (v != 1 && v != 2 && v != 4 && v != 8) {
            return -1;
        }
        *out = (int8_t)v;
    }
    return 0;
}

static int halow_cli_parse_gi(const char *s, int8_t *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(s, "none") == 0 || strcmp(s, "auto") == 0 || strcmp(s, "off") == 0) {
        *out = -1;
        return 0;
    }
    if (strcmp(s, "short") == 0 || strcmp(s, "0") == 0) {
        *out = 0;
        return 0;
    }
    if (strcmp(s, "long") == 0 || strcmp(s, "1") == 0) {
        *out = 1;
        return 0;
    }
    return -1;
}

static void halow_print_rate_info_lines(const halow_wireless_config_t *hc)
{
    char mcs[12];
    char bw[16];
    char gi[12];

    if (hc == NULL) {
        return;
    }

    if (hc->rc_mcs < 0) {
        strncpy(mcs, "auto", sizeof(mcs));
    } else {
        snprintf(mcs, sizeof(mcs), "%d", (int)hc->rc_mcs);
    }
    if (hc->rc_bw_mhz < 0) {
        strncpy(bw, "auto", sizeof(bw));
    } else {
        snprintf(bw, sizeof(bw), "%d MHz", (int)hc->rc_bw_mhz);
    }
    if (hc->rc_gi < 0) {
        strncpy(gi, "auto", sizeof(gi));
    } else if (hc->rc_gi == 0) {
        strncpy(gi, "short", sizeof(gi));
    } else {
        strncpy(gi, "long", sizeof(gi));
    }
    mcs[sizeof(mcs) - 1] = '\0';
    bw[sizeof(bw) - 1] = '\0';
    gi[sizeof(gi) - 1] = '\0';

    printf("HALOW_RATE_MCS: %s\r\nHALOW_RATE_BW: %s\r\nHALOW_RATE_GI: %s\r\n", mcs, bw, gi);
}
#endif

static int netif_manager_cmd(int argc, char* argv[]) 
{
    int ret = 0;
    char *if_name = NULL;
    uint8_t enable = 1;
    sl_net_wakeup_mode_t wakeup_mode = WAKEUP_MODE_NORMAL;
    netif_info_t *if_info_list = NULL;
    netif_config_t if_cfg = {0};
    wireless_scan_result_t *scan_result = NULL;

    if (argc == 1) {
        LOG_SIMPLE("\r\nDefault netif: %s (%s)", nm_get_default_netif_name(), nm_get_set_default_netif_name());
        LOG_SIMPLE("Dns server list: %d.%d.%d.%d, %d.%d.%d.%d", default_dns_server[0].addr & 0xFF, (default_dns_server[0].addr >> 8) & 0xFF, (default_dns_server[0].addr >> 16) & 0xFF, (default_dns_server[0].addr >> 24) & 0xFF, default_dns_server[1].addr & 0xFF, (default_dns_server[1].addr >> 8) & 0xFF, (default_dns_server[1].addr >> 16) & 0xFF, (default_dns_server[1].addr >> 24) & 0xFF);
        LOG_SIMPLE("Sntp server list: %s, %s, %s", default_sntp_server[0], default_sntp_server[1], default_sntp_server[2]);
        LOG_SIMPLE("Netif list:\r\n");
        // Print all initialized network interface information, default interface first
        ret = nm_get_netif_list(&if_info_list);
        if (ret <= 0) return ret;
        nm_print_netif_info(nm_get_default_netif_name(), NULL);
        for (int i = 0; i < ret; i++) {
            if (strcmp(if_info_list[i].if_name, nm_get_default_netif_name()) == 0) continue;
            if (if_info_list[i].state < NETIF_STATE_DOWN) continue;
            nm_print_netif_info(NULL, &if_info_list[i]);
        }
        nm_free_netif_list(if_info_list);
        return 0;
    }

    if (argc < 3) {
        LOG_SIMPLE("Usage: ifconfig [name] [cmd]\r\nname: lo/wl/ap/hw/wn/4g/ue \r\ncmd: init/up/down/deinit/cfg/info\r\n");
        return -1;
    }

    if (strcmp(argv[1], NETIF_NAME_WIFI_STA) == 0) if_name = NETIF_NAME_WIFI_STA;
    else if (strcmp(argv[1], NETIF_NAME_WIFI_AP) == 0) if_name = NETIF_NAME_WIFI_AP;
#if NETIF_WIFI_HALOW_IS_ENABLE
    else if (strcmp(argv[1], NETIF_NAME_WIFI_HALOW) == 0) if_name = NETIF_NAME_WIFI_HALOW;
#endif
    else if (strcmp(argv[1], NETIF_NAME_LOCAL) == 0) if_name = NETIF_NAME_LOCAL;
#if NETIF_ETH_WAN_IS_ENABLE
    else if (strcmp(argv[1], NETIF_NAME_ETH_WAN) == 0) if_name = NETIF_NAME_ETH_WAN;
#endif
#if NETIF_4G_CAT1_IS_ENABLE
    else if (strcmp(argv[1], NETIF_NAME_4G_CAT1) == 0) if_name = NETIF_NAME_4G_CAT1;
#endif
#if NETIF_USB_ECM_IS_ENABLE
    else if (strcmp(argv[1], NETIF_NAME_USB_ECM) == 0) if_name = NETIF_NAME_USB_ECM;
#endif
    else {
        LOG_SIMPLE("Invalid netif name: %s\r\n", argv[1]);
        return -1;
    }

    if (strcmp(argv[2], "init") == 0) ret = nm_ctrl_netif_init(if_name);
    else if (strcmp(argv[2], "up") == 0) ret = nm_ctrl_netif_up(if_name);
    else if (strcmp(argv[2], "down") == 0) ret = nm_ctrl_netif_down(if_name);
    else if (strcmp(argv[2], "deinit") == 0) ret = nm_ctrl_netif_deinit(if_name);
    else if (strcmp(argv[2], "info") == 0) {
        nm_print_netif_info(if_name, NULL);
        ret = 0;
    } else if (strcmp(argv[2], "cfg") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)
#if NETIF_WIFI_HALOW_IS_ENABLE
            && strcmp(if_name, NETIF_NAME_WIFI_HALOW)
#endif
            && strcmp(if_name, NETIF_NAME_4G_CAT1) && strcmp(if_name, NETIF_NAME_ETH_WAN)) {
            LOG_SIMPLE("Only wl/ap/hw/4g/wn support cfg cmd\r\n");
            return -1;
        }
        if (argc < 4) {
            LOG_SIMPLE("Usage: ifconfig [name] cfg [ssid] [pw]\r\nname: wl/ap\r\n");
            LOG_SIMPLE("Usage: ifconfig [name] cfg [apn]\r\nname: 4g\r\n");
            LOG_SIMPLE("Usage: ifconfig [name] cfg [ip_addr] [gw] [netmask]\r\nname: wn\r\n");
#if NETIF_WIFI_HALOW_IS_ENABLE
            LOG_SIMPLE("Usage: ifconfig hw cfg reg <CC>\r\n");
#endif
            return -1;
        }
        // Get current configuration
        ret = nm_get_netif_cfg(if_name, &if_cfg);
        if (ret != 0) return ret;
#if NETIF_WIFI_HALOW_IS_ENABLE
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0 &&
            strcmp(argv[3], "reg") == 0) {
            if (argc < 5 || strlen(argv[4]) < 2U) {
                LOG_SIMPLE("Usage: ifconfig hw cfg reg <CC>\r\n");
                return -1;
            }
            if (!mm_halow_regdomain_is_supported(argv[4])) {
                LOG_SIMPLE("Regdomain %s not in firmware BCF (ifconfig hw reg_list)\r\n", argv[4]);
                return -1;
            }
            strncpy(if_cfg.halow_cfg.country_code, argv[4],
                    sizeof(if_cfg.halow_cfg.country_code) - 1);
            if_cfg.halow_cfg.country_code[sizeof(if_cfg.halow_cfg.country_code) - 1] = '\0';
            return nm_set_netif_cfg(if_name, &if_cfg);
        }
#endif
        // Update configuration
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0 || strcmp(if_name, NETIF_NAME_WIFI_AP) == 0
#if NETIF_WIFI_HALOW_IS_ENABLE
            || strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0
#endif
        ) {
            strncpy(if_cfg.wireless_cfg.ssid, argv[3], NETIF_SSID_VALUE_SIZE - 1);
            if_cfg.wireless_cfg.ssid[NETIF_SSID_VALUE_SIZE - 1] = '\0';
            if (argc > 4 && argv[4][0] != '\0') {
                strncpy(if_cfg.wireless_cfg.pw, argv[4], NETIF_PW_VALUE_SIZE - 1);
                if_cfg.wireless_cfg.pw[NETIF_PW_VALUE_SIZE - 1] = '\0';
#if NETIF_WIFI_HALOW_IS_ENABLE
                if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
                    if (argc > 5) {
                        if (strcmp(argv[5], "open") == 0) if_cfg.wireless_cfg.security = WIRELESS_OPEN;
                        else if (strcmp(argv[5], "owe") == 0) if_cfg.wireless_cfg.security = WIRELESS_OWE;
                        else if (strcmp(argv[5], "sae") == 0) if_cfg.wireless_cfg.security = WIRELESS_SAE;
                        else if_cfg.wireless_cfg.security = WIRELESS_SAE;
                    } else {
                        if_cfg.wireless_cfg.security = WIRELESS_SAE;
                    }
                } else
#endif
                {
                    if_cfg.wireless_cfg.security = WIRELESS_WPA_WPA2_MIXED;
                }
            } else {
                memset(if_cfg.wireless_cfg.pw, 0, NETIF_PW_VALUE_SIZE);
                if_cfg.wireless_cfg.security = WIRELESS_OPEN;
            }
        } else if (strcmp(if_name, NETIF_NAME_4G_CAT1) == 0) {
            strncpy(if_cfg.cellular_cfg.apn, argv[3], sizeof(if_cfg.cellular_cfg.apn));
        } else if (strcmp(if_name, NETIF_NAME_ETH_WAN) == 0) {
            if_cfg.ip_mode = NETIF_IP_MODE_STATIC;
            // parse ip_addr, gw, netmask, from argv (192.168.1.100 192.168.1.1 255.255.255.0)
            if (argc < 4) {
                LOG_SIMPLE("Usage: ifconfig [name] cfg [ip_addr] [gw] [netmask]\r\nname: wn\r\n");
                return -1;
            }
            unsigned int ip_addr_u32[4];
            // IP address
            if (sscanf(argv[3], "%u.%u.%u.%u", &ip_addr_u32[0], &ip_addr_u32[1], &ip_addr_u32[2], &ip_addr_u32[3]) != 4) {
                LOG_SIMPLE("Invalid ip_addr: %s\r\n", argv[3]);
                return -1;
            }
            for (int i = 0; i < 4; i++) {
                if (ip_addr_u32[i] > 255) {
                    LOG_SIMPLE("Invalid ip_addr value: %u\r\n", ip_addr_u32[i]);
                    return -1;
                }
                if_cfg.ip_addr[i] = (uint8_t)ip_addr_u32[i];
            }
            // Gateway (optional)
            if (argc > 4) {
                if (sscanf(argv[4], "%u.%u.%u.%u", &ip_addr_u32[0], &ip_addr_u32[1], &ip_addr_u32[2], &ip_addr_u32[3]) != 4) {
                    LOG_SIMPLE("Invalid gw: %s\r\n", argv[4]);
                    return -1;
                }
                for (int i = 0; i < 4; i++) {
                    if (ip_addr_u32[i] > 255) {
                        LOG_SIMPLE("Invalid gw value: %u\r\n", ip_addr_u32[i]);
                        return -1;
                    }
                    if_cfg.gw[i] = (uint8_t)ip_addr_u32[i];
                }
            }
            // Netmask (optional)
            if (argc > 5) {
                if (sscanf(argv[5], "%u.%u.%u.%u", &ip_addr_u32[0], &ip_addr_u32[1], &ip_addr_u32[2], &ip_addr_u32[3]) != 4) {
                    LOG_SIMPLE("Invalid netmask: %s\r\n", argv[5]);
                    return -1;
                }
                for (int i = 0; i < 4; i++) {
                    if (ip_addr_u32[i] > 255) {
                        LOG_SIMPLE("Invalid netmask value: %u\r\n", ip_addr_u32[i]);
                        return -1;
                    }
                    if_cfg.netmask[i] = (uint8_t)ip_addr_u32[i];
                }
            }
        } else {
            LOG_SIMPLE("Invalid netif name: %s\r\n", if_name);
            return -1;
        }
        // Enable configuration
        ret = nm_set_netif_cfg(if_name, &if_cfg);
    } else if (strcmp(argv[2], "error_test") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP) && strcmp(if_name, NETIF_NAME_ETH_WAN)) {
            LOG_SIMPLE("Only wl/ap/wn support error_test cmd\r\n");
            return -1;
        }
        if (strcmp(if_name, NETIF_NAME_ETH_WAN) == 0) {
            w5500_netif_reset_test();
        } else {
            sli_firmware_error_callback(0x1234);
        }
    } else if (strcmp(argv[2], "fbcast") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)) {
            LOG_SIMPLE("Only wl/ap support fbcast cmd\r\n");
            return -1;
        }
        if (argc > 3) enable = atoi(argv[3]);
        ret = sl_net_netif_filter_broadcast_ctrl(enable);
    } else if (strcmp(argv[2], "lpwr") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)) {
            LOG_SIMPLE("Only wl/ap support lpwr cmd\r\n");
            return -1;
        }
        if (argc > 3) enable = atoi(argv[3]);
        ret = sl_net_netif_low_power_mode_ctrl(enable);
    } else if (strcmp(argv[2], "rmode") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)) {
            LOG_SIMPLE("Only wl/ap support rmode cmd\r\n");
            return -1;
        }
        if (argc > 3) wakeup_mode = (sl_net_wakeup_mode_t)atoi(argv[3]);
        ret = sl_net_netif_romote_wakeup_mode_ctrl(wakeup_mode);
    } else if (strcmp(argv[2], "scan") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)
#if NETIF_WIFI_HALOW_IS_ENABLE
            && strcmp(if_name, NETIF_NAME_WIFI_HALOW)
#endif
        ) {
            LOG_SIMPLE("Only wl/ap/hw support scan cmd\r\n");
            return -1;
        }
        wireless_scan_cb_if_name = if_name;
        ret = nm_wireless_start_scan_ex(if_name, wireless_scan_callback_func);
        return ret;
    } else if (strcmp(argv[2], "scan_result") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)
#if NETIF_WIFI_HALOW_IS_ENABLE
            && strcmp(if_name, NETIF_NAME_WIFI_HALOW)
#endif
        ) {
            LOG_SIMPLE("Only wl/ap/hw support scan_result cmd\r\n");
            return -1;
        }
        scan_result = nm_wireless_get_scan_result_ex(if_name);
        nm_print_wireless_scan_result(if_name, scan_result);
    } else if (strcmp(argv[2], "scan_update") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) && strcmp(if_name, NETIF_NAME_WIFI_AP)
#if NETIF_WIFI_HALOW_IS_ENABLE
            && strcmp(if_name, NETIF_NAME_WIFI_HALOW)
#endif
        ) {
            LOG_SIMPLE("Only wl/ap/hw support scan_update cmd\r\n");
            return -1;
        }
        ret = nm_wireless_update_scan_result_ex(if_name, 3000);
#if NETIF_WIFI_HALOW_IS_ENABLE
    } else if (strcmp(argv[2], "reg") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0 || argc < 4) {
            LOG_SIMPLE("Usage: ifconfig hw reg <CC>\r\n");
            return -1;
        }
        if (!mm_halow_regdomain_is_supported(argv[3])) {
            LOG_SIMPLE("Regdomain %s not in regulatory_db (ifconfig hw reg_list)\r\n", argv[3]);
            return -1;
        }
        ret = mm_halow_set_regdomain(argv[3]);
    } else if (strcmp(argv[2], "reg_list") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Usage: ifconfig hw reg_list\r\n");
            return -1;
        }
        ret = mm_halow_list_regdomains();
    } else if (strcmp(argv[2], "txpwr") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0 || argc < 4) {
            LOG_SIMPLE("Usage: ifconfig hw txpwr <dbm|0>\r\n");
            return -1;
        }
        ret = mm_halow_set_tx_power((uint16_t)atoi(argv[3]));
    } else if (strcmp(argv[2], "rate") == 0) {
        int8_t mcs = -1;
        int8_t bw = -1;
        int8_t gi = -1;

        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Only hw supports rate cmd\r\n");
            return -1;
        }
        if (argc < 4) {
            ret = mm_halow_print_rate_override();
        } else if (strcmp(argv[3], "off") == 0 || strcmp(argv[3], "none") == 0) {
            ret = mm_halow_set_rate_override(-1, -1, -1);
        } else {
            if (halow_cli_parse_mcs(argv[3], &mcs) != 0) {
                LOG_SIMPLE("Usage: ifconfig hw rate [mcs|none] [bw|none] [gi|none]\r\n");
                LOG_SIMPLE("  mcs: 0-9 or none; bw: 1/2/4/8 or none; gi: short/long/none\r\n");
                LOG_SIMPLE("  e.g. rate 7 | rate 7 8 short | rate off\r\n");
                return -1;
            }
            if (argc > 4) {
                if (halow_cli_parse_bw(argv[4], &bw) != 0) {
                    LOG_SIMPLE("Invalid bw: %s (use 1/2/4/8 or none)\r\n", argv[4]);
                    return -1;
                }
            }
            if (argc > 5) {
                if (halow_cli_parse_gi(argv[5], &gi) != 0) {
                    LOG_SIMPLE("Invalid gi: %s (use short/long/none)\r\n", argv[5]);
                    return -1;
                }
            }
            ret = mm_halow_set_rate_override(mcs, bw, gi);
        }
    } else if (strcmp(argv[2], "ps") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0 || argc < 4) {
            LOG_SIMPLE("Usage: ifconfig hw ps <0|1>\r\n");
            return -1;
        }
        ret = mm_halow_set_power_save((uint8_t)atoi(argv[3]));
    } else if (strcmp(argv[2], "scan_cfg") == 0) {
        uint8_t ndp = 0;
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0 || argc < 4) {
            LOG_SIMPLE("Usage: ifconfig hw scan_cfg <dwell_ms> [ndp 0|1]\r\n");
            return -1;
        }
        if (argc > 4) ndp = (uint8_t)atoi(argv[4]);
        ret = mm_halow_set_scan_config((uint32_t)atoi(argv[3]), ndp);
    } else if (strcmp(argv[2], "version") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Only hw supports version cmd\r\n");
            return -1;
        }
        ret = mm_halow_print_version();
    } else if (strcmp(argv[2], "bcf_info") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Only hw supports bcf_info cmd\r\n");
            return -1;
        }
        if (argc >= 4) {
            ret = mm_halow_print_bcf_info(argv[3]);
        } else {
            ret = mm_halow_print_bcf_info(NULL);
        }
    } else if (strcmp(argv[2], "dpp") == 0) {
        uint32_t timeout_ms = 120000U;
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Only hw supports dpp cmd\r\n");
            return -1;
        }
        if (argc >= 4 && strcmp(argv[3], "stop") == 0) {
            ret = mm_halow_dpp_stop();
        } else {
            if (argc >= 4) {
                timeout_ms = (uint32_t)atoi(argv[3]) * 1000U;
            }
            ret = mm_halow_dpp_start(timeout_ms, netif_cli_halow_dpp_cb, NULL);
        }
    } else if (strcmp(argv[2], "dpp_stop") == 0) {
        if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) != 0) {
            LOG_SIMPLE("Only hw supports dpp_stop cmd\r\n");
            return -1;
        }
        ret = mm_halow_dpp_stop();
#endif
    } else {
        LOG_SIMPLE("Invalid netif cmd: %s\r\n", argv[2]);
        return -1;
    }
    
    LOG_SIMPLE("Netif(%s) exec CMD(%s) ret: %d\r\n", if_name, argv[2], ret);
    return ret;
}

void netif_manager_change_default_if(void)
{
    int i = sizeof(if_name_type_list) / sizeof(if_name_type_t);
    const char *if_name = NULL;
    struct netif *default_if = NULL;
    netif_state_t state = NETIF_STATE_MAX;

    osMutexAcquire(netif_manager_mutex, osWaitForever);
    state = nm_get_netif_state(default_if_name);
    if (state == NETIF_STATE_UP) {
        if_name = default_if_name;
    } else {
        for (; i > 0; i--) {
            state = nm_get_netif_state(if_name_type_list[i - 1].if_name);
            if (state == NETIF_STATE_UP) {
                if_name = if_name_type_list[i - 1].if_name;
                break;
            }
        }
    }
    
    if (if_name != NULL) {
#if NETIF_ETH_WAN_IS_ENABLE
        if (strcmp(if_name, NETIF_NAME_ETH_WAN) == 0) default_if = w5500_netif_ptr();
        else
#endif
#if NETIF_4G_CAT1_IS_ENABLE
        if (strcmp(if_name, NETIF_NAME_4G_CAT1) == 0) default_if = eg912u_netif_ptr();
        else
#endif
#if NETIF_USB_ECM_IS_ENABLE
        if (strcmp(if_name, NETIF_NAME_USB_ECM) == 0) default_if = usb_ecm_netif_ptr();
        else
#endif
        if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0) default_if = sl_net_client_netif_ptr();
        else if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) default_if = sl_net_ap_netif_ptr();
#if NETIF_WIFI_HALOW_IS_ENABLE
        else if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) default_if = mm_halow_netif_ptr();
#endif
        if (default_if != NULL && default_if != netif_get_default()) {
            (void)netifapi_netif_set_default(default_if);
            LOG_DRV_INFO("Set default netif: %s\r\n", if_name);
        }
    }
    osMutexRelease(netif_manager_mutex);
}

#if IP_NAT
static ip4_nat_entry_t ap_nat_wn_entry = {0};
static uint8_t ap_nat_wn_is_add = 0;
static ip4_nat_entry_t ap_nat_sta_entry = {0};
static uint8_t ap_nat_sta_is_add = 0;
void netif_manager_change_nat_route(void)
{
    netif_state_t ap_state = NETIF_STATE_MAX, eth_state = NETIF_STATE_MAX;
    netif_state_t sta_state = NETIF_STATE_MAX;
    struct netif *ap = NULL, *eth = NULL;
    struct netif *sta = NULL;
    err_t ret = ERR_OK;

    osMutexAcquire(netif_manager_mutex, osWaitForever);
    ap_state = nm_get_netif_state(NETIF_NAME_WIFI_AP);
    sta_state = nm_get_netif_state(NETIF_NAME_WIFI_STA);
    eth_state = nm_get_netif_state(NETIF_NAME_ETH_WAN);
    if (ap_state == NETIF_STATE_UP && sta_state == NETIF_STATE_UP && ap_nat_sta_is_add == 0) {
        ap = sl_net_ap_netif_ptr();
        ap_nat_sta_entry.source_net.addr = ap->ip_addr.addr;
        ap_nat_sta_entry.source_netmask.addr = ap->netmask.addr;
        ap_nat_sta_entry.in_if = ap;
        sta = sl_net_client_netif_ptr();
        ap_nat_sta_entry.dest_net.addr = sta->ip_addr.addr;
        ap_nat_sta_entry.dest_netmask.addr = sta->netmask.addr;
        ap_nat_sta_entry.out_if = sta;
        ret = ip4_nat_add(&ap_nat_sta_entry);
        if (ret == ERR_OK) {
            ap_nat_sta_is_add = 1;
            LOG_DRV_INFO("Nat add: AP <-> STA");
        } else {
            LOG_DRV_ERROR("Nat add fail: AP <-> STA, ret: %d", ret);
        }
    } else if (ap_nat_sta_is_add) {
        ip4_nat_remove(&ap_nat_sta_entry);
        ap_nat_sta_is_add = 0;
        LOG_DRV_INFO("Nat remove: AP <-> STA");
    }
    
    if (ap_state == NETIF_STATE_UP && eth_state == NETIF_STATE_UP && ap_nat_wn_is_add == 0) {
        ap = sl_net_ap_netif_ptr();
        ap_nat_wn_entry.source_net.addr = ap->ip_addr.addr;
        ap_nat_wn_entry.source_netmask.addr = ap->netmask.addr;
        ap_nat_wn_entry.in_if = ap;
        eth = w5500_netif_ptr();
        ap_nat_wn_entry.dest_net.addr = eth->ip_addr.addr;
        ap_nat_wn_entry.dest_netmask.addr = eth->netmask.addr;
        ap_nat_wn_entry.out_if = eth;
        ret = ip4_nat_add(&ap_nat_wn_entry);
        if (ret == ERR_OK) {
            ap_nat_wn_is_add = 1;
            LOG_DRV_INFO("Nat add: AP <-> WN");
        } else {
            LOG_DRV_ERROR("Nat add fail: AP <-> WN, ret: %d", ret);
        }
    } else if (ap_nat_wn_is_add) {
        ip4_nat_remove(&ap_nat_wn_entry);
        ap_nat_wn_is_add = 0;
        LOG_DRV_INFO("Nat remove: AP <-> WN");
    }
    osMutexRelease(netif_manager_mutex);
}
#endif

int netif_manager_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{   
    int ret = -1;
    uint32_t start_tick = 0, end_tick = 0;
    struct netif *lo_netif = NULL;
    netif_info_t *netif_info = NULL;
    netif_state_t last_state = NETIF_STATE_MAX;
    if (netif_manager_mutex == NULL) return AICAM_ERROR_NOT_INITIALIZED;
    
    start_tick = HAL_GetTick();
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0 || strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
        sl_net_netif_ctrl(if_name, NETIF_CMD_STATE, &last_state);
        ret = sl_net_netif_ctrl(if_name, cmd, param);
#if NETIF_WIFI_HALOW_IS_ENABLE
    } else if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
        mm_halow_netif_ctrl(if_name, NETIF_CMD_STATE, &last_state);
        ret = mm_halow_netif_ctrl(if_name, cmd, param);
#endif
#if NETIF_4G_CAT1_IS_ENABLE
    } else if (strcmp(if_name, NETIF_NAME_4G_CAT1) == 0) {
        eg912u_netif_ctrl(if_name, NETIF_CMD_STATE, &last_state);
        ret = eg912u_netif_ctrl(if_name, cmd, param);
#endif
#if NETIF_ETH_WAN_IS_ENABLE
    } else if (strcmp(if_name, NETIF_NAME_ETH_WAN) == 0) {
        w5500_netif_ctrl(if_name, NETIF_CMD_STATE, &last_state);
        ret = w5500_netif_ctrl(if_name, cmd, param);
#endif
#if NETIF_USB_ECM_IS_ENABLE
    } else if (strcmp(if_name, NETIF_NAME_USB_ECM) == 0) {
        usb_ecm_netif_ctrl(if_name, NETIF_CMD_STATE, &last_state);
        ret = usb_ecm_netif_ctrl(if_name, cmd, param);
#endif
    } else if (strcmp(if_name, NETIF_NAME_LOCAL) == 0) {
        switch (cmd) {
            case NETIF_CMD_INFO:
                netif_info = (netif_info_t *)param;
                lo_netif = netif_find(NETIF_NAME_LOCAL"0");
                if (netif_info != NULL && lo_netif != NULL) {
                    netif_info = (netif_info_t *)param;

                    netif_info->if_name = NETIF_NAME_LOCAL;
                #if LWIP_NETIF_HOSTNAME
                    netif_info->host_name = lo_netif->hostname;
                #else
                    netif_info->host_name = NULL;
                #endif
                    netif_info->state = NETIF_STATE_UP;
                    netif_info->type = NETIF_TYPE_LOCAL;
                    netif_info->rssi = 0;
                    netif_info->ip_mode = NETIF_IP_MODE_STATIC;
                    snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "lwip_%d.%d.%d_r%d", LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION, LWIP_VERSION_RC);
                    memcpy(netif_info->if_mac, lo_netif->hwaddr, sizeof(netif_info->if_mac));
                    memcpy(netif_info->ip_addr, &lo_netif->ip_addr.addr, sizeof(netif_info->ip_addr));
                    memcpy(netif_info->netmask, &lo_netif->netmask.addr, sizeof(netif_info->netmask));
                    memcpy(netif_info->gw, &lo_netif->gw.addr, sizeof(netif_info->gw));
                    ret = 0;
                }
                break;
            case NETIF_CMD_STATE:
                *(netif_state_t *)param = NETIF_STATE_UP;
                ret = 0;
                break;
            default:
                break;
        }
    }
    if (ret == 0 && (cmd == NETIF_CMD_UP || (last_state == NETIF_STATE_UP && (cmd == NETIF_CMD_DOWN || cmd == NETIF_CMD_UNINIT)))) {
        netif_manager_change_default_if();
    #if IP_NAT
        netif_manager_change_nat_route();
    #endif
    }

    if (cmd == NETIF_CMD_INIT || cmd == NETIF_CMD_UP) {
        end_tick = HAL_GetTick();
        printf("%s %s: %ld / %ld ms\r\n", if_name, (cmd == NETIF_CMD_INIT) ? "init" : "up", end_tick - start_tick, end_tick);
    }

    return ret;
}

// netif_manager_process removed - initialization is now handled by netif_init_manager
// This function is no longer needed as network interfaces are initialized asynchronously
// by the application layer using netif_init_manager

debug_cmd_reg_t if_manager_cmd_table[] = {
    {"ifconfig",    "Netif control.",      netif_manager_cmd},
};

static void ifconfig_cmd_register(void)
{
    debug_cmdline_register(if_manager_cmd_table, sizeof(if_manager_cmd_table) / sizeof(if_manager_cmd_table[0]));
}

void netif_manager_init(void)
{
    if (netif_manager_mutex != NULL) return;
    
    LOG_DRV_INFO("Netif manager framework initialization (lightweight)");
    uint32_t start_time = osKernelGetTickCount();
    
    netif_manager_mutex = osMutexNew(NULL);
    if (netif_manager_mutex == NULL) {
        LOG_DRV_ERROR("Failed to create netif manager mutex");
        return;
    }

    // 1. Initialize WiFi driver framework (fast, < 1s)
    sl_net_netif_init();
    
    // 2. Initialize mbedTLS threading support
    mbedtls_threading_alt_init();
    
    // 3. Initialize LwIP TCP/IP stack
    tcpip_init(NULL, NULL);
    
#if IP_NAT
    // 4. Initialize NAT if enabled
    ip4_nat_init();
#endif

#if LWIP_MDNS_RESPONDER
    // 5. Initialize mDNS responder
    mdns_resp_init();
#endif
    
    // 6. Set DNS servers
    dns_setserver(0, &default_dns_server[0]);
    dns_setserver(1, &default_dns_server[1]);
    
    // 7. Set SNTP servers
    sntp_setservername(0, default_sntp_server[0]);
    sntp_setservername(1, default_sntp_server[1]);
    sntp_setservername(2, default_sntp_server[2]);
    sntp_init();
    
    uint32_t elapsed_ms = osKernelGetTickCount() - start_time;
    LOG_DRV_INFO("Netif manager framework initialized in %u ms", elapsed_ms);
}

void netif_manager_register_commands(void)
{
    wifi_mode_process();
    driver_cmd_register_callback("ifconfig", ifconfig_cmd_register);
#if USE_OLD_CAT1
    driver_cmd_register_callback(CAT1_DEVICE_NAME, cat1_cmd_register);
#endif
    driver_cmd_register_callback("modem", modem_device_register);
    iperf_test_register();
    ms_mqtt_client_test_register();
    ms_network_test_register();
    http_client_test_register();
    aws_capture_register();
    icmp_client_register();
    wifi_register();
#ifdef SLI_SI91X_ENABLE_BLE
    driver_cmd_register_callback("ble", sl_ble_test_commands_register);
#endif
    driver_cmd_register_callback("rtmp_test", rtmp_push_test_register_commands);
}

/// @brief Register network interface manager to system
/// @param None
void netif_manager_register(void)
{
    netif_manager_init();
}

void netif_manager_unregister(void)
{
    // if (if_manager_dev) {
    //     device_unregister(if_manager_dev);
    //     hal_mem_free(if_manager_dev);
    //     if_manager_dev = NULL;
    // }
}

/// @brief Get network interface information list
/// @param netif_info_list Pointer to pointer of information list (needs external release)
/// @return Less than 0: failure, greater than or equal to 0: number of network interfaces
int nm_get_netif_list(netif_info_t **netif_info_list)
{
    int ret = 0, i = 0, netif_num = sizeof(if_name_type_list) / sizeof(if_name_type_t);
    if (netif_info_list == NULL) return AICAM_ERROR_INVALID_PARAM;
    
    *netif_info_list = hal_mem_alloc_large(netif_num * sizeof(netif_info_t));
    if (*netif_info_list == NULL) return AICAM_ERROR_NO_MEMORY;

    for (i = 0; i < netif_num; i++) {
        ret = nm_get_netif_info(if_name_type_list[i].if_name, &((*netif_info_list)[i]));
        if (ret != AICAM_OK) {
            hal_mem_free(*netif_info_list);
            *netif_info_list = NULL;
            return ret;
        }
    }
    return netif_num;
}

/// @brief Free network interface information list
/// @param netif_info_list Information list pointer
void nm_free_netif_list(netif_info_t *netif_info_list)
{
    if (netif_info_list) hal_mem_free(netif_info_list);
}

/// @brief Get network interface state
/// @param if_name Network interface name
/// @return Network interface state
netif_state_t nm_get_netif_state(const char *if_name)
{
    int ret = 0;
    netif_state_t state = NETIF_STATE_MAX;
    
    ret = netif_manager_ctrl(if_name, NETIF_CMD_STATE, &state);
    if (ret != 0) {
        LOG_DRV_ERROR("get netif state failed(ret = %d)!", ret);
        return NETIF_STATE_MAX;
    }
    return state;
}

/// @brief Get network interface information
/// @param if_name Network interface name
/// @param netif_info Network interface information pointer
/// @return Error code
int nm_get_netif_info(const char *if_name, netif_info_t *netif_info)
{
    int ret = 0;
    if (if_name == NULL || netif_info == NULL) return AICAM_ERROR_INVALID_PARAM;

    ret = netif_manager_ctrl(if_name, NETIF_CMD_INFO, netif_info);
    if (ret != 0) {
        LOG_DRV_ERROR("get netif info failed(ret = %d)!", ret);
        return ret;
    }

    return AICAM_OK;
}

/// @brief Print network interface information
/// @param if_name Network interface name (if not empty, use it first)
/// @param netif_info Network interface information pointer
void nm_print_netif_info(const char *if_name, netif_info_t *netif_info)
{
    const char *name_for_print = if_name;
    if (if_name != NULL) {
        netif_info = (netif_info_t *)hal_mem_alloc_large(sizeof(netif_info_t));
        if (netif_info == NULL) return;
        if (nm_get_netif_info(if_name, netif_info) != AICAM_OK) {
            hal_mem_free(netif_info); 
            return;
        }
    }
    if (netif_info == NULL) return;
    if (name_for_print == NULL) {
        name_for_print = netif_info->if_name;
    }

    printf("================== NETIF INFO ==================\r\n");
    printf("IF_NAME: %s\r\n", netif_info->if_name);
    printf("HOST_NAME: %s\r\n", (netif_info->host_name == NULL) ? "" : netif_info->host_name);
    printf("STATE: %s\r\n", netif_state_str[netif_info->state]);
    printf("TYPE: %s\r\n", netif_type_str[netif_info->type]);
    printf("FW_VERSION: %s\r\n", netif_info->fw_version);
    if (netif_info->type == NETIF_TYPE_WIRELESS) {
        const char *print_if = (if_name != NULL) ? if_name : netif_info->if_name;
        if (print_if != NULL && strcmp(print_if, NETIF_NAME_WIFI_STA) == 0) {
            printf("BSSID: "NETIF_MAC_STR_FMT"\r\n", NETIF_MAC_PARAMETER(netif_info->wireless_cfg.bssid));
        }
        printf("SSID: %s\r\n", netif_info->wireless_cfg.ssid);
        printf("PW: %s\r\n", netif_info->wireless_cfg.pw);
        if (netif_info->wireless_cfg.security < WIRELESS_SECURITY_MAX) {
            printf("SECURITY: %s\r\n", netif_security_str[netif_info->wireless_cfg.security]);
        } else {
            printf("SECURITY: unknown\r\n");
        }
        printf("ENCRYPTION: %s\r\n", netif_encryption_str[netif_info->wireless_cfg.encryption]);
        printf("CHANNEL: %d\r\n", netif_info->wireless_cfg.channel);
#if NETIF_WIFI_HALOW_IS_ENABLE
        if (print_if != NULL && strcmp(print_if, NETIF_NAME_WIFI_HALOW) == 0) {
            printf("HALOW_REG: %s\r\n", netif_info->halow_cfg.country_code);
            printf("HALOW_TX_PWR: %u dBm (0=reg max)\r\n", netif_info->halow_cfg.tx_power_dbm);
            printf("HALOW_PS: %u\r\n", netif_info->halow_cfg.ps_mode);
            printf("HALOW_SCAN_DWELL: %lu ms\r\n", (unsigned long)netif_info->halow_cfg.scan_dwell_ms);
            halow_print_rate_info_lines(&netif_info->halow_cfg);
        }
#endif
        if (print_if != NULL && strcmp(print_if, NETIF_NAME_WIFI_AP) == 0) {
            printf("MAX CLIENT NUM: %d\r\n", netif_info->wireless_cfg.max_client_num);
        }
    } else if (netif_info->type == NETIF_TYPE_4G) {
        printf("MODEL: %s\r\n", netif_info->cellular_info.model_name);
        printf("IMEI: %s\r\n", netif_info->cellular_info.imei);
        printf("APN CONTEXT ID: %d\r\n", netif_info->cellular_cfg.apn_context_id);
        printf("APN: %s\r\n", netif_info->cellular_cfg.apn);
        printf("USER: %s\r\n", netif_info->cellular_cfg.user);
        printf("PASSWD: %s\r\n", netif_info->cellular_cfg.passwd);
        printf("AUTH: %d\r\n", netif_info->cellular_cfg.authentication);
        printf("ROAMING: %d\r\n", netif_info->cellular_cfg.is_enable_roam);
        printf("ISP SELECTED: %d\r\n", netif_info->cellular_cfg.isp_selected);
        printf("PLMN: %s\r\n", netif_info->cellular_cfg.plmn);
        printf("PPP CONTEXT ID: %d\r\n", netif_info->cellular_cfg.ppp_context_id);
        printf("OPERATOR: %s\r\n", netif_info->cellular_info.operator);
        printf("SIM STATUS: %s\r\n", netif_info->cellular_info.sim_status);
        printf("SIM ICCID: %s\r\n", netif_info->cellular_info.iccid);
        printf("SIM IMSI: %s\r\n", netif_info->cellular_info.imsi);
        printf("SIM PIN: %s\r\n", netif_info->cellular_cfg.pin);
        printf("SIM PUK: %s\r\n", netif_info->cellular_cfg.puk);
        printf("CSQ: %d,%d\r\n", netif_info->cellular_info.csq_value, netif_info->cellular_info.ber_value);
        printf("CSQ LEVEL: %d\r\n", netif_info->cellular_info.csq_level);
        printf("PLMN ID: %s\r\n", netif_info->cellular_info.plmn_id);
        printf("CELL ID: %s\r\n", netif_info->cellular_info.cell_id);
        printf("LAC: %s\r\n", netif_info->cellular_info.lac);
        printf("NETWORK TYPE: %s\r\n", netif_info->cellular_info.network_type);
        printf("REG STATUS: %s\r\n", netif_info->cellular_info.registration_status);
    }
    if (netif_info->type == NETIF_TYPE_WIRELESS || netif_info->type == NETIF_TYPE_4G) printf("RSSI: %ddBm\r\n", netif_info->rssi);
    if (netif_info->type != NETIF_TYPE_4G) {
        printf("IF_MAC: "NETIF_MAC_STR_FMT"\r\n", NETIF_MAC_PARAMETER(netif_info->if_mac));
        printf("IP_MODE: %s\r\n", (netif_info->ip_mode == NETIF_IP_MODE_STATIC) ? "static" : ((netif_info->ip_mode == NETIF_IP_MODE_DHCP) ? "dhcp client" : "dhcp server"));
    }
    printf("IP: "NETIF_IPV4_STR_FMT"\r\n", NETIF_IPV4_PARAMETER(netif_info->ip_addr));
    printf("GW: "NETIF_IPV4_STR_FMT"\r\n", NETIF_IPV4_PARAMETER(netif_info->gw));
    printf("MASK: "NETIF_IPV4_STR_FMT"\r\n", NETIF_IPV4_PARAMETER(netif_info->netmask));
    printf("================================================\r\n\r\n");

    if (if_name != NULL) hal_mem_free(netif_info);
}

/// @brief Get network interface configuration
/// @param if_name Network interface name
/// @param netif_cfg Network interface configuration pointer
/// @return Error code
int nm_get_netif_cfg(const char *if_name, netif_config_t *netif_cfg)
{
    int ret = 0;
    netif_info_t *netif_info = NULL;
    if (if_name == NULL || netif_cfg == NULL) return AICAM_ERROR_INVALID_PARAM;

    netif_info = (netif_info_t *)hal_mem_alloc_large(sizeof(netif_info_t));
    if (netif_info == NULL) return AICAM_ERROR_NO_MEMORY;

    ret = nm_get_netif_info(if_name, netif_info);
    if (ret != AICAM_OK) {
        hal_mem_free(netif_info);
        return ret;
    }

    netif_cfg->host_name = netif_info->host_name;
    netif_cfg->ip_mode = netif_info->ip_mode;
    memcpy(&netif_cfg->wireless_cfg, &netif_info->wireless_cfg, sizeof(netif_cfg->wireless_cfg));
#if NETIF_WIFI_HALOW_IS_ENABLE
    memcpy(&netif_cfg->halow_cfg, &netif_info->halow_cfg, sizeof(netif_cfg->halow_cfg));
#endif
    memcpy(&netif_cfg->cellular_cfg, &netif_info->cellular_cfg, sizeof(netif_cfg->cellular_cfg));
    memcpy(&netif_cfg->diy_mac, &netif_info->if_mac, sizeof(netif_cfg->diy_mac));
    memcpy(&netif_cfg->ip_addr, &netif_info->ip_addr, sizeof(netif_cfg->ip_addr));
    memcpy(&netif_cfg->gw, &netif_info->gw, sizeof(netif_cfg->gw));
    memcpy(&netif_cfg->netmask, &netif_info->netmask, sizeof(netif_cfg->netmask));
    hal_mem_free(netif_info);
    return AICAM_OK;
}

/// @brief Set network interface configuration
/// @param if_name Network interface name
/// @param netif_cfg Network interface configuration pointer
/// @return Error code
int nm_set_netif_cfg(const char *if_name, netif_config_t *netif_cfg)
{
    int ret = 0;
    // int is_need_up = 0;
    if (if_name == NULL || netif_cfg == NULL) return AICAM_ERROR_INVALID_PARAM;

    // if (nm_get_netif_state(if_name) == NETIF_STATE_UP) {
    //     ret = netif_manager_ctrl(if_name, NETIF_CMD_DOWN, NULL);
    //     if (ret != 0) return ret;
    //     is_need_up = 1;
    // }

    // printf("Set netif(%s) cfg: %s\r\n", if_name, netif_cfg->wireless_cfg.ssid);
    ret = netif_manager_ctrl(if_name, NETIF_CMD_CFG_EX, netif_cfg);
    if (ret != 0) {
        LOG_DRV_ERROR("set netif cfg ex failed(ret = %d)!", ret);
        return ret;
    }

    // if (is_need_up) {
    //     ret = netif_manager_ctrl(if_name, NETIF_CMD_UP, NULL);
    //     if (ret != 0) return ret;
    // }

    return AICAM_OK;
}

/// @brief Initialize network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_init(const char *if_name)
{
    int ret = 0;
    if (if_name == NULL) return AICAM_ERROR_INVALID_PARAM;

    ret = netif_manager_ctrl(if_name, NETIF_CMD_INIT, NULL);
    if (ret != 0) {
        LOG_DRV_ERROR("init netif failed(ret = %d)!", ret);
        return ret;
    }
    return AICAM_OK;
}

/// @brief Start network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_up(const char *if_name)
{
    int ret = 0;
    if (if_name == NULL) return AICAM_ERROR_INVALID_PARAM;

    ret = netif_manager_ctrl(if_name, NETIF_CMD_UP, NULL);
    if (ret != 0) {
        LOG_DRV_ERROR("up netif failed(ret = %d)!", ret);
        return ret;
    }
    return AICAM_OK;
}

/// @brief Stop network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_down(const char *if_name)
{
    int ret = 0;
    if (if_name == NULL) return AICAM_ERROR_INVALID_PARAM;

    ret = netif_manager_ctrl(if_name, NETIF_CMD_DOWN, NULL);
    if (ret != 0) {
        LOG_DRV_ERROR("down netif failed(ret = %d)!", ret);
        return ret;
    }
    return AICAM_OK;
}

/// @brief Deinitialize network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_netif_deinit(const char *if_name)
{
    int ret = 0;
    if (if_name == NULL) return AICAM_ERROR_INVALID_PARAM;

    ret = netif_manager_ctrl(if_name, NETIF_CMD_UNINIT, NULL);
    if (ret != 0) {
        LOG_DRV_ERROR("uninit netif failed(ret = %d)!", ret);
        return ret;
    }
    return AICAM_OK;
}

/// @brief Get default network interface name currently in use by system
/// @param None
/// @return Default network interface name
const char *nm_get_default_netif_name(void)
{
    struct netif *netif = netif_get_default();
    if (netif == NULL) return "NULL";
    
    for (int i = 0; i < sizeof(if_name_type_list) / sizeof(if_name_type_t); i++) {
        if (strncmp(netif->name, if_name_type_list[i].if_name, strlen(if_name_type_list[i].if_name)) == 0) {
            return if_name_type_list[i].if_name;
        }
    }
    return "NULL";
}

/// @brief Get default network interface information
/// @param netif_info Default network interface information
/// @return Error code
int nm_ctrl_get_default_netif_info(netif_info_t *netif_info)
{
    return nm_get_netif_info(nm_get_default_netif_name(), netif_info);
}

/// @brief Set default network interface
/// @param if_name Network interface name
/// @return Error code
int nm_ctrl_set_default_netif(const char *if_name)
{
    netif_state_t state = NETIF_STATE_MAX;
    if (if_name == NULL) return AICAM_ERROR_INVALID_PARAM;

    state = nm_get_netif_state(if_name);
    if (state >= NETIF_STATE_MAX) return AICAM_ERROR_INVALID_PARAM;

    default_if_name = if_name;
    netif_manager_change_default_if();

    return AICAM_OK;
}

/// @brief Get configured default network interface name
/// @param None
/// @return Default network interface name
const char *nm_get_set_default_netif_name(void)
{
    return default_if_name;
}

/// @brief Set DNS server (maximum number is DNS_MAX_SERVERS)
/// @param idx DNS server index
/// @param dns_server DNS server address
/// @return Error code
int nm_ctrl_set_dns_server(int idx, uint8_t *dns_server)
{
    if (idx >= DNS_MAX_SERVERS || dns_server == NULL) return AICAM_ERROR_INVALID_PARAM;

    IP4_ADDR(&default_dns_server[idx], dns_server[0], dns_server[1], dns_server[2], dns_server[3]);
    LOG_DRV_INFO("Set DNS Server[%d]: %s", idx, ipaddr_ntoa(&default_dns_server[idx]));
    dns_setserver(idx, &default_dns_server[idx]);
    return AICAM_OK;
}

/// @brief Get DNS server (maximum number is DNS_MAX_SERVERS)
/// @param idx DNS server index
/// @param dns_server DNS server address
/// @return Error code
int nm_ctrl_get_dns_server(int idx, uint8_t *dns_server)
{
	const ip_addr_t *dns_addr = NULL;
    if (idx >= DNS_MAX_SERVERS || dns_server == NULL) return AICAM_ERROR_INVALID_PARAM;

    dns_addr = dns_getserver(idx);
    memcpy(dns_server, &(dns_addr->addr), 4);
    return AICAM_OK;
}

/// @brief Wireless scan
/// @param callback Result callback function
/// @return Error code
int nm_wireless_start_scan(wireless_scan_callback_t callback)
{
    return nm_wireless_start_scan_ex(NETIF_NAME_WIFI_STA, callback);
}

int nm_wireless_start_scan_ex(const char *if_name, wireless_scan_callback_t callback)
{
    int ret = 0;

    if (if_name == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

#if NETIF_WIFI_HALOW_IS_ENABLE
    if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
        ret = mm_halow_start_scan(callback);
    } else
#endif
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0 || strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
        ret = sl_net_start_scan(callback);
    } else {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (ret != 0) {
        LOG_DRV_ERROR("wireless scan failed on %s (ret = %d)!", if_name, ret);
        return ret;
    }
    return AICAM_OK;
}

wireless_scan_result_t *nm_wireless_get_scan_result(void)
{
    return nm_wireless_get_scan_result_ex(NETIF_NAME_WIFI_STA);
}

wireless_scan_result_t *nm_wireless_get_scan_result_ex(const char *if_name)
{
    if (if_name == NULL) {
        return NULL;
    }
#if NETIF_WIFI_HALOW_IS_ENABLE
    if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
        return mm_halow_get_storage_scan_result();
    }
#endif
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0 || strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
        return sl_net_get_strorage_scan_result();
    }
    return NULL;
}

int nm_wireless_update_scan_result(uint32_t timeout)
{
    return nm_wireless_update_scan_result_ex(NETIF_NAME_WIFI_STA, timeout);
}

int nm_wireless_update_scan_result_ex(const char *if_name, uint32_t timeout)
{
    int ret = 0;

    if (if_name == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

#if NETIF_WIFI_HALOW_IS_ENABLE
    if (strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
        ret = mm_halow_update_storage_scan_result(timeout);
    } else
#endif
    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0 || strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
        ret = sl_net_update_strorage_scan_result(timeout);
    } else {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (ret != 0) {
        LOG_DRV_ERROR("wireless update scan on %s failed(ret = %d)!", if_name, ret);
        return ret;
    }
    return AICAM_OK;
}

void nm_print_wireless_scan_result(const char *if_name, wireless_scan_result_t *scan_result)
{
    int i = 0;
    uint8_t is_halow = 0;

    if (scan_result == NULL) {
        return;
    }

#if NETIF_WIFI_HALOW_IS_ENABLE
    if (if_name != NULL && strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0) {
        is_halow = 1;
    }
#endif

    printf("\r\n================================== SCAN RESULT ==================================\r\n");
    printf("Scan Result Count: %d\r\n", scan_result->scan_count);
    if (scan_result->scan_count > 0) {
        if (is_halow) {
            printf("%-24s %-16s %12s %12s %10s %7s\r\n", "SSID", "SECURITY", "BSSID", "FREQ_HZ", "BW_MHZ", "RSSI");
        } else {
            printf("%-24s %-16s %12s %14s %7s\r\n", "SSID", "SECURITY", "BSSID", "CHANNEL", "RSSI");
        }
        for (i = 0; i < scan_result->scan_count; i++) {
            const char *sec = "unknown";
            if (scan_result->scan_info[i].security < WIRELESS_SECURITY_MAX) {
                sec = netif_security_str[scan_result->scan_info[i].security];
            }
            if (is_halow) {
                printf("%-24s %-16s "NETIF_MAC_STR_FMT" %12lu %10u %7ddBm\r\n",
                       scan_result->scan_info[i].ssid, sec,
                       NETIF_MAC_PARAMETER(scan_result->scan_info[i].bssid),
                       (unsigned long)scan_result->scan_info[i].channel_freq_hz,
                       scan_result->scan_info[i].bw_mhz,
                       scan_result->scan_info[i].rssi);
            } else {
                printf("%-24s %-16s "NETIF_MAC_STR_FMT" %6d %8ddBm\r\n",
                       scan_result->scan_info[i].ssid, sec,
                       NETIF_MAC_PARAMETER(scan_result->scan_info[i].bssid),
                       scan_result->scan_info[i].channel, scan_result->scan_info[i].rssi);
            }
        }
    }
    printf("================================================================================\r\n\r\n");
}
