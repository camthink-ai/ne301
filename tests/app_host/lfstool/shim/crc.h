#ifndef HOST_SHIM_CRC_H
#define HOST_SHIM_CRC_H
#include <stdint.h>
#include <stddef.h>
uint32_t CRC_Accumulate(uint32_t crc, const uint32_t *buffer, size_t size);
#endif
