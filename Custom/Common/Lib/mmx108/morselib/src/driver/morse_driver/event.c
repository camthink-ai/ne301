/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#include "mmpkt.h"
#include "mmdrv.h"
#include "errno.h"

#include "driver/driver.h"
#include "morse.h"
#include "common/morse_commands.h"
#include "skbq.h"

static void handle_beacon_loss_evt(struct morse_cmd_evt_beacon_loss *bcn_loss_evt)
{
    uint32_t num_bcns = le32toh(bcn_loss_evt->num_bcns);
    MMLOG_WRN("Lost %lu beacons\n", num_bcns);
    mmdrv_host_beacon_loss(num_bcns);
}

static void handle_umac_traffic_control(
    struct driver_data *driverd,
    struct morse_cmd_evt_umac_traffic_control *umac_traffic_control)
{
    bool pause_data_traffic = umac_traffic_control->pause_data_traffic ? true : false;

    if (pause_data_traffic)
    {
        mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_TRAFFIC_CTRL, true);
        driver_task_notify_event(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND);
        MMLOG_INF("UMAC Traffic control paused by 0x%02lx\n", umac_traffic_control->sources);
    }
    else
    {
        driver_task_notify_event(driverd, DRV_EVT_TRAFFIC_RESUME_PEND);
        mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_TRAFFIC_CTRL, false);
        MMLOG_INF("UMAC Traffic control unpaused\n");
    }
}

static void handle_umac_dhcp_lease_update(struct morse_cmd_evt_dhcp_lease_update *dhcp_lease)
{
    uint32_t ip = dhcp_lease->my_ip;
    uint32_t mask = dhcp_lease->netmask;
    uint32_t gw = dhcp_lease->router;
    uint32_t dns = dhcp_lease->dns;
    mmdrv_host_dhcp_lease_update(ip, mask, gw, dns);
}

static void handle_umac_connection_loss(struct morse_cmd_evt_connection_loss *connection_loss)
{
    mmdrv_host_connection_loss(connection_loss->reason);
}

int morse_mac_event_recv(struct driver_data *driverd, struct mmpktview *view)
{
    struct morse_cmd_header *hdr;
    uint16_t event_id;
    uint16_t event_iid;
    uint16_t event_len;

    MM_UNUSED(driverd);

    if (mmpkt_get_data_length(view) < sizeof(*hdr))
    {
        MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
        return -EINVAL;
    }

    hdr = (struct morse_cmd_header *)mmpkt_get_data_start(view);
    event_id = le16toh(hdr->message_id);
    event_iid = le16toh(hdr->vif_id);
    event_len = le16toh(hdr->len);

    if (!(hdr->flags & MORSE_CMD_TYPE_EVT))
    {
        MMLOG_WRN("Ignoring non-event\n");
        return -EINVAL;
    }


    if (event_iid != 0)
    {
        MMLOG_WRN("Ignoring event with non-zero IID (%04x)\n", event_iid);
        return -EINVAL;
    }

    switch (event_id)
    {
        case MORSE_CMD_ID_EVT_BEACON_LOSS:
        {
            struct morse_cmd_evt_beacon_loss *bcn_loss_evt =
                (struct morse_cmd_evt_beacon_loss *)mmpkt_remove_from_start(view,
                                                                            sizeof(*bcn_loss_evt));
            if (bcn_loss_evt == NULL)
            {
                MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
                break;
            }
            handle_beacon_loss_evt(bcn_loss_evt);
            break;
        }

        case MORSE_CMD_ID_EVT_UMAC_TRAFFIC_CONTROL:
        {
            struct morse_cmd_evt_umac_traffic_control *umac_traffic_control =
                (struct morse_cmd_evt_umac_traffic_control *)mmpkt_remove_from_start(
                    view,
                    sizeof(*umac_traffic_control));
            if (umac_traffic_control == NULL)
            {
                MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
                break;
            }
            handle_umac_traffic_control(driverd, umac_traffic_control);
            break;
        }

        case MORSE_CMD_ID_EVT_DHCP_LEASE_UPDATE:
        {
            struct morse_cmd_evt_dhcp_lease_update *dhcp_lease =
                (struct morse_cmd_evt_dhcp_lease_update *)mmpkt_remove_from_start(
                    view,
                    sizeof(*dhcp_lease));
            if (dhcp_lease == NULL)
            {
                MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
                break;
            }
            handle_umac_dhcp_lease_update(dhcp_lease);

            break;
        }

        case MORSE_CMD_ID_EVT_CONNECTION_LOSS:
        {
            struct morse_cmd_evt_connection_loss *connection_loss =
                (struct morse_cmd_evt_connection_loss *)mmpkt_remove_from_start(
                    view,
                    sizeof(*connection_loss));
            if (connection_loss == NULL)
            {
                MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
                break;
            }
            handle_umac_connection_loss(connection_loss);
            break;
        }

        case MORSE_CMD_ID_EVT_HW_SCAN_DONE:
        {
            struct morse_cmd_evt_hw_scan_done *hw_scan_done =
                (struct morse_cmd_evt_hw_scan_done *)mmpkt_remove_from_start(view,
                                                                             sizeof(*hw_scan_done));
            if (hw_scan_done == NULL)
            {
                MMLOG_WRN("Received event too short (%lu)\n", mmpkt_get_data_length(view));
                break;
            }
            enum mmwlan_scan_state state = hw_scan_done->aborted ? MMWLAN_SCAN_TERMINATED :
                                                                   MMWLAN_SCAN_SUCCESSFUL;

            MMLOG_DBG("HW scan done evt, %s\n", hw_scan_done->aborted ? "aborted" : "success");
            mmdrv_host_hw_scan_complete(state);
            break;
        }

        default:
            MMLOG_INF("Mac EVT 0x%04x LEN %u\n", event_id, event_len);
            break;
    }

    return 0;
}
