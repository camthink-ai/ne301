/**
 * @file api_capture_module.h
 * @brief Capture Settings API Module - capture-mode/storage/retry/schedule config + record listing
 */

#ifndef API_CAPTURE_MODULE_H
#define API_CAPTURE_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

aicam_result_t web_api_register_capture_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_CAPTURE_MODULE_H */
