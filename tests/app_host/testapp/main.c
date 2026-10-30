#include "app_host_abi.h"

int app_entry(const app_host_api_table_t *api, uint32_t abi_version)
{
    if (api == (void *)0) {
        return -1;
    }
    if (api->table_size < (uint32_t)sizeof(app_host_api_table_t)) {
        return -2;
    }
    if (abi_version != APP_HOST_ABI_VERSION) {
        return -3;
    }
    if (api->log == (void *)0 || api->tick_ms == (void *)0) {
        return -4;
    }
    api->log("ne301 apphost test app: abi v1 table ok");
    int tick = api->tick_ms();
    if (tick < 0) {
        return -5;
    }
    return 0x600D;
}
