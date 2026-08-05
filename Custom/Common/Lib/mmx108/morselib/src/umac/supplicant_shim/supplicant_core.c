/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmlog.h"
#include "mmpkt.h"
#include "mmwlan.h"
#include "umac/connection/umac_connection.h"
#include "umac/data/umac_data.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac_supp_shim_private.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/interface/umac_interface.h"
#include "umac/scan/umac_scan.h"
#include "mmosal.h"

#if MMLOG_LEVEL < MMLOG_LEVEL_ERR
#define WPA_LOG_LEVEL (MSG_ERROR + 1)
#elif MMLOG_LEVEL == MMLOG_LEVEL_ERR
#define WPA_LOG_LEVEL (MSG_ERROR)
#elif MMLOG_LEVEL == MMLOG_LEVEL_WRN
#define WPA_LOG_LEVEL (MSG_WARNING)
#elif MMLOG_LEVEL == MMLOG_LEVEL_INF
#define WPA_LOG_LEVEL (MSG_INFO)
#elif MMLOG_LEVEL == MMLOG_LEVEL_DBG
#define WPA_LOG_LEVEL (MSG_DEBUG)
#elif MMLOG_LEVEL == MMLOG_LEVEL_VRB
#define WPA_LOG_LEVEL (MSG_EXCESSIVE)
#endif

SUP_TRACE_DECLARE

enum mmwlan_status umac_supp_start_supp(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (data->is_started)
    {
        MMLOG_DBG("Supplicant already started\n");
        return MMWLAN_SUCCESS;
    }

    struct wpa_params params = { 0 };

    SUP_TRACE_INIT();

    params.wpa_debug_level = WPA_LOG_LEVEL;

    data->global = wpa_supplicant_init(&params);
    if (!data->global)
    {
        MMLOG_WRN("WPAS: wpa supplicant initialization failed\n");
        goto out;
    }
    else
    {
        MMLOG_INF("WPAS: wpa supplicant initialized\n");
    }

    SUP_TRACE("run");
    wpa_supplicant_run(data->global);
    data->is_started = true;

    return MMWLAN_SUCCESS;

out:
    if (data->global)
    {
        os_free(data->global);
        data->global = NULL;
    }

    return MMWLAN_ERROR;
}

