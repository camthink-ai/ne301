/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */


#include <endian.h>

#include "umac_supp_shim_private.h"

#include "umac/ies/mmie.h"
#include "umac/keys/umac_keys.h"


#define AAD_LENGTH 20

#define MIC_LENGTH            8
#define AES128_BLOCK_SIZE     16
#define MAX_SUPPORTED_KEY_LEN 16


static void bip_aad(uint8_t *aad, const struct dot11_hdr *header)
{

    memcpy(aad, &header->frame_control, sizeof(header->frame_control));
    memcpy(aad + sizeof(header->frame_control), header->addr1, 3 * DOT11_MAC_ADDR_LEN);


    struct dot11_hdr *aad_hdr = (struct dot11_hdr *)aad;
    aad_hdr->frame_control &=
        ~htole16(DOT11_MASK_FC_RETRY | DOT11_MASK_FC_POWER_MGMT | DOT11_MASK_FC_MORE_DATA);
}


static int bip_generate_mic(const uint8_t *key,
                            const struct dot11_hdr *header,
                            const uint8_t *data,
                            size_t data_len,
                            uint8_t *mic)
{
    uint8_t *buf = (uint8_t *)mmosal_calloc(AAD_LENGTH + data_len, sizeof(uint8_t));
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    bip_aad(buf, header);
    memcpy(buf + AAD_LENGTH, data, data_len);
    memset(buf + data_len + AAD_LENGTH - MIC_LENGTH, 0, MIC_LENGTH);


    int ret = omac1_aes_128(key, buf, data_len + AAD_LENGTH, mic);
    if (ret < 0)
    {
        mmosal_free(buf);
        MMLOG_WRN("Encryption error %d\n", ret);
        return -1;
    }
    mmosal_free(buf);

    return 0;
}

static uint64_t parse_mmie_packet_number(const uint8_t *array)
{
    return (((uint64_t)(*(array)) << 0)) |
           (((uint64_t)(*(array + 1)) << 8)) |
           (((uint64_t)(*(array + 2)) << 16)) |
           (((uint64_t)(*(array + 3)) << 24)) |
           (((uint64_t)(*(array + 4)) << 32)) |
           (((uint64_t)(*(array + 5)) << 40));
}

static void write_mmie_packet_number(uint8_t *array, uint64_t packet_num)
{
    array[0] = packet_num;
    array[1] = packet_num >> 8;
    array[2] = packet_num >> 16;
    array[3] = packet_num >> 24;
    array[4] = packet_num >> 32;
    array[5] = packet_num >> 40;
}

bool bip_is_valid(struct umac_sta_data *stad,
                  const struct dot11_hdr *header,
                  const uint8_t *data,
                  size_t data_len)
{
    uint8_t mic[AES128_BLOCK_SIZE];

    MMLOG_VRB("Validate BIP MIC\n");

    const struct dot11_ie_mmie *mmie = ie_mmie_find(data, data_len);
    if (mmie == NULL)
    {
        MMLOG_DBG("Failed to find BIP\n");
        return false;
    }

    if (umac_keys_get_key_type(stad, le16toh(mmie->key_id)) != UMAC_KEY_TYPE_IGTK)
    {
        MMLOG_INF("Unsupported key type for BIP.");
        return false;
    }

    if (umac_keys_get_key_len(stad, le16toh(mmie->key_id)) > MAX_SUPPORTED_KEY_LEN)
    {
        MMLOG_WRN("Unsupported Key length given.");
        return false;
    }

    if (bip_generate_mic(umac_keys_get_key_data(stad, le16toh(mmie->key_id)),
                         header,
                         data,
                         data_len,
                         mic))
    {
        MMLOG_DBG("Failed to generate BIP MIC\n");
        return false;
    }


    if (memcmp(mmie->mic, mic, MIC_LENGTH))
    {
        MMLOG_DBG("Invalid MIC received\n");

        return false;
    }
    MMLOG_DBG("Valid MIC received\n");

    uint64_t packet_number = parse_mmie_packet_number(mmie->sequence_number);
    enum mmwlan_status status =
        umac_keys_check_and_update_rx_replay(stad,
                                             mmie->key_id,
                                             packet_number,
                                             UMAC_KEY_RX_COUNTER_SPACE_DEFAULT);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_DBG("BIP Invalid RX replay counter\n");
        return false;
    }
    return true;
}

bool bip_generate_mmie(struct umac_sta_data *stad, uint8_t *data, size_t data_len)
{
    const struct dot11_hdr *header = (const struct dot11_hdr *)data;
    struct dot11_ie_mmie *mmie = (struct dot11_ie_mmie *)(data + data_len - sizeof(*mmie));
    if (data_len < sizeof(*header) + sizeof(*mmie))
    {
        MMLOG_WRN("Data too short\n");
        return false;
    }
    data += sizeof(*header);
    data_len -= sizeof(*header);

    memset(mmie, 0, sizeof(*mmie));

    int key_id = umac_keys_get_active_key_id(stad, UMAC_KEY_TYPE_IGTK);
    if (key_id < 0)
    {
        MMLOG_INF("IGTK key not available for BIP.\n");
        return false;
    }

    MMLOG_DBG("Generate MMIE: key_id=%d, data_len (exc hdr): %u\n", key_id, data_len);
    MMOSAL_DEV_ASSERT(key_id <= UINT8_MAX);

    mmie->key_id = htole16(key_id);

    size_t key_len = umac_keys_get_key_len(stad, key_id);
    if (key_len > MAX_SUPPORTED_KEY_LEN)
    {
        MMLOG_WRN("Unsupported Key length given (%u > %u)\n", key_len, MAX_SUPPORTED_KEY_LEN);
        return false;
    }

    uint8_t mic[AES128_BLOCK_SIZE];
    const uint8_t *key_data = umac_keys_get_key_data(stad, key_id);
    if (key_data == NULL)
    {
        MMLOG_WRN("Failed to get key data for key_id %u\n", key_id);
        return false;
    }

    umac_keys_increment_tx_seq(stad, key_id);
    uint64_t tx_seq = umac_keys_get_tx_seq(stad, key_id);
    write_mmie_packet_number(mmie->sequence_number, tx_seq);

    mmie->header.element_id = DOT11_IE_MANAGEMENT_MIC;
    mmie->header.length = sizeof(*mmie) - sizeof(mmie->header);

    int ret = bip_generate_mic(key_data, header, data, data_len, mic);
    if (ret != 0)
    {
        MMLOG_WRN("Failed to generate BIP MIC.\n");
        return false;
    }

    memcpy(mmie->mic, mic, MIC_LENGTH);

    return true;
}
