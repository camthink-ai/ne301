/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_supp_shim_private.h"


void umac_supp_handle_pb_result(const struct morse_dpp_event *evt)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct mmwlan_dpp_cb_args event;

    event.event = MMWLAN_DPP_EVT_PB_RESULT;
    switch (evt->args.pb_result.result)
    {
        case MORSE_DPP_PB_RESULT_SUCCESS:
            event.args.pb_result.result = MMWLAN_DPP_PB_RESULT_SUCCESS;
            break;

        case MORSE_DPP_PB_RESULT_FAILED:
            event.args.pb_result.result = MMWLAN_DPP_PB_RESULT_ERROR;
            break;

        case MORSE_DPP_PB_RESULT_SESSION_OVERLAP:
            event.args.pb_result.result = MMWLAN_DPP_PB_RESULT_SESSION_OVERLAP;
            break;

        case MORSE_DPP_PB_RESULT_NO_CONFIG:
        case MORSE_DPP_PB_RESULT_COULD_NOT_CONNECT:
        default:
            MMOSAL_DEV_ASSERT(false);
            event.args.pb_result.result = MMWLAN_DPP_PB_RESULT_ERROR;
            break;
    }

    if (evt->args.pb_result.conf_obj != NULL)
    {
        const struct dpp_config_obj *conf_obj = evt->args.pb_result.conf_obj;
        event.args.pb_result.passphrase = conf_obj->passphrase;
        event.args.pb_result.ssid = conf_obj->ssid;
        event.args.pb_result.ssid_len = conf_obj->ssid_len;
    }

    umac_connection_handle_dpp_event(umacd, &event);
}

void morse_dpp_event(const char *func, int line, const struct morse_dpp_event *evt)
{
    MMLOG_DBG("%s[%d] DPP Event\n", func, line);

    switch (evt->type)
    {
        case MORSE_DPP_EVT_PB_RESULT:
            umac_supp_handle_pb_result(evt);
            break;

        default:
            MMOSAL_DEV_ASSERT(false);
            break;
    }
}
