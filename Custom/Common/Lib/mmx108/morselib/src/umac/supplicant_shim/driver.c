/*
 * Copyright 2021-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#include "mmlog.h"
#include "mmosal.h"
#include "mmutils.h"
#include "mmwlan.h"
#include "umac/regdb/umac_regdb.h"
#include "umac_supp_shim_private.h"

#include "common/common.h"
#include "common/consbuf.h"
#include "common/mac_address.h"
#include "dot11/dot11.h"
#include "umac/data/umac_data.h"
#include "umac/config/umac_config.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/ps/umac_ps.h"
#include "umac/scan/umac_scan.h"
#include "umac/connection/umac_connection.h"
#include "umac/frames/association.h"
#include "umac/frames/authentication.h"
#include "umac/frames/deauthentication.h"
#include "umac/frames/action.h"
#include "umac/ies/s1g_operation.h"
#include "umac/ies/ssid.h"
#include "umac/interface/umac_interface.h"
#include "umac/stats/umac_stats.h"
#include "umac/wnm_sleep/umac_wnm_sleep.h"
#include "umac/ies/s1g_capabilities.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wc++-compat"
#include "hostap/src/utils/includes.h"
#include "hostap/src/utils/os.h"
#include "hostap/wpa_supplicant/bss.h"
#include "hostap/wpa_supplicant/wpa_supplicant_i.h"
#pragma GCC diagnostic pop


#define HZ_TO_KHZ(x) ((x) / 1000)

#define KHZ_TO_HZ(x) ((x) * 1000)


#define DEV_ASSERT_IN_S1G_RANGE(x) MMOSAL_DEV_ASSERT((x) > 800000)


static enum mmwlan_pmf_mode translate_supp_to_mmwlan_pmf_mode(enum mfp_options mfp_option)
{
    switch (mfp_option)
    {
        case NO_MGMT_FRAME_PROTECTION:
            return MMWLAN_PMF_DISABLED;

        case MGMT_FRAME_PROTECTION_OPTIONAL:
        case MGMT_FRAME_PROTECTION_REQUIRED:
            return MMWLAN_PMF_REQUIRED;

        default:
            MMOSAL_ASSERT(false);
    }


    MMOSAL_ASSERT(false);
    return MMWLAN_PMF_REQUIRED;
}


static enum mmwlan_security_type translate_supp_to_mmwlan_security(unsigned int key_mgmt_suite)
{
    MMLOG_DBG("Key Management 0x%x\n", key_mgmt_suite);

    if (key_mgmt_suite == 0 || (key_mgmt_suite & (key_mgmt_suite - 1)) != 0)
    {
        MMLOG_WRN("Invalid or multiple key_mgmt bits set: 0x%x\n", key_mgmt_suite);
        MMOSAL_DEV_ASSERT(false);
    }

    if (key_mgmt_suite & WPA_KEY_MGMT_SAE)
    {
        return MMWLAN_SAE;
    }
    else if (key_mgmt_suite & WPA_KEY_MGMT_OWE)
    {
        return MMWLAN_OWE;
    }
    else if (key_mgmt_suite & WPA_KEY_MGMT_NONE)
    {
        return MMWLAN_OPEN;
    }

    MMLOG_WRN("Unsupport key_mgmt_suite 0x%x\n", key_mgmt_suite);
    MMOSAL_ASSERT(false);

    return MMWLAN_SAE;
}

static void *mmwpas_init(void *ctx, const char *ifname)
{
    MM_UNUSED(ifname);
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    data->sta_driver_ctx = ctx;

    return umacd;
}

static void mmwpas_deinit(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    mmosal_free(data->bss_cache);
    data->bss_cache = NULL;

    data->sta_driver_ctx = NULL;
}

static int mmwpas_get_capa(void *priv, struct wpa_driver_capa *capa)
{
    MM_UNUSED(priv);
    memset(capa, 0x0, sizeof(*capa));


    static const uint8_t extended_capa[] = {
        DOT11_MASK_S1G_EXT_CAP0_ECSA_SUPPORTED,
    };

    static const uint8_t extended_capa_mask[] = {
        DOT11_MASK_S1G_EXT_CAP0_ECSA_SUPPORTED,
    };

    capa->extended_capa = extended_capa;
    capa->extended_capa_mask = extended_capa_mask;
    capa->extended_capa_len = sizeof(extended_capa);
    MM_STATIC_ASSERT(sizeof(extended_capa) == sizeof(extended_capa_mask),
                     "extended_capa vs extended_capa_mask size mismatch");

    capa->flags = WPA_DRIVER_FLAGS_SME | WPA_DRIVER_FLAGS_SAE;
    return 0;
}

static const u8 *mmwpas_get_mac_addr(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    const u8 *mac_addr;


    if (umac_interface_borrow_vif_mac_addr(umacd, MMWLAN_VIF_STA, &mac_addr) != MMWLAN_SUCCESS)
    {
        return mac_addr_zero;
    }

    return mac_addr;
}


struct mmwpas_minsnr_bitrate_entry
{
    int32_t minsnr;
    uint32_t bitrate;
};


static const struct mmwpas_minsnr_bitrate_entry mmwpas_s1g8mhz_table[] = {
    { 3, 2925 },
    { 5, 5850 },
    { 7, 8775 },
    { 10, 11700 },
    { 13, 17550 },
    { 18, 23400 },
    { 19, 26325 },
    { 21, 29250 },
    { 26, 35100 },
    { 27, 39000 },
    { -1, 39000 }
};

static uint32_t mmwpas_interpolate_rate(int32_t snr,
                                        int32_t snr0,
                                        int32_t snr1,
                                        int32_t rate0,
                                        int32_t rate1)
{
    return rate0 + (snr - snr0) * (rate1 - rate0) / (snr1 - snr0);
}


static int32_t mmwpas_max_s1g_bitrate(int32_t snr)
{
    const struct mmwpas_minsnr_bitrate_entry *prev, *entry = mmwpas_s1g8mhz_table;

    while ((entry->minsnr != -1) && (snr >= entry->minsnr))
    {
        entry++;
    }
    if (entry == mmwpas_s1g8mhz_table)
    {
        return entry->bitrate;
    }
    prev = entry - 1;
    if (entry->minsnr == -1)
    {
        return prev->bitrate;
    }
    return mmwpas_interpolate_rate(snr, prev->minsnr, entry->minsnr, prev->bitrate, entry->bitrate);
}

static struct wpa_scan_res *mmwpas_alloc_and_fill_scan_result(const struct umac_scan_response *rsp)
{
    struct wpa_scan_res *res;
    size_t res_len = sizeof(*res) + rsp->frame.ies_len;

    res = (struct wpa_scan_res *)os_malloc(res_len);
    if (res == NULL)
    {
        MMLOG_WRN("Failed to allocate wpa_scan_res\n");
        return NULL;
    }

    memset(res, 0, sizeof(*res));

    res->flags = WPA_SCAN_QUAL_INVALID | WPA_SCAN_LEVEL_DBM;
    PACK_LE64(res->tsf, rsp->frame.timestamp);
    mac_addr_copy(res->bssid, rsp->frame.bssid);
    res->freq = KHZ_TO_MHZ(HZ_TO_KHZ(rsp->channel_freq_hz));
    res->freq_offset = KHZ_TO_S1G_OFFSET(HZ_TO_KHZ(rsp->channel_freq_hz));
    res->beacon_int = rsp->frame.beacon_interval;
    res->caps = rsp->frame.capability_info;
    res->level = rsp->rssi;
    res->ie_len = rsp->frame.ies_len;
    res->noise = rsp->noise_dbm;
    res->est_throughput = mmwpas_max_s1g_bitrate(rsp->rssi - rsp->noise_dbm);

    uint8_t *ies = (uint8_t *)(res + 1);
    memcpy(ies, rsp->frame.ies, res->ie_len);

#if MMLOG_LEVEL >= MMLOG_LEVEL_VRB
    char ssid[33] = { 0 };
    MMOSAL_ASSERT(rsp->frame.ssid_len < sizeof(ssid) - 1);
    memcpy(ssid, rsp->frame.ssid, rsp->frame.ssid_len);
    MMLOG_VRB("> '%s' " MM_MAC_ADDR_FMT ", %d dBm, %d kHz\n",
              ssid,
              MM_MAC_ADDR_VAL(rsp->frame.bssid),
              rsp->rssi,
              HZ_TO_KHZ(rsp->channel_freq_hz));
#endif

    return res;
}

static void mmwpas_scan_rx_handler(struct umac_data *umacd, const struct umac_scan_response *rsp)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    MMOSAL_ASSERT(data->in_progress_scan_results != NULL);

    const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);
    if (sta_args != NULL && sta_args->scan_rx_cb != NULL)
    {
        struct mmwlan_scan_result scan_result;
        umac_scan_fill_result(&scan_result, rsp);
        sta_args->scan_rx_cb(&scan_result, sta_args->scan_rx_cb_arg);
    }


    if (data->in_progress_scan_results->num == 0)
    {
        MMLOG_VRB("Inserting scan result " MM_MAC_ADDR_FMT " @ idx 0 (num, %u)\n",
                  MM_MAC_ADDR_VAL(rsp->frame.bssid),
                  data->in_progress_scan_results->num);

        MMOSAL_ASSERT(data->in_progress_scan_results->res[0] == NULL);
        data->in_progress_scan_results->res[0] = mmwpas_alloc_and_fill_scan_result(rsp);

        if (data->in_progress_scan_results->res[0] != NULL)
        {
            data->in_progress_scan_results->num = 1;
        }
        return;
    }


    int ii;
    int insert_at_idx = 0;
    int old_entry_idx = -1;
    for (ii = 0; ii < (int)data->in_progress_scan_results->num; ii++)
    {
        struct wpa_scan_res *res = data->in_progress_scan_results->res[ii];
        MMOSAL_ASSERT(res != NULL);
        if (res->level >= rsp->rssi)
        {
            insert_at_idx = ii + 1;
        }

        if (mm_mac_addr_is_equal(rsp->frame.bssid, res->bssid))
        {
            MMOSAL_ASSERT(old_entry_idx == -1);
            old_entry_idx = ii;
        }
    }

    if (insert_at_idx >= data->max_scan_results)
    {
        MMLOG_VRB("Scan result too quiet to add to results list (" MM_MAC_ADDR_FMT
                  ") num=%u, max=%u\n",
                  MM_MAC_ADDR_VAL(rsp->frame.bssid),
                  data->in_progress_scan_results->num,
                  data->max_scan_results);
        return;
    }


    if (old_entry_idx >= 0)
    {
        struct wpa_scan_res *old_entry_res = data->in_progress_scan_results->res[old_entry_idx];

        MMLOG_VRB("Found an existing entry for " MM_MAC_ADDR_FMT "\n",
                  MM_MAC_ADDR_VAL(rsp->frame.bssid));
        MMLOG_VRB("    Old entry @ %d, RSSI %d dBm\n", old_entry_idx, old_entry_res->level);
        MMLOG_VRB("    New entry @ %d, RSSI %d dBm\n", insert_at_idx, rsp->rssi);
        if (old_entry_idx <= insert_at_idx)
        {
            MMLOG_VRB("Old entry has better RSSI than new entry. Dropping new entry\n");
            return;
        }

        MMLOG_VRB("Removing old entry with lower RSSI\n");

        os_free(old_entry_res);
        data->in_progress_scan_results->num--;
        for (ii = old_entry_idx; ii < (int)data->in_progress_scan_results->num; ii++)
        {
            data->in_progress_scan_results->res[ii] = data->in_progress_scan_results->res[ii + 1];
        }
    }

    MMLOG_VRB("Inserting scan result " MM_MAC_ADDR_FMT " @ idx %d (num %u)\n",
              MM_MAC_ADDR_VAL(rsp->frame.bssid),
              insert_at_idx,
              data->in_progress_scan_results->num);

    struct wpa_scan_res *res = mmwpas_alloc_and_fill_scan_result(rsp);
    if (res == NULL)
    {
        return;
    }


    MMOSAL_ASSERT(data->in_progress_scan_results->num <= data->max_scan_results);
    if (data->in_progress_scan_results->num == data->max_scan_results)
    {
        size_t idx = data->max_scan_results - 1;
        MMLOG_VRB("Discarding scan result " MM_MAC_ADDR_FMT "\n",
                  MM_MAC_ADDR_VAL(data->in_progress_scan_results->res[idx]->bssid));
        os_free(data->in_progress_scan_results->res[idx]);
        data->in_progress_scan_results->res[idx] = NULL;
        data->in_progress_scan_results->num--;
    }

    for (ii = (int)data->in_progress_scan_results->num; ii > insert_at_idx; ii--)
    {
        data->in_progress_scan_results->res[ii] = data->in_progress_scan_results->res[ii - 1];
    }
    data->in_progress_scan_results->res[insert_at_idx] = res;
    data->in_progress_scan_results->num++;
}

static void mmwpas_clean_up_scan_data(struct umac_supp_shim_data *data)
{
    mmosal_free(data->scan_req.args.extra_ies);
    data->scan_req.args.extra_ies = NULL;
    wpa_scan_results_free(data->in_progress_scan_results);
    data->in_progress_scan_results = NULL;
    mmosal_free(data->bss_cache);
    data->bss_cache = NULL;
}


static bool mmwpas_bss_cache_build(struct umac_supp_shim_data *data,
                                   struct wpa_scan_results *scan_results)
{
    MMOSAL_ASSERT(data->bss_cache == NULL);

    data->bss_cache = (struct bss_cache *)mmosal_malloc(BSS_CACHE_SIZE(scan_results->num));
    if (data->bss_cache == NULL)
    {
        return false;
    }
    data->bss_cache->num_entries = scan_results->num;

    unsigned ii;
    for (ii = 0; ii < scan_results->num; ii++)
    {
        struct bss_cache_entry *entry = &data->bss_cache->entries[ii];
        struct wpa_scan_res *res = scan_results->res[ii];

        MMOSAL_ASSERT(res != NULL);
        memcpy(entry->bssid, res->bssid, sizeof(entry->bssid));

        const uint8_t *ie =
            ie_find((const uint8_t *)(res + 1), res->ie_len, DOT11_IE_S1G_OPERATION, NULL);

        MMOSAL_ASSERT(ie != NULL);

        MMOSAL_ASSERT(((const struct dot11_ie_s1g_operation *)ie)->header.length ==
                      sizeof(entry->s1g_operation_ie) - sizeof(struct dot11_ie_hdr));

        memcpy(entry->s1g_operation_ie, ie, sizeof(entry->s1g_operation_ie));

        entry->beacon_interval = res->beacon_int;
    }
    return true;
}


static bool mmwpas_bss_cache_lookup(struct umac_supp_shim_data *data,
                                    const uint8_t *bssid,
                                    struct umac_connection_bss_cfg *config)
{
    if (data->bss_cache == NULL)
    {
        return false;
    }

    unsigned ii;
    for (ii = 0; ii < data->bss_cache->num_entries; ii++)
    {
        struct bss_cache_entry *entry = &data->bss_cache->entries[ii];
        if (mm_mac_addr_is_equal(entry->bssid, bssid))
        {
            bool ok =
                ie_s1g_operation_parse((struct dot11_ie_s1g_operation *)entry->s1g_operation_ie,
                                       &config->channel_cfg);
            if (!ok)
            {
                MMLOG_WRN("S1G IE unparseable for " MM_MAC_ADDR_FMT "\n",
                          MM_MAC_ADDR_VAL(entry->bssid));
                return false;
            }
            config->beacon_interval = entry->beacon_interval;
            return true;
        }
    }

    return false;
}

/**
 * Seed the WPAS BSS table so connect_without_scan can call sme_authenticate()
 * with a non-NULL @c wpa_bss (SME returns immediately when bss is NULL).
 */
