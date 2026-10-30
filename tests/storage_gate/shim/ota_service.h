#ifndef SHIM_OTA_SERVICE_H
#define SHIM_OTA_SERVICE_H
#include "upgrade_manager.h"
SystemState *ota_get_system_state(void);
const slot_info_t *ota_get_slot_info(FirmwareType type, int slot);
#endif
