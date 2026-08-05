/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_ba.h"
#include "umac_ba_data.h"
#include "common/common.h"
#include "umac/frames/action.h"
#include "mmlog.h"

#include "umac/config/umac_config.h"
#include "umac/connection/umac_connection.h"
#include "umac/interface/umac_interface.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/core/umac_core.h"

void umac_ba_deinit(struct umac_sta_data *stad)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    memset(data, 0, sizeof(*data));
}


#define ADDBA_REQ_BASE_TIMEOUT_MS 100


#define ADDBA_REQ_MULTIPLIER 2


#define ADDBA_REQ_MAX_TIMEOUT_MS 60000


static uint32_t umac_ba_calculate_addba_attempt_backoff(uint32_t current_backoff)
{
    if (!current_backoff)
    {
        return ADDBA_REQ_BASE_TIMEOUT_MS;
    }

    if ((current_backoff * ADDBA_REQ_MULTIPLIER) >= ADDBA_REQ_MAX_TIMEOUT_MS)
    {
        return ADDBA_REQ_MAX_TIMEOUT_MS;
    }

    return current_backoff * ADDBA_REQ_MULTIPLIER;
}


static enum mmwlan_status umac_ba_tx_delba(struct umac_sta_data *stad,
                                           enum dot11_delba_initiator initiator,
                                           uint8_t tid,
                                           enum dot11_reason_code reason)
{
    struct dot11_action_field_delba delba = { 0 };

    delba.category = DOT11_ACTION_CATEGORY_BLOCK_ACK;
    delba.ba_action = DOT11_BA_ACTION_NDP_DELBA;
    DOT11_DELBA_PARAMETER_SET_FIELD_SET_INITIATOR(delba.delba_param_set, initiator);
    DOT11_DELBA_PARAMETER_SET_FIELD_SET_TID(delba.delba_param_set, (uint16_t)tid);
    delba.reason_code = htole16(reason);

    struct frame_data_action params = { .bssid = umac_sta_data_peek_bssid(stad),
                                        .dst_address = umac_sta_data_peek_peer_addr(stad),
                                        .src_address = umac_interface_peek_mac_addr(stad),
                                        .action_field = (uint8_t *)&delba,
                                        .action_field_len = sizeof(delba) };

    return umac_datapath_build_and_tx_mgmt_frame(stad, frame_action_build, &params);
}


static void umac_ba_addba_req_timeout_handler(void *arg1, void *arg2)
{
    struct umac_sta_data *stad = (struct umac_sta_data *)arg1;
    struct umac_ba_session *session = (struct umac_ba_session *)arg2;

    if (session->status != UMAC_BA_REQUESTED)
    {
        MMLOG_DBG("BA session already establish or closed. TID (%u)\n", session->tid);
        return;
    }


    enum mmwlan_status status = umac_ba_tx_delba(stad,
                                                 DOT11_DELBA_INITIATOR_ORIGINATOR,
                                                 session->tid,
                                                 DOT11_REASON_INACTIVITY);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("DELBA failed to TX. TID (%u).\n", session->tid);
        return;
    }

    session->status = UMAC_BA_DISABLED;
    MMLOG_DBG("Attempt backoff (%lu) reached, retrying on next TX. TID (%u)\n",
              session->attempt_backoff,
              session->tid);
}


static void umac_ba_rx_delba(struct umac_sta_data *stad, const uint8_t *field, uint32_t field_len)
{
    const struct dot11_action_field_delba *delba = (const struct dot11_action_field_delba *)field;
    if (field_len < sizeof(*delba))
    {
        MMLOG_WRN("DELBA response too short. Received: %lu; Expected: %u\n",
                  field_len,
                  sizeof(*delba));
        return;
    }

    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);

    uint8_t tid = (uint8_t)dot11_delba_parameter_set_field_get_tid(delba->delba_param_set);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        MMLOG_DBG("Recived invalid TID (%u) in delba\n", tid);
        return;
    }

    struct umac_ba_session *session = NULL;

    if (dot11_delba_parameter_set_field_get_initiator(delba->delba_param_set) ==
        DOT11_DELBA_INITIATOR_ORIGINATOR)
    {
        session = &data->sessions.recipient[tid];
    }
    else
    {
        session = &data->sessions.originator[tid];
    }

    if (session->status == UMAC_BA_DISABLED)
    {

        return;
    }


    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    umac_core_cancel_timeout(umacd, umac_ba_addba_req_timeout_handler, stad, session);

    umac_datapath_flush_rx_reorder_list_for_tid(stad, session->tid);

    MMLOG_DBG("BA session disabled. TID %u.\n", session->tid);
    session->status = UMAC_BA_DISABLED;
}


