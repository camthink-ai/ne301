#include <stdint.h>
#include <string.h>

#include "chip_id_mac.h"
#include "stm32n6xx_hal.h"

static void chip_id_read_uid(uint32_t chip_id[3])
{
    chip_id[0] = HAL_GetUIDw0();
    chip_id[1] = HAL_GetUIDw1();
    chip_id[2] = HAL_GetUIDw2();
}

static void chip_id_expand_bytes(const uint32_t chip_id[3], uint8_t chip_id_bytes[12])
{
    int i;

    memset(chip_id_bytes, 0, 12U);
    for (i = 0; i < 3; i++) {
        chip_id_bytes[4 * i] = (uint8_t)(chip_id[i] & 0xFFU);
        chip_id_bytes[4 * i + 1] = (uint8_t)((chip_id[i] >> 8) & 0xFFU);
        chip_id_bytes[4 * i + 2] = (uint8_t)((chip_id[i] >> 16) & 0xFFU);
        chip_id_bytes[4 * i + 3] = (uint8_t)((chip_id[i] >> 24) & 0xFFU);
    }
}

void netif_chip_id_get_mac(uint8_t *mac, netif_chip_mac_kind_t kind)
{
    uint32_t chip_id[3];
    uint8_t chip_id_bytes[12];
    uint32_t ui_mcu_id;
    uint8_t sum = 0;
    uint8_t xor_val = 0;
    int i;

    chip_id_read_uid(chip_id);
    chip_id_expand_bytes(chip_id, chip_id_bytes);

    ui_mcu_id = (chip_id[0] >> 1U) + (chip_id[1] >> 2U) + (chip_id[2] >> 3U);
    for (i = 0; i < 12; i++) {
        sum += chip_id_bytes[i];
        xor_val ^= chip_id_bytes[i];
    }

    switch (kind) {
    case NETIF_CHIP_MAC_W5500:
        mac[0] = (uint8_t)(ui_mcu_id & 0xFCU);
        mac[1] = (uint8_t)((ui_mcu_id >> 8) & 0xFFU);
        mac[2] = (uint8_t)((ui_mcu_id >> 16) & 0xFFU);
        mac[3] = (uint8_t)((ui_mcu_id >> 24) & 0xFFU);
        mac[4] = sum;
        mac[5] = xor_val;
        break;

    case NETIF_CHIP_MAC_HALOW:
        /* Locally administered unicast; bytes 1-5 differ from W5500 on same UID. */
        mac[0] = 0x02U;
        mac[1] = (uint8_t)((ui_mcu_id >> 8) & 0xFFU) ^ 0xC0U;
        mac[2] = (uint8_t)((ui_mcu_id >> 16) & 0xFFU) ^ 0x01U;
        mac[3] = (uint8_t)((ui_mcu_id >> 24) & 0xFFU) ^ 0x10U;
        mac[4] = sum;
        mac[5] = (uint8_t)(xor_val ^ 0xA5U);
        if ((mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0U) {
            mac[5] = 0x01U;
        }
        break;

    default:
        memset(mac, 0, 6U);
        break;
    }
}
