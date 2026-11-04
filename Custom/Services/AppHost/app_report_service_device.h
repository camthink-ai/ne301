#ifndef APP_REPORT_SERVICE_DEVICE_H
#define APP_REPORT_SERVICE_DEVICE_H

#include <stdint.h>

#include "aicam_types.h"
#include "app_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

aicam_result_t app_report_service_device_start(void);

aicam_result_t app_report_service_device_stop(void);

aicam_bool_t app_report_service_device_running(void);

aicam_result_t app_report_service_device_uninstall_prepare(
    const app_install_io_t *io);

void app_report_service_device_uninstall_finish(const app_install_io_t *io);

const app_uninstall_cleanup_ops_t *app_report_service_device_cleanup_ops(void);

void app_report_service_device_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif
