/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_scan.h"
#include "umac_scan_data.h"
#include "mmlog.h"
#include "umac/data/umac_data.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/frames/probe_response.h"
#include "umac/ies/s1g_operation.h"
#include "umac/regdb/umac_regdb.h"
#include "mmdrv.h"
#include "umac/core/umac_core_private.h"

#include "hw_scan_fsm.h"
#include "hw_scan.h"

enum umac_scan_event
{
    UMAC_SCAN_EVENT_REQUEST,
    UMAC_SCAN_EVENT_ABORT_ALL,
    UMAC_SCAN_EVENT_ABORT_ACTIVE,
};

static void umac_scan_handle_event(struct umac_scan_data *data, enum umac_scan_event event)
{
    switch (event)
    {
        case UMAC_SCAN_EVENT_REQUEST:
            hw_scan_fsm_handle_event(&data->hw_scan_data.fsm_inst, HW_SCAN_FSM_EVENT_REQUEST);
            break;

        case UMAC_SCAN_EVENT_ABORT_ALL:
            hw_scan_fsm_handle_event(&data->hw_scan_data.fsm_inst, HW_SCAN_FSM_EVENT_ABORT_ALL);
            break;

        case UMAC_SCAN_EVENT_ABORT_ACTIVE:
            hw_scan_fsm_handle_event(&data->hw_scan_data.fsm_inst, HW_SCAN_FSM_EVENT_ABORT_ACTIVE);
            break;
    }
}

void umac_scan_init(struct umac_data *umacd)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    hw_scan_fsm_init(&data->hw_scan_data.fsm_inst);
    data->hw_scan_data.fsm_inst.arg = umacd;
}

void umac_scan_deinit(struct umac_data *umacd)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    MMOSAL_ASSERT(data->active_scan_req == NULL);
    MMOSAL_ASSERT(data->pending_scan_req == NULL);
}

void umac_scan_abort(struct umac_data *umacd, const struct umac_scan_req *scan_req)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    if (scan_req == NULL)
    {
        umac_scan_handle_event(data, UMAC_SCAN_EVENT_ABORT_ALL);
    }
    else if (scan_req == data->active_scan_req)
    {
        umac_scan_handle_event(data, UMAC_SCAN_EVENT_ABORT_ACTIVE);
    }
    else if (scan_req == data->pending_scan_req)
    {
        const struct umac_scan_req *pending_req = data->pending_scan_req;

        data->pending_scan_req = NULL;

        pending_req->complete_cb(umacd, MMWLAN_SCAN_TERMINATED);
    }
    else
    {
        MMLOG_WRN("Invalid scan req\n");
        MMOSAL_ASSERT(false);
    }
}

void umac_scan_handle_hw_restarted(struct umac_data *umacd)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    if (data->active_scan_req != NULL)
    {

        umac_scan_hw_scan_done(umacd, MMWLAN_SCAN_TERMINATED);
    }
}

bool umac_scan_has_scan_req(struct umac_data *umacd)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    return data->active_scan_req || data->pending_scan_req;
}


static void umac_req_scan_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    const struct umac_scan_req *scan_req = evt->args.req_scan.scan_req;
    MMOSAL_ASSERT(scan_req != NULL);
    MMOSAL_ASSERT(scan_req->rx_cb != NULL);
    MMOSAL_ASSERT(scan_req->complete_cb != NULL);
    MMOSAL_ASSERT(scan_req->args.dwell_time_ms != 0);

    if (data->active_scan_req == NULL)
    {
        MMLOG_INF("Scan request: starting scan (dwell=%lu ms)\n", scan_req->args.dwell_time_ms);
        data->active_scan_req = scan_req;
    }
    else if (data->pending_scan_req == NULL)
    {
        data->pending_scan_req = scan_req;
        MMLOG_INF("Scan request: already queued\n");

        return;
    }
    else
    {

        MMLOG_WRN("Scan request: cannot accept\n");
        scan_req->complete_cb(umacd, MMWLAN_SCAN_TERMINATED);
        return;
    }

    umac_scan_handle_event(data, UMAC_SCAN_EVENT_REQUEST);
}

