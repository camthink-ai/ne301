#ifndef APP_HOST_RUN_H
#define APP_HOST_RUN_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "app_host_abi.h"
#include "app_host_validate.h"
#include "app_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_RUN_LOG_CAP 512u
#define APP_RUN_REASON_SLOT 48u

typedef enum {
    APP_RUN_OK = 0,
    APP_RUN_ERR_PARAM = -1,
    APP_RUN_ERR_NOT_INSTALLED = -2,
    APP_RUN_ERR_UNAVAILABLE = -3,
    APP_RUN_ERR_STORAGE_STATE_UNKNOWN = -4,
    APP_RUN_ERR_BUSY = -5,
    APP_RUN_ERR_LOAD_FAILED = -6,
    APP_RUN_ERR_RUNTIME_UNAVAILABLE = -7
} app_run_result_t;

typedef struct {
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
} app_run_admission_ctx_t;

typedef struct {
    app_run_result_t result;
    aicam_bool_t entry_returned;
    int entry_result;
    app_install_record_t record;
    char reason[APP_RUN_REASON_SLOT];
} app_run_outcome_t;

typedef struct {
    void *ctx;
    uint8_t *(*stage_base)(void *ctx, size_t *cap);
    int (*stage_commit)(void *ctx, size_t stage_cap, const app_host_image_info_t *info);
    int (*invoke_entry)(void *ctx, const app_host_api_table_t *api,
                        const app_host_image_info_t *info, int *raw);
    void (*scrub)(void *ctx);
    void (*log_sink)(void *ctx, const char *text);
    int (*tick_ms)(void *ctx);
} app_host_run_backend_t;

void app_host_run_init(const app_host_run_backend_t *backend);

app_host_check_t app_host_run_stage_load(uint8_t *stage, size_t stage_cap,
                                         size_t native_file_len,
                                         const app_host_load_policy_t *policy,
                                         app_host_image_info_t *out);

app_run_result_t app_host_run_installed(const app_install_io_t *io,
                                        app_run_outcome_t *out);

size_t app_host_run_take_log(char *buf, size_t cap);

aicam_bool_t app_host_run_log_truncated(void);

const char *app_host_run_result_str(app_run_result_t result);

const app_host_run_backend_t *app_host_run_device_backend(void);

#ifdef __cplusplus
}
#endif

#endif
