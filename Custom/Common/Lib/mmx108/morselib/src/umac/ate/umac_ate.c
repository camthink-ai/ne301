/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#include <errno.h>

#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "mmlog.h"
#include "common/morse_commands.h"
#include "umac/ba/umac_ba.h"
#include "umac/core/umac_core.h"
#include "umac/connection/umac_connection.h"
#include "umac/config/umac_config.h"
#include "umac/ate/umac_ate.h"
#include "umac/keys/umac_keys.h"
#include "umac/stats/umac_stats.h"
#include "dot11/dot11.h"
#include "umac/supplicant_shim/umac_supp_shim.h"

static enum mmwlan_status umac_ate_configure_interop(struct umac_data *umacd,
                                                     uint8_t *command,
                                                     uint8_t *response,
                                                     uint32_t *response_len)
{
    struct morse_cmd_req_iot_configure_interop *cmd =
        (struct morse_cmd_req_iot_configure_interop *)command;
    struct morse_cmd_resp *resp = (struct morse_cmd_resp *)response;

    if (cmd->disable_op_class_checking)
    {
        umac_config_set_opclass_check_enabled(umacd, false);
    }
    else
    {
        umac_config_set_opclass_check_enabled(umacd, true);
    }

    if (cmd->enable_channel_width_workaround)
    {
        umac_config_set_supported_channel_width_field_override(
            umacd,
            DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_MHZ);
    }
    else
    {
        umac_config_set_supported_channel_width_field_override(umacd, -1);
    }

    if (resp != NULL)
    {
        resp->hdr.message_id = cmd->hdr.message_id;
        resp->hdr.vif_id = cmd->hdr.vif_id;
        resp->hdr.host_id = cmd->hdr.host_id;
        resp->hdr.len = htole16(sizeof(*resp) - sizeof(resp->hdr));
        resp->hdr.flags = 0;
        resp->status = 0;

        if (response_len != NULL)
        {
            *response_len = sizeof(*resp);
        }
    }

    return MMWLAN_SUCCESS;
}

static enum mmwlan_status umac_ate_sta_send_addba(struct umac_data *umacd, uint8_t *command)
{
    const uint8_t *tid = NULL;
    struct morse_cmd_req_iot_send_addba *req_ba = (struct morse_cmd_req_iot_send_addba *)command;

    if (!umac_connection_addr_matches_bssid(umacd, req_ba->mac_addr.octet))
    {
        MMLOG_WRN("BSSID and mac_addr mismatch for send ADDBA event.\n");
        return MMWLAN_ERROR;
    }

    tid = &req_ba->tid;

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_ba_session_deinit(stad, *tid, DOT11_DELBA_INITIATOR_ORIGINATOR);
    umac_ba_session_init(stad, *tid, 0, DOT11_BLOCK_ACK_TIMEOUT_DISABLED);

    return MMWLAN_SUCCESS;
}

static enum mmwlan_status umac_ate_sta_reconnect(struct umac_data *umacd)
{
    return umac_connection_reassoc(umacd);
}


static enum mmwlan_status umac_ate_exec_fw_command(uint8_t *command,
                                                   uint8_t *response,
                                                   uint32_t *response_len)
{
    enum mmwlan_status status;
    struct morse_cmd_resp *resp = (struct morse_cmd_resp *)response;

    int ret = mmdrv_execute_command(command, response, response_len);
    if (ret == 0 && resp != NULL)
    {
        if (resp->status != 0)
        {
            ret = (int)(le32toh(resp->status));
        }
    }

    switch (ret)
    {
        case 0:
            status = MMWLAN_SUCCESS;
            break;

        case -ENOMEM:
            status = MMWLAN_NO_MEM;
            break;

        case -ENODEV:
            status = MMWLAN_UNAVAILABLE;
            break;

        case -EINVAL:
            status = MMWLAN_INVALID_ARGUMENT;
            break;

        case -ETIMEDOUT:
            status = MMWLAN_TIMED_OUT;
            break;

        default:
            status = MMWLAN_ERROR;
            break;
    }

    return status;
}

