#ifndef OTA_GATE_STUB_WIFI_H
#define OTA_GATE_STUB_WIFI_H
#include <stddef.h>
int wifi_get_flash_version(char *version, size_t size);
int wifi_get_running_version(char *version, size_t size);
void wifi_mark_update_pending(void);
#endif
