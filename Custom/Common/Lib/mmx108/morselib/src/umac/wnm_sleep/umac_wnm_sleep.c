/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmlog.h"
#include "mmdrv.h"
#include "umac/connection/umac_connection.h"
#include "umac_wnm_sleep.h"
#include "umac/interface/umac_interface.h"
#include "umac_wnm_sleep_data.h"
#include "umac/data/umac_data.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac/core/umac_core.h"
#include "umac/regdb/umac_regdb.h"


#define WNM_SLEEP_RETRY_REQ_TIMEOUT_MS 1000


#define WNM_SLEEP_MAX_RETRY_REQ 60

void umac_wnm_sleep_init(struct umac_data *umacd)
{
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);
    wnm_sleep_fsm_init(&data->fsm_inst);

    data->fsm_inst.arg = umacd;
    data->retry_counter = 0;
}

void umac_wnm_sleep_set_chip_powerdown(struct umac_data *umacd, bool chip_powerdown_enabled)
{
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);
    if (data->fsm_inst.current_state != WNM_SLEEP_FSM_STATE_ENTRY_REQUESTED &&
        data->fsm_inst.current_state != WNM_SLEEP_FSM_STATE_ACTIVE)
    {
        umac_config_set_chip_powerdown_enabled(umacd, chip_powerdown_enabled);
    }
}

enum mmwlan_status umac_wnm_sleep_register_cb(struct umac_data *umacd,
                                              umac_wnm_sleep_request_cb_t callback,
                                              void *arg)
{
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);

    if (data->callback)
    {
        return MMWLAN_UNAVAILABLE;
    }

    data->callback = callback;
    data->arg = arg;
    data->retry_counter = 0;

    return MMWLAN_SUCCESS;
}

static void umac_wnm_sleep_request_cb(struct umac_data *umacd, enum mmwlan_status status, void *arg)
{
    struct umac_wnm_sleep_data *data = (struct umac_wnm_sleep_data *)arg;
    struct mmosal_semb *semb = data->semb;

    MM_UNUSED(umacd);

    MMOSAL_ASSERT(data->status != NULL);
    MMOSAL_ASSERT(data->semb != NULL);

    *data->status = status;
    data->semb = NULL;
    data->status = NULL;

    mmosal_semb_give(semb);
}

enum mmwlan_status umac_wnm_sleep_register_semb(struct umac_data *umacd,
                                                struct mmosal_semb *semb,
                                                volatile enum mmwlan_status *status)
{
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);

    if (data->callback)
    {
        return MMWLAN_UNAVAILABLE;
    }

    data->semb = semb;
    data->status = status;
    umac_wnm_sleep_register_cb(umacd, umac_wnm_sleep_request_cb, data);

    return MMWLAN_SUCCESS;
}