enum mmwlan_status umac_scan_queue_request(struct umac_data *umacd, struct umac_scan_req *scan_req)
{
    struct umac_evt evt = UMAC_EVT_INIT(umac_req_scan_evt_handler);

    evt.args.req_scan.scan_req = scan_req;
    if (!umac_core_evt_queue(umacd, &evt))
    {
        MMLOG_WRN("Failed to queue event\n");
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}

void umac_scan_hw_scan_done(struct umac_data *umacd, enum mmwlan_scan_state state)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);


    switch (state)
    {
        case MMWLAN_SCAN_TERMINATED:
            hw_scan_fsm_handle_event(&data->hw_scan_data.fsm_inst, HW_SCAN_FSM_EVENT_ABORT);
            break;

        case MMWLAN_SCAN_SUCCESSFUL:
            hw_scan_fsm_handle_event(&data->hw_scan_data.fsm_inst, HW_SCAN_FSM_EVENT_DONE);
            break;

        default:
            MMOSAL_DEV_ASSERT(false);
            break;
    }
}

enum mmwlan_status umac_scan_store_scan_config(struct umac_data *umacd,
                                               struct mmwlan_scan_args *scan_args)
{
    return hw_scan_store_scan_config(umacd, scan_args);
}

void umac_scan_fill_result(struct mmwlan_scan_result *res, const struct umac_scan_response *rsp)
{
    *res = (struct mmwlan_scan_result){
        .rssi = rsp->rssi,
        .bssid = rsp->frame.bssid,
        .ssid = rsp->frame.ssid,
        .ies = rsp->frame.ies,
        .beacon_interval = rsp->frame.beacon_interval,
        .capability_info = rsp->frame.capability_info,
        .ies_len = rsp->frame.ies_len,
        .ssid_len = rsp->frame.ssid_len,
        .channel_freq_hz = rsp->channel_freq_hz,
        .bw_mhz = rsp->bw_mhz,
        .op_bw_mhz = rsp->op_bw_mhz,
        .noise_dbm = rsp->noise_dbm,
    };
    PACK_LE64(res->tsf, rsp->frame.timestamp);
}

void umac_scan_process_probe_resp(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    if (data->active_scan_req == NULL)
    {
        MMLOG_VRB("Ignoring probe response: no scan active\n");
        return;
    }

    struct umac_scan_response rsp = { 0 };
    bool ok = frame_probe_response_parse(rxbufview, &rsp.frame);
    if (!ok)
    {
        MMLOG_VRB("Ignoring malformed probe response\n");
        return;
    }

    const struct dot11_ie_s1g_operation *s1g_op =
        ie_s1g_operation_find(rsp.frame.ies, rsp.frame.ies_len);
    const struct mmwlan_s1g_channel *matched_channel =
        umac_regdb_get_channel(umacd, s1g_op->channel_center_freq);

    MMLOG_VRB("Probe response recieved (op_class: %d, centre_freq: %d, prim_chan_num: %d)\n",
              s1g_op->operating_class,
              s1g_op->channel_center_freq,
              s1g_op->primary_channel_number);
    if (matched_channel == NULL ||
        !umac_regdb_op_class_match(umacd, s1g_op->operating_class, matched_channel))
    {
        MMLOG_WRN("Ignoring probe response: channel not in reg db "
                  "(op_class: %d, centre_freq: %d, prim_chan_num: %d)\n",
                  s1g_op->operating_class,
                  s1g_op->channel_center_freq,
                  s1g_op->primary_channel_number);
        return;
    }

    struct mmpkt *rxbuf = mmpkt_from_view(rxbufview);
    struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);
    rsp.rssi = rx_metadata->rssi;
    rsp.bw_mhz = rx_metadata->bw_mhz;
    rsp.op_bw_mhz = ie_s1g_operation_get_operating_bw(s1g_op);
    rsp.channel_freq_hz = rx_metadata->freq_100khz * 100 * 1000;
    rsp.noise_dbm = rx_metadata->noise_dbm;

    data->active_scan_req->rx_cb(umacd, &rsp);
}