static bool mmwpas_inject_quick_connect_bss(struct umac_supp_shim_data *data,
                                            const struct mmwlan_sta_args *args)
{
    struct wpa_supplicant *wpa_s;
    struct wpa_scan_res *res;
    size_t res_len;
    uint8_t *ies;
    struct os_reltime now;
    uint32_t freq_hz;

    if (data == NULL || data->sta_wpa_s == NULL || args == NULL) {
        return false;
    }
    if (!args->preconnect_bss_valid || mm_mac_addr_is_zero(args->bssid)) {
        return false;
    }

    wpa_s = data->sta_wpa_s;
    if (args->preconnect_probe_ies_len == 0U) {
        return false;
    }

    res_len = sizeof(*res) + args->preconnect_probe_ies_len;
    res = (struct wpa_scan_res *)os_zalloc(res_len);
    if (res == NULL) {
        return false;
    }

    freq_hz = args->preconnect_channel_freq_hz;
    if (freq_hz == 0U) {
        struct ie_s1g_operation s1g_op;
        if (ie_s1g_operation_parse((struct dot11_ie_s1g_operation *)
                                       args->preconnect_s1g_operation_ie,
                                   &s1g_op)) {
            const struct mmwlan_s1g_channel *chan =
                umac_regdb_get_channel(umac_data_get_umacd(),
                                       s1g_op.operating_channel_index);
            if (chan != NULL) {
                freq_hz = chan->centre_freq_hz;
            }
        }
    }

    res->flags = WPA_SCAN_QUAL_INVALID | WPA_SCAN_LEVEL_DBM;
    mac_addr_copy(res->bssid, args->bssid);
    if (freq_hz != 0U) {
        res->freq = KHZ_TO_MHZ(HZ_TO_KHZ(freq_hz));
        res->freq_offset = KHZ_TO_S1G_OFFSET(HZ_TO_KHZ(freq_hz));
    }
    res->beacon_int = args->preconnect_beacon_interval;
    res->level = args->preconnect_rssi;
    res->ie_len = args->preconnect_probe_ies_len;
    res->noise = 0;
    ies = (uint8_t *)(res + 1);
    memcpy(ies, args->preconnect_probe_ies, res->ie_len);

    os_get_reltime(&now);
    wpa_bss_update_start(wpa_s);
    wpa_bss_update_scan_res(wpa_s, res, &now);
    wpa_bss_update_end(wpa_s, NULL, 1);
    os_free(res);

    if (wpa_bss_get_bssid_latest(wpa_s, args->bssid) == NULL) {
        return false;
    }

    return true;
}


