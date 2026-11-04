#ifndef SHIM_AI_SERVICE_H
#define SHIM_AI_SERVICE_H
#include "aicam_types.h"
aicam_result_t ai_pipeline_start(void);
aicam_result_t ai_pipeline_stop(void);
aicam_result_t ai_set_confidence_threshold(uint32_t threshold);
uint32_t ai_get_confidence_threshold(void);
#endif