enum mmwlan_status umac_supp_add_sta_interface(struct umac_data *umacd, const char *confname)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (data->sta_wpa_s != NULL)
    {
        MMLOG_INF("STA interface already active. Reloading config\n");
        os_free(data->sta_wpa_s->confname);
        data->sta_wpa_s->confname = os_strdup(confname);

        if (wpa_supplicant_reload_configuration(data->sta_wpa_s))
        {
            MMLOG_WRN("WPAS: config reload failed\n");
            return MMWLAN_ERROR;
        }
        return MMWLAN_SUCCESS;
    }

    enum mmwlan_status status = umac_supp_start_supp(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    struct wpa_interface iface = {
        .confname = confname,
        .driver = UMAC_SUPP_STA_DRIVER_NAME,
        .ifname = UMAC_SUPP_STA_CONFIG_NAME,
    };

    data->sta_wpa_s = wpa_supplicant_add_iface(data->global, &iface, NULL);
    if (data->sta_wpa_s == NULL)
    {
        MMLOG_WRN("WPAS: %s interface addition failed\n", iface);
        return MMWLAN_ERROR;
    }
    else
    {
        MMLOG_INF("WPAS: %s interface addition successful\n", iface);
    }

    data->sta_wpa_s->auto_reconnect_disabled = data->auto_reconnect_disabled;

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_supp_remove_sta_interface(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (!data->is_started)
    {
        return MMWLAN_UNAVAILABLE;
    }

    MMLOG_INF("Removing %s Supp interface\n", "STA");

    if (data->sta_wpa_s == NULL)
    {
        return MMWLAN_NOT_FOUND;
    }

    int ret = wpa_supplicant_remove_iface(data->global, data->sta_wpa_s, 0);
    data->sta_wpa_s = NULL;

    if (ret == 0)
    {
        return MMWLAN_SUCCESS;
    }
    else
    {
        return MMWLAN_ERROR;
    }
}

enum mmwlan_status umac_supp_connect(struct umac_data *umacd)
{
    MMLOG_DBG("WPAS: Connect\n");

    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    const struct mmwlan_sta_args *args = umac_connection_get_sta_args(umacd);

    if (!data->is_started || data->sta_wpa_s == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    /*
     * Quick join skips WPAS scan but HAL/mmipal may still leave the hw scan VIF
     * active. Abort before set_channel during auth (avoids SET_CHANNEL timeout).
     */
    wpas_abort_ongoing_scan(data->sta_wpa_s);
    umac_scan_abort(umacd, NULL);
    if (args != NULL && args->preconnect_bss_valid)
    {
        mmosal_task_sleep(100);
    }

    (void)mmwpas_prepare_quick_connect(data, args);
    wpas_request_connection(data->sta_wpa_s);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_supp_reconnect(struct umac_data *umacd)
{
    MMLOG_DBG("WPAS: Reconnect\n");

    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    if (!data->is_started || data->sta_wpa_s == NULL || data->sta_wpa_s->disconnected)
    {
        return MMWLAN_UNAVAILABLE;
    }
    wpa_supplicant_reconnect(data->sta_wpa_s);
    return MMWLAN_SUCCESS;
}

#if !(defined(MMWLAN_DPP_DISABLED) && MMWLAN_DPP_DISABLED)
enum mmwlan_status umac_supp_dpp_push_button(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    struct wpa_supplicant *wpa_s = (struct wpa_supplicant *)data->sta_driver_ctx;
    if (wpa_s == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    int ret = wpas_dpp_push_button(wpa_s, NULL);

    return ret ? MMWLAN_ERROR : MMWLAN_SUCCESS;
}

void umac_supp_dpp_push_button_stop(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    struct wpa_supplicant *wpa_s = (struct wpa_supplicant *)data->sta_driver_ctx;
    if (wpa_s == NULL)
    {
        return;
    }

    wpas_dpp_push_button_stop(wpa_s);
}

#endif

void umac_supp_disconnect(struct umac_data *umacd)
{
    MMLOG_INF("WPAS: Disconnect\n");

    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    bool sta_is_active = data->is_started && data->sta_wpa_s;
    if (sta_is_active)
    {
        wpas_request_disconnection(data->sta_wpa_s);
    }
}

void umac_supp_deinit(struct umac_data *umacd)
{
    MMLOG_DBG("WPAS: Deinit\n");

    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    if (data->global)
    {
        wpa_supplicant_deinit(data->global);
    }
    data->sta_wpa_s = NULL;
    data->ap_wpa_s = NULL;
    data->global = NULL;
    data->is_started = false;
}

void umac_supp_set_auto_reconnect_disabled(struct umac_data *umacd, bool auto_reconnect_disabled)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    data->auto_reconnect_disabled = auto_reconnect_disabled;
    if (data->is_started)
    {
        if (data->sta_wpa_s != NULL)
        {
            data->sta_wpa_s->auto_reconnect_disabled = data->auto_reconnect_disabled;
        }
    }
}

void umac_supp_l2_sock_receive(struct umac_data *umacd,
                               const uint8_t *payload,
                               size_t payload_len,
                               const uint8_t *src_addr)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (data->l2.rx_callback == NULL)
    {
        MMLOG_ERR("L2 socket callback not initialised\n");
        return;
    }

    data->l2.rx_callback(data->l2.rx_callback_ctx, src_addr, payload, payload_len);
}

void umac_supp_process_deauth(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    umac_supp_event(data->sta_driver_ctx, EVENT_DEAUTH, NULL);
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++-compat"

void umac_supp_wnm_enter(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.wnm.sleep_action = WNM_SLEEP_ENTER;
    wpa_event_data.wnm.sleep_intval = 0;

    umac_supp_event(data->sta_driver_ctx, EVENT_WNM, &wpa_event_data);
}

void umac_supp_wnm_exit(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.wnm.sleep_action = WNM_SLEEP_EXIT;
    wpa_event_data.wnm.sleep_intval = 0;

    umac_supp_event(data->sta_driver_ctx, EVENT_WNM, &wpa_event_data);
}

#pragma GCC diagnostic pop

void umac_supp_process_auth_resp(struct umac_data *umacd, struct frame_data_auth *auth_data)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    mac_addr_copy(wpa_event_data.auth.peer, auth_data->sta_address);
    mac_addr_copy(wpa_event_data.auth.bssid, auth_data->bssid);
    wpa_event_data.auth.auth_type = auth_data->auth_alg;
    wpa_event_data.auth.auth_transaction = auth_data->seq;
    wpa_event_data.auth.status_code = auth_data->status_code;
    wpa_event_data.auth.ies = auth_data->auth_data;
    wpa_event_data.auth.ies_len = auth_data->auth_data_len;

    umac_supp_event(data->sta_driver_ctx, EVENT_AUTH, &wpa_event_data);
}

void umac_supp_process_assoc_reassoc_resp(struct umac_data *umacd,
                                          struct frame_data_assoc_rsp *assoc_data)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    if (assoc_data->status_code == DOT11_STATUS_SUCCESS)
    {
        wpa_event_data.assoc_info.reassoc = assoc_data->is_reassoc_rsp;
        wpa_event_data.assoc_info.resp_ies_len = assoc_data->ies_len;
        wpa_event_data.assoc_info.resp_ies = assoc_data->ies;

        umac_supp_event(data->sta_driver_ctx, EVENT_ASSOC, &wpa_event_data);
    }
    else
    {
        wpa_event_data.assoc_reject.resp_ies_len = assoc_data->ies_len;
        wpa_event_data.assoc_reject.resp_ies = assoc_data->ies;
        wpa_event_data.assoc_reject.status_code = assoc_data->status_code;

        umac_supp_event(data->sta_driver_ctx, EVENT_ASSOC_REJECT, &wpa_event_data);
    }
}

void umac_supp_process_disassoc_req(struct umac_data *umacd, uint16_t reason_code)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.disassoc_info.reason_code = reason_code;
    umac_supp_event(data->sta_driver_ctx, EVENT_DISASSOC, &wpa_event_data);
}

