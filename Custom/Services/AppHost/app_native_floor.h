#ifndef APP_NATIVE_VNEXT_FLOOR_H
#define APP_NATIVE_VNEXT_FLOOR_H

#include "app_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NATIVE_VNEXT_FLOOR_KEY "app_native_vnext_floor"
#define APP_NATIVE_VNEXT_EPOCH_KEY "app_native_vnext_epoch"
#define APP_NATIVE_VNEXT_MAJOR 4u

app_install_result_t app_native_vnext_floor_epoch_advance(
    const app_install_io_t *io, uint32_t *out_epoch);

app_install_result_t app_native_vnext_floor_validate(const app_install_io_t *io);

app_install_result_t app_native_vnext_floor_check(const app_install_io_t *io,
                                                   const app_install_record_t *record,
                                                   uint32_t container_major);

app_install_result_t app_native_vnext_floor_raise(const app_install_io_t *io,
                                                   const app_install_record_t *record,
                                                   uint32_t session_generation,
                                                   uint32_t boot_id,
                                                   uint32_t request_id,
                                                   uint32_t config_generation,
                                                   uint32_t expected_state_revision);

app_install_result_t app_native_vnext_floor_verify(
    const app_install_io_t *io,
    const app_install_record_t *record,
    uint32_t session_generation,
    uint32_t boot_id,
    uint32_t request_id,
    uint32_t config_generation,
    uint32_t expected_state_revision);

app_install_result_t app_native_vnext_floor_retire_prepare(
    const app_install_io_t *io,
    const app_install_record_t *record);

app_install_result_t app_native_vnext_floor_retire_finish(
    const app_install_io_t *io,
    const app_install_record_t *record);

app_install_result_t app_native_vnext_floor_recover(const app_install_io_t *io);

#ifdef __cplusplus
}
#endif

#endif
