/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_relay_private.h"

#include "mmlog.h"
#include "mmpkt.h"
#include "dot11/dot11.h"
#include "umac/connection/umac_connection.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/interface/umac_interface.h"
#include "umac/frames/s1g_action.h"
#include "umac/ies/reachable_address.h"

#if defined(ENABLE_MMWLAN_RELAY) && ENABLE_MMWLAN_RELAY

enum mmwlan_status umac_relay_get_update_packet(struct umac_data *umacd,
                                                struct umac_relay_data *data,
                                                const uint8_t *bssid,
                                                const uint8_t *own_addr,
                                                struct mmpkt **pkt)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    unsigned update_count = 0;


    const size_t max_payload_len = 1000;
    uint8_t *payload = (uint8_t *)mmosal_calloc(1, max_payload_len);
    if (payload == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    struct consbuf buf = CONSBUF_INIT_WITH_BUF(payload, max_payload_len);

    MMLOG_DBG("Building Reachable Address Update:\n");

    for (struct umac_relay_table_entry *entry = data->remove_pending_head; entry != NULL;
         entry = entry->next)
    {
        bool ok = ie_reachable_address_build_single(&buf,
                                                    own_addr,
                                                    REACHABLE_ADDRESS_REMOVE,
                                                    entry->relay_capable,
                                                    entry->da);
        if (!ok)
        {

            MMOSAL_DEV_ASSERT(update_count != 0);
            break;
        }
        MMLOG_DBG("    - " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(entry->da));
        update_count++;
    }

    for (struct umac_relay_table_entry *entry = data->add_pending_head; entry != NULL;
         entry = entry->next)
    {
        bool ok = ie_reachable_address_build_single(&buf,
                                                    own_addr,
                                                    REACHABLE_ADDRESS_ADD,
                                                    entry->relay_capable,
                                                    entry->da);
        if (!ok)
        {

            MMOSAL_DEV_ASSERT(update_count != 0);
            break;
        }
        MMLOG_INF("    + " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(entry->da));
        update_count++;
    }

    if (update_count == 0)
    {
        MMLOG_DBG("No updates\n");
        status = MMWLAN_NOT_FOUND;
        goto done;
    }

    struct frame_data_s1g_action s1g_action_args = {
        .bssid = bssid,
        .dst_address = bssid,
        .src_address = own_addr,
        .s1g_action = DOT11_S1G_ACTION_REACHABLE_ADDRESS_UPDATE,
        .payload = payload,
        .payload_len = buf.offset,
    };
    *pkt = build_mgmt_frame(umacd, frame_s1g_action_build, &s1g_action_args);
    if (*pkt == NULL)
    {
        status = MMWLAN_NO_MEM;
        goto done;
    }

    MMLOG_DBG("Reachable Address Update frame constructed with %u updates\n", update_count);
    status = MMWLAN_SUCCESS;

done:
    mmosal_free(payload);
    return status;
}

void umac_relay_handle_update_acked(struct umac_relay_data *data,
                                    const struct frame_data_s1g_action *s1g_action)
{
    const uint8_t *ies_iter = s1g_action->payload;
    const uint8_t *ies_end = s1g_action->payload + s1g_action->payload_len;

    MMLOG_DBG("Reachable Address Update acknowledged:\n");

    while (ies_iter < ies_end)
    {
        const struct dot11_ie_reachable_address *raw_ie =
            ie_reachable_address_find(ies_iter, ies_end - ies_iter, NULL);
        if (raw_ie == NULL)
        {
            break;
        }
        const uint8_t *initiator_mac = NULL;
        uint8_t address_count;
        bool ok = ie_reachable_address_parse(raw_ie, &initiator_mac, &address_count);
        if (!ok)
        {
            MMLOG_WRN("Error parsing generated Reachable Address Update\n");
            MMOSAL_DEV_ASSERT(false);
            break;
        }

        for (size_t ii = 0; ii < address_count; ii++)
        {
            enum reachable_address_update_type update_type;
            bool relay_capable;
            const uint8_t *mac;
            ok = ie_reachable_address_get_update(raw_ie, ii, &update_type, &relay_capable, &mac);
            if (!ok)
            {
                MMLOG_WRN("Error parsing generated Reachable Address Update\n");
                MMOSAL_DEV_ASSERT(false);
                break;
            }

            const char *operation;
            if (update_type == REACHABLE_ADDRESS_ADD)
            {
                operation = "+";
                (void)umac_relay_table_add_done(data, mac);
            }
            else
            {
                (void)umac_relay_table_remove_done(data, mac);
                operation = "-";
            }

            MMLOG_DBG("    %s " MM_MAC_ADDR_FMT "\n", operation, MM_MAC_ADDR_VAL(mac));
        }

        ies_iter += sizeof(raw_ie->header) + raw_ie->header.length;
    }
}

#endif
