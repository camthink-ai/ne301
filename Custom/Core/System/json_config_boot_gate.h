#ifndef _JSON_CONFIG_BOOT_GATE_H_
#define _JSON_CONFIG_BOOT_GATE_H_

#include <stdint.h>
#include "storage_media_gate.h"
#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_CONFIG_KEY_PRESENT = 0,
    JSON_CONFIG_KEY_MISSING = 1,
    JSON_CONFIG_KEY_UNKNOWN = 2
} json_config_key_status_t;

json_config_key_status_t json_config_boot_gate_key_status(aicam_result_t read_result);
int json_config_boot_gate_first_boot(storage_media_state_t user_state);
int json_config_boot_gate_backfill_allowed(json_config_key_status_t status);
int json_config_boot_gate_credential_persist_default_allowed(storage_media_state_t user_state,
                                                             json_config_key_status_t new_key);
int json_config_boot_gate_credential_migrate_allowed(json_config_key_status_t new_key,
                                                     json_config_key_status_t old_key);
int json_config_boot_gate_credential_trusted(json_config_key_status_t new_key,
                                             json_config_key_status_t old_key,
                                             storage_media_state_t user_state);
int json_config_boot_gate_credential_trusted_persisted(json_config_key_status_t new_key,
                                                       json_config_key_status_t old_key,
                                                       storage_media_state_t user_state,
                                                       aicam_result_t persist_result);
int json_config_nvs_key_is_sensitive(const char *key);

#ifdef __cplusplus
}
#endif

#endif
