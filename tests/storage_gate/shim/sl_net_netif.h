#ifndef SHIM_SL_NET_NETIF_H
#define SHIM_SL_NET_NETIF_H

#include <stddef.h>

#define NETIF_WIFI_COUNTRY_CODE_LEN 8U

int sl_net_wifi_region_canonicalize(const char *input, char *canonical, size_t canonical_size);

#endif
