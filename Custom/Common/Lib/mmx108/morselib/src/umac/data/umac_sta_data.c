/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmwlan.h"
#include "umac_data_private.h"

struct umac_data *umac_sta_data_get_umacd(struct umac_sta_data *stad)
{
    UMAC_DATA_SANITY_CHECK(stad->umacd);
    return stad->umacd;
}

void umac_sta_data_set_aid(struct umac_sta_data *stad, uint16_t aid)
{
    MMOSAL_ASSERT(stad != NULL);
    MMOSAL_DEV_ASSERT(stad->aid == 0 || aid == 0);
    stad->aid = aid;
}

uint16_t umac_sta_data_get_aid(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return stad->aid;
}

void umac_sta_data_set_vif_id(struct umac_sta_data *stad, uint16_t vif_id)
{
    MMOSAL_ASSERT(stad != NULL);
    stad->vif_id = vif_id;
}

uint16_t umac_sta_data_get_vif_id(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return stad->vif_id;
}

void umac_sta_data_set_bssid(struct umac_sta_data *stad, const uint8_t *bssid)
{
    MMOSAL_ASSERT(stad != NULL);
    memcpy(stad->bssid, bssid, sizeof(stad->bssid));
}

void umac_sta_data_get_bssid(struct umac_sta_data *stad, uint8_t *bssid)
{
    MMOSAL_ASSERT(stad != NULL);
    memcpy(bssid, stad->bssid, sizeof(stad->bssid));
}

const uint8_t *umac_sta_data_peek_bssid(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return stad->bssid;
}

bool umac_sta_data_matches_bssid(struct umac_sta_data *stad, const uint8_t *bssid)
{
    MMOSAL_ASSERT(stad != NULL);
    return mm_mac_addr_is_equal(stad->bssid, bssid);
}

void umac_sta_data_set_peer_addr(struct umac_sta_data *stad, const uint8_t *addr)
{
    MMOSAL_ASSERT(stad != NULL);
    memcpy(stad->peer_addr, addr, sizeof(stad->peer_addr));
}

void umac_sta_data_get_peer_addr(struct umac_sta_data *stad, uint8_t *addr)
{
    MMOSAL_ASSERT(stad != NULL);
    memcpy(addr, stad->peer_addr, sizeof(stad->peer_addr));
}

const uint8_t *umac_sta_data_peek_peer_addr(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return stad->peer_addr;
}

bool umac_sta_data_matches_peer_addr(struct umac_sta_data *stad, const uint8_t *addr)
{
    MMOSAL_ASSERT(stad != NULL);
    return mm_mac_addr_is_equal(stad->peer_addr, addr);
}

void umac_sta_data_set_security(struct umac_sta_data *stad,
                                enum mmwlan_security_type security_type,
                                enum mmwlan_pmf_mode pmf_mode)
{
    MMOSAL_ASSERT(stad != NULL);
    stad->security_type = security_type;
    stad->pmf_mode = pmf_mode;
}

bool umac_sta_data_pmf_is_required(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return ((stad->security_type != MMWLAN_OPEN) && (stad->pmf_mode == MMWLAN_PMF_REQUIRED));
}

enum mmwlan_security_type umac_sta_data_get_security_type(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return stad->security_type;
}

void umac_sta_data_queue_pkt(struct umac_sta_data *stad, struct mmpkt *pkt)
{
    MMOSAL_ASSERT(stad != NULL);
    MMOSAL_DEV_ASSERT(pkt);
    mmpkt_list_append(&stad->txq, pkt);
}

struct mmpkt *umac_sta_data_pop_pkt(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return mmpkt_list_dequeue(&stad->txq);
}

uint32_t umac_sta_data_get_queued_len(struct umac_sta_data *stad)
{
    return mmpkt_list_length(&stad->txq);
}

bool umac_sta_data_is_paused(struct umac_sta_data *stad)
{
    MM_UNUSED(stad);

    return false;
}
