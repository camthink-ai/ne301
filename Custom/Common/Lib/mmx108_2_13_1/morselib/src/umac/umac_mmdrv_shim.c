/*
 *  Copyright 2022 Morse Micro
 *  SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "common/common.h"
#include "mmdrv.h"
#include "mmlog.h"
#include "umac/ap/umac_ap.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/connection/umac_connection.h"
#include "umac/health_check/umac_health_check.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/stats/umac_stats.h"
#include "umac/supplicant_shim/umac_supp_shim.h"



void mmdrv_host_process_rx_frame(struct mmpkt *rxbuf, uint16_t channel)
{
    MM_UNUSED(channel);

    MMOSAL_DEV_ASSERT(channel == 0);
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_core_is_running(umacd))
    {
        MMLOG_WRN("Event loop not running. Dropping frame.\n");
        mmpkt_release(rxbuf);
        return;
    }

    umac_datapath_rx_frame(umacd, rxbuf);
}

void mmdrv_host_process_tx_status(struct mmpkt *mmpkt)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_core_is_running(umacd))
    {
        mmpkt_release(mmpkt);
        return;
    }

    umac_datapath_handle_tx_status(umacd, mmpkt);
}

void mmdrv_host_set_tx_paused(uint16_t sources_mask, bool paused)
{
    struct umac_data *umacd = umac_data_get_umacd();
    if (paused)
    {
        umac_datapath_pause(umacd, sources_mask);
    }
    else
    {
        umac_datapath_unpause(umacd, sources_mask);
    }
}

void mmdrv_host_update_tx_paused(uint16_t sources_mask, mmdrv_host_update_tx_paused_cb_t cb)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_datapath_update_tx_paused(umacd, sources_mask, cb);
}

static void hw_restart_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(evt);

    if (umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP) != MMDRV_VIF_ID_INVALID)
    {
        MMLOG_ERR("Unable to recover from hardware restart with AP interface active\n");
        MMOSAL_ASSERT(false);
    }

    if (umac_interface_is_active(umacd))
    {
        const char *country_code = umac_regdb_get_country_code(umacd);
        if (country_code == NULL)
        {
            MMLOG_ERR("Channel list not set\n");
            return;
        }

        mmdrv_deinit();
        MMOSAL_ASSERT(mmdrv_init(NULL, country_code) == MMWLAN_SUCCESS);

        umac_health_check_start(umacd);
        umac_stats_increment_hw_restart_counter(umacd);
        umac_scan_handle_hw_restarted(umacd);
        umac_connection_handle_hw_restarted(umacd);
    }

    MMLOG_DBG("Notify MMDRV that restart has completed\n");
    mmdrv_hw_restart_completed();
}

void mmdrv_host_hw_restart_required(void)
{
    struct umac_data *umacd = umac_data_get_umacd();

    mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_HW_RESTART, true);

    struct umac_evt evt = UMAC_EVT_INIT(hw_restart_evt_handler);
    bool ok = umac_core_evt_queue_at_start(umacd, &evt);
    if (!ok)
    {

        MMLOG_ERR("Failed to queue HW_RESTARTED event.\n");
        MMOSAL_ASSERT(false);
    }
}

static void health_check_required_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(evt);
    umac_health_check_demand_check(umacd);
}

void mmdrv_host_health_check_required(void)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt = UMAC_EVT_INIT(health_check_required_evt_handler);
    bool ok = umac_core_evt_queue_at_start(umacd, &evt);
    if (!ok)
    {
        MMLOG_WRN("Failed to queue HEALTH_CHECK_REQUIRED event.\n");
    }
}

static void beacon_loss_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(evt);

    umac_connection_handle_beacon_loss(umacd);
}

void mmdrv_host_beacon_loss(uint32_t num_bcns)
{
    MM_UNUSED(num_bcns);
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt = UMAC_EVT_INIT(beacon_loss_evt_handler);
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_WRN("Failed to queue BEACON_LOSS event.\n");
    }
}

static void connection_loss_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(evt);

    MMLOG_WRN("UMAC_EVT_CONNECTION_LOSS event received with reason code %lu\n",
              evt->args.connection_loss.reason);
    umac_connection_process_disassoc_req(umacd, NULL);
}

void mmdrv_host_connection_loss(uint32_t reason)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt = UMAC_EVT_INIT(connection_loss_evt_handler);
    evt.args.connection_loss.reason = reason;
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_WRN("Failed to queue CONNECTION_LOSS event.\n");
    }
}

void mmdrv_host_cqm_event(enum mmdrv_cqm_event event, int16_t rssi)
{
    struct umac_data *umacd = umac_data_get_umacd();

    switch (event)
    {
        case MMDRV_CQM_EVENT_RSSI_THRESHOLD_HIGH:
        case MMDRV_CQM_EVENT_RSSI_THRESHOLD_LOW:

            umac_supp_notify_signal_change(umacd,
                                           rssi,
                                           (event == MMDRV_CQM_EVENT_RSSI_THRESHOLD_HIGH));
            break;

        default:
            MMLOG_WRN("Unknown CQM event %u\n", event);
            break;
    }
}

static void hw_scan_complete_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    umac_scan_hw_scan_done(umacd, evt->args.hw_scan_done.state);
}

void mmdrv_host_hw_scan_complete(enum mmwlan_scan_state state)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt =
        UMAC_EVT_INIT_ARGS(hw_scan_complete_evt_handler, hw_scan_done, .state = state);
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_ERR("Failed to queue HW_SCAN_DONE event.\n");
    }
}

void mmdrv_host_stats_increment_datapath_driver_rx_alloc_failures(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_stats_increment_datapath_driver_rx_alloc_failures(umacd);
}


void mmdrv_host_stats_increment_datapath_driver_rx_read_failures(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_stats_increment_datapath_driver_rx_read_failures(umacd);
}

void mmdrv_host_stats_increment_datapath_driver_tx_skbq_timeout(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_stats_increment_datapath_driver_tx_skbq_timeout(umacd);
}

void mmdrv_host_stats_increment_datapath_driver_tx_pending_status_timeout(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_stats_increment_datapath_driver_tx_pending_status_timeout(umacd);
}

struct mmpkt *mmdrv_host_get_beacon(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_ap_get_beacon(umacd);
}