bool mmwpas_prepare_quick_connect(struct umac_supp_shim_data *data,
                                  const struct mmwlan_sta_args *args)
{
    struct umac_connection_bss_cfg cfg;

    if (data == NULL || data->sta_wpa_s == NULL || args == NULL)
    {
        return false;
    }

    if (!args->preconnect_bss_valid || mm_mac_addr_is_zero(args->bssid))
    {
        return false;
    }

    if (!ie_s1g_operation_parse((struct dot11_ie_s1g_operation *)args->preconnect_s1g_operation_ie,
                                &cfg.channel_cfg))
    {
        return false;
    }

    mmosal_free(data->bss_cache);
    data->bss_cache = NULL;

    data->bss_cache = (struct bss_cache *)mmosal_malloc(BSS_CACHE_SIZE(1));
    if (data->bss_cache == NULL)
    {
        return false;
    }

    data->bss_cache->num_entries = 1;
    struct bss_cache_entry *entry = &data->bss_cache->entries[0];
    memcpy(entry->bssid, args->bssid, sizeof(entry->bssid));
    memcpy(entry->s1g_operation_ie,
           args->preconnect_s1g_operation_ie,
           sizeof(entry->s1g_operation_ie));
    entry->beacon_interval = args->preconnect_beacon_interval;

    if (!mmwpas_inject_quick_connect_bss(data, args)) {
        return false;
    }

    data->sta_wpa_s->connect_without_scan = data->sta_wpa_s->conf->ssid;
    return true;
}


