/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#include "mmlog.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "umac_supp_shim_private.h"

#include "common/common.h"
#include "common/consbuf.h"
#include "common/mac_address.h"
#include "dot11/dot11.h"
#include "umac/data/umac_data.h"
#include "umac/ap/umac_ap.h"
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


#define HZ_TO_KHZ(x) ((x) / 1000)

static void *mmwpas_init_ap(void *ctx, const char *ifname)
{
    MM_UNUSED(ifname);
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    data->ap_driver_ctx = ctx;

    return umacd;
}

static void mmwpas_deinit_ap(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    data->ap_driver_ctx = NULL;
}

static int mmwpas_get_capa_ap(void *priv, struct wpa_driver_capa *capa)
{
    MM_UNUSED(priv);
    memset(capa, 0x0, sizeof(*capa));
    capa->flags = WPA_DRIVER_FLAGS_SME |
                  WPA_DRIVER_FLAGS_SAE |
                  WPA_DRIVER_FLAGS_AP |
                  WPA_DRIVER_FLAGS_AP_MLME;
    return 0;
}

struct hostapd_hw_modes *mmwpas_get_hw_feature_data_ap(void *priv,
                                                       u16 *num_modes,
                                                       u16 *flags,
                                                       u8 *dfs)
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
    ie_s1g_capabilities_build_ap(umacd, &buf);
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

static const u8 *mmwpas_get_mac_addr_ap(void *priv)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    static u8 mac_addr[MMWLAN_MAC_ADDR_LEN] = { 0 };

    if (umac_ap_get_bssid(umacd, mac_addr) != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to get BSSID\n");
        return mac_addr_zero;
    }

    return mac_addr;
}

static int mmwpas_scan2_ap(void *priv, struct wpa_driver_scan_params *params)
{
    MM_UNUSED(params);

    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    umac_supp_event(data->ap_driver_ctx, EVENT_SCAN_RESULTS, NULL);

    return 0;
}

struct wpa_scan_results *mmwpas_get_scan_results2_ap(void *priv)
{
    MM_UNUSED(priv);

    struct wpa_scan_results *scan_results =
        (struct wpa_scan_results *)os_calloc(1, sizeof(*scan_results));
    if (scan_results == NULL)
    {
        return NULL;
    }

    scan_results->res = (struct wpa_scan_res **)os_calloc(1, sizeof(struct wpa_scan_res *));
    scan_results->num = 0;
    return scan_results;
}

int mmwpas_associate_ap(void *priv, struct wpa_driver_associate_params *params)
{
    MM_UNUSED(priv);
    MM_UNUSED(params);

    MMLOG_INF("Skip associate for AP\n");
    return 0;
}

int mmwpas_set_ap(void *priv, struct wpa_driver_ap_params *params)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    const uint8_t *bssid = mmwpas_get_mac_addr_ap(priv);

    MMLOG_DBG("Configuring AP with SSID %.*s\n", (int)params->ssid_len, params->ssid);

    struct umac_ap_config cfg = {
        .ssid_len = params->ssid_len,
        .beacon_interval_tus = params->beacon_int,
        .dtim_period = params->dtim_period,
        .head = params->head,
        .head_len = params->head_len,
        .tail = params->tail,
        .tail_len = params->tail_len,
    };

    memcpy(cfg.bssid, bssid, sizeof(cfg.bssid));
    if (params->ssid_len > sizeof(cfg.ssid))
    {
        MMLOG_ERR("SSID too long\n");
        return -1;
    }
    memcpy(cfg.ssid, params->ssid, params->ssid_len);


    params->head = NULL;
    params->tail = NULL;

    return umac_ap_start(umacd, &cfg);
}


static enum morse_sta_state wpa_sta_flags_to_sta_state(uint32_t flags)
{
    if (flags & WPA_STA_AUTHORIZED)
    {
        return MORSE_STA_AUTHORIZED;
    }
    if (flags & WPA_STA_ASSOCIATED)
    {
        return MORSE_STA_ASSOCIATED;
    }
    if (flags & WPA_STA_AUTHENTICATED)
    {
        return MORSE_STA_AUTHENTICATED;
    }
    return MORSE_STA_NONE;
}

