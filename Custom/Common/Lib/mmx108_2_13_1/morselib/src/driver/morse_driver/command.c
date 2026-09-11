/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "common/common.h"
#include "command.h"
#include "common/morse_command_utils.h"
#include "skbq.h"
#include "mac.h"
#include "skb_header.h"
#include "ps.h"
#include "common/morse_error.h"
#include "mmwlan.h"

#define MM_BA_TIMEOUT        (5000)
#define MM_MAX_COMMAND_RETRY 3

#ifdef ENABLE_DRVCMD_TRACE
#include "mmtrace.h"
static mmtrace_channel drvcmd_channel_handle;
#define DRVCMD_TRACE_INIT()                                         \
    do {                                                            \
        drvcmd_channel_handle = mmtrace_register_channel("drvcmd"); \
    } while (0)
#define DRVCMD_TRACE(_fmt, ...) mmtrace_printf(drvcmd_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define DRVCMD_TRACE_INIT() \
    do {                    \
    } while (0)
#define DRVCMD_TRACE(_fmt, ...) \
    do {                        \
    } while (0)
#endif

struct mmdrv_cmd_metadata
{
    int ret;
    uint32_t length;
    struct morse_cmd_resp *dest_resp;
    uint32_t resp_maxlen;
};

enum mmwlan_status morse_cmd_tx(struct driver_data *driverd,
                                struct morse_cmd_resp *resp,
                                struct morse_cmd_req *cmd,
                                uint32_t resp_maxlen,
                                uint32_t timeout,
                                bool skip_errlog)
{
    int cmd_len, ret, fw_ret = 0;
    uint16_t cmd_seq;
    int retry = 0;
    struct mmpkt *mmpkt;
    struct mmpktview *view;
    enum mmwlan_status status;

    DRVCMD_TRACE("cmd req %x", le16toh(cmd->hdr.message_id));

    if (driverd->cfg == NULL || driverd->cfg->ops == NULL)
    {

        return MMWLAN_NOT_RUNNING;
    }

    struct morse_skbq *cmd_q = driverd->cfg->ops->skbq_cmd_tc_q(driverd);
    if (cmd_q == NULL)
    {

        return MMWLAN_NOT_RUNNING;
    }

    cmd_len = sizeof(*cmd) + le16toh(cmd->hdr.len);
    cmd->hdr.flags = htole16(MORSE_CMD_TYPE_REQ);

    if (!mmosal_mutex_get(driverd->cmd.wait, UINT32_MAX))
    {
        return MMWLAN_UNAVAILABLE;
    }


    if (!driverd->started)
    {
        mmosal_mutex_release(driverd->cmd.wait);
        return MMWLAN_NOT_RUNNING;
    }

    mmhal_set_deep_sleep_veto(MORSELIB_VETO_COMMAND);

    driverd->cmd.seq++;
    if (driverd->cmd.seq > MORSE_CMD_IID_SEQ_MAX)
    {
        driverd->cmd.seq = 1;
    }
    cmd_seq = driverd->cmd.seq << MORSE_CMD_IID_SEQ_SHIFT;


    morse_ps_disable_async(driverd, PS_WAKER_COMMAND);

    do {
        cmd->hdr.host_id = htole16(cmd_seq | retry);

        mmpkt = morse_skbq_alloc_mmpkt_for_cmd(cmd_len);
        if (!mmpkt)
        {
            ret = -ENOMEM;
            break;
        }

        view = mmpkt_open(mmpkt);
        mmpkt_append_data(view, (uint8_t *)cmd, cmd_len);
        mmpkt_close(&view);

        DRVCMD_TRACE("cmd hostid %x", le16toh(cmd->hdr.host_id));
        MMLOG_DBG("CMD 0x%04x:%04x\n", le16toh(cmd->hdr.message_id), le16toh(cmd->hdr.host_id));

        MMOSAL_DEV_ASSERT(driverd->cmd.rspview == NULL);

        MMOSAL_MUTEX_GET_INF(driverd->cmd.lock);
        timeout = timeout ? timeout : MM_CMD_TIMEOUT_DEFAULT;
        ret = morse_skbq_mmpkt_tx(cmd_q, mmpkt, MORSE_SKB_CHAN_COMMAND);
        if (ret == 0)
        {

            driverd->cmd.pending_cmd_id = le16toh(cmd->hdr.message_id);
            driverd->cmd.pending_cmd_host_id = le16toh(cmd->hdr.host_id);
        }
        MMOSAL_MUTEX_RELEASE(driverd->cmd.lock);

        if (ret != 0)
        {
            MMLOG_ERR("morse_skbq_tx fail: %d\n", ret);
            break;
        }

        DRVCMD_TRACE("cmd wait");
        mmosal_semb_wait(driverd->cmd.semb, timeout);
        MMOSAL_MUTEX_GET_INF(driverd->cmd.lock);


        if ((retry + 1) == MM_MAX_COMMAND_RETRY)
        {
            driverd->cmd.pending_cmd_id = 0;
            driverd->cmd.pending_cmd_host_id = 0;
        }

        struct mmpktview *rspview = driverd->cmd.rspview;
        driverd->cmd.rspview = NULL;
        MMOSAL_MUTEX_RELEASE(driverd->cmd.lock);

        if (rspview == NULL)
        {
            DRVCMD_TRACE("cmd t/o");
            MMLOG_INF("Try:%d Command %04x:%04x timeout after %lu ms\n",
                      retry,
                      le16toh(cmd->hdr.message_id),
                      le16toh(cmd->hdr.host_id),
                      timeout);
            ret = -ETIMEDOUT;
        }
        else
        {
            uint32_t rxrsp_pkt_len = mmpkt_get_data_length(rspview);
            if (rxrsp_pkt_len < sizeof(struct morse_cmd_resp))
            {
                MMLOG_WRN("Malformed response: %s\n", "too short");
                ret = -EBADMSG;
            }
            else
            {
                struct morse_cmd_resp *rxrsp =
                    (struct morse_cmd_resp *)mmpkt_get_data_start(rspview);


                MMOSAL_DEV_ASSERT(rxrsp->hdr.message_id == cmd->hdr.message_id);
                MMOSAL_DEV_ASSERT((rxrsp->hdr.host_id & MORSE_CMD_IID_SEQ_MASK) ==
                                  (cmd->hdr.host_id & MORSE_CMD_IID_SEQ_MASK));

                uint32_t rxrsp_len = le16toh(rxrsp->hdr.len) + sizeof(struct morse_cmd_header);
                if (rxrsp_len > rxrsp_pkt_len)
                {
                    MMLOG_WRN("Malformed response: %s\n", "overflow");
                    ret = -EBADMSG;
                }
                else if ((resp_maxlen >= sizeof(struct morse_cmd_resp)) && resp != NULL)
                {
                    fw_ret = (int)le32toh(rxrsp->status);
                    uint32_t copy_length = MM_MIN(rxrsp_len, resp_maxlen);
                    memcpy(resp, rxrsp, copy_length);
                }
                else
                {
                    fw_ret = (int)le32toh(rxrsp->status);
                }
            }

            struct mmpkt *rsppkt = mmpkt_from_view(rspview);
            mmpkt_close(&rspview);
            mmpkt_release(rsppkt);

            DRVCMD_TRACE("cmd ret %d", ret);

            MMLOG_DBG("Command 0x%04x:%04x status %d (0x%08x)\n",
                      le16toh(cmd->hdr.message_id),
                      le16toh(cmd->hdr.host_id),
                      ret,
                      (unsigned)ret);
            if (ret)
            {
                MMLOG_WRN("Command 0x%04x:%04x error %d\n",
                          le16toh(cmd->hdr.message_id),
                          le16toh(cmd->hdr.host_id),
                          ret);
            }
        }

        spin_lock(&cmd_q->lock);
        morse_skbq_tx_finish(cmd_q, mmpkt, NULL);
        spin_unlock(&cmd_q->lock);

        retry++;

    } while (((ret == -ETIMEDOUT) || (ret == -EBADMSG)) &&
             (fw_ret == 0) &&
             (retry < MM_MAX_COMMAND_RETRY));

    morse_ps_enable_async(driverd, PS_WAKER_COMMAND);
    mmhal_clear_deep_sleep_veto(MORSELIB_VETO_COMMAND);
    MMOSAL_MUTEX_RELEASE(driverd->cmd.wait);

    DRVCMD_TRACE("cmd done %d", ret);

    if (ret < 0)
    {

        status = errno_to_status(ret);
    }
    else
    {

        status = (fw_ret == 0) ? MMWLAN_SUCCESS : MMWLAN_COMMAND_ERROR;
    }

    if (skip_errlog)
    {

        return status;
    }

    if (ret == -ETIMEDOUT)
    {
        MMLOG_ERR("Command %02x:%02x timed out\n",
                  le16toh(cmd->hdr.message_id),
                  le16toh(cmd->hdr.host_id));
    }
    else if (ret != 0)
    {
        MMLOG_ERR("Command %02x:%02x failed with rc %d (0x%x)\n",
                  le16toh(cmd->hdr.message_id),
                  le16toh(cmd->hdr.host_id),
                  ret,
                  (unsigned)ret);
    }

    return status;
}

int morse_cmd_resp_process(struct driver_data *driverd, struct mmpkt *mmpkt, uint8_t channel)
{
    int ret = -ESRCH;
    struct mmpktview *view = mmpkt_open(mmpkt);
    struct morse_cmd_resp *src_resp = (struct morse_cmd_resp *)(mmpkt_get_data_start(view));
    uint16_t cmd_id;
    uint16_t cmd_host_id;
    uint16_t resp_id = le16toh(src_resp->hdr.message_id);
    uint16_t resp_host_id = le16toh(src_resp->hdr.host_id);
    bool notify = false;

    MM_UNUSED(driverd);
    MM_UNUSED(channel);

    MMLOG_DBG("EVT 0x%04x:0x%04x\n", resp_id, resp_host_id);
    DRVCMD_TRACE("cmd rsp %x:%x", resp_id, resp_host_id);

    MMOSAL_MUTEX_GET_INF(driverd->cmd.lock);
    cmd_id = driverd->cmd.pending_cmd_id;
    cmd_host_id = driverd->cmd.pending_cmd_host_id;

    if (!MORSE_CMD_IS_RESP(src_resp))
    {
        ret = morse_mac_event_recv(driverd, view);
        goto exit;
    }


    if ((cmd_id == 0) ||
        (cmd_id != resp_id) ||
        ((cmd_host_id & MORSE_CMD_IID_SEQ_MASK) != (resp_host_id & MORSE_CMD_IID_SEQ_MASK)))
    {
        MMLOG_WRN("Late response for timed out cmd 0x%04x:%04x have 0x%04x:%04x 0x%04x\n",
                  resp_id,
                  resp_host_id,
                  cmd_id,
                  cmd_host_id,
                  driverd->cmd.seq);
        goto exit;
    }
    if ((cmd_host_id & MORSE_CMD_IID_RETRY_MASK) != (resp_host_id & MORSE_CMD_IID_RETRY_MASK))
    {
        MMLOG_INF("Command retry mismatch 0x%04x:%04x 0x%04x:%04x\n",
                  cmd_id,
                  cmd_host_id,
                  resp_id,
                  resp_host_id);
    }

    MMOSAL_DEV_ASSERT(driverd->cmd.rspview == NULL);
    if (driverd->cmd.rspview != NULL)
    {

        struct mmpkt *old = mmpkt_from_view(driverd->cmd.rspview);
        mmpkt_close(&driverd->cmd.rspview);
        mmpkt_release(old);
    }
    driverd->cmd.rspview = view;
    view = NULL;
    mmpkt = NULL;

    driverd->cmd.pending_cmd_id = 0;
    driverd->cmd.pending_cmd_host_id = 0;
    notify = true;

exit:
    mmpkt_close(&view);
    mmpkt_release(mmpkt);

    MMOSAL_MUTEX_RELEASE(driverd->cmd.lock);
    if (notify)
    {
        mmosal_semb_give(driverd->cmd.semb);
    }

    return ret;
}

int morse_cmd_init(struct driver_data *driverd)
{
    DRVCMD_TRACE_INIT();

    driverd->cmd.lock = mmosal_mutex_create("cmd.lock");
    if (driverd->cmd.lock == NULL)
    {
        goto failure;
    }

    driverd->cmd.wait = mmosal_mutex_create("cmd.wait");
    if (driverd->cmd.wait == NULL)
    {
        goto failure;
    }

    driverd->cmd.semb = mmosal_semb_create("cmdrsp");
    if (driverd->cmd.semb == NULL)
    {
        goto failure;
    }

    return MORSE_SUCCESS;

failure:
    MMLOG_ERR("Mutex/semb creation failed\n");
    mmosal_mutex_delete(driverd->cmd.lock);
    driverd->cmd.lock = NULL;
    mmosal_mutex_delete(driverd->cmd.wait);
    driverd->cmd.wait = NULL;

    return -ENOMEM;
}

void morse_cmd_deinit(struct driver_data *driverd)
{
    if (driverd->cmd.lock == NULL)
    {
        return;
    }


    MMOSAL_MUTEX_GET_INF(driverd->cmd.lock);
    if (driverd->cmd.pending_cmd_id != 0)
    {
        driverd->cmd.pending_cmd_id = 0;
        driverd->cmd.pending_cmd_host_id = 0;
        mmosal_semb_give(driverd->cmd.semb);
    }
    MMOSAL_MUTEX_RELEASE(driverd->cmd.lock);


    MMOSAL_MUTEX_GET_INF(driverd->cmd.wait);
    MMOSAL_MUTEX_RELEASE(driverd->cmd.wait);

    if (driverd->cmd.rspview != NULL)
    {
        struct mmpkt *rsppkt = mmpkt_from_view(driverd->cmd.rspview);
        driverd->cmd.rspview = NULL;
        mmpkt_release(rsppkt);
    }

    mmosal_mutex_delete(driverd->cmd.wait);
    driverd->cmd.wait = NULL;
    mmosal_mutex_delete(driverd->cmd.lock);
    driverd->cmd.lock = NULL;
    if (driverd->cmd.semb != NULL)
    {
        mmosal_semb_delete(driverd->cmd.semb);
    }
}
