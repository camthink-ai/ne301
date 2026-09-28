/**
 * @file    api_camera_module.h
 * @brief Camera Source API Module Header
 * @details Image source (native DCMIPP / USB UVC) selection, detection and
 *          per-source stream configuration. Reboot-apply semantics.
 */

#ifndef API_CAMERA_MODULE_H
#define API_CAMERA_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register camera source API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_camera_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_CAMERA_MODULE_H */