int mmwpas_sta_set_flags(void *priv,
                         const u8 *addr,
                         unsigned int total_flags,
                         unsigned int flags_or,
                         unsigned int flags_and)
{
    MM_UNUSED(flags_and);
    MM_UNUSED(flags_or);

    struct umac_data *umacd = (struct umac_data *)priv;

    struct umac_ap_sta_info sta_info = {
        .sta_state = wpa_sta_flags_to_sta_state(total_flags),
    };
    memcpy(sta_info.mac_addr, addr, MMWLAN_MAC_ADDR_LEN);
    enum mmwlan_status status = umac_ap_update_sta(umacd, &sta_info);
    if (status == MMWLAN_SUCCESS)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int mmwpas_get_bssid_ap(void *priv, u8 *bssid)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    return umac_ap_get_bssid(umacd, bssid);
}

int mmwpas_get_bssid_sta(void *priv, u8 *bssid)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad != NULL)
    {
        umac_sta_data_get_bssid(stad, bssid);
        return 0;
    }
    else
    {
        return -1;
    }
}

int mmwpas_set_key_ap(void *priv, struct wpa_driver_set_key_params *params)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    struct umac_sta_data *stad = NULL;
    uint16_t vif_id;

    vif_id = umac_ap_get_vif_id(umacd);
    if (params->key_flag & KEY_FLAG_PAIRWISE)
    {
        stad = umac_ap_lookup_sta_by_addr(umacd, params->addr);
        MMLOG_INF("... install key for stad %p\n", stad);
    }
    else
    {
        stad = umac_ap_lookup_sta_by_addr(umacd, NULL);
    }

    if (stad == NULL)
    {
        if (params->alg == WPA_ALG_NONE)
        {

            return 0;
        }

        if (params->addr == NULL)
        {
            MMLOG_WRN("Unable to find STA record. flags=0x%04x, addr=NULL\n", params->key_flag);
        }
        else
        {
            MMLOG_WRN("Unable to find STA record. flags=0x%04x, addr=" MM_MAC_ADDR_FMT "\n",
                      params->key_flag,
                      MM_MAC_ADDR_VAL(params->addr));
        }

        return -1;
    }

    enum mmwlan_status status = MMWLAN_ERROR;

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
        else if (params->key_flag == KEY_FLAG_GROUP_RX ||
                 params->key_flag == KEY_FLAG_GROUP_TX_DEFAULT)
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
            MMLOG_WRN("Unsupported combination with key_flag: %u, alg: %u, key_index: %d\n",
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static int mmwpas_send_mlme(void *priv,
                            const u8 *data,
                            size_t data_len,
                            int noack,
                            unsigned int freq,
                            const u16 *csa_offs,
                            size_t csa_offs_len,
                            int no_encrypt,
                            unsigned int wait,
                            int link_id)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    struct mmpkt *tx_pkt = umac_datapath_alloc_raw_tx_mmpkt(MMDRV_PKT_CLASS_DATA_TID7, 0, data_len);
    if (tx_pkt == NULL)
    {
        MMLOG_INF("Failed to allocate mgmt frame (len %u)\n", data_len);
        return -1;
    }

    struct mmpktview *tx_pktview = mmpkt_open(tx_pkt);
    mmpkt_append_data(tx_pktview, data, data_len);
    mmpkt_close(&tx_pktview);
    umac_datapath_tx_mgmt_frame_ap(umacd, tx_pkt, NULL);

    return 0;
}

#pragma GCC diagnostic pop

static int mmwpas_set_country(void *priv, const char *alpha2)
{
    MM_UNUSED(priv);
    MM_UNUSED(alpha2);

    struct umac_data *umacd = (struct umac_data *)priv;
    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);

    if (channel_list == NULL)
    {
        return -1;
    }

    if (strncmp(alpha2, (const char *)channel_list->country_code, 2) != 0)
    {
        MMLOG_WRN("Trying to change country code different to channel list. "
                  "Requested '%s', chan_list '%s'\n",
                  alpha2,
                  channel_list->country_code);
    }

    return 0;
}

static int mmwpas_get_country(void *priv, char *alpha2)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);

    if (channel_list == NULL)
    {
        return -1;
    }

    memcpy(alpha2, channel_list->country_code, 3);
    return 0;
}

static int mmwpas_sta_add(void *priv, struct hostapd_sta_add_params *params)
{
    struct umac_data *umacd = (struct umac_data *)priv;

    MMLOG_INF("Add STA " MM_MAC_ADDR_FMT ": aid=%u, flags=%08lx, flags_mask=%08lx\n",
              MM_MAC_ADDR_VAL(params->addr),
              params->aid,
              params->flags,
              params->flags_mask);

    struct umac_ap_sta_info sta_info = {
        .sta_state = wpa_sta_flags_to_sta_state(params->flags),
    };
    memcpy(sta_info.mac_addr, params->addr, MMWLAN_MAC_ADDR_LEN);
    enum mmwlan_status status = umac_ap_add_sta(umacd, params->aid, &sta_info);
    if (status == MMWLAN_SUCCESS)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

static int mmwpas_sta_remove(void *priv, const u8 *addr)
{
    struct umac_data *umacd = (struct umac_data *)priv;
    MMLOG_INF("Rem STA " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(addr));
    enum mmwlan_status status = umac_ap_remove_sta(umacd, addr);
    if (status == MMWLAN_SUCCESS)
    {
        return 0;
    }
    else
    {
        return -1;
    }
    return 0;
}

static int mmwpas_hapd_send_eapol(void *priv,
                                  const uint8_t *addr,
                                  const uint8_t *data,
                                  size_t data_len,
                                  int encrypt,
                                  const uint8_t *own_addr,
                                  u32 flags,
                                  int link_id)
{
    MM_UNUSED(link_id);
    MM_UNUSED(flags);

    struct umac_data *umacd = (struct umac_data *)priv;

    struct ieee8023_hdr header = { 0 };

    struct umac_sta_data *stad = umac_ap_lookup_sta_by_addr(umacd, addr);
    if (stad == NULL)
    {
        MMLOG_WRN("Unknown STA " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(addr));
        return -1;
    }


    struct mmpkt *txbuf =
        umac_datapath_alloc_mmpkt_for_qos_data_tx(sizeof(header) + data_len, MMDRV_PKT_CLASS_MGMT);
    if (txbuf == NULL)
    {
        MMLOG_DBG("Failed to allocate for L2 packet TX\n");
        return -1;
    }

    struct mmpktview *txbufview = mmpkt_open(txbuf);
    memcpy(header.dest, addr, ETH_ALEN);
    memcpy(header.src, own_addr, ETH_ALEN);
    header.ethertype = htobe16(ETHERTYPE_EAPOL);
    mmpkt_append_data(txbufview, (uint8_t *)&header, sizeof(header));
    mmpkt_append_data(txbufview, data, data_len);
    mmpkt_close(&txbufview);
    mmdrv_get_tx_metadata(txbuf)->tid = 7;

    enum mmwlan_status status =
        umac_datapath_tx_frame(umacd,
                               txbuf,
                               encrypt ? ENCRYPTION_ENABLED : ENCRYPTION_DISABLED,
                               NULL);
    if (status == MMWLAN_SUCCESS)
    {
        return 0;
    }
    else
    {
        MMLOG_WRN("EAPOL send failed with %d\n", status);
        return -1;
    }
}

int mmwpas_get_seq_num(const char *ifname,
                       void *priv,
                       const uint8_t *addr,
                       int idx,
                       int link_id,
                       uint8_t *seq)
{
    MM_UNUSED(ifname);
    MM_UNUSED(priv);
    MM_UNUSED(idx);
    MM_UNUSED(link_id);

    if (addr != NULL)
    {

        return -1;
    }
    else
    {

        memset(seq, 0, 6);
        return 0;
    }
}


const struct wpa_driver_ops mmwlan_wpas_ops_ap = {
    .name = UMAC_SUPP_AP_DRIVER_NAME,
    .desc = "",
    .init = mmwpas_init_ap,
    .deinit = mmwpas_deinit_ap,
    .get_capa = mmwpas_get_capa_ap,
    .get_hw_feature_data = mmwpas_get_hw_feature_data_ap,
    .get_mac_addr = mmwpas_get_mac_addr_ap,
    .scan2 = mmwpas_scan2_ap,
    .get_scan_results2 = mmwpas_get_scan_results2_ap,
    .associate = mmwpas_associate_ap,
    .get_bssid = mmwpas_get_bssid_ap,
    .set_key = mmwpas_set_key_ap,
    .send_mlme = mmwpas_send_mlme,
    .set_ap = mmwpas_set_ap,
    .sta_set_flags = mmwpas_sta_set_flags,
    .set_country = mmwpas_set_country,
    .get_country = mmwpas_get_country,
    .sta_add = mmwpas_sta_add,
    .sta_remove = mmwpas_sta_remove,
    .hapd_send_eapol = mmwpas_hapd_send_eapol,
    .get_seqnum = mmwpas_get_seq_num,
};
