/*
 * Copyright 2021-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_ps.h"
#include "umac_ps_data.h"
#include "umac/interface/umac_interface.h"
#include "umac/config/umac_config.h"
#include "mmlog.h"
#include "mmosal.h"
#include "mmdrv.h"

void umac_ps_handle_hw_restarted(struct umac_data *umacd)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);


    bool pre_reset_suspend = data->suspended;
    enum mmwlan_ps_mode pre_reset_mode = data->pwr_mode;


    umac_ps_reset(umacd);


    umac_ps_set_suspended(umacd, pre_reset_suspend);
    umac_ps_update_mode(umacd);


    MMOSAL_DEV_ASSERT(data->pwr_mode == pre_reset_mode);

    MMLOG_DBG("Restore Power Save module\n");
}

void umac_ps_reset(struct umac_data *umacd)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);
    data->pwr_mode = MMWLAN_PS_DISABLED;
    data->suspended = false;
    MMLOG_DBG("Power save reset\n");
}


void umac_ps_update_mode(struct umac_data *umacd)
{
    enum mmwlan_ps_mode new_mode = umac_config_get_ps_mode(umacd);
    struct umac_ps_data *data = umac_data_get_ps(umacd);

    uint16_t sta_vif_id =
        umac_interface_get_vif_id(umacd,
                                  UMAC_INTERFACE_NONE | UMAC_INTERFACE_SCAN | UMAC_INTERFACE_STA);

    if (sta_vif_id == MMDRV_VIF_ID_INVALID)
    {
        MMLOG_DBG("No STA interface. PS disabled\n");
        data->pwr_mode = MMWLAN_PS_DISABLED;
        return;
    }

    uint16_t ap_vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    if (ap_vif_id != MMDRV_VIF_ID_INVALID)
    {
        MMLOG_DBG("Disabling power save due to AP interface\n");
        new_mode = MMWLAN_PS_DISABLED;
    }

    if (data->pwr_mode == new_mode)
    {
        MMLOG_DBG("PS mode already set to %s\n",
                  new_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled");
        return;
    }

    MMLOG_DBG("PS update: %s -> %s, (PS %ssuspended)\n",
              data->pwr_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled",
              new_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled",
              data->suspended ? "" : "not ");

    enum mmwlan_status status = MMWLAN_SUCCESS;
    switch (new_mode)
    {
        case MMWLAN_PS_ENABLED:
            status = mmdrv_set_wake_enabled(data->suspended);
            MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
            status = mmdrv_set_chip_power_save_enabled(sta_vif_id, true);
            MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
            data->pwr_mode = new_mode;
            break;

        case MMWLAN_PS_DISABLED:
            status = mmdrv_set_wake_enabled(true);
            MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
            status = mmdrv_set_chip_power_save_enabled(sta_vif_id, false);
            MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
            data->pwr_mode = new_mode;
            break;

        default:
            MMLOG_ERR("Unknown Power Save Mode requested\n");
            break;
    }
}

void umac_ps_set_suspended(struct umac_data *umacd, bool suspended)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);
    if (suspended == data->suspended)
    {
        return;
    }

    data->suspended = suspended;

    if (data->pwr_mode == MMWLAN_PS_DISABLED)
    {
        return;
    }

    MMLOG_INF("Power save %s\n", suspended ? "suspended" : "resumed");
    enum mmwlan_status status = mmdrv_set_wake_enabled(suspended);
    MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
}
