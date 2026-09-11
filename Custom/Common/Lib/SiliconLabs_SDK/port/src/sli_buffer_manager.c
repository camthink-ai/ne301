/***************************************************************************/ /**
 * @file sli_buffer_manager.c
 * @brief Stub for missing sli_buffer_manager component in vendored SDK drop.
 ******************************************************************************/
#include "sli_buffer_manager.h"
#include "sli_wifi_memory_manager.h"

void sli_buffer_manager_free_buffer(sli_buffer_t *buffer)
{
  sl_wifi_buffer_t *host_buffer;

  if (buffer == NULL) {
    return;
  }

  host_buffer = (sl_wifi_buffer_t *)((uint8_t *)buffer - offsetof(sl_wifi_buffer_t, data));
  sli_wifi_memory_manager_free_buffer(host_buffer);
}