static void mmwpas_scan_complete_handler(struct umac_data *umacd,
                                         enum mmwlan_scan_state result_code)
{
    MM_UNUSED(result_code);
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    MMOSAL_ASSERT(data->in_progress_scan_results != NULL);
    data->completed_scan_results = data->in_progress_scan_results;
    data->in_progress_scan_results = NULL;
    mmwpas_clean_up_scan_data(data);
    mmwpas_bss_cache_build(data, data->completed_scan_results);

    MMLOG_INF("%u scan results\n", data->completed_scan_results->num);
#if MMLOG_LEVEL >= MMLOG_LEVEL_DBG
    unsigned ii;
    for (ii = 0; ii < data->completed_scan_results->num; ii++)
    {
        char ssid[33] = { 0 };
        const struct dot11_ie_ssid *ssid_ie =
            ie_ssid_find((const uint8_t *)(data->completed_scan_results->res[ii] + 1),
                         data->completed_scan_results->res[ii]->ie_len);
        MMOSAL_ASSERT(ssid_ie != NULL);
        MMOSAL_ASSERT(ssid_ie->header.length < sizeof(ssid) - 1);
        memcpy(ssid, ssid_ie->ssid, ssid_ie->header.length);
        MMLOG_DBG("  - " MM_MAC_ADDR_FMT "(%ddBm): %s\n",
                  MM_MAC_ADDR_VAL(data->completed_scan_results->res[ii]->bssid),
                  data->completed_scan_results->res[ii]->level,
                  ssid);
    }
#endif

    umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_SCAN_COMPLETE);


    umac_supp_event(data->sta_driver_ctx, EVENT_SCAN_RESULTS, NULL);

    umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_SCAN_COMPLETE);
}

static int mmwpas_initialise_scan_data(struct umac_data *umacd,
                                       struct umac_supp_shim_data *data,
                                       struct wpa_driver_scan_params *params)
{
    struct mmwlan_scan_args *args = &data->scan_req.args;
    bool dwell_on_home;
    size_t alloc_size;

    if (data->in_progress_scan_results != NULL)
    {

        MMLOG_WRN("Unable to start scan: already in progress\n");
        return -1;
    }


    MMOSAL_ASSERT(args->extra_ies == NULL);


    mmwpas_clean_up_scan_data(data);

    if (params->num_ssids == 1 && params->ssids[0].ssid_len != 0)
    {
        args->ssid_len = params->ssids[0].ssid_len;
        MMOSAL_ASSERT(args->ssid_len <= sizeof(args->ssid));
        memcpy(args->ssid, params->ssids[0].ssid, params->ssids[0].ssid_len);
    }
    else
    {
        memset(args->ssid, 0, MMWLAN_SSID_MAXLEN);
        args->ssid_len = 0;
    }


    if ((args->ssid_len == 0) && umac_config_is_ndp_probe_supported(umacd))
    {
        const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);
        if (sta_args != NULL)
        {
            args->ssid_len = sta_args->ssid_len;
            MMOSAL_ASSERT(args->ssid_len <= sizeof(args->ssid));
            memcpy(args->ssid, sta_args->ssid, args->ssid_len);
            MMLOG_INF("Using SSID from STA args (len=%u)\n", args->ssid_len);
        }
    }


    args->dwell_time_ms = umac_config_get_supp_scan_dwell_time(umacd);
    dwell_on_home = mmwlan_get_sta_state() == MMWLAN_STA_CONNECTED && !data->sta_wpa_s->reassociate;
    args->dwell_on_home_ms = dwell_on_home ? umac_config_get_supp_scan_home_dwell_time(umacd) : 0;

    if (params->extra_ies_len)
    {
        args->extra_ies = (uint8_t *)mmosal_malloc(params->extra_ies_len);
        if (args->extra_ies)
        {
            memcpy(args->extra_ies, params->extra_ies, params->extra_ies_len);
            args->extra_ies_len = params->extra_ies_len;
        }
    }

    data->scan_req.rx_cb = mmwpas_scan_rx_handler;
    data->scan_req.complete_cb = mmwpas_scan_complete_handler;

    data->in_progress_scan_results =
        (struct wpa_scan_results *)os_malloc(sizeof(*(data->in_progress_scan_results)));
    if (data->in_progress_scan_results == NULL)
    {
        goto error;
    }

    memset(data->in_progress_scan_results, 0, sizeof(*(data->in_progress_scan_results)));
    data->max_scan_results = umac_config_get_max_supp_scan_results(umacd);
    alloc_size = sizeof(struct wpa_scan_res *) * data->max_scan_results;
    data->in_progress_scan_results->res = (struct wpa_scan_res **)(os_malloc(alloc_size));
    if (data->in_progress_scan_results->res == NULL)
    {
        goto error;
    }
    memset(data->in_progress_scan_results->res, 0, alloc_size);

    return 0;

error:
    mmwpas_clean_up_scan_data(data);
    return -1;
}

static int mmwpas_scan2(void *priv, struct wpa_driver_scan_params *params)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    int ret = mmwpas_initialise_scan_data(umacd, data, params);
    if (ret != 0)
    {
        return ret;
    }

    umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_SCAN_REQUESTED);


    umac_scan_queue_request(umacd, &data->scan_req);

    umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_SCAN_REQUEST);

    return 0;
}

struct wpa_scan_results *mmwpas_get_scan_results2(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    struct wpa_scan_results *results = data->completed_scan_results;
    data->completed_scan_results = NULL;
    return results;
}

static int mmwpas_abort_scan(void *priv, u64 scan_cookie)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    MM_UNUSED(scan_cookie);

    umac_scan_abort(umacd, NULL);

    umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_SCAN_ABORT);

    return 0;
}

static int mmwpas_authenticate(void *priv, struct wpa_driver_auth_params *params)
{
    enum mmwlan_status status;
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (params->auth_alg != WPA_AUTH_ALG_OPEN && params->auth_alg != WPA_AUTH_ALG_SAE)
    {
        MMLOG_WRN("Failed to send an Auth frame: unsupported auth_alg: %d\n", params->auth_alg);
        return -1;
    }

    uint8_t own_addr[DOT11_MAC_ADDR_LEN];
    if (umac_interface_get_vif_mac_addr(umacd, MMWLAN_VIF_STA, own_addr) != MMWLAN_SUCCESS)
    {
        return -1;
    }

    struct umac_connection_bss_cfg config = { 0 };
    bool ok = mmwpas_bss_cache_lookup(data, params->bssid, &config);
    if (!ok)
    {
        MMLOG_WRN("Failed to find entry in BSS cache for " MM_MAC_ADDR_FMT "\n",
                  MM_MAC_ADDR_VAL(params->bssid));
        return -1;
    }

    MMLOG_VRB("Found BSS cache entry for " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(params->bssid));

    struct frame_data_auth auth_params = { .auth_alg = (params->auth_alg == WPA_AUTH_ALG_OPEN) ?
                                                           DOT11_AUTH_ALG_OPEN :
                                                           DOT11_AUTH_ALG_SAE,
                                           .bssid = params->bssid,
                                           .sta_address = own_addr,
                                           .auth_data_len = params->auth_data_len,
                                           .auth_data = params->auth_data };

    status = umac_connection_set_bss_cfg(umacd, params->bssid, &config);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set BSS for connection.\n");
        return -1;
    }

    status = umac_connection_process_auth_req(umacd, &auth_params);

    if (status == MMWLAN_SUCCESS)
    {
        umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_AUTH_REQUEST);
        return 0;
    }

    return -1;
}

