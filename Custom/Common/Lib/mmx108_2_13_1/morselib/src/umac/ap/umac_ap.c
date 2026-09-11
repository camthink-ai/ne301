/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "common/common.h"
#include "mmlog.h"

#include "dot11/dot11_utils.h"

#include "umac_ap.h"
#include "umac/core/umac_core.h"
#include "umac/stats/umac_stats.h"
#include "umac_ap_data.h"
#include "umac/data/umac_data.h"
#include "umac/config/umac_config.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/frames/frames_common.h"
#include "umac/frames/probe_response.h"
#include "umac/ies/s1g_tim.h"
#include "umac/ies/vendor_ie.h"
#include "umac/interface/umac_interface.h"
#include "umac/keys/umac_keys.h"
#include "umac/rc/umac_rc.h"
#include "umac/relay/umac_relay.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/supplicant_shim/umac_supp_shim.h"


uint32_t ieee80211_crc32(const u8 *frame, size_t frame_len);


static uint32_t umac_ap_generate_cssid(const uint8_t *ssid, size_t ssid_len)
{
    return ieee80211_crc32(ssid, ssid_len);
}


static void umac_ap_set_stad_sleep_state_(struct umac_sta_data *stad, bool asleep)
{
    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    sta_data->asleep = asleep;
}


static bool umac_ap_set_stad_state_(struct umac_sta_data *stad, enum morse_sta_state state)
{
    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    bool changed = sta_data->sta_state != state;
    sta_data->sta_state = state;
    return changed;
}

#if MMLOG_LEVEL >= MMLOG_LEVEL_VRB
static void dump_sta_list(struct umac_ap_data *data)
{
    mmosal_printf("AP connected STA list:\n");
    for (size_t ii = 0; ii < data->max_stas; ii++)
    {
        if (data->stas[ii] != NULL)
        {
            struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(data->stas[ii]);
            mmosal_printf("    %2u: " MM_MAC_ADDR_FMT " state=%u, asleep=%u, last_active=%lu\n",
                          ii,
                          MM_MAC_ADDR_VAL(umac_sta_data_peek_peer_addr(data->stas[ii])),
                          sta_data->sta_state,
                          sta_data->asleep,
                          sta_data->last_active_ms);
        }
    }
}
#endif

bool umac_ap_validate_ap_args(struct umac_data *umacd, const struct mmwlan_ap_args *args)
{
    if (args->security_type == MMWLAN_OWE)
    {
        MMLOG_ERR("OWE security is not currently supported by AP mode\n");
        return false;
    }

    bool auto_select = (args->op_class == 0 && args->s1g_chan_num == 0);

    struct mmwlan_vif_channel_info sta_channel_info = {};
    bool sta_active =
        (mmwlan_get_vif_channel_info(MMWLAN_VIF_STA, &sta_channel_info) == MMWLAN_SUCCESS);

    if (auto_select)
    {

        if (!sta_active)
        {
            MMLOG_ERR("Auto channel selection requires an active STA VIF\n");
            return false;
        }
    }
    else
    {
        const struct mmwlan_s1g_channel *chan = umac_regdb_get_channel(umacd, args->s1g_chan_num);
        if (chan == NULL || !umac_regdb_op_class_match(umacd, args->op_class, chan))
        {
            MMLOG_ERR("No matching channel (reg_dom=%s, op_class=%u, chan#=%u)\n",
                      umac_regdb_get_country_code(umacd),
                      args->op_class,
                      args->s1g_chan_num);
#if MMLOG_LEVEL >= MMLOG_LEVEL_INF
            const struct mmwlan_s1g_channel_list *channel_list =
                umac_config_get_channel_list(umacd);
            if (channel_list != NULL)
            {
                MMLOG_INF("Configured channel list (%s):\n", channel_list->country_code);
                for (size_t ii = 0; ii < channel_list->num_channels; ii++)
                {
                    const struct mmwlan_s1g_channel *channel = &channel_list->channels[ii];
                    MMLOG_INF("  chan_num=%u, global_op_class=%u, s1g_op_class=%u, bw=%u MHz\n",
                              channel->s1g_chan_num,
                              channel->global_operating_class,
                              channel->s1g_operating_class,
                              channel->bw_mhz);
                }
            }
#endif
            return false;
        }

        if (sta_active && (!umac_regdb_op_class_match(umacd, sta_channel_info.op_class, chan) ||
                           args->s1g_chan_num != sta_channel_info.s1g_chan_num ||
                           args->pri_bw_mhz != sta_channel_info.pri_bw_mhz ||
                           args->pri_1mhz_chan_idx != sta_channel_info.pri_1mhz_chan_idx))
        {
            MMLOG_ERR("AP channel must match active STA channel\n"
                      "STA: op_class=%u chan=%u pri_bw=%u pri_1mhz_idx=%u\n"
                      "AP: op_class=%u chan=%u pri_bw=%u pri_1mhz_idx=%u\n",
                      sta_channel_info.op_class,
                      sta_channel_info.s1g_chan_num,
                      sta_channel_info.pri_bw_mhz,
                      sta_channel_info.pri_1mhz_chan_idx,
                      args->op_class,
                      args->s1g_chan_num,
                      args->pri_bw_mhz,
                      args->pri_1mhz_chan_idx);
            return false;
        }

        if (args->pri_bw_mhz > MM_MIN(chan->bw_mhz, 2))
        {
            MMLOG_ERR("Invalid pri_bw_mhz, %u > %u\n", args->pri_bw_mhz, MM_MIN(chan->bw_mhz, 2));
            return false;
        }

        if (args->pri_1mhz_chan_idx >= chan->bw_mhz)
        {
            MMLOG_ERR("Invalid pri_1mhz_chan_idx, %u > %u\n",
                      args->pri_1mhz_chan_idx,
                      chan->bw_mhz);
            return false;
        }
    }

    if (args->max_stas > MMWLAN_AP_MAX_STAS_LIMIT)
    {
        MMLOG_ERR("Unable to support %u STAs\n", args->max_stas);
        return false;
    }

    return true;
}

enum mmwlan_status umac_ap_enable_ap(struct umac_data *umacd, const struct mmwlan_ap_args *args)
{
    enum mmwlan_status status = MMWLAN_ERROR;

    if (!umac_ap_validate_ap_args(umacd, args))
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data != NULL)
    {
        MMLOG_WRN("AP already active\n");
        return MMWLAN_UNAVAILABLE;
    }

    data = umac_data_alloc_ap(umacd);
    if (data == NULL)
    {
        MMLOG_WRN("Failed to allocate AP data\n");
        return MMWLAN_NO_MEM;
    }


    data->max_stas = 1 + (args->max_stas ? args->max_stas : MMWLAN_DEFAULT_AP_MAX_STAS);
    data->stas =
        (struct umac_sta_data **)mmosal_calloc(data->max_stas, sizeof(struct umac_sta_data *));
    if (data->stas == NULL)
    {
        MMLOG_ERR("Failed to allocate STA pointer array\n");
        status = MMWLAN_NO_MEM;
        goto error;
    }


    data->sta_common = umac_sta_data_alloc(umacd);

    data->stas[0] = data->sta_common;
    if (data->sta_common == NULL)
    {
        MMLOG_ERR("AP sta_common alloc failed\n");
        status = MMWLAN_NO_MEM;
        goto error;
    }


    if (args->max_stas > MMWLAN_DEFAULT_AP_MAX_STAS)
    {
        status = umac_core_alloc_extra_timeouts(umacd);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_ERR("Failed to allocate extra timeouts\n");
            goto error;
        }
    }

    memcpy(&(data->args), args, sizeof(data->args));


    if (args->op_class == 0 && args->s1g_chan_num == 0)
    {
        struct mmwlan_vif_channel_info sta_channel_info = {};
        enum mmwlan_status sta_status =
            mmwlan_get_vif_channel_info(MMWLAN_VIF_STA, &sta_channel_info);
        MMOSAL_ASSERT(sta_status == MMWLAN_SUCCESS);

        MMLOG_INF("STA VIF active; inheriting channel info "
                  "(op_class=%u, chan=%u, pri_bw=%u, pri_1mhz_idx=%u)\n",
                  sta_channel_info.op_class,
                  sta_channel_info.s1g_chan_num,
                  sta_channel_info.pri_bw_mhz,
                  sta_channel_info.pri_1mhz_chan_idx);

        data->args.op_class = sta_channel_info.op_class;
        data->args.s1g_chan_num = sta_channel_info.s1g_chan_num;
        data->args.pri_bw_mhz = sta_channel_info.pri_bw_mhz;
        data->args.pri_1mhz_chan_idx = sta_channel_info.pri_1mhz_chan_idx;
    }

    data->specified_chan = umac_regdb_get_channel(umacd, data->args.s1g_chan_num);

    if (data->args.beacon_interval_tus == 0)
    {
        data->args.beacon_interval_tus = MMWLAN_DEFAULT_AP_BEACON_INTERVAL_TUS;
    }
    if (data->args.dtim_period == 0)
    {
        data->args.dtim_period = MMWLAN_DEFAULT_AP_DTIM_PERIOD;
    }
    if (data->args.pri_bw_mhz == 0)
    {
        if (data->specified_chan->bw_mhz == 1)
        {
            data->args.pri_bw_mhz = 1;
        }
        else
        {
            data->args.pri_bw_mhz = 2;
        }
    }

    if (!mm_mac_addr_is_zero(data->args.bssid))
    {
        MMLOG_DBG("Using given BSSID: " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(data->args.bssid));
        umac_sta_data_set_bssid(data->sta_common, data->args.bssid);
    }
    else
    {

        uint8_t bssid[MMWLAN_MAC_ADDR_LEN] = { 0 };
        status = umac_interface_get_device_mac_addr(umacd, bssid);
        if (status != MMWLAN_SUCCESS)
        {
            goto error;
        }

        bssid[0] ^= 0x02;
        MMLOG_DBG("Basing BSSID on device MAC addr: " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(bssid));
        umac_sta_data_set_bssid(data->sta_common, bssid);
    }

    status = umac_supp_add_ap_interface(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_INF("Supplicant start failed\n");
        umac_interface_remove(umacd, UMAC_INTERFACE_AP);
        goto error;
    }

    return MMWLAN_SUCCESS;

error:
    mmosal_free(data->stas);
    umac_data_dealloc_ap(umacd);
    return status;
}

enum mmwlan_status umac_ap_start(struct umac_data *umacd, const struct umac_ap_config *cfg)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    MMOSAL_ASSERT(data != NULL);

    enum mmwlan_status status = MMWLAN_ERROR;

    memcpy(&(data->config), cfg, sizeof(data->config));

    MMLOG_INF("Starting AP with BSSID " MM_MAC_ADDR_FMT " and SSID: %.*s\n",
              MM_MAC_ADDR_VAL(cfg->bssid),
              (int)cfg->ssid_len,
              cfg->ssid);

    if (!umac_sta_data_matches_bssid(data->sta_common, cfg->bssid))
    {
        MMOSAL_DEV_ASSERT(false);
        MMLOG_WRN("BSSID cfg mismatch\n");
        umac_sta_data_set_bssid(data->sta_common, cfg->bssid);
    }

    data->dtim_count = data->config.dtim_period - 1;
    umac_ap_set_stad_sleep_state_(data->sta_common, true);
    umac_ap_set_stad_state_(data->sta_common, MORSE_STA_AUTHORIZED);
    umac_sta_data_set_security(data->sta_common, data->args.security_type, data->args.pmf_mode);
    umac_keys_init(data->sta_common);

    status = umac_interface_add(umacd, UMAC_INTERFACE_AP, umac_datapath_ops_ap, data->config.bssid);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Interface add failed\n");
        goto failure;
    }

    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        MMLOG_WRN("Invalid VIF ID on AP interface\n");
        MMOSAL_DEV_ASSERT(false);
        status = MMWLAN_ERROR;
        goto failure;
    }
    umac_sta_data_set_vif_id(data->sta_common, vif_id);

    const struct dot11_ie_s1g_operation *dot11_s1g_op =
        ie_s1g_operation_find(data->config.tail, data->config.tail_len);
    if (dot11_s1g_op == NULL)
    {
        MMLOG_ERR("Missing S1G Operation IE\n");
        goto failure;
    }

    struct ie_s1g_operation s1g_op = {};
    bool ok = ie_s1g_operation_parse(dot11_s1g_op, &s1g_op);
    if (!ok)
    {
        MMLOG_ERR("Invalid S1G Operation IE generated\n");
        goto failure;
    }

    status = umac_interface_set_channel(umacd, &s1g_op);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Set channel failed\n");
        goto failure;
    }

    uint32_t cssid = umac_ap_generate_cssid(cfg->ssid, cfg->ssid_len);
    MMLOG_DBG("Configure BSS: vif_id=%u beacon_interval=%u, dtim_period=%u\n",
              vif_id,
              data->config.beacon_interval_tus,
              data->config.dtim_period);
    status =
        mmdrv_cfg_bss(vif_id, data->config.beacon_interval_tus, data->config.dtim_period, cssid);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Config BSS failed (%u)\n", status);
        goto failure;
    }

    status = mmdrv_start_beaconing(vif_id);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Start beaconing failed (%u)\n", status);
        goto failure;
    }

    enum mmwlan_relay_state relay_state = umac_relay_get_state(umacd);
    if (relay_state == MMWLAN_RELAY_STATE_DISABLED || relay_state == MMWLAN_RELAY_STATE_ROOT)
    {

        struct mmwlan_vif_state state = {
            .vif = MMWLAN_VIF_AP,
            .link_state = MMWLAN_LINK_UP,
        };
        umac_interface_invoke_vif_state_cb(umacd, &state);
    }

    return MMWLAN_SUCCESS;

failure:
    mmosal_free(data->config.head);
    mmosal_free(data->config.tail);
    memset(&(data->config), 0, sizeof(data->config));
    return status;
}

void umac_ap_build_beacon(struct umac_data *umacd, struct consbuf *buf, void *params)
{
    const bool *traffic_indicator = (const bool *)params;

    struct umac_ap_data *data = umac_data_get_ap(umacd);
    MMOSAL_ASSERT(data != NULL);

    consbuf_append(buf, data->config.head, data->config.head_len);
    ie_s1g_tim_build(buf,
                     data->dtim_count,
                     data->config.dtim_period,
                     *traffic_indicator,
                     data->bitmap);
    consbuf_append(buf, data->config.tail, data->config.tail_len);

    umac_relay_emit_ies(umacd, buf, DOT11_FC_TYPE_EXT, DOT11_FC_SUBTYPE_S1G_BEACON);

    vendor_ie_list_emit(umac_config_get_vendor_ies(umacd), buf, MMWLAN_VENDOR_IE_MGMT_BEACON);
}

void umac_ap_signal_beacon_critical_update(struct umac_data *umacd)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL ||
        data->config.head == NULL ||
        data->config.head_len < sizeof(struct dot11_s1g_beacon_hdr))
    {

        return;
    }


    struct dot11_s1g_beacon_hdr *hdr = (struct dot11_s1g_beacon_hdr *)data->config.head;
    hdr->change_sequence++;
}

struct mmpkt *umac_ap_get_beacon(struct umac_data *umacd)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    MMOSAL_ASSERT(data != NULL);


    MMOSAL_TASK_ENTER_CRITICAL();
    umac_ap_set_stad_sleep_state_(data->sta_common, true);
    bool traffic_indicator = (data->dtim_count == 0) &&
                             umac_sta_data_get_queued_len(data->sta_common);
    MMOSAL_TASK_EXIT_CRITICAL();
    struct mmpkt *beacon = build_mgmt_frame(umacd, umac_ap_build_beacon, &traffic_indicator);


    if (data->dtim_count == 0)
    {
        data->dtim_count = data->config.dtim_period - 1;
    }
    else
    {
        --data->dtim_count;
    }

    if (beacon == NULL)
    {
        MMLOG_WRN("Failed to generate beacon\n");
        return NULL;
    }


    if (traffic_indicator)
    {
        MMLOG_DBG("Releasing group addressed traffic with beacon\n");
        umac_ap_set_stad_sleep_state_(data->sta_common, false);
        umac_core_evt_wake(umacd);
    }

    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(beacon);

    tx_metadata->flags = MMDRV_TX_FLAG_IMMEDIATE_REPORT;
    tx_metadata->tid = MMWLAN_MAX_QOS_TID;
    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    MMOSAL_DEV_ASSERT(vif_id < UINT8_MAX);
    tx_metadata->vif_id = vif_id;

    umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);
    return beacon;
}


static bool umac_ap_should_ignore_probe_req(struct umac_data *umacd,
                                            struct mmdrv_rx_metadata *rx_metadata)
{
    const struct mmwlan_ap_args *args = umac_ap_get_args(umacd);
    const struct mmwlan_s1g_channel *op_chan = umac_ap_get_specified_s1g_channel(umacd);

    uint8_t check_bw_mhz = MM_MIN(rx_metadata->bw_mhz, args->pri_bw_mhz);

    const struct mmwlan_s1g_channel *pri_chan =
        umac_interface_calc_pri_channel(umacd, op_chan, args->pri_1mhz_chan_idx, check_bw_mhz);
    if (pri_chan == NULL)
    {
        MMLOG_DBG("Probe request ignored: failed to resolve primary channel\n");
        return true;
    }

    uint16_t pri_chan_freq_100khz = pri_chan->centre_freq_hz / 100000;
    if (pri_chan_freq_100khz != rx_metadata->freq_100khz)
    {
        MMLOG_DBG("Probe request not on primary: %u.%u MHz (%u MHz BW) != %u.%u MHz\n",
                  rx_metadata->freq_100khz / 10,
                  rx_metadata->freq_100khz % 10,
                  rx_metadata->bw_mhz,
                  pri_chan_freq_100khz / 10,
                  pri_chan_freq_100khz % 10);
        return true;
    }

    if (umac_relay_get_state(umacd) == MMWLAN_RELAY_STATE_RELAY &&
        umac_relay_get_depth(umacd) == MMWLAN_RELAY_DEPTH_UNKNOWN)
    {
        return true;
    }
    return false;
}

void umac_ap_handle_probe_req(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        MMLOG_WRN("Ignoring probe req\n");
        return;
    }

    struct mmdrv_rx_metadata *rx_metadata = mmpkt_get_metadata(mmpkt_from_view(rxbufview)).rx;
    MMOSAL_ASSERT(rx_metadata != NULL);

    if (umac_ap_should_ignore_probe_req(umacd, rx_metadata))
    {
        return;
    }

    const struct dot11_hdr *probe_req_header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);


    uint16_t capability_info = DOT11_MASK_CAPINFO_ESS;

    if (data->args.security_type != MMWLAN_OPEN)
    {

        capability_info |= DOT11_MASK_CAPINFO_PRIVACY;
    }

    struct frame_data_probe_response probe_rsp_args = {
        .destination_address = dot11_get_sa(probe_req_header),
        .timestamp = NULL,
        .bssid = data->config.bssid,
        .ssid = data->config.ssid,
        .ssid_len = data->config.ssid_len,
        .ies = data->config.tail,
        .ies_len = data->config.tail_len,
        .capability_info = capability_info,
    };

    struct mmpkt *probe_rsp = build_mgmt_frame(umacd, frame_probe_response_build, &probe_rsp_args);
    if (probe_rsp == NULL)
    {
        MMLOG_WRN("Failed to contruct probe rsp\n");
        return;
    }

    struct mmrc_rate mmrc_rate_override = {
        .attempts = 5,
        .rate = MMRC_MCS0,
        .bw = (rx_metadata->bw_mhz == 1) ? MMRC_BW_1MHZ : MMRC_BW_2MHZ,
        .guard = MMRC_GUARD_LONG,
        .ss = MMRC_SPATIAL_STREAM_1,
        .flags = 0,
    };

    MMLOG_DBG("Scheduled Probe RSP TX (BSSID=" MM_MAC_ADDR_FMT ", DA=" MM_MAC_ADDR_FMT ")\n",
              MM_MAC_ADDR_VAL(probe_rsp_args.bssid),
              MM_MAC_ADDR_VAL(probe_rsp_args.destination_address));
    enum mmwlan_status status =
        umac_datapath_tx_mgmt_frame_ap(umacd, probe_rsp, &mmrc_rate_override);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to send probe rsp\n");
    }
}


static enum mmwlan_ap_sta_state umac_ap_morse_sta_state_to_mmwlan_ap_sta_state(
    enum morse_sta_state sta_state)
{
    switch (sta_state)
    {
        case MORSE_STA_MAX:
            MMOSAL_DEV_ASSERT(false);
            break;

        case MORSE_STA_AUTHENTICATED:
            return MMWLAN_AP_STA_AUTHENTICATED;

        case MORSE_STA_NONE:
        case MORSE_STA_NOTEXIST:
            return MMWLAN_AP_STA_UNKNOWN;

        case MORSE_STA_ASSOCIATED:
            return MMWLAN_AP_STA_ASSOCIATED;

        case MORSE_STA_AUTHORIZED:
            return MMWLAN_AP_STA_AUTHORIZED;
    }

    return MMWLAN_AP_STA_UNKNOWN;
}


static struct umac_sta_data *umac_ap_pop_sta_by_addr(struct umac_ap_data *data,
                                                     const uint8_t *sta_addr)
{
    MMOSAL_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(sta_addr && !mm_mac_addr_is_multicast(sta_addr));

    if (sta_addr == NULL || mm_mac_addr_is_multicast(sta_addr))
    {
        MMLOG_WRN("Popping the common STAD is not supported\n");
        return NULL;
    }

    for (size_t ii = 1; ii < data->max_stas; ii++)
    {
        struct umac_sta_data *stad = data->stas[ii];
        if (stad != NULL)
        {
            if (umac_sta_data_matches_peer_addr(stad, sta_addr))
            {
                data->stas[ii] = NULL;
                return stad;
            }
        }
    }
    return NULL;
}

struct umac_sta_data *umac_ap_lookup_sta_by_addr(struct umac_data *umacd, const uint8_t *sta_addr)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);

    if (data == NULL)
    {
        return NULL;
    }

    if (sta_addr == NULL || mm_mac_addr_is_multicast(sta_addr))
    {
        return data->sta_common;
    }

    for (size_t ii = 1; ii < data->max_stas; ii++)
    {
        struct umac_sta_data *stad = data->stas[ii];
        if (stad != NULL)
        {
            if (umac_sta_data_matches_peer_addr(stad, sta_addr))
            {
                return stad;
            }
        }
    }
    return NULL;
}

struct umac_sta_data *umac_ap_lookup_sta_by_aid(struct umac_data *umacd, uint16_t aid)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL || aid >= data->max_stas)
    {
        return NULL;
    }

    return data->stas[aid];
}


static struct umac_sta_data *umac_ap_alloc_sta(struct umac_data *umacd,
                                               struct umac_ap_data *data,
                                               uint16_t aid)
{
    if (aid == 0 || aid >= data->max_stas)
    {
        MMLOG_WRN("Requested AID %u exceeds max connected STAs %u\n", aid, data->max_stas - 1);
        MMOSAL_DEV_ASSERT(aid != 0);
        return NULL;
    }
    struct umac_sta_data *stad = data->stas[aid];
    if (stad)
    {
        MMLOG_ERR("AID %d already assigned to a STA %p\n", aid, stad);
        MMOSAL_DEV_ASSERT(0);
        return NULL;
    }
    stad = umac_sta_data_alloc(umacd);
    if (stad != NULL)
    {
        umac_sta_data_set_aid(stad, aid);
        data->stas[aid] = stad;
    }

    return stad;
}


static void umac_ap_dealloc_sta(struct umac_ap_data *data, struct umac_sta_data *stad)
{
    uint16_t aid = umac_sta_data_get_aid(stad);
    if (stad != data->stas[aid])
    {
        MMLOG_ERR("Dealloc invalid STA\n");
        MMOSAL_ASSERT(false);
    }
    data->stas[aid] = NULL;
    mmosal_free(stad);
}

enum mmwlan_status umac_ap_add_sta(struct umac_data *umacd,
                                   uint16_t aid,
                                   const struct umac_ap_sta_info *sta_info)
{
    if (!aid_is_valid(aid) || mm_mac_addr_is_multicast(sta_info->mac_addr))
    {
        return MMWLAN_INVALID_ARGUMENT;
    }
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    MMOSAL_ASSERT(data != NULL);
    struct umac_sta_data *stad = umac_ap_lookup_sta_by_addr(umacd, sta_info->mac_addr);
    if (stad != NULL)
    {
        MMLOG_ERR("MAC addr " MM_MAC_ADDR_FMT " already has a STA entry\n",
                  MM_MAC_ADDR_VAL(sta_info->mac_addr));
        return MMWLAN_UNAVAILABLE;
    }
    stad = umac_ap_alloc_sta(umacd, data, aid);
    if (stad == NULL)
    {
        MMLOG_WRN("Failed to alloc new STA\n");
        return MMWLAN_NO_MEM;
    }

    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    umac_sta_data_set_vif_id(stad, vif_id);
    umac_sta_data_set_bssid(stad, data->config.bssid);
    umac_sta_data_set_peer_addr(stad, sta_info->mac_addr);
    umac_sta_data_set_security(stad, data->args.security_type, data->args.pmf_mode);
    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    sta_data->last_active_ms = mmosal_get_time_ms();

    umac_keys_init(stad);


    uint8_t sgi_flags = 0;
    uint8_t max_mcs = 7;
    umac_rc_start(stad, sgi_flags, max_mcs);

    enum mmwlan_status status = umac_ap_update_sta(umacd, sta_info);
    if (status != MMWLAN_SUCCESS)
    {
        umac_ap_dealloc_sta(data, stad);
    }

#if MMLOG_LEVEL >= MMLOG_LEVEL_VRB
    dump_sta_list(data);
#endif

    return status;
}


static void umac_ap_notify_sta_status_change(struct umac_sta_data *stad,
                                             struct umac_ap_data *data,
                                             uint16_t aid,
                                             const uint8_t *mac_addr,
                                             enum mmwlan_ap_sta_state state)
{
    struct mmwlan_ap_sta_status sta_status = {
        .aid = aid,
        .state = state,
    };
    mac_addr_copy(sta_status.mac_addr, mac_addr);

    if (data->args.sta_status_cb != NULL)
    {
        data->args.sta_status_cb(&sta_status, data->args.sta_status_cb_arg);
    }

    umac_relay_handle_ap_sta_state(stad, &sta_status);
}

enum mmwlan_status umac_ap_update_sta(struct umac_data *umacd,
                                      const struct umac_ap_sta_info *sta_info)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    MMOSAL_ASSERT(data != NULL);

    struct umac_sta_data *stad = umac_ap_lookup_sta_by_addr(umacd, sta_info->mac_addr);
    if (stad == NULL)
    {
        MMLOG_ERR("STA record for " MM_MAC_ADDR_FMT " not found. Unable to set state %u\n",
                  MM_MAC_ADDR_VAL(sta_info->mac_addr),
                  sta_info->sta_state);
        return MMWLAN_ERROR;
    }

    enum morse_sta_state sta_state = sta_info->sta_state;
    bool state_changed = umac_ap_set_stad_state_(stad, sta_state);
    uint16_t aid = umac_sta_data_get_aid(stad);

    MMLOG_DBG("Updating STA record: " MM_MAC_ADDR_FMT ", aid=%u, state=%u\n",
              MM_MAC_ADDR_VAL(sta_info->mac_addr),
              aid,
              sta_state);

    uint16_t vif_id = umac_sta_data_get_vif_id(stad);
    enum mmwlan_status status = mmdrv_update_sta_state(vif_id, aid, sta_info->mac_addr, sta_state);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Unable to update sta state in mmdrv (ret: %u).\n", status);
        return status;
    }

    if (state_changed)
    {
        MMLOG_VRB("Invoke STA callback (update)\n");
        enum mmwlan_ap_sta_state state = umac_ap_morse_sta_state_to_mmwlan_ap_sta_state(sta_state);
        umac_ap_notify_sta_status_change(stad, data, aid, sta_info->mac_addr, state);
    }

    return MMWLAN_SUCCESS;
}

static enum mmwlan_status umac_ap_remove_sta_record(struct umac_data *umacd,
                                                    struct umac_ap_data *data,
                                                    struct umac_sta_data *stad)
{
    enum mmwlan_status status = MMWLAN_ERROR;


    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    MMOSAL_DEV_ASSERT(vif_id != MMDRV_VIF_ID_INVALID);

    uint16_t aid = umac_sta_data_get_aid(stad);

    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    bool update_required =
        (sta_data->sta_state != MORSE_STA_NOTEXIST && sta_data->sta_state != MORSE_STA_NONE);

    uint8_t mac_addr[6];
    umac_sta_data_get_peer_addr(stad, mac_addr);


    int ret =
        mmdrv_update_sta_state(vif_id, umac_sta_data_get_aid(stad), mac_addr, MORSE_STA_NOTEXIST);
    if (ret)
    {
        MMLOG_WRN("Unable to update sta state in mmdrv (ret: %d).\n", ret);
        status = MMWLAN_ERROR;
    }
    else
    {
        MMLOG_WRN("Removed STA record for " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(mac_addr));
        status = MMWLAN_SUCCESS;
    }

    umac_rc_stop(stad);
    umac_rc_deinit(stad);
    umac_datapath_stad_flush_txq(umacd, stad);
    ap_traffic_bitmap_clear_aid_bit(data->bitmap, aid);

    mmosal_free(stad);

    if (update_required)
    {
        MMLOG_VRB("Invoke STA callback (remove)\n");
        umac_ap_notify_sta_status_change(stad, data, aid, mac_addr, MMWLAN_AP_STA_UNKNOWN);
    }

#if MMLOG_LEVEL >= MMLOG_LEVEL_VRB
    dump_sta_list(data);
#endif

    return status;
}

enum mmwlan_status umac_ap_remove_sta(struct umac_data *umacd, const uint8_t *mac_addr)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        MMOSAL_DEV_ASSERT(false);
        return MMWLAN_UNAVAILABLE;
    }

    MMOSAL_DEV_ASSERT(mac_addr && !mm_mac_addr_is_multicast(mac_addr));


    struct umac_sta_data *stad = umac_ap_pop_sta_by_addr(data, mac_addr);
    if (stad == NULL)
    {
        if (!mac_addr || mm_mac_addr_is_multicast(mac_addr))
        {
            MMLOG_WRN("Removing the common STA record not supported\n");
            return MMWLAN_ERROR;
        }
        MMLOG_INF("No STA record for " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(mac_addr));
        return MMWLAN_SUCCESS;
    }

    return umac_ap_remove_sta_record(umacd, data, stad);
}

enum mmwlan_status umac_ap_get_bssid(struct umac_data *umacd, uint8_t *bssid)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL || data->sta_common == NULL)
    {
        MMLOG_DBG("AP mode not active\n");
        return MMWLAN_UNAVAILABLE;
    }

    umac_sta_data_get_bssid(data->sta_common, bssid);

    return MMWLAN_SUCCESS;
}

const uint8_t *umac_ap_peek_bssid(struct umac_data *umacd)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL || data->sta_common == NULL)
    {
        MMLOG_DBG("AP mode not active\n");
        return NULL;
    }

    return umac_sta_data_peek_bssid(data->sta_common);
}

enum mmwlan_sta_state umac_ap_get_sta_state(struct umac_sta_data *stad)
{
    if (stad != NULL)
    {
        struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);

        if (sta_data->sta_state == MORSE_STA_AUTHORIZED)
        {
            return MMWLAN_STA_CONNECTED;
        }
        else
        {
            return MMWLAN_STA_CONNECTING;
        }
    }
    else
    {
        return MMWLAN_STA_DISABLED;
    }
}

const struct mmwlan_ap_args *umac_ap_get_args(struct umac_data *umacd)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        return NULL;
    }

    return &(data->args);
}

const struct mmwlan_s1g_channel *umac_ap_get_specified_s1g_channel(struct umac_data *umacd)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        return NULL;
    }

    return data->specified_chan;
}

bool umac_ap_is_scan_channel_allowed(struct umac_data *umacd,
                                     const struct mmwlan_s1g_channel *scan_ch)
{
    const struct mmwlan_s1g_channel *ap_op_ch = umac_ap_get_specified_s1g_channel(umacd);
    if (ap_op_ch == NULL)
    {
        return true;
    }

    const struct mmwlan_ap_args *ap_args = umac_ap_get_args(umacd);
    const struct mmwlan_s1g_channel *ap_pri_ch =
        umac_interface_calc_pri_channel(umacd,
                                        ap_op_ch,
                                        ap_args->pri_1mhz_chan_idx,
                                        ap_args->pri_bw_mhz);
    MMOSAL_DEV_ASSERT(ap_pri_ch && ap_pri_ch->bw_mhz <= 2);

    return ap_pri_ch == scan_ch;
}

enum mmwlan_status umac_ap_get_channel_info(struct umac_data *umacd,
                                            struct mmwlan_vif_channel_info *cfg)
{
    if (cfg == NULL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }


    uint8_t bssid[MMWLAN_MAC_ADDR_LEN] = { 0 };
    umac_sta_data_get_bssid(data->sta_common, bssid);
    if (mm_mac_addr_is_zero(bssid))
    {
        return MMWLAN_UNAVAILABLE;
    }

    cfg->op_class = data->args.op_class;
    cfg->s1g_chan_num = data->args.s1g_chan_num;

    cfg->pri_bw_mhz = data->args.pri_bw_mhz;
    cfg->pri_1mhz_chan_idx = data->args.pri_1mhz_chan_idx;

    return MMWLAN_SUCCESS;
}


static bool umac_ap_get_stad_sleep_state(struct umac_sta_data *stad)
{
    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    return sta_data->asleep;
}

void umac_ap_queue_pkt(struct umac_data *umacd, struct umac_sta_data *stad, struct mmpkt *mmpkt)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        MMOSAL_DEV_ASSERT(false);
        mmpkt_release(mmpkt);
        return;
    }

    MMOSAL_TASK_ENTER_CRITICAL();
    umac_sta_data_queue_pkt(stad, mmpkt);
    umac_stats_update_datapath_txq_high_water_mark(umacd, ++data->num_pkts_queued);
    MMOSAL_TASK_EXIT_CRITICAL();

    bool asleep = umac_ap_get_stad_sleep_state(stad);
    uint16_t aid = umac_sta_data_get_aid(stad);
    if (asleep && aid)
    {
        ap_traffic_bitmap_set_aid_bit(data->bitmap, aid);
        MMLOG_INF("Set AP traffic pending bit for AID %d\n", aid);
    }
}

bool umac_ap_is_stad_paused(struct umac_sta_data *stad)
{
    return umac_ap_get_stad_sleep_state(stad) || umac_sta_data_is_paused(stad);
}


static struct umac_sta_data *umac_ap_get_next_sta_for_tx(struct umac_ap_data *data)
{

    struct umac_sta_data *stad = data->sta_common;
    if (!umac_ap_is_stad_paused(stad))
    {

        bool has_traffic = umac_sta_data_get_queued_len(stad);
        if (has_traffic)
        {
            return stad;
        }

        umac_ap_set_stad_sleep_state_(stad, true);
        MMLOG_DBG("No more queued traffic for common STA, restoring sleep\n");
    }

    for (size_t ii = 1; ii < data->max_stas; ++ii)
    {
        stad = data->stas[ii];

        if (stad != NULL && !umac_ap_is_stad_paused(stad) && umac_sta_data_get_queued_len(stad))
        {
            return stad;
        }
    }

    return NULL;
}

bool umac_ap_tx_dequeue_frame(struct umac_data *umacd,
                              struct umac_sta_data **stad_ptr,
                              struct mmpkt **txbuf_ptr)
{
    MMOSAL_ASSERT(umacd && stad_ptr && txbuf_ptr);
    *stad_ptr = NULL;
    *txbuf_ptr = NULL;

    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        return false;
    }

    struct umac_sta_data *stad = umac_ap_get_next_sta_for_tx(data);
    if (stad == NULL)
    {
        return false;
    }

    bool has_more = false;
    MMOSAL_TASK_ENTER_CRITICAL();
    struct mmpkt *txbuf = umac_sta_data_pop_pkt(stad);
    if (txbuf != NULL)
    {
        has_more = --data->num_pkts_queued;
    }
    else
    {
        stad = NULL;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    *stad_ptr = stad;
    *txbuf_ptr = txbuf;
    return has_more;
}

bool umac_ap_set_stad_sleep_state(struct umac_sta_data *stad, bool asleep)
{
    uint16_t aid = umac_sta_data_get_aid(stad);
    if (!aid_is_valid(aid))
    {

        MMLOG_WRN("Attempt to mark invalid AID as asleep %d\n", aid);
        return false;
    }

    bool state_change = (umac_ap_get_stad_sleep_state(stad) != asleep);
    if (!state_change)
    {
        return true;
    }
    umac_ap_set_stad_sleep_state_(stad, asleep);
    MMLOG_VRB("STAD AID %d - sleep state %s\n", aid, asleep ? "asleep" : "awake");
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (!asleep)
    {
        ap_traffic_bitmap_clear_aid_bit(data->bitmap, aid);
    }
    else if (umac_sta_data_get_queued_len(stad))
    {

        ap_traffic_bitmap_set_aid_bit(data->bitmap, aid);
    }
    return true;
}

enum mmwlan_status umac_ap_get_sta_status(struct umac_data *umacd,
                                          const uint8_t *sta_addr,
                                          struct mmwlan_ap_sta_status *sta_status)
{
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        MMLOG_DBG("AP mode not active\n");
        return MMWLAN_UNAVAILABLE;
    }

    struct umac_sta_data *stad = umac_ap_lookup_sta_by_addr(umacd, sta_addr);
    if (stad == NULL)
    {
        return MMWLAN_NOT_FOUND;
    }

    if (sta_status != NULL)
    {
        struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
        sta_status->state = umac_ap_morse_sta_state_to_mmwlan_ap_sta_state(sta_data->sta_state);
        sta_status->aid = umac_sta_data_get_aid(stad);
        umac_sta_data_get_peer_addr(stad, sta_status->mac_addr);
    }

    return MMWLAN_SUCCESS;
}

void umac_ap_update_stad_last_active(struct umac_sta_data *stad, uint32_t activity_ms)
{
    MMOSAL_DEV_ASSERT(stad);

    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    if (mmosal_time_lt(sta_data->last_active_ms, activity_ms))
    {
        sta_data->last_active_ms = activity_ms;
    }
}

uint32_t umac_ap_get_stad_last_active(struct umac_sta_data *stad)
{
    MMOSAL_DEV_ASSERT(stad);
    struct umac_ap_sta_data *sta_data = umac_sta_data_get_ap(stad);
    return sta_data->last_active_ms;
}

enum mmwlan_status umac_ap_disable_ap(struct umac_data *umacd)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_ap_data *data = umac_data_get_ap(umacd);
    if (data == NULL)
    {
        MMLOG_INF("AP not active\n");
        return MMWLAN_SUCCESS;
    }

    enum mmwlan_relay_state relay_state = umac_relay_get_state(umacd);
    if (relay_state == MMWLAN_RELAY_STATE_DISABLED || relay_state == MMWLAN_RELAY_STATE_ROOT)
    {

        struct mmwlan_vif_state state = {
            .vif = MMWLAN_VIF_AP,
            .link_state = MMWLAN_LINK_DOWN,
        };
        umac_interface_invoke_vif_state_cb(umacd, &state);
    }

    MMLOG_DBG("Removing AP Supplicant interface\n");

    enum mmwlan_status beacon_status = mmdrv_stop_beaconing();
    if (beacon_status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Stop beaconing failed (%u)\n", beacon_status);
        MMOSAL_ASSERT(false);
    }

    status = umac_supp_remove_ap_interface(umacd);

    MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);

    for (size_t ii = 1; ii < data->max_stas; ii++)
    {
        if (data->stas[ii] != NULL)
        {
            MMLOG_DBG("Removing STA record for " MM_MAC_ADDR_FMT " due to AP disable\n",
                      MM_MAC_ADDR_VAL(umac_sta_data_peek_peer_addr(data->stas[ii])));
            umac_ap_remove_sta_record(umacd, data, data->stas[ii]);
            data->stas[ii] = NULL;
        }
    }

    umac_datapath_stad_flush_txq(umacd, data->sta_common);


    mmosal_task_sleep(100);

    mmosal_free(data->sta_common);
    data->sta_common = NULL;
    mmosal_free(data->stas);
    data->stas = NULL;
    mmosal_free(data->config.head);
    data->config.head = NULL;
    mmosal_free(data->config.tail);
    data->config.tail = NULL;

    MMLOG_DBG("Removing AP interface\n");
    umac_interface_remove(umacd, UMAC_INTERFACE_AP);
    MMLOG_DBG("Deallocating AP data structure\n");
    umac_data_dealloc_ap(umacd);
    MMLOG_INF("AP mode disabled\n");

    return MMWLAN_SUCCESS;
}
