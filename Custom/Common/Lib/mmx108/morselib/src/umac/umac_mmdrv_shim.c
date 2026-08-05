/*
 *  Copyright 2022 Morse Micro
 *  SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "common/common.h"
#include "mmlog.h"
#include "umac/ap/umac_ap.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/connection/umac_connection.h"
#include "umac/offload/umac_offload.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/stats/umac_stats.h"



void mmdrv_host_process_rx_frame(struct mmpkt *rxbuf, uint16_t channel)
{
    MM_UNUSED(channel);

    MMOSAL_DEV_ASSERT(channel == 0);
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_core_is_running(umacd))
    {
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

    if (umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP) != UMAC_INTERFACE_VIF_ID_INVALID)
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
        MMOSAL_ASSERT(mmdrv_init(NULL, country_code) == 0);

        umac_interface_configure_periodic_health_check(umacd);
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

    struct umac_evt evt = UMAC_EVT_INIT(hw_restart_evt_handler);
    bool ok = umac_core_evt_queue_at_start(umacd, &evt);
    if (!ok)
    {

        MMLOG_ERR("Failed to queue HW_RESTARTED event.\n");
        MMOSAL_ASSERT(false);
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

static void dhcp_lease_update_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    umac_offload_dhcp_lease_update(umacd, &evt->args.dhcp_offload_lease_update.lease_info);
}

void mmdrv_host_dhcp_lease_update(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt = UMAC_EVT_INIT(dhcp_lease_update_evt_handler);
    evt.args.dhcp_offload_lease_update.lease_info.ip4_addr = ip;
    evt.args.dhcp_offload_lease_update.lease_info.mask4_addr = mask;
    evt.args.dhcp_offload_lease_update.lease_info.gw4_addr = gw;
    evt.args.dhcp_offload_lease_update.lease_info.dns4_addr = dns;

    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_WRN("Failed to queue DHCP_OFFLOAD_LEASE_UPDATE event.\n");
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

struct mmpkt *mmdrv_host_get_beacon(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_ap_get_beacon(umacd);
}
