/***************************************************************************/ /**
 * @file sli_buffer_manager.h
 * @brief Stub for missing sli_buffer_manager component in vendored SDK drop.
 ******************************************************************************/
#ifndef SLI_BUFFER_MANAGER_H
#define SLI_BUFFER_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include "sl_wifi_types.h"

typedef sl_wifi_buffer_t sli_buffer_t;

void sli_buffer_manager_free_buffer(sli_buffer_t *buffer);

#endif /* SLI_BUFFER_MANAGER_H */
