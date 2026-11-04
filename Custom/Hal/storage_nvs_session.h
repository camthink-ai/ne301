#ifndef _STORAGE_NVS_SESSION_H_
#define _STORAGE_NVS_SESSION_H_

#include <stdint.h>
#include <stddef.h>
#include "nvs.h"
#include "storage_media_gate.h"

#ifndef NVS_SESSION_WRITE_BLOCK_SIZE
#define NVS_SESSION_WRITE_BLOCK_SIZE 4
#endif

#ifndef NVS_SESSION_ERASE_VALUE
#define NVS_SESSION_ERASE_VALUE 0xFF
#endif

typedef struct {
    nvs_flash_ops_t flash_ops;
    void *(*mutex_new)(void);
    void (*mutex_del)(void *mutex);
    void (*lock)(void *mutex);
    void (*unlock)(void *mutex);
} storage_nvs_session_io_t;

int storage_nvs_session_init(nvs_fs_t *nvs, uint32_t flash_offset, size_t sector_size,
                             size_t sector_count, int gate_partition,
                             const storage_nvs_session_io_t *io);

int storage_nvs_session_result_is_media_fault(int rc);
void storage_nvs_session_note_result(int gate_partition, int rc);

#endif
