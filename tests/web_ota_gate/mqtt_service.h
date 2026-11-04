#ifndef OTA_GATE_STUB_MQTT_SERVICE_H
#define OTA_GATE_STUB_MQTT_SERVICE_H
#include "aicam_types.h"
aicam_bool_t mqtt_service_is_running(void);
aicam_result_t mqtt_service_stop(void);
void mqtt_service_start(void);
#endif
