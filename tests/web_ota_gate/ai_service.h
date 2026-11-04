#ifndef OTA_GATE_STUB_AI_SERVICE_H
#define OTA_GATE_STUB_AI_SERVICE_H
#include "aicam_types.h"
aicam_result_t ai_pipeline_start(void);
aicam_result_t ai_pipeline_stop(void);
aicam_bool_t ai_pipeline_is_running(void);
#endif