void umac_wnm_sleep_report_event(struct umac_data *umacd, enum umac_wnm_sleep_event event)
{
    MMLOG_DBG("Supervisor recieved Event %d\n", event);

    MMOSAL_DEV_ASSERT(umac_connection_get_state(umacd) == MMWLAN_STA_CONNECTED);

    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);

    switch (event)
    {
        case UMAC_WNM_SLEEP_EVENT_REQUEST_ENTRY:
            if (data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_IDLE ||
                data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_EXIT_REQUESTED)
            {
                wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_ENABLE);
            }
            else if (data->callback != NULL)
            {
                data->callback(umacd, MMWLAN_UNAVAILABLE, data->arg);
                data->callback = NULL;
            }
            break;

        case UMAC_WNM_SLEEP_EVENT_REQUEST_EXIT:
            if (data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_ACTIVE ||
                data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_ENTRY_REQUESTED)
            {
                wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_DISABLE);
            }
            else if (data->callback != NULL)
            {
                data->callback(umacd, MMWLAN_UNAVAILABLE, data->arg);
                data->callback = NULL;
            }
            break;

        case UMAC_WNM_SLEEP_EVENT_ENTRY_CONFIRMED:
            if (data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_ENTRY_REQUESTED)
            {
                wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_ENTRY_CONFIRMED);
            }
            break;

        case UMAC_WNM_SLEEP_EVENT_EXIT_CONFIRMED:
            if (data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_EXIT_REQUESTED)
            {
                wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_EXIT_CONFIRMED);
            }
            break;

        case UMAC_WNM_SLEEP_EVENT_CONNECTION_LOST:
            if (data->fsm_inst.current_state != WNM_SLEEP_FSM_STATE_IDLE)
            {
                wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_CONNECTION_LOST);
            }
            break;

        case UMAC_WNM_SLEEP_EVENT_HW_RESTARTED:
            if (data->fsm_inst.current_state == WNM_SLEEP_FSM_STATE_ACTIVE)
            {
                MMLOG_DBG("Restoring WNM State in the chip.\n");
                uint16_t sta_vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
                MMOSAL_ASSERT(sta_vif_id != UMAC_INTERFACE_VIF_ID_INVALID);
                mmdrv_set_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_WNM_SLEEP);
                (void)mmdrv_set_chip_wnm_sleep_enabled(sta_vif_id, true);
            }
            break;

        default:
            break;
    }
}


static void umac_wnm_sleep_retry_request(void *arg1, void *arg2)
{
    MM_UNUSED(arg2);

    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);

    if (data->retry_counter < WNM_SLEEP_MAX_RETRY_REQ)
    {
        data->retry_counter++;
        wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_RETRY_REQUEST);
    }
    else
    {
        wnm_sleep_fsm_handle_event(&data->fsm_inst, WNM_SLEEP_FSM_EVENT_RETRIES_EXHAUSTED);
    }
}




#define WNM_SLEEP_FSM_LOG(format_str, ...) MMLOG_DBG(format_str, __VA_ARGS__)
#include "wnm_sleep_fsm.def"

static void wnm_sleep_fsm_entry_requested_entry(struct wnm_sleep_fsm_instance *inst,
                                                enum wnm_sleep_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    umac_supp_wnm_enter(umacd);

    bool ok = umac_core_register_timeout(umacd,
                                         WNM_SLEEP_RETRY_REQ_TIMEOUT_MS,
                                         umac_wnm_sleep_retry_request,
                                         umacd,
                                         NULL);
    if (!ok)
    {
        MMLOG_WRN("Failed to register retry request timeout\n");
    }
}

static void wnm_sleep_fsm_entry_requested_exit(struct wnm_sleep_fsm_instance *inst,
                                               enum wnm_sleep_fsm_event event)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);
    (void)umac_core_cancel_timeout(umacd, umac_wnm_sleep_retry_request, umacd, NULL);

    switch (event)
    {
        case WNM_SLEEP_FSM_EVENT_ENTRY_CONFIRMED:
            status = MMWLAN_SUCCESS;
            break;

        case WNM_SLEEP_FSM_EVENT_RETRIES_EXHAUSTED:
            MMLOG_ERR("WNM sleep request retries exhausted\n");
            status = MMWLAN_TIMED_OUT;
            break;

        case WNM_SLEEP_FSM_EVENT_CONNECTION_LOST:
            status = MMWLAN_ERROR;
            break;

        default:
            break;
    }

    if (data->callback != NULL && event != WNM_SLEEP_FSM_EVENT_RETRY_REQUEST)
    {
        data->callback(umacd, status, data->arg);
        data->callback = NULL;
    }
}

