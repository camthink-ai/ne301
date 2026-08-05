#ifndef CHIP_ID_MAC_H
#define CHIP_ID_MAC_H

#include <stdint.h>

typedef enum {
    NETIF_CHIP_MAC_W5500 = 0,
    NETIF_CHIP_MAC_HALOW = 1,
} netif_chip_mac_kind_t;

/** Derive a unicast MAC from STM32 96-bit UID (HAL_GetUIDw*). */
void netif_chip_id_get_mac(uint8_t *mac, netif_chip_mac_kind_t kind);

#endif /* CHIP_ID_MAC_H */