void umac_supp_process_unprotected_deauth(struct umac_data *umacd,
                                          uint16_t reason_code,
                                          const uint8_t *sa,
                                          const uint8_t *da)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.unprot_deauth.reason_code = reason_code;
    wpa_event_data.unprot_deauth.sa = sa;
    wpa_event_data.unprot_deauth.da = da;

    umac_supp_event(data->sta_driver_ctx, EVENT_UNPROT_DEAUTH, &wpa_event_data);
}

void umac_supp_process_unprotected_disassoc(struct umac_data *umacd,
                                            uint16_t reason_code,
                                            const uint8_t *sa,
                                            const uint8_t *da)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.unprot_disassoc.reason_code = reason_code;
    wpa_event_data.unprot_disassoc.sa = sa;
    wpa_event_data.unprot_disassoc.da = da;

    umac_supp_event(data->sta_driver_ctx, EVENT_UNPROT_DISASSOC, &wpa_event_data);
}

void umac_supp_process_mgmt_frame(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct mmpkt *mmpkt = mmpkt_from_view(rxbufview);
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.rx_mgmt.frame = mmpkt_get_data_start(rxbufview);
    wpa_event_data.rx_mgmt.frame_len = mmpkt_get_data_length(rxbufview);

    wpa_event_data.rx_mgmt.freq = (mmpkt_get_metadata(mmpkt).rx->freq_100khz * 100);


    MMOSAL_DEV_ASSERT(!(data->ap_driver_ctx && data->sta_driver_ctx));
    if (data->ap_driver_ctx != NULL)
    {
        umac_supp_event(data->ap_driver_ctx, EVENT_RX_MGMT, &wpa_event_data);
    }
    if (data->sta_driver_ctx != NULL)
    {
        umac_supp_event(data->sta_driver_ctx, EVENT_RX_MGMT, &wpa_event_data);
    }
}

