/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmlog.h"
#include "mmpkt.h"
#include "mmwlan.h"
#include "umac/connection/umac_connection.h"
#include "umac/data/umac_data.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac_supp_shim_private.h"
#include "umac/datapath/umac_datapath.h"

enum mmwlan_status umac_supp_add_ap_interface(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (data->ap_wpa_s != NULL)
    {
        MMLOG_WRN("AP interface already active\n");

        MMOSAL_DEV_ASSERT(false);
        return MMWLAN_SUCCESS;
    }

    enum mmwlan_status status = umac_supp_start_supp(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    struct wpa_interface iface = {
        .confname = UMAC_SUPP_AP_CONFIG_NAME,
        .driver = UMAC_SUPP_AP_DRIVER_NAME,
        .ifname = UMAC_SUPP_AP_CONFIG_NAME,
    };

    data->ap_wpa_s = wpa_supplicant_add_iface(data->global, &iface, NULL);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_supp_remove_ap_interface(struct umac_data *umacd)
{
    struct umac_supp_shim_data *data = umac_data_get_supp_shim(umacd);

    if (!data->is_started)
    {
        return MMWLAN_UNAVAILABLE;
    }

    MMLOG_INF("Removing %s Supp interface\n", "AP");

    if (data->ap_wpa_s == NULL)
    {
        return MMWLAN_NOT_FOUND;
    }

    int ret = wpa_supplicant_remove_iface(data->global, data->ap_wpa_s, 0);
    data->ap_wpa_s = NULL;

    if (ret == 0)
    {
        return MMWLAN_SUCCESS;
    }
    else
    {
        return MMWLAN_ERROR;
    }
}