int mmwpas_associate(void *priv, struct wpa_driver_associate_params *params)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        MMOSAL_DEV_ASSERT(false);
        return -1;
    }

    MMLOG_DBG("WPAS: Assoc\n");

    uint8_t own_addr[DOT11_MAC_ADDR_LEN];
    if (umac_interface_get_vif_mac_addr(umacd, MMWLAN_VIF_STA, own_addr) != MMWLAN_SUCCESS)
    {
        return -1;
    }

    struct frame_data_assoc_req assoc_req_params = { .bssid = params->bssid,
                                                     .prev_bssid = params->prev_bssid,
                                                     .sta_address = own_addr,
                                                     .ssid_len = params->ssid_len,
                                                     .ssid = params->ssid,
                                                     .wpa_ie_len = params->wpa_ie_len,
                                                     .wpa_ie = params->wpa_ie,
                                                     .extra_assoc_ies = sta_args->extra_assoc_ies,
                                                     .extra_assoc_ies_len =
                                                         sta_args->extra_assoc_ies_len };

    enum mmwlan_status status = umac_connection_process_assoc_req(umacd, &assoc_req_params);
    if (status != MMWLAN_SUCCESS)
    {
        return -1;
    }

    umac_sta_data_set_security(stad,
                               translate_supp_to_mmwlan_security(params->key_mgmt_suite),
                               translate_supp_to_mmwlan_pmf_mode(params->mgmt_frame_protection));
    umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_ASSOC_REQUEST);

    return 0;
}

struct hostapd_hw_modes *mmwpas_get_hw_feature_data(void *priv, u16 *num_modes, u16 *flags, u8 *dfs)
{
    MM_UNUSED(flags);
    MM_UNUSED(dfs);

    struct umac_data *umacd = (struct umac_data *)priv;

    struct hostapd_hw_modes *hw_mode = (struct hostapd_hw_modes *)os_calloc(1, sizeof(*hw_mode));
    if (hw_mode == NULL)
    {
        goto failure;
    }

    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);
    MMOSAL_ASSERT(channel_list != NULL);

    hw_mode->mode = HOSTAPD_MODE_IEEE80211AH;
    hw_mode->num_channels = channel_list->num_channels;
    hw_mode->channels =
        (struct hostapd_channel_data *)os_calloc(hw_mode->num_channels, sizeof(*hw_mode->channels));
    if (hw_mode->channels == NULL)
    {
        goto failure;
    }

    for (unsigned ii = 0; ii < channel_list->num_channels; ii++)
    {
        hw_mode->channels[ii].chan = channel_list->channels[ii].s1g_chan_num;
        hw_mode->channels[ii].freq = 0;
        hw_mode->channels[ii].freq_khz = HZ_TO_KHZ(channel_list->channels[ii].centre_freq_hz);
        switch (channel_list->channels[ii].bw_mhz)
        {
            case 16:
                hw_mode->channels[ii].allowed_bw |= HOSTAPD_CHAN_WIDTH_16;
                MM_FALLTHROUGH;

            case 8:
                hw_mode->channels[ii].allowed_bw |= HOSTAPD_CHAN_WIDTH_8;
                MM_FALLTHROUGH;

            case 4:
                hw_mode->channels[ii].allowed_bw |= HOSTAPD_CHAN_WIDTH_4;
                MM_FALLTHROUGH;

            case 2:
                hw_mode->channels[ii].allowed_bw |= HOSTAPD_CHAN_WIDTH_2;
                MM_FALLTHROUGH;

            case 1:
                hw_mode->channels[ii].allowed_bw |= HOSTAPD_CHAN_WIDTH_1;
                break;

            default:
                MMLOG_WRN("Invalid channel BW MHz\n");
                MMOSAL_DEV_ASSERT(false);
                break;
        }
        hw_mode->channels[ii].max_tx_power = channel_list->channels[ii].max_tx_eirp_dbm;
    }

    struct dot11_ie_s1g_capabilities s1g_cap_ie;
    struct consbuf buf = CONSBUF_INIT_WITH_BUF((uint8_t *)&s1g_cap_ie, sizeof(s1g_cap_ie));
    ie_s1g_capabilities_build(umacd, &buf);
    MM_STATIC_ASSERT(sizeof(s1g_cap_ie.s1g_capabilities_information) == sizeof(hw_mode->s1g_capab),
                     "Size of S1G Capabilities Element in Driver and Supplicant do not match");
    MM_STATIC_ASSERT(sizeof(s1g_cap_ie.supported_s1g_mcs_nss_set) == sizeof(hw_mode->s1g_mcs),
                     "Size of S1G MCS_NSS Element in Driver and Supplicant do not match");

    memcpy(hw_mode->s1g_capab, s1g_cap_ie.s1g_capabilities_information, sizeof(hw_mode->s1g_capab));
    memcpy(hw_mode->s1g_mcs, s1g_cap_ie.supported_s1g_mcs_nss_set, sizeof(hw_mode->s1g_mcs));

    hw_mode->band = NL80211_BAND_S1GHZ;

    *num_modes = 1;
    return hw_mode;