static uint16_t umac_ba_build_param_set(struct umac_ba_session *session)
{
    uint16_t param_set = 0;

    DOT11_BA_PARAMETER_SET_FIELD_SET_AMSDU_SUPPORTED(param_set, DOT11_BA_AMSDU_NOT_SUPPORTED);
    DOT11_BA_PARAMETER_SET_FIELD_SET_BA_POLICY(param_set, DOT11_BA_POLICY_IMMEDIATE);
    DOT11_BA_PARAMETER_SET_FIELD_SET_TID(param_set, (uint16_t)session->tid);
    DOT11_BA_PARAMETER_SET_FIELD_SET_BUFFER_SIZE(param_set, session->buffer_size);

    return param_set;
}


static void umac_ba_tx_addba_resp(struct umac_sta_data *stad, struct umac_ba_session *session)
{
    struct dot11_action_field_addba_resp resp = { 0 };

    resp.category = DOT11_ACTION_CATEGORY_BLOCK_ACK;
    resp.ba_action = DOT11_BA_ACTION_NDP_ADDBA_RESP;
    resp.dialog_token = session->dialog_token;
    resp.status_code = htole16(DOT11_STATUS_SUCCESS);
    resp.ba_param_set = umac_ba_build_param_set(session);

    struct frame_data_action params = { .bssid = umac_sta_data_peek_bssid(stad),
                                        .dst_address = umac_sta_data_peek_peer_addr(stad),
                                        .src_address = umac_interface_peek_mac_addr(stad),
                                        .action_field = (uint8_t *)&resp,
                                        .action_field_len = sizeof(resp) };

    enum mmwlan_status status =
        umac_datapath_build_and_tx_mgmt_frame(stad, frame_action_build, &params);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to send ADDBA resp for TID (%u).\n", session->tid);
        return;
    }

    MMLOG_DBG("BA session successful, recipient. TID %u.\n", session->tid);
    session->status = UMAC_BA_SUCCESS;
}


static void umac_ba_rx_addba_resp(struct umac_sta_data *stad,
                                  const uint8_t *field,
                                  uint32_t field_len)
{
    const struct dot11_action_field_addba_resp *resp =
        (const struct dot11_action_field_addba_resp *)field;
    if (field_len < sizeof(*resp))
    {
        MMLOG_WRN("ADDBA response too short. Received: %lu; Expected: %u\n",
                  field_len,
                  sizeof(*resp));
        return;
    }
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);

    uint8_t tid = (uint8_t)dot11_ba_parameter_set_field_get_tid(resp->ba_param_set);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        umac_ba_tx_delba(stad, DOT11_DELBA_INITIATOR_ORIGINATOR, tid, DOT11_REASON_UNSPECIFIED);
        MMLOG_DBG("Received invalid TID (%u) in addba resp.\n", tid);
        return;
    }


    struct umac_ba_session *session = &data->sessions.originator[tid];


    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    umac_core_cancel_timeout(umacd, umac_ba_addba_req_timeout_handler, stad, session);

    if ((session->status != UMAC_BA_REQUESTED) || (session->dialog_token != resp->dialog_token))
    {
        MMLOG_DBG("Erroneous ADDBA resp recieved, ignoring. TID (%u), Token (%u).\n",
                  tid,
                  resp->dialog_token);
        return;
    }


    if (le16toh(resp->status_code) != DOT11_STATUS_SUCCESS)
    {
        MMLOG_DBG("Block Ack session refused, status code %u, TID (%u).\n",
                  le16toh(resp->status_code),
                  tid);
        session->status = UMAC_BA_REFUSED;
        return;
    }

    if (dot11_ba_parameter_set_field_get_ba_policy(resp->ba_param_set) != DOT11_BA_POLICY_IMMEDIATE)
    {
        umac_ba_tx_delba(stad, DOT11_DELBA_INITIATOR_ORIGINATOR, tid, DOT11_REASON_UNSPECIFIED);
        MMLOG_DBG("Delayed Block Ack unsupported, TID (%u).\n", tid);
        return;
    }

    session->buffer_size = min_u16(dot11_ba_parameter_set_field_get_buffer_size(resp->ba_param_set),
                                   session->buffer_size);

    MMLOG_DBG("BA session successful, originator. TID %u.\n", session->tid);
    session->status = UMAC_BA_SUCCESS;
}


