/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdint.h>

#include "umac_data.h"

#include "umac/ap/umac_ap_data.h"
#include "umac/ba/umac_ba_data.h"
#include "umac/keys/umac_keys_data.h"
#include "umac/datapath/umac_datapath_data.h"
#include "umac/rc/umac_rc_data.h"


#ifdef ENABLE_UMAC_DATA_SANITY_CHECK
#define UMAC_DATA_SANITY_CHECK(umacd) MMOSAL_ASSERT(umacd == umac_data_get_umacd())
#else
#define UMAC_DATA_SANITY_CHECK(umacd) MM_UNUSED(umacd)
#endif

struct umac_sta_data
{
    uint16_t vif_id;
    uint16_t aid;
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
    uint8_t peer_addr[MMWLAN_MAC_ADDR_LEN];

    enum mmwlan_security_type security_type;

    enum mmwlan_pmf_mode pmf_mode;
    struct umac_data *umacd;
    struct umac_ap_sta_data ap;
    struct umac_ba_sta_data ba;
    struct umac_keys_sta_data keys;
    struct umac_datapath_sta_data datapath;
    struct umac_rc_sta_data rc;
    struct mmpkt_list txq;
};