void umac_supp_process_probe_req_frame(struct umac_data *umacd,
                                       const uint8_t *frame,
                                       uint32_t frame_len,
                                       int16_t rssi_dbm)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };
    const struct dot11_hdr *header = (const struct dot11_hdr *)frame;

    wpa_event_data.rx_probe_req.sa = header->addr2;
    wpa_event_data.rx_probe_req.da = header->addr1;
    wpa_event_data.rx_probe_req.bssid = header->addr3;
    wpa_event_data.rx_probe_req.ie = frame + sizeof(*header);
    wpa_event_data.rx_probe_req.ie_len = frame_len - sizeof(*header);
    wpa_event_data.rx_probe_req.ssi_signal = rssi_dbm;

    umac_supp_event(data->ap_driver_ctx, EVENT_RX_PROBE_REQ, &wpa_event_data);
}

void umac_supp_notify_signal_change(struct umac_data *umacd, int16_t rssi, bool is_above_threshold)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    union wpa_event_data wpa_event_data = { 0 };

    wpa_event_data.signal_change.data.signal = rssi;
    wpa_event_data.signal_change.above_threshold = (is_above_threshold ? 1 : 0);
    umac_supp_event(data->sta_driver_ctx, EVENT_SIGNAL_CHANGE, &wpa_event_data);
}

void umac_supp_roam(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);

    if (data->sta_wpa_s == NULL)
    {
        return;
    }

    memcpy(data->sta_wpa_s->conf->ssid->bssid, sta_args->bssid, MMWLAN_MAC_ADDR_LEN);

    MMLOG_DBG("WPAS: trigger reassociation for roaming\n");
    data->sta_wpa_s->reassociate = 1;
    wpa_supplicant_req_scan(data->sta_wpa_s, 0, 0);
}

void umac_supp_tx_status(struct umac_data *umacd, struct mmpkt *pkt, bool acked)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);
    struct mmpktview *view = mmpkt_open(pkt);
    if (view == NULL)
    {
        return;
    }

    void *driver_ctx = NULL;
    uint16_t vif_types =
        umac_interface_get_vif_type_mask(umacd, mmpkt_get_metadata(pkt).tx->vif_id);
    if (vif_types & UMAC_INTERFACE_AP)
    {
        driver_ctx = data->ap_driver_ctx;
    }
    else if (vif_types & UMAC_INTERFACE_STA)
    {
        driver_ctx = data->sta_driver_ctx;
    }

    struct dot11_hdr *hdr = (struct dot11_hdr *)mmpkt_get_data_start(view);


    if (dot11_frame_control_get_type(hdr->frame_control) == DOT11_FC_TYPE_MGMT)
    {
        MMLOG_DBG("TX status mgmt pkt: %08lx\n", (uint32_t)pkt);
        union wpa_event_data wpa_event_data = { .tx_status = {
                                                    .type = dot11_frame_control_get_type(
                                                        hdr->frame_control),
                                                    .stype = dot11_frame_control_get_subtype(
                                                        hdr->frame_control),
                                                    .dst = dot11_get_da(hdr),
                                                    .data = mmpkt_get_data_start(view),
                                                    .data_len = mmpkt_get_data_length(view),
                                                    .ack = acked,
                                                    .link_id = 0,
                                                } };

        umac_supp_event(driver_ctx, EVENT_TX_STATUS, &wpa_event_data);
    }

    mmpkt_close(&view);
}

void umac_supp_event(void *ctx, enum wpa_event_type event, union wpa_event_data *data)
{
    if (ctx == NULL)
    {
        return;
    }

    wpa_supplicant_event(ctx, event, data);
}

struct l2_packet_data *l2_packet_init(
    const char *ifname,
    const u8 *own_addr,
    unsigned short protocol,
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf, size_t len),
    void *rx_callback_ctx,
    int l2_hdr)
{
    MM_UNUSED(ifname);
    MM_UNUSED(protocol);

    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    data->l2.umacd = umacd;
    data->l2.rx_callback = rx_callback;
    data->l2.rx_callback_ctx = rx_callback_ctx;
    data->l2.l2_hdr = l2_hdr;
    memcpy(data->l2.own_addr, own_addr, sizeof(data->l2.own_addr));

    return &data->l2;
}

