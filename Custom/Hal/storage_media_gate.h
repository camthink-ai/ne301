#ifndef _STORAGE_MEDIA_GATE_H_
#define _STORAGE_MEDIA_GATE_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MEDIA_GATE_FACTORY   0
#define STORAGE_MEDIA_GATE_USER      1
#define STORAGE_MEDIA_GATE_PART_MAX  2

#define STORAGE_MEDIA_GATE_REGION_MAX 2

typedef enum {
    STORAGE_MEDIA_UNTRUSTED = 0,
    STORAGE_MEDIA_BLANK = 1,
    STORAGE_MEDIA_HEALTHY = 2,
    STORAGE_MEDIA_STARTUP = 3,
    STORAGE_MEDIA_DEGRADED = 4
} storage_media_state_t;

typedef int (*storage_media_read_fn)(uint32_t offset, void *data, size_t len);

typedef struct {
    uint32_t offset;
    size_t size;
    int partition;
} storage_media_region_t;

void storage_media_gate_reset(void);
void storage_media_gate_set_state(int partition, storage_media_state_t state);
storage_media_state_t storage_media_gate_state(int partition);
int storage_media_gate_writable(int partition);

void storage_media_gate_register_region(uint32_t offset, size_t size, int partition);
const storage_media_region_t *storage_media_gate_region(int index);
int storage_media_gate_range_writable(uint32_t offset, size_t size);

int storage_media_blank_probe(storage_media_read_fn read, uint32_t base, size_t size,
                              uint8_t erase_value, int *is_blank);

#ifdef __cplusplus
}
#endif

#endif
