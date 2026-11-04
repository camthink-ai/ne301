#ifndef SHIM_WIFI_H
#define SHIM_WIFI_H
#include <stddef.h>
int wifi_get_flash_version(char *version, size_t size);
int wifi_get_running_version(char *version, size_t size);
#endif
