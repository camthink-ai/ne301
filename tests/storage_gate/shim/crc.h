#ifndef HOST_SHIM_CRC_H
#define HOST_SHIM_CRC_H
#include <stdint.h>
#include <stddef.h>
static inline uint32_t CRC_Accumulate(uint32_t seed, uint32_t *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = seed ^ 0xFFFFFFFFU;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}
#endif