static void umac_ba_tx_addba_req(struct umac_sta_data *stad, struct umac_ba_session *session)
{
    struct dot11_action_field_addba_req req = { 0 };

    req.category = DOT11_ACTION_CATEGORY_BLOCK_ACK;
    req.ba_action = DOT11_BA_ACTION_NDP_ADDBA_REQ;
    req.dialog_token = session->dialog_token;
    req.ba_param_set = umac_ba_build_param_set(session);
    req.ba_timeout = htole16(session->timeout);
    req.ba_ssc = session->starting_seq_ctrl;

    struct frame_data_action params = { .bssid = umac_sta_data_peek_bssid(stad),
                                        .dst_address = umac_sta_data_peek_peer_addr(stad),
                                        .src_address = umac_interface_peek_mac_addr(stad),
                                        .action_field = (uint8_t *)&req,
                                        .action_field_len = sizeof(req) };

    enum mmwlan_status status =
        umac_datapath_build_and_tx_mgmt_frame(stad, frame_action_build, &params);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to send ADDBA req for TID (%u).\n", session->tid);
        return;
    }

    MMLOG_DBG("BA session requested, originator. TID %u.\n", session->tid);
    session->status = UMAC_BA_REQUESTED;
}


static void umac_ba_rx_addba_req(struct umac_sta_data *stad,
                                 const uint8_t *field,
                                 uint32_t field_len)
{
    const struct dot11_action_field_addba_req *req =
        (const struct dot11_action_field_addba_req *)field;
    if (field_len < sizeof(*req))
    {
        MMLOG_WRN("ADDBA request too short. Received: %lu; Expected: %u\n",
                  field_len,
                  sizeof(*req));
        return;
    }

    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);

    uint8_t tid = (uint8_t)dot11_ba_parameter_set_field_get_tid(req->ba_param_set);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        umac_ba_tx_delba(stad, DOT11_DELBA_INITIATOR_RECIPIENT, tid, DOT11_REASON_UNSPECIFIED);
        MMLOG_DBG("Recieved invalid TID (%u) in addba req.\n", tid);
        return;
    }

    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    if (!MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), AMPDU))
    {
        MMLOG_INF("Received an ADDBA request when the interface does not support AMPDU.\n");
        umac_ba_tx_delba(stad, DOT11_DELBA_INITIATOR_RECIPIENT, tid, DOT11_REASON_UNSPECIFIED);
        return;
    }


    struct umac_ba_session *session = &data->sessions.recipient[tid];
    MMLOG_DBG("BA session requested, recipient. TID %u.\n", session->tid);

    if (session->status != UMAC_BA_DISABLED)
    {
        MMLOG_DBG("BA session already established/in progress, TID (%u).\n", tid);
        return;
    }
    session->tid = tid;

    if (dot11_ba_parameter_set_field_get_ba_policy(req->ba_param_set) != DOT11_BA_POLICY_IMMEDIATE)
    {
        umac_ba_tx_delba(stad, DOT11_DELBA_INITIATOR_RECIPIENT, tid, DOT11_REASON_UNSPECIFIED);
        MMLOG_DBG("Delayed Block Ack unsupported, TID (%u).\n", tid);
        return;
    }

    session->buffer_size = min_u16(dot11_ba_parameter_set_field_get_buffer_size(req->ba_param_set),
                                   umac_config_get_datapath_rx_reorder_list_maxlen(umacd));

    session->dialog_token = req->dialog_token;


    MMOSAL_DEV_ASSERT(req->ba_timeout == DOT11_BLOCK_ACK_TIMEOUT_DISABLED);
    session->timeout = DOT11_BLOCK_ACK_TIMEOUT_DISABLED;

    umac_ba_tx_addba_resp(stad, session);
}

