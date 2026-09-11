/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>

#include "umac_keys.h"
#include "umac_keys_data.h"
#include "connection_keys.h"
#include "mmdrv.h"
#include "mmlog.h"

void umac_keys_init(struct umac_sta_data *stad)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    connection_keys_init(&sta_data->keys);
}


static enum mmwlan_status umac_keys_mmdrv_install_key(uint16_t vif_id,
                                                      uint16_t aid,
                                                      struct umac_key *key)
{
    if ((key->key_type == UMAC_KEY_TYPE_PAIRWISE) || (key->key_type == UMAC_KEY_TYPE_GROUP))
    {
        struct mmdrv_key_conf key_conf = {
            .is_pairwise = (key->key_type == UMAC_KEY_TYPE_PAIRWISE) ? true : false,
            .key_idx = key->key_id,
            .length = key->key_len,
            .tx_pn = key->tx_seq
        };
        memcpy(key_conf.key, key->key_data, key->key_len);

        MMLOG_DBG("Installing key (type=%u)\n", key->key_type);

        int ret = mmdrv_install_key(vif_id, aid, &key_conf);
        if (ret != 0)
        {
            return MMWLAN_ERROR;
        }
    }
    else
    {
        MMLOG_DBG("Hardware decrypt not supported for key (id: %u, type: %u).\n",
                  key->key_id,
                  key->key_type);
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_keys_install_key(struct umac_sta_data *stad,
                                         uint16_t vif_id,
                                         struct umac_key *key)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    uint16_t aid = umac_sta_data_get_aid(stad);

    if (!connection_keys_install_key(&sta_data->keys, key))
    {
        MMLOG_WRN("Failed to install key %u onto keychain.\n", key->key_id);
        return MMWLAN_ERROR;
    }

    MMLOG_DBG("Installing key %u\n", key->key_id);

    return umac_keys_mmdrv_install_key(vif_id, aid, key);
}

enum mmwlan_status umac_keys_uninstall_key(struct umac_sta_data *stad,
                                           uint16_t vif_id,
                                           uint8_t key_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    uint16_t aid = umac_sta_data_get_aid(stad);

    if (!connection_keys_uninstall_key(&sta_data->keys, key_id))
    {
        MMLOG_DBG("No key %u to uninstall from keychain.\n", key_id);
        return MMWLAN_SUCCESS;
    }

    enum umac_key_type key_type = connection_keys_get_key_type(&sta_data->keys, key_id);

    if ((key_type == UMAC_KEY_TYPE_PAIRWISE) || (key_type == UMAC_KEY_TYPE_GROUP))
    {
        int ret = mmdrv_disable_key(vif_id, aid, key_id, (key_type == UMAC_KEY_TYPE_PAIRWISE));
        if (ret != 0)
        {
            MMLOG_DBG("Failed to disable key id: %d\n", key_id);
            return MMWLAN_ERROR;
        }
    }

    MMLOG_DBG("Successfully uninstalled key %u\n", key_id);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_keys_reinstall_keys(struct umac_sta_data *stad, uint16_t vif_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    uint16_t aid = umac_sta_data_get_aid(stad);

    for (unsigned ii = 0; ii < countof(sta_data->keys.keys); ii++)
    {
        struct umac_key *key = sta_data->keys.keys[ii];
        if (key != NULL)
        {
            enum mmwlan_status status = umac_keys_mmdrv_install_key(vif_id, aid, key);
            if (status != MMWLAN_SUCCESS)
            {
                MMLOG_ERR("Failed to reinstall key %u\n", key->key_id);
                return status;
            }
        }
    }
    return MMWLAN_SUCCESS;
}

enum umac_key_type umac_keys_get_key_type(struct umac_sta_data *stad, uint8_t key_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    return connection_keys_get_key_type(&sta_data->keys, key_id);
}

enum mmwlan_status umac_keys_check_and_update_rx_replay(struct umac_sta_data *stad,
                                                        uint8_t key_id,
                                                        uint64_t packet_number,
                                                        enum umac_key_rx_counter_space space)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    return connection_keys_check_and_update_rx_replay(&sta_data->keys,
                                                      key_id,
                                                      packet_number,
                                                      space);
}

size_t umac_keys_get_key_len(struct umac_sta_data *stad, uint8_t key_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    return connection_keys_get_key_len(&sta_data->keys, key_id);
}

const uint8_t *umac_keys_get_key_data(struct umac_sta_data *stad, uint8_t key_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    return connection_keys_get_key_data(&sta_data->keys, key_id);
}

int umac_keys_get_active_key_id(struct umac_sta_data *stad, enum umac_key_type key_type)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    return connection_keys_get_active_key_id(&sta_data->keys, key_type);
}

void umac_keys_increment_tx_seq(struct umac_sta_data *stad, uint8_t key_id)
{
    struct umac_keys_sta_data *sta_data = umac_sta_data_get_keys(stad);
    connection_keys_increment_tx_seq(&sta_data->keys, key_id);
}