enum mmwlan_status umac_ate_execute_command(struct umac_data *umacd,
                                            uint8_t *command,
                                            uint32_t command_len,
                                            uint8_t *response,
                                            uint32_t *response_len)
{

    struct morse_cmd_header *hdr = (struct morse_cmd_header *)command;
    struct morse_cmd_resp *resp = (struct morse_cmd_resp *)response;
    uint32_t response_max_data_len = 0;

    if (command_len < sizeof(*hdr))
    {
        MMLOG_ERR("Command too short\n");
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (command_len < le16toh(hdr->len + sizeof(*hdr)))
    {
        MMLOG_ERR("Command too short\n");
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (response_len != NULL)
    {
        if (*response_len < sizeof(*resp))
        {
            MMLOG_ERR("Response buf too short\n");
            return MMWLAN_INVALID_ARGUMENT;
        }
        response_max_data_len = *response_len - sizeof(*resp);

        resp->hdr.message_id = hdr->message_id;
        resp->hdr.vif_id = hdr->vif_id;
        resp->hdr.host_id = hdr->host_id;
    }

    switch (le16toh(hdr->message_id))
    {
        case MORSE_CMD_ID_IOT_CONFIGURE_INTEROP:
            return umac_ate_configure_interop(umacd, command, response, response_len);

        case MORSE_CMD_ID_IOT_SEND_ADDBA:
            return umac_ate_sta_send_addba(umacd, command);

        case MORSE_CMD_ID_IOT_STA_REASSOC:
            return umac_ate_sta_reconnect(umacd);

        case MORSE_CMD_ID_IOT_DUMP_STATS:
            umac_stats_dump(umacd);
            return MMWLAN_SUCCESS;

        case MORSE_CMD_ID_IOT_READ_STATS:
        {
            int ret = umac_stats_serialise(umacd, resp->data, response_max_data_len);
            if (ret < 0)
            {
                return MMWLAN_NO_MEM;
            }
            else
            {
                resp->hdr.len = ret;
                if (response_len != NULL)
                {
                    *response_len = sizeof(*resp) + ret;
                }
                return MMWLAN_SUCCESS;
            }
        }

        default:
            break;
    }

    enum mmwlan_status status = umac_ate_exec_fw_command(command, response, response_len);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    return MMWLAN_SUCCESS;
}

static bool copy_key_info(struct umac_data *umacd, uint8_t key_id, struct mmwlan_key_info *key_info)
{
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        return false;
    }

    enum umac_key_type key_type = umac_keys_get_key_type(stad, key_id);
    switch (key_type)
    {
        case UMAC_KEY_TYPE_PAIRWISE:
            key_info->key_type = MMWLAN_KEY_TYPE_PAIRWISE;
            break;

        case UMAC_KEY_TYPE_GROUP:
            key_info->key_type = MMWLAN_KEY_TYPE_GROUP;
            break;

        case UMAC_KEY_TYPE_IGTK:
            key_info->key_type = MMWLAN_KEY_TYPE_IGTK;
            break;

        default:
            return false;
    }

    key_info->key_id = key_id;

    key_info->key_len = umac_keys_get_key_len(stad, key_id);
    MMOSAL_ASSERT(key_info->key_len <= MMWLAN_MAX_KEYLEN);

    const uint8_t *key_data = umac_keys_get_key_data(stad, key_id);
    memcpy(key_info->key_data, key_data, key_info->key_len);

    return true;
}

MM_STATIC_ASSERT(MMWLAN_MAX_KEYLEN >= UMAC_CONNECTION_KEY_MAX_LEN, "MMWLAN_MAX_KEYLEN too short");

enum mmwlan_status umac_ate_get_key_info(struct umac_data *umacd,
                                         struct mmwlan_key_info *key_info,
                                         uint32_t *key_info_count)
{
    uint32_t ii;
    uint32_t num_keys_copied = 0;
    uint8_t max_key_info_count = *key_info_count;
    if (*key_info_count > MMWLAN_MAX_KEYS)
    {
        max_key_info_count = MMWLAN_MAX_KEYS;
    }

    for (ii = 0; ii < max_key_info_count; ii++)
    {
        bool key_copied = copy_key_info(umacd, ii, &key_info[num_keys_copied]);
        if (key_copied)
        {
            num_keys_copied++;
        }
    }

    *key_info_count = num_keys_copied;
    return MMWLAN_SUCCESS;
}
