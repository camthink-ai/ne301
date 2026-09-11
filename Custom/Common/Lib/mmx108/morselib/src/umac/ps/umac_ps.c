/*
 * Copyright 2021-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_ps.h"
#include "umac_ps_data.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/connection/umac_connection.h"
#include "umac/core/umac_core.h"
#include "umac/config/umac_config.h"
#include "common/common.h"
#include "mmlog.h"
#include "mmosal.h"
#include "mmdrv.h"

void umac_ps_handle_hw_restarted(struct umac_data *umacd)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);


    bool pre_reset_suspend = data->suspended;
    enum mmwlan_ps_mode pre_reset_mode = data->pwr_mode;


    umac_ps_reset(umacd);


    data->suspended = pre_reset_suspend;
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

enum mmwlan_ps_mode umac_ps_get_mode(struct umac_data *umacd)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);
    return data->pwr_mode;
}


static void umac_ps_update(struct umac_data *umacd, bool suspended_state_changed)
{
    enum mmwlan_ps_mode new_mode = umac_config_get_ps_mode(umacd);
    struct umac_ps_data *data = umac_data_get_ps(umacd);

    uint16_t sta_vif_id =
        umac_interface_get_vif_id(umacd,
                                  UMAC_INTERFACE_NONE | UMAC_INTERFACE_SCAN | UMAC_INTERFACE_STA);

    if (sta_vif_id == UMAC_INTERFACE_VIF_ID_INVALID)
    {
        MMLOG_DBG("No STA interface. PS disabled\n");
        data->pwr_mode = MMWLAN_PS_DISABLED;
        return;
    }

    uint16_t ap_vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    if (ap_vif_id != UMAC_INTERFACE_VIF_ID_INVALID)
    {
        MMLOG_DBG("Disabling power save due to AP interface\n");
        new_mode = MMWLAN_PS_DISABLED;
    }

    if (data->pwr_mode == new_mode && !suspended_state_changed)
    {
        MMLOG_DBG("PS mode already set to %s\n",
                  new_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled");
        return;
    }

    MMLOG_DBG("PS update: %s -> %s, suspend state %s\n",
              data->pwr_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled",
              new_mode == MMWLAN_PS_DISABLED ? "disabled" : "enabled",
              suspended_state_changed ? "changed" : "unchanged");

    switch (new_mode)
    {
        case MMWLAN_PS_ENABLED:
            if (!data->suspended)
            {
                if (suspended_state_changed)
                {
                    MMLOG_DBG("Enabling PS due to resumption\n");
                }
                (void)mmdrv_set_chip_power_save_enabled(sta_vif_id, true);
            }
            else
            {
                MMLOG_DBG("Disabling PS due to suspension\n");
                (void)mmdrv_set_chip_power_save_enabled(sta_vif_id, false);
            }
            (void)mmdrv_set_wake_enabled(false);
            data->pwr_mode = new_mode;
            break;

        case MMWLAN_PS_DISABLED:
            (void)mmdrv_set_wake_enabled(true);
            (void)mmdrv_set_chip_power_save_enabled(sta_vif_id, false);
            data->pwr_mode = new_mode;
            break;

        default:
            MMLOG_ERR("Unknown Power Save Mode requested\n");
            break;
    }
}

void umac_ps_update_mode(struct umac_data *umacd)
{
    umac_ps_update(umacd, false);
}

void umac_ps_set_suspended(struct umac_data *umacd, bool suspended)
{
    struct umac_ps_data *data = umac_data_get_ps(umacd);
    if (suspended == data->suspended)
    {
        return;
    }

    MMLOG_INF("Power save %s\n", suspended ? "suspended" : "resumed");

    data->suspended = suspended;
    umac_ps_update(umacd, true);
}