struct l2_packet_data *l2_packet_init_bridge(
    const char *br_ifname,
    const char *ifname,
    const u8 *own_addr,
    unsigned short protocol,
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf, size_t len),
    void *rx_callback_ctx,
    int l2_hdr)
{
    MM_UNUSED(ifname);

    return l2_packet_init(br_ifname, own_addr, protocol, rx_callback, rx_callback_ctx, l2_hdr);
}

void l2_packet_deinit(struct l2_packet_data *l2)
{
    if (l2 != NULL)
    {
        memset(l2, 0, sizeof(*l2));
    }
}

int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
    memcpy(addr, l2->own_addr, sizeof(l2->own_addr));
    return 0;
}

int l2_packet_send(struct l2_packet_data *l2,
                   const u8 *dst_addr,
                   u16 proto,
                   const u8 *buf,
                   size_t len)
{
    struct ieee8023_hdr header;
    bool add_header = (l2 && (l2->l2_hdr == 0));
    size_t data_len = len + (add_header ? sizeof(header) : 0);
    int ret = -1;
    struct mmpkt *txbuf;
    struct mmpktview *txbufview;

    if (l2 == NULL)
    {
        MMLOG_DBG("L2 not initialised\n");
        goto fail;
    }


    txbuf = umac_datapath_alloc_mmpkt_for_qos_data_tx(data_len, MMDRV_PKT_CLASS_MGMT);
    if (txbuf == NULL)
    {
        MMLOG_DBG("Failed to allocate for L2 packet TX\n");
        goto fail;
    }

    if (add_header)
    {
        memcpy(header.dest, dst_addr, ETH_ALEN);
        memcpy(header.src, l2->own_addr, ETH_ALEN);
        header.ethertype = bswap_16(proto);
    }

    txbufview = mmpkt_open(txbuf);
    if (add_header)
    {
        mmpkt_append_data(txbufview, (uint8_t *)&header, sizeof(header));
    }
    mmpkt_append_data(txbufview, buf, len);
    mmpkt_close(&txbufview);
    mmdrv_get_tx_metadata(txbuf)->tid = 0;

    umac_datapath_tx_frame(l2->umacd, txbuf, ENCRYPTION_AUTO, NULL);
    ret = 0;
fail:
    return ret;
}

void l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
    MM_UNUSED(l2);
}

int l2_packet_set_packet_filter(struct l2_packet_data *l2, enum l2_packet_filter_type type)
{
    MM_UNUSED(l2);
    MM_UNUSED(type);

    return -1;
}

int morse_twt_conf(const char *ifname, struct morse_twt *twt_config)
{
    MM_UNUSED(ifname);

    MMLOG_INF("TWT: wake_interval_us=0x" MM_X64_FMT ", wake_duration_us=%lu, setup_command=%lu\n",
              MM_X64_VAL(twt_config->wake_interval_us),
              twt_config->wake_duration_us,
              twt_config->setup_command);

    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_twt_command cmd = {
        .type = UMAC_TWT_CMD_TYPE_CONFIGURE,
        .wake_interval_us = twt_config->wake_interval_us,
        .min_wake_duration_us = twt_config->wake_duration_us,
        .twt_setup_command = twt_config->setup_command,
    };

    const struct mmwlan_twt_config_args *twt_config_args = umac_twt_get_config(umacd);
    if (twt_config_args->twt_wake_interval_mantissa || twt_config_args->twt_wake_interval_exponent)
    {
        cmd.type = UMAC_TWT_CMD_TYPE_CONFIGURE_EXPLICIT;
        cmd.expl.wake_interval_mantissa = twt_config_args->twt_wake_interval_mantissa;
        cmd.expl.wake_interval_exponent = twt_config_args->twt_wake_interval_exponent;
    }

    enum mmwlan_status status = umac_twt_handle_command(umacd, &cmd);

    return (status == MMWLAN_SUCCESS) ? 0 : -1;
}

int morse_cac_conf(const char *ifname, bool enable)
{
    MM_UNUSED(ifname);
    MM_UNUSED(enable);



    return 0;
}
