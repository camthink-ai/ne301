#include <stdint.h>
#include <stddef.h>

#include "app_host.h"

extern uint8_t __app_host_ram_base__[];
extern uint8_t __app_host_ram_end__[];

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    if (base == NULL || size == NULL) {
        return AICAM_FALSE;
    }
    *base = (uint32_t)(uintptr_t)__app_host_ram_base__;
    *size = (uint32_t)(__app_host_ram_end__ - __app_host_ram_base__);
    return (*size > 0u) ? AICAM_TRUE : AICAM_FALSE;
}
