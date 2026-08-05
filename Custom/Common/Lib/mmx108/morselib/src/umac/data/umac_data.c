/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_data.h"
#include "umac_data_private.h"
#include "umac/umac_root_data.h"
#include "umac/ap/umac_ap_data.h"
#include "umac/config/umac_config_data.h"
#include "umac/connection/umac_connection_data.h"
#include "umac/core/umac_core_data.h"
#include "umac/datapath/umac_datapath_data.h"
#include "umac/interface/umac_interface_data.h"
#include "umac/ps/umac_ps_data.h"
#include "umac/rc/umac_rc_data.h"
#include "umac/scan/umac_scan_data.h"
#include "umac/supplicant_shim/umac_supp_shim_data.h"
#include "umac/twt/umac_twt_data.h"
#include "umac/wnm_sleep/umac_wnm_sleep_data.h"
#include "umac/offload/umac_offload_data.h"
#include "mmwlan_stats.h"

static struct umac_data
{
    struct umac_config_data config;
    struct umac_connection_data connection;
    struct umac_core_data core;
    struct umac_datapath_data datapath;
    struct umac_interface_data interface;
    struct umac_ps_data ps;
    struct umac_scan_data scan;
    struct mmwlan_stats_umac_data stats;
    struct umac_supp_shim_data supp_shim;
    struct umac_twt_data twt;
    struct umac_root_data root;
    struct umac_wnm_sleep_data wnm_sleep;
    struct umac_offload_data offload;
    struct umac_ap_data *ap;
    struct umac_interface_vif_data interface_vif_sta;
    struct umac_interface_vif_data interface_vif_ap;
} umac_data;

struct umac_data *umac_data_get_umacd(void)
{
    MMOSAL_DEV_ASSERT(umac_data.root.is_initialised);
    return &umac_data;
}



struct umac_config_data *umac_data_get_config(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->config;
}

struct umac_connection_data *umac_data_get_connection(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->connection;
}

struct umac_core_data *umac_data_get_core(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->core;
}

struct umac_datapath_data *umac_data_get_datapath(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->datapath;
}

struct umac_interface_data *umac_data_get_interface(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->interface;
}

struct umac_interface_vif_data *umac_data_get_interface_vif(struct umac_data *umacd,
                                                            enum mmwlan_vif vif)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    if (vif == MMWLAN_VIF_STA)
    {
        return &umacd->interface_vif_sta;
    }
    else if (vif == MMWLAN_VIF_AP)
    {
        return &umacd->interface_vif_ap;
    }
    else
    {
        return NULL;
    }
}

struct umac_ps_data *umac_data_get_ps(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->ps;
}

struct umac_scan_data *umac_data_get_scan(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->scan;
}

struct mmwlan_stats_umac_data *umac_data_get_stats(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->stats;
}

struct umac_supp_shim_data *umac_data_get_supp_shim(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->supp_shim;
}

struct umac_twt_data *umac_data_get_twt(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->twt;
}

struct umac_root_data *umac_data_get_root(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->root;
}

struct umac_wnm_sleep_data *umac_data_get_wnm_sleep(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->wnm_sleep;
}

struct umac_offload_data *umac_data_get_offload(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return &umacd->offload;
}

struct umac_ap_data *umac_data_get_ap(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return umacd->ap;
}

struct umac_ap_data *umac_data_alloc_ap(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    MMOSAL_ASSERT(umacd->ap == NULL);
    umacd->ap = (struct umac_ap_data *)mmosal_calloc(1, sizeof(*umacd->ap));
    return umacd->ap;
}

void umac_data_dealloc_ap(struct umac_data *umacd)
{
    mmosal_free(umacd->ap);
    umacd->ap = NULL;
}



struct umac_ba_sta_data *umac_sta_data_get_ba(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return &stad->ba;
}

struct umac_keys_sta_data *umac_sta_data_get_keys(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return &stad->keys;
}

struct umac_datapath_sta_data *umac_sta_data_get_datapath(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return &stad->datapath;
}

struct umac_rc_sta_data *umac_sta_data_get_rc(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    return &stad->rc;
}

struct umac_ap_sta_data *umac_sta_data_get_ap(struct umac_sta_data *stad)
{
    return &stad->ap;
}



void umac_data_init(void)
{
    memset(&umac_data, 0, sizeof(umac_data));
    umac_data.root.is_initialised = true;
}

void umac_data_deinit(void)
{
    umac_data.root.is_initialised = false;
}

bool umac_data_is_initialised(struct umac_data *umacd)
{
    UMAC_DATA_SANITY_CHECK(umacd);
    return umacd->root.is_initialised;
}

static struct umac_sta_data umac_sta_data;

struct umac_sta_data *umac_sta_data_alloc_static(struct umac_data *umacd)
{
    struct umac_sta_data *stad = &umac_sta_data;
    memset(stad, 0, sizeof(*stad));
    stad->umacd = umacd;
    return stad;
}

struct umac_sta_data *umac_sta_data_alloc(struct umac_data *umacd)
{
    struct umac_sta_data *stad = (struct umac_sta_data *)mmosal_calloc(1, sizeof(*stad));
    if (stad != NULL)
    {
        stad->umacd = umacd;
    }
    return stad;
}