static void wnm_sleep_fsm_active_entry(struct wnm_sleep_fsm_instance *inst,
                                       enum wnm_sleep_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    uint16_t sta_vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
    MMOSAL_ASSERT(sta_vif_id != UMAC_INTERFACE_VIF_ID_INVALID);

    umac_datapath_pause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_WNM_SLEEP);
    if (umac_config_is_chip_powerdown_enabled(umacd))
    {
        mmdrv_deinit();
    }
    else
    {
        (void)mmdrv_set_chip_wnm_sleep_enabled(sta_vif_id, true);
    }


    mmdrv_set_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_WNM_SLEEP);
    umac_connection_set_monitor_disable(umacd, true);
}

static void wnm_sleep_fsm_active_exit(struct wnm_sleep_fsm_instance *inst,
                                      enum wnm_sleep_fsm_event event)
{
    MM_UNUSED(event);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    uint16_t sta_vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
    MMOSAL_ASSERT(sta_vif_id != UMAC_INTERFACE_VIF_ID_INVALID);

    if (umac_config_is_chip_powerdown_enabled(umacd))
    {
        const char *country_code = umac_regdb_get_country_code(umacd);
        MMOSAL_ASSERT(mmdrv_init(NULL, country_code) == 0);
        umac_connection_handle_hw_restarted(umacd);
        umac_config_set_chip_powerdown_enabled(umacd, false);
    }
    else
    {
        (void)mmdrv_set_chip_wnm_sleep_enabled(sta_vif_id, false);
    }

    umac_datapath_unpause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_WNM_SLEEP);
    mmdrv_unset_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_WNM_SLEEP);

    umac_connection_set_monitor_disable(umacd, false);
}

static void wnm_sleep_fsm_exit_requested_entry(struct wnm_sleep_fsm_instance *inst,
                                               enum wnm_sleep_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    umac_supp_wnm_exit(umacd);

    bool ok = umac_core_register_timeout(umacd,
                                         WNM_SLEEP_RETRY_REQ_TIMEOUT_MS,
                                         umac_wnm_sleep_retry_request,
                                         umacd,
                                         NULL);
    if (!ok)
    {
        MMLOG_WRN("Failed to register retry request timeout\n");
    }
}

static void wnm_sleep_fsm_exit_requested_exit(struct wnm_sleep_fsm_instance *inst,
                                              enum wnm_sleep_fsm_event event)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_wnm_sleep_data *data = umac_data_get_wnm_sleep(umacd);
    (void)umac_core_cancel_timeout(umacd, umac_wnm_sleep_retry_request, umacd, NULL);

    switch (event)
    {
        case WNM_SLEEP_FSM_EVENT_EXIT_CONFIRMED:
        case WNM_SLEEP_FSM_EVENT_CONNECTION_LOST:
            status = MMWLAN_SUCCESS;
            break;

        case WNM_SLEEP_FSM_EVENT_RETRIES_EXHAUSTED:
            MMLOG_ERR("WNM sleep request retries exhausted\n");
            status = MMWLAN_TIMED_OUT;
            break;

        default:
            break;
    }

    if (data->callback != NULL && event != WNM_SLEEP_FSM_EVENT_RETRY_REQUEST)
    {
        data->callback(umacd, status, data->arg);
        data->callback = NULL;
    }
}

static void wnm_sleep_fsm_transition_error(struct wnm_sleep_fsm_instance *inst,
                                           enum wnm_sleep_fsm_event event)
{
    MMLOG_INF("wnm_sleep_fsm: invalid event %u (%s) in state %u (%s)\n",
              event,
              wnm_sleep_fsm_event_tostr(event),
              inst->current_state,
              wnm_sleep_fsm_state_tostr(inst->current_state));
    MMOSAL_DEV_ASSERT_LOG_DATA(false, inst->current_state, event);
}

static void wnm_sleep_fsm_reentrance_error(struct wnm_sleep_fsm_instance *inst)
{
    MMLOG_INF("wnm_sleep_fsm: invalid reentrance in state %u (%s)\n",
              inst->current_state,
              wnm_sleep_fsm_state_tostr(inst->current_state));
    MMOSAL_DEV_ASSERT_LOG_DATA(false, inst->current_state);
}
