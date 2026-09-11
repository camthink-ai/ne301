/*
 * Source for UMAC key interface
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_keys.h"
#include "connection_keys.h"
#include "mmlog.h"

static struct umac_key *connection_keys_alloc(struct connection_keys_data *data)
{
    struct umac_key *key = NULL;


    int i;
    for (i = 0; i < UMAC_KEYS_KEYCHAIN_LEN; i++)
    {
        if (data->pool[i].key_type == UMAC_KEY_TYPE_BLANK)
        {
            key = &data->pool[i];
            break;
        }
    }

    return key;
}

static void connection_keys_free(struct connection_keys_data *data, uint8_t key_id)
{

    enum umac_key_type key_type = data->keys[key_id]->key_type;
    if (data->active_key_lookup[key_type] == key_id)
    {
        data->active_key_lookup[key_type] = -1;
    }

    memset(data->keys[key_id], 0, sizeof(*data->keys[key_id]));

    data->keys[key_id] = NULL;
}


static bool connection_keys_key_id_is_valid(uint8_t key_id)
{
    return (key_id < UMAC_KEYS_NUM_KEY_IDS);
}


static bool connection_keys_key_is_installed(struct connection_keys_data *data, uint8_t key_id)
{
    return (data->keys[key_id] != NULL);
}

static bool umac_keys_key_params_are_valid(struct umac_key *key)
{
    if (!connection_keys_key_id_is_valid(key->key_id))
    {
        MMLOG_DBG("Key ID out of range %u\n", key->key_id);
        return false;
    }

    if (key->key_type == UMAC_KEY_TYPE_BLANK)
    {
        MMLOG_DBG("Attempt to install blank key type\n");
        return false;
    }

    if (key->key_len > UMAC_CONNECTION_KEY_MAX_LEN)
    {
        MMLOG_DBG("Max key length exceeded %u\n", key->key_len);
        return false;
    }

    return true;
}

void connection_keys_init(struct connection_keys_data *data)
{
    memset(data, 0, sizeof(*data));
    for (int i = 0; i < UMAC_KEY_TYPE_NUM; i++)
    {
        data->active_key_lookup[i] = -1;
    }
}

bool connection_keys_install_key(struct connection_keys_data *data, struct umac_key *key)
{
    if (!umac_keys_key_params_are_valid(key))
    {
        MMLOG_WRN("Invalid key parameter given\n");
        return false;
    }

    struct umac_key *new_key = NULL;
    if (connection_keys_key_is_installed(data, key->key_id))
    {
        MMLOG_DBG("Re-installing key id %u\n", key->key_id);
        new_key = data->keys[key->key_id];
    }
    else
    {
        new_key = connection_keys_alloc(data);
        if (new_key == NULL)
        {
            MMLOG_DBG("No more space on keychain.\n");
            return false;
        }
    }

    MMOSAL_TASK_ENTER_CRITICAL();

    data->keys[key->key_id] = new_key;
    data->keys[key->key_id]->key_id = key->key_id;
    data->keys[key->key_id]->key_type = key->key_type;
    for (int i = 0; i < UMAC_KEY_RX_COUNTER_NUM; i++)
    {
        data->keys[key->key_id]->rx_seq[i] = key->rx_seq[i];
    }
    data->keys[key->key_id]->tx_seq = key->tx_seq;
    memcpy(data->keys[key->key_id]->key_data, key->key_data, key->key_len);
    data->keys[key->key_id]->key_len = key->key_len;

    data->active_key_lookup[key->key_type] = key->key_id;
    MMOSAL_TASK_EXIT_CRITICAL();

    return true;
}

bool connection_keys_uninstall_key(struct connection_keys_data *data, uint8_t key_id)
{
    if (!connection_keys_key_id_is_valid(key_id))
    {
        return false;
    }

    if (!connection_keys_key_is_installed(data, key_id))
    {
        return false;
    }

    MMOSAL_TASK_ENTER_CRITICAL();
    connection_keys_free(data, key_id);
    MMOSAL_TASK_EXIT_CRITICAL();
    return true;
}

enum umac_key_type connection_keys_get_key_type(struct connection_keys_data *data, uint8_t key_id)
{
    MMOSAL_ASSERT(connection_keys_key_id_is_valid(key_id));

    enum umac_key_type key_type = UMAC_KEY_TYPE_BLANK;

    MMOSAL_TASK_ENTER_CRITICAL();
    if (connection_keys_key_is_installed(data, key_id))
    {
        key_type = data->keys[key_id]->key_type;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    return key_type;
}

enum mmwlan_status connection_keys_check_and_update_rx_replay(struct connection_keys_data *data,
                                                              uint8_t key_id,
                                                              uint64_t packet_number,
                                                              enum umac_key_rx_counter_space space)
{
    MMOSAL_ASSERT(connection_keys_key_id_is_valid(key_id));

    enum mmwlan_status status = MMWLAN_ERROR;
    MMOSAL_TASK_ENTER_CRITICAL();
    if (connection_keys_key_is_installed(data, key_id))
    {

        if (packet_number > data->keys[key_id]->rx_seq[space])
        {

            status = MMWLAN_SUCCESS;
            data->keys[key_id]->rx_seq[space] = packet_number;
        }
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    return status;
}

size_t connection_keys_get_key_len(struct connection_keys_data *data, uint8_t key_id)
{
    MMOSAL_ASSERT(connection_keys_key_id_is_valid(key_id));

    size_t key_len = 0;

    MMOSAL_TASK_ENTER_CRITICAL();
    if (connection_keys_key_is_installed(data, key_id))
    {
        key_len = data->keys[key_id]->key_len;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    return key_len;
}

const uint8_t *connection_keys_get_key_data(struct connection_keys_data *data, uint8_t key_id)
{
    MMOSAL_ASSERT(connection_keys_key_id_is_valid(key_id));

    uint8_t *key_data = NULL;

    MMOSAL_TASK_ENTER_CRITICAL();
    if (connection_keys_key_is_installed(data, key_id))
    {
        key_data = data->keys[key_id]->key_data;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    return key_data;
}

static int umac_keys_get_active_key_id_protected(struct connection_keys_data *data,
                                                 enum umac_key_type key_type)
{
    return data->active_key_lookup[key_type];
}

int connection_keys_get_active_key_id(struct connection_keys_data *data,
                                      enum umac_key_type key_type)
{
    int key_id = -1;

    MMOSAL_TASK_ENTER_CRITICAL();
    key_id = umac_keys_get_active_key_id_protected(data, key_type);
    MMOSAL_TASK_EXIT_CRITICAL();

    return key_id;
}

void connection_keys_increment_tx_seq(struct connection_keys_data *data, uint8_t key_id)
{
    MMOSAL_ASSERT(connection_keys_key_id_is_valid(key_id));

    bool success = false;

    MMOSAL_TASK_ENTER_CRITICAL();
    if (connection_keys_key_is_installed(data, key_id))
    {

        data->keys[key_id]->tx_seq++;
        success = true;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    if (!success)
    {
        MMLOG_WRN("Unable to find active key %d\n", key_id);
    }
}

uint64_t connection_keys_get_tx_seq(struct connection_keys_data *data, enum umac_key_type key_type)
{
    bool success = false;
    uint64_t tx_seq = 0;

    MMOSAL_TASK_ENTER_CRITICAL();
    int key_id = umac_keys_get_active_key_id_protected(data, key_type);
    if (key_id >= 0)
    {

        tx_seq = data->keys[key_id]->tx_seq;
        success = true;
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    if (!success)
    {
        MMLOG_WRN("Unable to find active key for key type %u\n", key_type);
    }

    return tx_seq;
}
