#ifndef DEVSHIM_STM32N6XX_HAL_H
#define DEVSHIM_STM32N6XX_HAL_H

#include <stdint.h>

#define DCMIPP_P1PPCR_FORMAT_Pos (0UL)
#define DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1 (1U << DCMIPP_P1PPCR_FORMAT_Pos)
#define DCMIPP_PIXEL_PACKER_FORMAT_ARGB8888 (2U << DCMIPP_P1PPCR_FORMAT_Pos)

void SCB_CleanInvalidateDCache_by_Addr(void *addr, int32_t dsize);
void SCB_InvalidateICache_by_Addr(void *addr, int32_t isize);
void __DSB(void);
void __ISB(void);

#endif
