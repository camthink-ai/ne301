#include <stdio.h>
#include <errno.h>
#include "storage_nvs_session.h"

int storage_nvs_session_init(nvs_fs_t *nvs, uint32_t flash_offset, size_t sector_size,
                             size_t sector_count, int gate_partition,
                             const storage_nvs_session_io_t *io)
{
    int ret;
    int is_blank = 0;

    if (!nvs || !io) {
        return -1;
    }

    nvs->offset = (int)flash_offset;
    nvs->ate_wra = flash_offset;
    nvs->data_wra = flash_offset + sizeof(struct nvs_ate);
    nvs->sector_size = sector_size;
    nvs->sector_count = sector_count;
    nvs->startup_flags = 0U;
    nvs->ready = false;
    nvs->flash_parameters.write_block_size = NVS_SESSION_WRITE_BLOCK_SIZE;
    nvs->flash_parameters.erase_value = NVS_SESSION_ERASE_VALUE;

    nvs->flash_ops = io->flash_ops;
    nvs->mutex_ops.lock = io->lock;
    nvs->mutex_ops.unlock = io->unlock;
    nvs->mutex = (io->mutex_new != NULL) ? io->mutex_new() : NULL;

    ret = nvs_blank_check(nvs, &is_blank);
    if (ret != 0) {
        printf("nvs blank probe read error(part=%d, ret=%d), media state untrusted, bytes preserved\r\n", gate_partition, ret);
        storage_media_gate_set_state(gate_partition, STORAGE_MEDIA_UNTRUSTED);
        if (nvs->mutex != NULL && io->mutex_del != NULL) {
            io->mutex_del(nvs->mutex);
        }
        nvs->mutex = NULL;
        return ret;
    }

    storage_media_gate_set_state(gate_partition, STORAGE_MEDIA_STARTUP);

    ret = nvs_init(nvs);
    if (ret != 0) {
        printf("nvs init failed(part=%d, ret=%d), media state untrusted, bytes preserved, no erase no reset\r\n", gate_partition, ret);
        storage_media_gate_set_state(gate_partition, STORAGE_MEDIA_UNTRUSTED);
        if (nvs->mutex != NULL && io->mutex_del != NULL) {
            io->mutex_del(nvs->mutex);
        }
        nvs->mutex = NULL;
        return ret;
    }

    if ((nvs->startup_flags & NVS_STARTUP_TEAR_SEEN) != 0U ||
        (nvs->startup_flags & NVS_STARTUP_RECOVERY_DEFERRED) != 0U) {
        printf("nvs startup tear or deferred recovery evidence(part=%d, flags=0x%02x), destructive writes refused this session, bytes preserved\r\n",
               gate_partition, nvs->startup_flags);
        storage_media_gate_set_state(gate_partition, STORAGE_MEDIA_DEGRADED);
        return 0;
    }

    storage_media_state_t state = is_blank ? STORAGE_MEDIA_BLANK : STORAGE_MEDIA_HEALTHY;
    storage_media_gate_set_state(gate_partition, state);
    printf("nvs media state(part=%d, state=%d, blank=%d)\r\n", gate_partition, (int)state, is_blank);
    return 0;
}

int storage_nvs_session_result_is_media_fault(int rc)
{
    if (rc >= 0) {
        return 0;
    }
    if (rc == -EINVAL || rc == -EACCES || rc == -ENOSPC) {
        return 0;
    }
    return 1;
}

void storage_nvs_session_note_result(int gate_partition, int rc)
{
    if (gate_partition < 0 || gate_partition >= STORAGE_MEDIA_GATE_PART_MAX) {
        return;
    }
    if (!storage_nvs_session_result_is_media_fault(rc)) {
        return;
    }
    printf("nvs media fault(part=%d, rc=%d), session downgraded to untrusted, destructive writes refused, bytes preserved\r\n",
           gate_partition, rc);
    storage_media_gate_set_state(gate_partition, STORAGE_MEDIA_UNTRUSTED);
}
