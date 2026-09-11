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
    bip_aad(buf, header);
    memcpy(buf + AAD_LENGTH, data, data_len);
    memset(buf + data_len + AAD_LENGTH - MIC_LENGTH, 0, MIC_LENGTH);


    if (omac1_aes_128(key, buf, data_len + AAD_LENGTH, mic) < 0)
    {
        mmosal_free(buf);
        MMLOG_DBG("encryption error\n");
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

bool bip_is_valid(struct umac_sta_data *stad,
                  const struct dot11_hdr *header,
                  const uint8_t *data,
                  size_t data_len)
{
    uint8_t mic[AES128_BLOCK_SIZE];

    const struct dot11_ie_mmie *mmie = ie_mmie_find(data, data_len);
    if (mmie == NULL)
    {
        return false;
    }

    if (umac_keys_get_key_type(stad, mmie->key_id) != UMAC_KEY_TYPE_IGTK)
    {
        MMLOG_INF("Unsupported key type for BIP.");
        return false;
    }

    if (umac_keys_get_key_len(stad, mmie->key_id) > MAX_SUPPORTED_KEY_LEN)
    {
        MMLOG_WRN("Unsupported Key length given.");
        return false;
    }

    if (bip_generate_mic(umac_keys_get_key_data(stad, mmie->key_id), header, data, data_len, mic))
    {
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

    return (status == MMWLAN_SUCCESS);
}
