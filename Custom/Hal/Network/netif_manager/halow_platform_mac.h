#ifndef HALOW_PLATFORM_MAC_H
#define HALOW_PLATFORM_MAC_H

#include <stdint.h>

/**
 * Select MAC returned by mmhal_read_mac_addr() for Morse UMAC (before STA enable).
 * Pass NULL to use chip-UID derived HaLow MAC again.
 */
void mmhal_wlan_use_platform_mac(const uint8_t *mac);

#endif /* HALOW_PLATFORM_MAC_H */
