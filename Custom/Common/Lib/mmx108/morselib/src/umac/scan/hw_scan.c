/*
 * Copyright 2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "common/morse_commands.h"
#include "common/morse_command_utils.h"
#include "mmlog.h"
#include "common/consbuf.h"
#include "umac_scan_data.h"
#include "umac/stats/umac_stats.h"
#include "umac/connection/umac_connection.h"
#include "umac/interface/umac_interface.h"
#include "umac/frames/probe_request.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/datapath/umac_datapath.h"
#include "hw_scan.h"


#define HW_SCAN_FSM_LOG(format_str, ...) MMLOG_DBG(format_str, __VA_ARGS__)

#include "hw_scan_fsm.h"
#include "hw_scan_fsm.def"


struct MM_PACKED hw_scan_tlv_hdr
{

    uint16_t tag;

    uint16_t len;
};


enum hw_scan_tlv_tag
{
    HW_SCAN_TLV_TAG_PAD = 0,
    HW_SCAN_TLV_TAG_PROBE_REQ = 1,
    HW_SCAN_TLV_TAG_CHAN_LIST = 2,
    HW_SCAN_TLV_TAG_POWER_LIST = 3,
    HW_SCAN_TLV_TAG_DWELL_ON_HOME = 4,
};


enum hw_scan_operating_bw
{
    HW_SCAN_CH_OP_BW_1MHZ = 0,
    HW_SCAN_CH_OP_BW_2MHZ = 1,
    HW_SCAN_CH_OP_BW_4MHZ = 2,
    HW_SCAN_CH_OP_BW_8MHZ = 3,
};


enum hw_scan_primary_bw
{
    HW_SCAN_CH_PRIM_CH_BW_1MHZ = 0,
    HW_SCAN_CH_PRIM_CH_BW_2MHZ = 1,
};

enum hw_scan_channel
{

    HW_SCAN_CH_FREQ_KHZ_SHIFT = 0,
    HW_SCAN_CH_FREQ_KHZ_MASK = 0xffffful << HW_SCAN_CH_FREQ_KHZ_SHIFT,

    HW_SCAN_CH_OP_BW_SHIFT = 20,
    HW_SCAN_CH_OP_BW_MASK = 0x3ul << HW_SCAN_CH_OP_BW_SHIFT,

    HW_SCAN_CH_PRIM_CH_WIDTH_SHIFT = 22,
    HW_SCAN_CH_PRIM_CH_WIDTH_MASK = 0x1ul << HW_SCAN_CH_PRIM_CH_WIDTH_SHIFT,

    HW_SCAN_CH_PRIM_CH_IDX_SHIFT = 23,
    HW_SCAN_CH_PRIM_CH_IDX_MASK = 0x7ul << HW_SCAN_CH_PRIM_CH_IDX_SHIFT,

    HW_SCAN_CH_PWR_LIST_IDX_SHIFT = 26,
    HW_SCAN_CH_PWR_LIST_IDX_MASK = 0x3Ful << HW_SCAN_CH_PWR_LIST_IDX_SHIFT,
};


struct MM_PACKED hw_scan_tlv_channel_list
{
    struct hw_scan_tlv_hdr hdr;

    uint32_t channels[];
};


struct MM_PACKED hw_scan_tlv_power_list
{
    struct hw_scan_tlv_hdr hdr;

    int32_t tx_power_qdbm[];
};


struct MM_PACKED hw_scan_tlv_probe_req
{
    struct hw_scan_tlv_hdr hdr;

    uint8_t buf[];
};


struct MM_PACKED hw_scan_tlv_dwell_on_home
{
    struct hw_scan_tlv_hdr hdr;

    uint32_t home_dwell_time_ms;
};

static uint32_t hw_scan_pack_channel(const struct mmwlan_s1g_channel *channel, uint8_t pwr_index)
{
    uint32_t packed_channel = 0;
    uint32_t freq_khz = channel->centre_freq_hz / 1000;

    uint8_t operating_bw = channel->bw_mhz == 8 ? HW_SCAN_CH_OP_BW_8MHZ :
                           channel->bw_mhz == 4 ? HW_SCAN_CH_OP_BW_4MHZ :
                           channel->bw_mhz == 2 ? HW_SCAN_CH_OP_BW_2MHZ :
                                                  HW_SCAN_CH_OP_BW_1MHZ;

    uint8_t primary_chan_width = channel->bw_mhz > 1 ? HW_SCAN_CH_PRIM_CH_BW_2MHZ :
                                                       HW_SCAN_CH_PRIM_CH_BW_1MHZ;
    uint8_t primary_chan_idx = 0;

    packed_channel =
        ((freq_khz << HW_SCAN_CH_FREQ_KHZ_SHIFT) & HW_SCAN_CH_FREQ_KHZ_MASK) |
        ((operating_bw << HW_SCAN_CH_OP_BW_SHIFT) & HW_SCAN_CH_OP_BW_MASK) |
        ((primary_chan_width << HW_SCAN_CH_PRIM_CH_WIDTH_SHIFT) & HW_SCAN_CH_PRIM_CH_WIDTH_MASK) |
        ((primary_chan_idx << HW_SCAN_CH_PRIM_CH_IDX_SHIFT) & HW_SCAN_CH_PRIM_CH_IDX_MASK) |
        ((pwr_index << HW_SCAN_CH_PWR_LIST_IDX_SHIFT) & HW_SCAN_CH_PWR_LIST_IDX_MASK);

    return packed_channel;
}

static void hw_scan_construct_channel_list_tlv(struct umac_data *umacd, struct consbuf *cbuf)
{
    MM_UNUSED(umacd);

    uint32_t offset_at_start = cbuf->offset;
    struct hw_scan_tlv_channel_list *channel_list_tlv =
        (struct hw_scan_tlv_channel_list *)consbuf_reserve(cbuf, sizeof(*channel_list_tlv));

    uint32_t current_channel_index = 0;
    const struct mmwlan_s1g_channel *s1g_channel;
    for (current_channel_index = 0;
         (s1g_channel = umac_regdb_get_channel_at_index(umacd, current_channel_index));
         current_channel_index++)
    {

        if (s1g_channel->bw_mhz <= 2)
        {
            uint32_t index;
            uint32_t sub_index;
            uint32_t unique_index = 0;

            for (index = 0; index < current_channel_index; index++)
            {
                const struct mmwlan_s1g_channel *channel_at_index =
                    umac_regdb_get_channel_at_index(umacd, index);


                if (channel_at_index->bw_mhz > 2)
                {
                    continue;
                }

                if (channel_at_index->max_tx_eirp_dbm == s1g_channel->max_tx_eirp_dbm)
                {

                    break;
                }


                for (sub_index = 0; sub_index < index; sub_index++)
                {
                    const struct mmwlan_s1g_channel *channel_at_sub_index =
                        umac_regdb_get_channel_at_index(umacd, sub_index);


                    if (channel_at_sub_index->bw_mhz > 2)
                    {
                        continue;
                    }

                    if (channel_at_index->max_tx_eirp_dbm == channel_at_sub_index->max_tx_eirp_dbm)
                    {

                        break;
                    }
                }

                if (sub_index == index)
                {

                    unique_index++;
                }
            }


            uint32_t *channel_slot = (uint32_t *)consbuf_reserve(cbuf, sizeof(uint32_t));
            if (channel_slot)
            {
                *channel_slot = htole32(hw_scan_pack_channel(s1g_channel, unique_index));
            }
        }
    }

    if (channel_list_tlv)
    {
        channel_list_tlv->hdr.tag = htole16(HW_SCAN_TLV_TAG_CHAN_LIST);
        channel_list_tlv->hdr.len =
            htole16((cbuf->offset - offset_at_start) - sizeof(channel_list_tlv->hdr));
    }
}

static void hw_scan_construct_power_list_tlv(struct umac_data *umacd, struct consbuf *cbuf)
{
    MM_UNUSED(umacd);

    uint32_t offset_at_start = cbuf->offset;
    struct hw_scan_tlv_power_list *power_list_tlv =
        (struct hw_scan_tlv_power_list *)consbuf_reserve(cbuf, sizeof(*power_list_tlv));


    uint32_t current_channel_index = 0;
    const struct mmwlan_s1g_channel *s1g_channel;
    for (current_channel_index = 0;
         (s1g_channel = umac_regdb_get_channel_at_index(umacd, current_channel_index));
         current_channel_index++)
    {

        if (s1g_channel->bw_mhz <= 2)
        {
            uint32_t index;

            for (index = 0; index < current_channel_index; index++)
            {
                const struct mmwlan_s1g_channel *channel_at_index =
                    umac_regdb_get_channel_at_index(umacd, index);


                if (channel_at_index->bw_mhz <= 2)
                {

                    if (channel_at_index->max_tx_eirp_dbm == s1g_channel->max_tx_eirp_dbm)
                    {
                        break;
                    }
                }
            }

            if (current_channel_index == index)
            {

                int32_t *power_slot = (int32_t *)consbuf_reserve(cbuf, sizeof(*power_slot));
                if (power_slot)
                {
                    *power_slot = htole32(DBM_TO_QDBM(s1g_channel->max_tx_eirp_dbm));
                }
            }
        }
    }

    if (power_list_tlv)
    {
        power_list_tlv->hdr.tag = htole16(HW_SCAN_TLV_TAG_POWER_LIST);
        power_list_tlv->hdr.len =
            htole16((cbuf->offset - offset_at_start) - sizeof(power_list_tlv->hdr));
    }
}

static void hw_scan_construct_dwell_on_home_tlv(struct umac_data *umacd,
                                                struct consbuf *cbuf,
                                                const struct mmwlan_scan_args *scan_args)
{
    MM_UNUSED(umacd);

    struct hw_scan_tlv_dwell_on_home *tlv =
        (struct hw_scan_tlv_dwell_on_home *)consbuf_reserve(cbuf, sizeof(*tlv));
    if (tlv)
    {
        tlv->hdr.tag = htole16(HW_SCAN_TLV_TAG_DWELL_ON_HOME);
        tlv->hdr.len = htole16(sizeof(*tlv) - sizeof(tlv->hdr));
        tlv->home_dwell_time_ms = scan_args->dwell_on_home_ms;
    }
}

static void hw_scan_construct_probe_req_tlv(struct umac_data *umacd,
                                            struct consbuf *cbuf,
                                            const struct mmwlan_scan_args *scan_args)
{
    uint32_t offset_at_start = cbuf->offset;
    uint8_t sta_addr[DOT11_MAC_ADDR_LEN];
    umac_interface_get_vif_mac_addr(umacd, MMWLAN_VIF_STA, sta_addr);
    if (mm_mac_addr_is_zero(sta_addr))
    {
        MMLOG_ERR("No STA MAC addr for scan\n");
        MMOSAL_ASSERT(false);
    }
    const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);
    MMOSAL_ASSERT(sta_args);

    struct hw_scan_tlv_probe_req *probe_req_tlv =
        (struct hw_scan_tlv_probe_req *)consbuf_reserve(cbuf, sizeof(*probe_req_tlv));

    struct frame_data_probe_request probe_request_params = { .bssid = sta_args->bssid,
                                                             .sta_address = sta_addr,
                                                             .ssid = scan_args->ssid,
                                                             .ssid_len = scan_args->ssid_len,
                                                             .extra_ies = scan_args->extra_ies,
                                                             .extra_ies_len =
                                                                 scan_args->extra_ies_len };
    frame_probe_request_build(umacd, cbuf, &probe_request_params);

    if (probe_req_tlv)
    {
        probe_req_tlv->hdr.tag = htole16(HW_SCAN_TLV_TAG_PROBE_REQ);
        probe_req_tlv->hdr.len =
            htole16((cbuf->offset - offset_at_start) - sizeof(probe_req_tlv->hdr));
    }
}

static struct morse_cmd_req_hw_scan *hw_scan_construct_base_request(
    struct umac_data *umacd,
    struct consbuf *cbuf,
    const struct mmwlan_scan_args *scan_args)
{
    struct morse_cmd_req_hw_scan *hw_scan_req =
        (struct morse_cmd_req_hw_scan *)consbuf_reserve(cbuf, sizeof(struct morse_cmd_req_hw_scan));

    hw_scan_construct_channel_list_tlv(umacd, cbuf);
    hw_scan_construct_power_list_tlv(umacd, cbuf);

    if (scan_args->dwell_on_home_ms != 0)
    {
        hw_scan_construct_dwell_on_home_tlv(umacd, cbuf, scan_args);
    }

    hw_scan_construct_probe_req_tlv(umacd, cbuf, scan_args);

    return hw_scan_req;
}

static enum mmwlan_status hw_scan_start(struct umac_data *umacd)
{
    enum mmwlan_status status = MMWLAN_SUCCESS;
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    struct consbuf cbuf = CONSBUF_INIT_WITHOUT_BUF;

    (void)hw_scan_construct_base_request(umacd, &cbuf, &data->active_scan_req->args);

    uint8_t *buf = (uint8_t *)mmosal_malloc(cbuf.offset);
    MMOSAL_ASSERT(buf);
    consbuf_reinit(&cbuf, buf, cbuf.offset);

    struct morse_cmd_req_hw_scan *hw_scan_req =
        hw_scan_construct_base_request(umacd, &cbuf, &data->active_scan_req->args);
    MMOSAL_ASSERT(hw_scan_req);


    morse_command_reinit_header(&hw_scan_req->hdr,
                                (cbuf.offset - sizeof(hw_scan_req->hdr)),
                                MORSE_CMD_ID_HW_SCAN,
                                data->hw_scan_data.vif_id);
    hw_scan_req->flags = MORSE_CMD_HW_SCAN_FLAGS_START;
    hw_scan_req->dwell_time_ms = data->active_scan_req->args.dwell_time_ms;

    if (mmdrv_execute_command(buf, NULL, 0))
    {
        MMLOG_ERR("Failed to execute %s HW_SCAN command\n", "START");
        status = MMWLAN_ERROR;
    }
    mmosal_free(buf);

    return status;
}

enum mmwlan_status hw_scan_store_scan_config(struct umac_data *umacd,
                                             struct mmwlan_scan_args *scan_args)
{
    enum mmwlan_status status = MMWLAN_SUCCESS;
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    struct consbuf cbuf = CONSBUF_INIT_WITHOUT_BUF;

    (void)hw_scan_construct_base_request(umacd, &cbuf, scan_args);

    uint8_t *buf = (uint8_t *)mmosal_malloc(cbuf.offset);
    MMOSAL_ASSERT(buf);
    consbuf_reinit(&cbuf, buf, cbuf.offset);

    struct morse_cmd_req_hw_scan *hw_scan_req =
        hw_scan_construct_base_request(umacd, &cbuf, scan_args);
    MMOSAL_ASSERT(hw_scan_req);


    morse_command_reinit_header(&hw_scan_req->hdr,
                                (cbuf.offset - sizeof(hw_scan_req->hdr)),
                                MORSE_CMD_ID_HW_SCAN,
                                data->hw_scan_data.vif_id);
    hw_scan_req->flags = MORSE_CMD_HW_SCAN_FLAGS_STORE;
    hw_scan_req->dwell_time_ms = scan_args->dwell_time_ms;

    if (mmdrv_execute_command(buf, NULL, 0))
    {
        MMLOG_ERR("Failed to execute %s HW_SCAN command\n", "STORE");
        status = MMWLAN_ERROR;
    }
    mmosal_free(buf);

    return status;
}

static enum mmwlan_status hw_scan_abort_scan(struct umac_data *umacd)
{
    enum mmwlan_status status = MMWLAN_SUCCESS;
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    struct morse_cmd_req_hw_scan req =
        MORSE_COMMAND_INIT(req,
                           MORSE_CMD_ID_HW_SCAN,
                           data->hw_scan_data.vif_id,
                           .flags = htole32(MORSE_CMD_HW_SCAN_FLAGS_ABORT));

    if (mmdrv_execute_command((uint8_t *)&req, NULL, 0))
    {
        MMLOG_ERR("Failed to execute %s HW_SCAN command\n", "ABORT");
        status = MMWLAN_ERROR;
    }

    return status;
}



static void hw_scan_fsm_in_progress_entry(struct hw_scan_fsm_instance *inst,
                                          enum hw_scan_fsm_state prev_state)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    MM_UNUSED(prev_state);
    MM_UNUSED(data);

    if (data->active_scan_req == NULL)
    {
        hw_scan_fsm_handle_event(inst, HW_SCAN_FSM_EVENT_COMPLETE_NO_PENDING);
        return;
    }

    if (data->active_scan_req->args.dwell_on_home_ms == 0)
    {
        umac_datapath_pause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_SCAN);
    }
    enum mmwlan_status status =
        umac_interface_add(umacd, UMAC_INTERFACE_SCAN, NULL, &data->hw_scan_data.vif_id);
    if (status != MMWLAN_SUCCESS)
    {
        hw_scan_fsm_handle_event(inst, HW_SCAN_FSM_EVENT_ABORT);
        return;
    }


    status = hw_scan_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        hw_scan_fsm_handle_event(inst, HW_SCAN_FSM_EVENT_ABORT);
        return;
    }
}

static void hw_scan_fsm_inactive_entry(struct hw_scan_fsm_instance *inst,
                                       enum hw_scan_fsm_state prev_state)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    if (prev_state != HW_SCAN_FSM_STATE_INACTIVE)
    {
        MMLOG_VRB("Removing interface\n");
        umac_interface_remove(umacd, UMAC_INTERFACE_SCAN);
        umac_datapath_unpause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_SCAN);
    }
}



static void hw_scan_fsm_req_abort(struct hw_scan_fsm_instance *inst, enum hw_scan_fsm_event event)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_scan_data *data = umac_data_get_scan(umacd);

    data->hw_scan_data.abort_all = (event == HW_SCAN_FSM_EVENT_ABORT_ALL);


    (void)hw_scan_abort_scan(umacd);
}

static void hw_scan_fsm_scan_aborted(struct hw_scan_fsm_instance *inst,
                                     enum hw_scan_fsm_event event)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    const struct umac_scan_req *active_req = data->active_scan_req;
    const struct umac_scan_req *pending_req = NULL;

    MM_UNUSED(event);

    data->active_scan_req = NULL;
    if (data->hw_scan_data.abort_all)
    {
        pending_req = data->pending_scan_req;
        data->pending_scan_req = NULL;
        data->hw_scan_data.abort_all = false;
    }

    if (active_req)
    {
        active_req->complete_cb(umacd, MMWLAN_SCAN_TERMINATED);
    }

    if (pending_req)
    {
        pending_req->complete_cb(umacd, MMWLAN_SCAN_TERMINATED);
    }
}

static void hw_scan_fsm_scan_done(struct hw_scan_fsm_instance *inst, enum hw_scan_fsm_event event)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_scan_data *data = umac_data_get_scan(umacd);
    const struct umac_scan_req *active_req = data->active_scan_req;
    const struct umac_scan_req *pending_req = NULL;

    MM_UNUSED(event);

    data->prev_scan_completion_time = mmosal_get_time_ms();


    if (data->hw_scan_data.abort_all)
    {
        pending_req = data->pending_scan_req;
        data->active_scan_req = NULL;
        data->pending_scan_req = NULL;
        data->hw_scan_data.abort_all = false;
    }
    else
    {
        data->active_scan_req = data->pending_scan_req;
        data->pending_scan_req = NULL;
    }

    if (active_req)
    {
        active_req->complete_cb(umacd, MMWLAN_SCAN_SUCCESSFUL);
        umac_stats_increment_num_scans_complete(umacd);
    }

    if (pending_req)
    {
        pending_req->complete_cb(umacd, MMWLAN_SCAN_TERMINATED);
    }
}



static void hw_scan_fsm_transition_error(struct hw_scan_fsm_instance *inst,
                                         enum hw_scan_fsm_event event)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    MM_UNUSED(umacd);

    MM_UNUSED(event);
    MM_UNUSED(inst);

    MMLOG_INF("hw_scan_fsm: invalid event %u (%s) in state %u (%s)\n",
              event,
              hw_scan_fsm_event_tostr(event),
              inst->current_state,
              hw_scan_fsm_state_tostr(inst->current_state));
    MMOSAL_DEV_ASSERT_LOG_DATA(false, inst->current_state, event);
}

static void hw_scan_fsm_reentrance_error(struct hw_scan_fsm_instance *inst)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    MM_UNUSED(umacd);

    MMLOG_INF("hw_scan_fsm: invalid reentrance in state %u (%s)\n",
              inst->current_state,
              hw_scan_fsm_state_tostr(inst->current_state));
    MMOSAL_ASSERT_LOG_DATA(false, inst->current_state);
}