void umac_ba_process_rx_frame(struct umac_sta_data *stad, const uint8_t *frame, uint32_t frame_len)
{
    MMOSAL_DEV_ASSERT(stad != NULL);
    if (stad == NULL)
    {
        MMLOG_WRN("Cannot process BA frame without a STA record\n");
        return;
    }
    const struct dot11_action *action_frame = (const struct dot11_action *)frame;


    MMOSAL_ASSERT(frame_len >= sizeof(*action_frame));
    const uint8_t *ba_action = action_frame->field.action_details;

    MMLOG_DBG("Recieved BA %u\n", *ba_action);

    switch (*ba_action)
    {
        case DOT11_BA_ACTION_NDP_ADDBA_REQ:
            umac_ba_rx_addba_req(stad,
                                 (const uint8_t *)&action_frame->field,
                                 (frame_len - sizeof(action_frame->hdr)));
            break;

        case DOT11_BA_ACTION_NDP_ADDBA_RESP:
            umac_ba_rx_addba_resp(stad,
                                  (const uint8_t *)&action_frame->field,
                                  (frame_len - sizeof(action_frame->hdr)));
            break;

        case DOT11_BA_ACTION_NDP_DELBA:
            umac_ba_rx_delba(stad,
                             (const uint8_t *)&action_frame->field,
                             (frame_len - sizeof(action_frame->hdr)));
            break;

        default:
            MMLOG_WRN("Recieved unsupported block ack action %u\n.", *ba_action);
            break;
    }
}

void umac_ba_session_init(struct umac_sta_data *stad, uint8_t tid, uint16_t ssc, uint16_t timeout)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        MMLOG_WRN("Received invalid TID (%u) for session init.\n", tid);
        return;
    }

    struct umac_ba_session *session = &data->sessions.originator[tid];

    if (session->status > UMAC_BA_DISABLED)
    {
        return;
    }

    session->tid = tid;
    session->timeout = timeout;
    session->dialog_token = (uint8_t)mmhal_random_u32(1, UINT8_MAX);
    session->buffer_size = umac_config_get_datapath_rx_reorder_list_maxlen(umacd);
    session->starting_seq_ctrl = ssc;
    session->attempt_backoff = umac_ba_calculate_addba_attempt_backoff(session->attempt_backoff);

    umac_ba_tx_addba_req(stad, session);

    bool ok = umac_core_register_timeout(umacd,
                                         session->attempt_backoff,
                                         umac_ba_addba_req_timeout_handler,
                                         stad,
                                         session);
    if (!ok)
    {
        MMLOG_ERR("Failed to schedule timeout handler for ADDBA req. TID (%u)\n", session->tid);
    }
}

void umac_ba_session_deinit(struct umac_sta_data *stad,
                            uint8_t tid,
                            enum dot11_delba_initiator initiator)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        MMLOG_WRN("Received invalid TID (%u) for session deinit.\n", tid);
        return;
    }

    struct umac_ba_session *session;
    if (initiator == DOT11_DELBA_INITIATOR_ORIGINATOR)
    {
        session = &data->sessions.originator[tid];
    }
    else
    {
        session = &data->sessions.recipient[tid];
    }

    if (session->status == UMAC_BA_DISABLED)
    {
        return;
    }

    enum mmwlan_status status = umac_ba_tx_delba(stad, initiator, tid, DOT11_REASON_END_TS_BS);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to deinit BA session. Initiator (%u), TID (%u).\n", initiator, tid);
        return;
    }

    memset(session, 0, sizeof(*session));
    MMLOG_DBG("Successful deinit of BA session. Initiator (%u), TID (%u)\n", initiator, tid);
}

uint8_t umac_ba_get_reorder_buffer_size(struct umac_sta_data *stad, uint8_t tid)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        return 0;
    }

    struct umac_ba_session *session = &data->sessions.recipient[tid];

    MMOSAL_ASSERT(session->buffer_size <= UINT8_MAX);
    return session->buffer_size;
}

int32_t umac_ba_get_expected_rx_seq_num(struct umac_sta_data *stad, uint8_t tid)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        return -1;
    }

    struct umac_ba_session *session = &data->sessions.recipient[tid];

    if (session->status != UMAC_BA_SUCCESS)
    {
        return -1;
    }

    return session->next_expected_rx_seq_num;
}

void umac_ba_set_expected_rx_seq_num(struct umac_sta_data *stad, uint8_t tid, uint16_t seq_num)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS)
    {
        return;
    }

    struct umac_ba_session *session = &data->sessions.recipient[tid];

    if (session->status != UMAC_BA_SUCCESS)
    {
        return;
    }

    session->next_expected_rx_seq_num = seq_num;
}

bool umac_ba_is_ampdu_permitted(struct umac_sta_data *stad, uint8_t tid)
{
    struct umac_ba_sta_data *data = umac_sta_data_get_ba(stad);
    if (tid >= UMAC_BA_MAX_SESSIONS || data->sessions.originator[tid].status != UMAC_BA_SUCCESS)
    {
        return false;
    }

    return (data->sessions.originator[tid].status == UMAC_BA_SUCCESS);
}
