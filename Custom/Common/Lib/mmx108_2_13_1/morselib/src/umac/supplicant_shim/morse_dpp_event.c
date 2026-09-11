/*
 * Copyright 2025-2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_supp_shim_private.h"

static void umac_supp_handle_pb_result(const struct morse_dpp_event *evt)
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
            MMLOG_INF("Unknown PB result %u\n", evt->args.pb_result.result);
            event.args.pb_result.result = MMWLAN_DPP_PB_RESULT_ERROR;
            break;
    }

    umac_connection_handle_dpp_event(umacd, &event);
}

static void umac_supp_handle_conf_received(const struct morse_dpp_event *evt)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct mmwlan_dpp_cb_args event = {
        .event = MMWLAN_DPP_EVT_CONF_RECEIVED,
    };

    if (evt->args.conf_received.conf_obj != NULL)
    {
        const struct dpp_config_obj *conf_obj = evt->args.conf_received.conf_obj;
        event.args.conf_received.passphrase = conf_obj->passphrase;
        event.args.conf_received.ssid = conf_obj->ssid;
        event.args.conf_received.ssid_len = conf_obj->ssid_len;
    }

    umac_connection_handle_dpp_event(umacd, &event);
}

static void umac_supp_handle_noargs(enum mmwlan_dpp_event type)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct mmwlan_dpp_cb_args event = { .event = type };
    umac_connection_handle_dpp_event(umacd, &event);
}

static void umac_supp_handle_auth_failure(const struct morse_dpp_event *evt)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct mmwlan_dpp_cb_args event = { .event = MMWLAN_DPP_EVT_AUTH_FAILURE };

    switch (evt->args.auth_failure.reason)
    {
        case MORSE_DPP_AUTH_FAILURE_NO_RESPONSE:
            event.args.auth_failure.reason = MMWLAN_DPP_AUTH_FAILURE_NO_RESPONSE;
            break;
        case MORSE_DPP_AUTH_FAILURE_NO_CONFIRM:
            event.args.auth_failure.reason = MMWLAN_DPP_AUTH_FAILURE_NO_CONFIRM;
            break;
        case MORSE_DPP_AUTH_FAILURE_INIT_FAILED:
        default:
            event.args.auth_failure.reason = MMWLAN_DPP_AUTH_FAILURE_INIT_FAILED;
            break;
    }

    umac_connection_handle_dpp_event(umacd, &event);
}

static void umac_supp_handle_conf_failed(const struct morse_dpp_event *evt)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct mmwlan_dpp_cb_args event = { .event = MMWLAN_DPP_EVT_CONF_FAILED };

    switch (evt->args.conf_failed.reason)
    {
        case MORSE_DPP_CONF_FAILURE_RESPONSE_ERROR:
            event.args.conf_failed.reason = MMWLAN_DPP_CONF_FAILURE_RESPONSE_ERROR;
            break;
        case MORSE_DPP_CONF_FAILURE_TIMEOUT:
            event.args.conf_failed.reason = MMWLAN_DPP_CONF_FAILURE_TIMEOUT;
            break;
        case MORSE_DPP_CONF_FAILURE_RESULT_TIMEOUT:
        default:
            event.args.conf_failed.reason = MMWLAN_DPP_CONF_FAILURE_RESULT_TIMEOUT;
            break;
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

        case MORSE_DPP_EVT_CONF_RECEIVED:
            umac_supp_handle_conf_received(evt);
            break;

        case MORSE_DPP_EVT_CHIRP_STARTED:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_CHIRP_STARTED);
            break;

        case MORSE_DPP_EVT_CHIRP_STOPPED:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_CHIRP_STOPPED);
            break;

        case MORSE_DPP_EVT_PB_STARTED:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_PB_STARTED);
            break;

        case MORSE_DPP_EVT_PB_DISCOVERY:
        {
            struct umac_data *umacd = umac_data_get_umacd();
            struct mmwlan_dpp_cb_args event = {
                .event = MMWLAN_DPP_EVT_PB_DISCOVERY,
                .args.pb_discovery.peer_mac = evt->args.pb_discovery.peer_mac,
            };
            umac_connection_handle_dpp_event(umacd, &event);
            break;
        }

        case MORSE_DPP_EVT_AUTH_REQ_RX:
        {
            struct umac_data *umacd = umac_data_get_umacd();
            struct mmwlan_dpp_cb_args event = {
                .event = MMWLAN_DPP_EVT_AUTH_REQ_RX,
                .args.auth_req_rx.peer_mac = evt->args.auth_req_rx.peer_mac,
            };
            umac_connection_handle_dpp_event(umacd, &event);
            break;
        }

        case MORSE_DPP_EVT_AUTH_RESP_TX:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_AUTH_RESP_TX);
            break;

        case MORSE_DPP_EVT_AUTH_SUCCESS:
        {
            struct umac_data *umacd = umac_data_get_umacd();
            struct mmwlan_dpp_cb_args event = {
                .event = MMWLAN_DPP_EVT_AUTH_SUCCESS,
                .args.auth_success.initiator = (bool)evt->args.auth_success.initiator,
            };
            umac_connection_handle_dpp_event(umacd, &event);
            break;
        }

        case MORSE_DPP_EVT_AUTH_FAILURE:
            umac_supp_handle_auth_failure(evt);
            break;

        case MORSE_DPP_EVT_CONF_REQ_TX:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_CONF_REQ_TX);
            break;

        case MORSE_DPP_EVT_CONF_RESULT_TX:
            umac_supp_handle_noargs(MMWLAN_DPP_EVT_CONF_RESULT_TX);
            break;

        case MORSE_DPP_EVT_CONF_FAILED:
            umac_supp_handle_conf_failed(evt);
            break;

        default:
            MMLOG_WRN("Unknown DPP event type %u\n", evt->type);
            break;
    }
}