failure:
    if (hw_mode != NULL)
    {
        os_free(hw_mode->channels);
    }
    os_free(hw_mode);
    return NULL;
}


int mmwpas_get_bssid(void *priv, u8 *bssid)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    return (umac_connection_get_bssid(umacd, bssid) == MMWLAN_SUCCESS ? 0 : -1);
}

int mmwpas_set_supp_port(void *priv, int authorized)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    MMLOG_DBG("WPAS: Set controlled port %d\n", authorized);

    umac_connection_handle_port_state(umacd, (authorized != 0));

    return 0;
}

int mmwpas_deauthenticate(void *priv, const u8 *addr, u16 reason_code)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    MMLOG_DBG("WPAS: Deauth\n");

    uint8_t own_addr[ETH_ALEN];
    if (umac_interface_get_vif_mac_addr(umacd, MMWLAN_VIF_STA, own_addr) != MMWLAN_SUCCESS)
    {
        return -1;
    }

    struct frame_data_deauth deauth_params = { .bssid = addr,
                                               .sta_address = own_addr,
                                               .reason_code = reason_code };

    enum mmwlan_status status = umac_connection_process_deauth_tx(umacd, &deauth_params);

    if (status == MMWLAN_SUCCESS)
    {
        umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_DEAUTH_TX);
        return 0;
    }

    return -1;
}

int mmwpas_get_ssid(void *priv, u8 *ssid)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    return umac_connection_get_ssid(umacd, ssid);
}

int mmwpas_set_key(void *priv, struct wpa_driver_set_key_params *params)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = (struct umac_data *)priv;

    uint16_t vif_id = umac_connection_get_vif_id(umacd);
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        if (params->alg == WPA_ALG_NONE)
        {
            MMLOG_DBG("Ignoring key removal before STA is fully initialized\n");
            return 0;
        }
        else
        {
            MMLOG_WRN("Setkey STA not initialized\n");
            MMOSAL_DEV_ASSERT(false);
            return -1;
        }
    }

    if (params->alg == WPA_ALG_NONE)
    {
        status = umac_keys_uninstall_key(stad, vif_id, params->key_idx);
        if (status != MMWLAN_SUCCESS)
        {
            return -1;
        }
    }
    else if ((params->key_flag & (KEY_FLAG_PAIRWISE | KEY_FLAG_GROUP)) &&
             ((params->alg == WPA_ALG_CCMP) || (params->alg == WPA_ALG_BIP_CMAC_128)))
    {
        struct umac_key key = { 0 };

        if (params->key_flag == KEY_FLAG_PAIRWISE_RX_TX)
        {
            key.key_type = UMAC_KEY_TYPE_PAIRWISE;
        }
        else if (params->key_flag == KEY_FLAG_GROUP_RX)
        {
            if (params->alg == WPA_ALG_BIP_CMAC_128)
            {
                key.key_type = UMAC_KEY_TYPE_IGTK;
            }
            else
            {
                key.key_type = UMAC_KEY_TYPE_GROUP;
            }
        }
        else
        {
            MMLOG_WRN(
                "morse_set_key - unsupported combination with key_flag: %u, alg: %u, key_index: %d",
                params->key_flag,
                params->alg,
                params->key_idx);
            return -1;
        }

        key.key_id = params->key_idx;

        if (params->key_len > sizeof(key.key_data))
        {
            MMLOG_WRN("set_key: too long %u", params->key_len);
            return -1;
        }

        key.key_len = params->key_len;
        memcpy(key.key_data, params->key, params->key_len);

        if (params->seq != NULL)
        {
            unsigned ii;
            for (ii = 0; ii < params->seq_len; ii++)
            {
                key.rx_seq[UMAC_KEY_RX_COUNTER_SPACE_DEFAULT] |= ((uint64_t)(params->seq[ii]))
                                                                 << (ii * 8);
            }
        }

        status = umac_keys_install_key(stad, vif_id, &key);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Failed to install key (status=%d)\n", status);
            return -1;
        }
    }
    else
    {
        MMLOG_WRN(
            "morse_set_key - unsupported combination with key_flag: %u, alg: %u, key_index: %d",
            params->key_flag,
            params->alg,
            params->key_idx);
        return -1;
    }

    return 0;
}

