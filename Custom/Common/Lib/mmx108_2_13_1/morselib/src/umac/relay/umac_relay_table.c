/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_relay_private.h"

#include "common/common.h"
#include "mmlog.h"
#include "mmpkt.h"

#include "dot11/dot11_utils.h"
#include "umac/ap/umac_ap.h"
#include "umac/connection/umac_connection.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/interface/umac_interface.h"

#if defined(ENABLE_MMWLAN_RELAY) && ENABLE_MMWLAN_RELAY

MM_STATIC_ASSERT((UMAC_RELAY_NUM_BUCKETS != 0) &&
                     ((UMAC_RELAY_NUM_BUCKETS & (UMAC_RELAY_NUM_BUCKETS - 1)) == 0) &&
                     (UMAC_RELAY_NUM_BUCKETS <= 256),
                 "UMAC_RELAY_NUM_BUCKETS must be a power of 2 and <= 256");

void umac_relay_table_init(struct umac_relay_data *data)
{
    for (size_t ii = 0; ii < data->args.forwarding_table_size; ii++)
    {
        data->table_pool[ii].next = data->table_free_list;
        data->table_free_list = &data->table_pool[ii];
    }
}

static struct umac_relay_table_entry *umac_relay_table_alloc(struct umac_relay_data *data)
{
    struct umac_relay_table_entry *ret = data->table_free_list;

    if (ret == NULL)
    {
        return NULL;
    }

    data->table_free_list = ret->next;
    ret->next = NULL;
    return ret;
}

static void umac_relay_table_free(struct umac_relay_data *data, struct umac_relay_table_entry *p)
{
    if (p == NULL)
    {
        return;
    }

    memset(p, 0, sizeof(*p));
    p->next = data->table_free_list;
    data->table_free_list = p;
}

struct umac_relay_table_entry **umac_relay_get_table_head(struct umac_relay_data *data,
                                                          const uint8_t *da)
{
    MMOSAL_DEV_ASSERT(da != NULL);
    size_t index = da[5] & (UMAC_RELAY_NUM_BUCKETS - 1);
    return &data->table_heads[index];
}

static void umac_relay_table_list_add_sorted_by_da(struct umac_relay_table_entry **head,
                                                   struct umac_relay_table_entry *entry)
{
    struct umac_relay_table_entry **cursor = head;

    MMOSAL_DEV_ASSERT(head != NULL);
    MMOSAL_DEV_ASSERT(entry != NULL);

    while (*cursor != NULL)
    {
        int cmp = memcmp((*cursor)->da, entry->da, sizeof(entry->da));
        if (cmp >= 0)
        {
            break;
        }
        cursor = &(*cursor)->next;
    }

    entry->next = *cursor;
    *cursor = entry;
}

static struct umac_relay_table_entry *umac_relay_table_list_find_by_da(
    struct umac_relay_table_entry **head,
    const uint8_t *da,
    bool remove)
{
    struct umac_relay_table_entry **cursor = head;
    struct umac_relay_table_entry *entry = NULL;

    MMOSAL_DEV_ASSERT(head != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);

    while ((entry = *cursor) != NULL)
    {
        int cmp = memcmp(entry->da, da, sizeof(entry->da));
        if (cmp >= 0)
        {
            break;
        }
        cursor = &entry->next;
    }

    if (entry == NULL || !mm_mac_addr_is_equal(entry->da, da))
    {
        return NULL;
    }

    if (remove)
    {
        *cursor = entry->next;
        entry->next = NULL;
    }

    return entry;
}

bool umac_relay_table_add(struct umac_relay_data *data,
                          const uint8_t *da,
                          const uint8_t *next_hop,
                          bool relay_capable)
{
    struct umac_relay_table_entry **head = umac_relay_get_table_head(data, da);
    struct umac_relay_table_entry *entry = NULL;

    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);
    MMOSAL_DEV_ASSERT(next_hop != NULL);

    if (mm_mac_addr_is_multicast(da))
    {
        MMLOG_WRN("Trying to add group addressed DA to Relay table!\n");
        MMOSAL_DEV_ASSERT(false);
        return false;
    }


    while ((entry = umac_relay_table_list_find_by_da(&data->remove_pending_head, da, true)) != NULL)
    {
        umac_relay_table_free(data, entry);
    }

    entry = umac_relay_table_list_find_by_da(head, da, false);
    if (entry != NULL)
    {
        if (mm_mac_addr_is_equal(entry->next_hop, next_hop) &&
            entry->relay_capable == relay_capable)
        {
            return true;
        }

        entry = umac_relay_table_list_find_by_da(head, da, true);
        if (entry == NULL)
        {
            return false;
        }

        MMLOG_VRB("Forwarding entry for DA=" MM_MAC_ADDR_FMT " updated to " MM_MAC_ADDR_FMT "\n",
                  MM_MAC_ADDR_VAL(da),
                  MM_MAC_ADDR_VAL(next_hop));
        memcpy(entry->next_hop, next_hop, sizeof(entry->next_hop));
        entry->relay_capable = relay_capable;
        umac_relay_table_list_add_sorted_by_da(&data->add_pending_head, entry);
        return true;
    }

    entry = umac_relay_table_list_find_by_da(&data->add_pending_head, da, false);
    if (entry != NULL)
    {
        MMLOG_VRB("Pending forwarding entry for DA=" MM_MAC_ADDR_FMT " updated to " MM_MAC_ADDR_FMT
                  "\n",
                  MM_MAC_ADDR_VAL(da),
                  MM_MAC_ADDR_VAL(next_hop));
        memcpy(entry->next_hop, next_hop, sizeof(entry->next_hop));
        entry->relay_capable = relay_capable;
        return true;
    }

    entry = umac_relay_table_alloc(data);
    if (entry == NULL)
    {
        return false;
    }

    MMLOG_VRB("Forwarding entry added for DA=" MM_MAC_ADDR_FMT " next hop=" MM_MAC_ADDR_FMT "\n",
              MM_MAC_ADDR_VAL(da),
              MM_MAC_ADDR_VAL(next_hop));
    memcpy(entry->da, da, sizeof(entry->da));
    memcpy(entry->next_hop, next_hop, sizeof(entry->next_hop));
    entry->relay_capable = relay_capable;
    umac_relay_table_list_add_sorted_by_da(&data->add_pending_head, entry);
    return true;
}

