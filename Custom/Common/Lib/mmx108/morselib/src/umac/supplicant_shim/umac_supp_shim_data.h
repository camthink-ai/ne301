/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_supp_shim.h"
#include "umac/scan/umac_scan.h"
#include "dot11/dot11_ies.h"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wc++-compat"
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "hostap/src/utils/includes.h"
#include "hostap/src/utils/common.h"
#include "hostap/src/utils/base64.h"
#include "hostap/src/utils/eloop.h"
#include "hostap/src/utils/os.h"
#include "hostap/src/common/eapol_common.h"
#include "hostap/src/crypto/aes_wrap.h"
#include "hostap/src/drivers/driver.h"
#include "hostap/src/l2_packet/l2_packet.h"
#include "hostap/wpa_supplicant/config.h"
#include "hostap/wpa_supplicant/config_ssid.h"
#include "hostap/wpa_supplicant/wpa_supplicant_i.h"
#include "hostap/wpa_supplicant/bss.h"
#pragma GCC diagnostic pop

struct l2_packet_data
{
    struct umac_data *umacd;
    void (*rx_callback)(void *ctx, const uint8_t *src_addr, const uint8_t *buf, size_t len);
    void *rx_callback_ctx;
    int l2_hdr;
    uint8_t own_addr[ETH_ALEN];
};


struct bss_cache_entry
{
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
    uint8_t s1g_operation_ie[sizeof(struct dot11_ie_s1g_operation)];

    uint16_t beacon_interval;
};


struct bss_cache
{
    size_t num_entries;
    struct bss_cache_entry entries[];
};


#define BSS_CACHE_SIZE(_num_entries) \
    (sizeof(struct bss_cache) + sizeof(struct bss_cache_entry) * (_num_entries))

struct umac_supp_shim_data
{
    bool is_started;
    struct wpa_global *global;
    struct wpa_supplicant *sta_wpa_s;
    struct wpa_supplicant *ap_wpa_s;

    void *ap_driver_ctx;


    struct l2_packet_data l2;


    void *sta_driver_ctx;

    struct umac_scan_req scan_req;

    uint16_t max_scan_results;

    struct wpa_scan_results *in_progress_scan_results;

    struct wpa_scan_results *completed_scan_results;

    struct bss_cache *bss_cache;

    bool auto_reconnect_disabled;

    bool in_progress_roc;
};