static int mmwpas_send_action(void *priv,
                              unsigned int freq,
                              unsigned int wait_time,
                              const u8 *dst,
                              const u8 *src,
                              const u8 *bssid,
                              const u8 *data,
                              size_t data_len,
                              int no_cck)
{

    MM_UNUSED(freq);
    MM_UNUSED(wait_time);

    MM_UNUSED(no_cck);

    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *supp_data = umac_data_get_supp_shim(umacd);


    if ((umac_connection_get_state(umacd) != MMWLAN_STA_CONNECTED) && !supp_data->in_progress_roc)
    {
        return -1;
    }

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        return -1;
    }

    uint8_t own_addr[ETH_ALEN];
    if (umac_interface_get_vif_mac_addr(umacd, MMWLAN_VIF_STA, own_addr) != MMWLAN_SUCCESS)
    {
        return -1;
    }


    MMOSAL_DEV_ASSERT(mm_mac_addr_is_equal(own_addr, src));

    struct frame_data_action params = { .bssid = bssid,
                                        .dst_address = dst,
                                        .src_address = own_addr,
                                        .action_field = data,
                                        .action_field_len = data_len };

    enum mmwlan_status status =
        umac_datapath_build_and_tx_mgmt_frame(stad, frame_action_build, &params);

    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

static void signal_remain_on_channel_evt(struct umac_data *umacd, const struct umac_evt *evt)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);


    data->in_progress_roc = true;

    union wpa_event_data wpa_event_data = { 0 };

    MMLOG_DBG("Signal remain on channel event\n");

    wpa_event_data.remain_on_channel.freq = evt->args.remain_on_channel.freq_khz;
    wpa_event_data.remain_on_channel.duration = evt->args.remain_on_channel.duration_ms;
    umac_supp_event(data->sta_driver_ctx, EVENT_REMAIN_ON_CHANNEL, &wpa_event_data);
}

static int mmwpas_remain_on_channel(void *priv, unsigned int freq, unsigned int duration)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);


    MMOSAL_DEV_ASSERT(os_strcmp(data->sta_wpa_s->confname, UMAC_SUPP_DPP_CONFIG_NAME) == 0);

    if (umac_connection_bss_is_configured(umacd))
    {
        MMLOG_WRN("ROC requested with BSS configured\n");
        return -1;
    }

    DEV_ASSERT_IN_S1G_RANGE(freq);
    const struct mmwlan_s1g_channel *channel =
        umac_regdb_get_channel_from_freq_and_bw(umacd, KHZ_TO_HZ(freq), (1 | 2));
    if (channel == NULL)
    {
        status = MMWLAN_CHANNEL_INVALID;
        goto exit;
    }

    status = umac_interface_set_channel_from_regdb(umacd, channel, true);
    if (status != MMWLAN_SUCCESS)
    {
        goto exit;
    }


    struct umac_evt evt = UMAC_EVT_INIT_ARGS(signal_remain_on_channel_evt,
                                             remain_on_channel,
                                             .freq_khz = freq,
                                             .duration_ms = duration);
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        status = MMWLAN_ERROR;
    }

    MMLOG_DBG("Remain on channel %d kHz %d ms\n", freq, duration);

exit:
    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

int mmwpas_cancel_remain_on_channel(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    data->in_progress_roc = false;

    MMLOG_DBG("Cancel remain on channel\n");

    return 0;
}

static int mmwpas_wnm_oper(void *priv, enum wnm_oper oper, const u8 *peer, u8 *buf, u16 *buf_len)
{
    MM_UNUSED(peer);
    MM_UNUSED(buf);
    MM_UNUSED(buf_len);

    struct umac_data *umacd = (struct umac_data *)priv;

    enum umac_wnm_sleep_event event;

    switch (oper)
    {
        case WNM_SLEEP_ENTER_CONFIRM:
            event = UMAC_WNM_SLEEP_EVENT_ENTRY_CONFIRMED;
            break;

        case WNM_SLEEP_EXIT_CONFIRM:
            event = UMAC_WNM_SLEEP_EVENT_EXIT_CONFIRMED;
            break;

        case WNM_SLEEP_ENTER_FAIL:
        case WNM_SLEEP_EXIT_FAIL:

            MMLOG_WRN("WNM Sleep enter/exit request failed.\n");
            return 0;
            break;

        default:
            MMLOG_DBG("Unsupported WNM operation recieved %d.\n", oper);
            return -1;
    }

    umac_wnm_sleep_report_event(umacd, event);

    return 0;
}

static int mmwpas_signal_monitor(void *priv, int threshold, int hysteresis)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    umac_connection_set_signal_monitor(umacd, threshold, hysteresis);

    return 0;
}

const struct wpa_driver_ops mmwlan_wpas_ops = {
    .name = UMAC_SUPP_STA_DRIVER_NAME,
    .desc = "",
    .init = mmwpas_init,
    .deinit = mmwpas_deinit,
    .get_capa = mmwpas_get_capa,
    .get_hw_feature_data = mmwpas_get_hw_feature_data,
    .get_mac_addr = mmwpas_get_mac_addr,
    .scan2 = mmwpas_scan2,
    .get_scan_results2 = mmwpas_get_scan_results2,
    .abort_scan = mmwpas_abort_scan,
    .authenticate = mmwpas_authenticate,
    .associate = mmwpas_associate,
    .get_bssid = mmwpas_get_bssid,
    .get_ssid = mmwpas_get_ssid,
    .set_supp_port = mmwpas_set_supp_port,
    .deauthenticate = mmwpas_deauthenticate,
    .set_key = mmwpas_set_key,
    .send_action = mmwpas_send_action,
    .remain_on_channel = mmwpas_remain_on_channel,
    .cancel_remain_on_channel = mmwpas_cancel_remain_on_channel,
    .wnm_oper = mmwpas_wnm_oper,
    .signal_monitor = mmwpas_signal_monitor,
};