bool umac_relay_table_add_done(struct umac_relay_data *data, const uint8_t *da)
{
    struct umac_relay_table_entry **head = umac_relay_get_table_head(data, da);
    struct umac_relay_table_entry *entry = NULL;
    struct umac_relay_table_entry *table_entry = NULL;

    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);

    entry = umac_relay_table_list_find_by_da(&data->add_pending_head, da, true);
    if (entry == NULL)
    {
        return false;
    }

    table_entry = umac_relay_table_list_find_by_da(head, da, false);
    if (table_entry != NULL)
    {
        memcpy(table_entry->next_hop, entry->next_hop, sizeof(table_entry->next_hop));
        table_entry->relay_capable = entry->relay_capable;
        umac_relay_table_free(data, entry);
        return true;
    }

    umac_relay_table_list_add_sorted_by_da(head, entry);
    return true;
}

bool umac_relay_table_remove(struct umac_relay_data *data,
                             const uint8_t *da,
                             const uint8_t *next_hop)
{
    struct umac_relay_table_entry **head = umac_relay_get_table_head(data, da);
    struct umac_relay_table_entry *entry = NULL;
    struct umac_relay_table_entry **cursor = NULL;

    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);
    MMOSAL_DEV_ASSERT(next_hop != NULL);

    cursor = head;
    while ((entry = *cursor) != NULL)
    {
        int cmp = memcmp(entry->da, da, sizeof(entry->da));
        if (cmp >= 0)
        {
            break;
        }
        cursor = &entry->next;
    }

    if (entry == NULL)
    {
        cursor = &data->add_pending_head;
        while ((entry = *cursor) != NULL)
        {
            int cmp = memcmp(entry->da, da, sizeof(entry->da));
            if (cmp >= 0)
            {
                break;
            }
            cursor = &entry->next;
        }
    }

    if (entry == NULL ||
        !mm_mac_addr_is_equal(entry->da, da) ||
        !mm_mac_addr_is_equal(entry->next_hop, next_hop))
    {
        return false;
    }

    *cursor = entry->next;
    entry->next = NULL;
    umac_relay_table_list_add_sorted_by_da(&data->remove_pending_head, entry);
    return true;
}

bool umac_relay_table_remove_done(struct umac_relay_data *data, const uint8_t *da)
{
    struct umac_relay_table_entry *entry = NULL;

    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);

    entry = umac_relay_table_list_find_by_da(&data->remove_pending_head, da, true);
    if (entry == NULL)
    {
        return false;
    }

    umac_relay_table_free(data, entry);
    return true;
}

struct umac_relay_table_entry *umac_relay_table_lookup_by_da(struct umac_relay_data *data,
                                                             const uint8_t *da)
{
    struct umac_relay_table_entry **head = umac_relay_get_table_head(data, da);
    struct umac_relay_table_entry *entry = NULL;

    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(da != NULL);

    entry = umac_relay_table_list_find_by_da(head, da, false);
    if (entry != NULL)
    {
        return entry;
    }

    return umac_relay_table_list_find_by_da(&data->add_pending_head, da, false);
}

void umac_relay_table_mark_entries_as_newly_added(struct umac_relay_data *data)
{
    MMOSAL_DEV_ASSERT(data != NULL);

    for (size_t ii = 0; ii < UMAC_RELAY_NUM_BUCKETS; ii++)
    {
        struct umac_relay_table_entry *entry = data->table_heads[ii];
        data->table_heads[ii] = NULL;

        while (entry != NULL)
        {
            struct umac_relay_table_entry *next = entry->next;
            entry->next = NULL;
            umac_relay_table_list_add_sorted_by_da(&data->add_pending_head, entry);
            entry = next;
        }
    }
}

#endif
