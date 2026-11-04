#include "storage_media_gate.h"

#define STORAGE_MEDIA_BLANK_SCRATCH_SIZE 256U

typedef struct {
    uint8_t state;
    uint8_t state_inv;
} storage_media_gate_entry_t;

static storage_media_gate_entry_t g_media_gate[STORAGE_MEDIA_GATE_PART_MAX];
static storage_media_region_t g_media_regions[STORAGE_MEDIA_GATE_REGION_MAX];
static uint8_t g_media_region_count;

static storage_media_gate_entry_t *media_gate_entry(int partition)
{
    if (partition < 0 || partition >= STORAGE_MEDIA_GATE_PART_MAX) {
        return NULL;
    }
    return &g_media_gate[partition];
}

void storage_media_gate_reset(void)
{
    for (int i = 0; i < STORAGE_MEDIA_GATE_PART_MAX; i++) {
        g_media_gate[i].state = (uint8_t)STORAGE_MEDIA_UNTRUSTED;
        g_media_gate[i].state_inv = (uint8_t)~(uint8_t)STORAGE_MEDIA_UNTRUSTED;
    }
    for (int i = 0; i < STORAGE_MEDIA_GATE_REGION_MAX; i++) {
        g_media_regions[i].offset = 0;
        g_media_regions[i].size = 0;
        g_media_regions[i].partition = -1;
    }
    g_media_region_count = 0;
}

static int media_state_valid(storage_media_state_t state)
{
    switch (state) {
    case STORAGE_MEDIA_UNTRUSTED:
    case STORAGE_MEDIA_BLANK:
    case STORAGE_MEDIA_HEALTHY:
    case STORAGE_MEDIA_STARTUP:
    case STORAGE_MEDIA_DEGRADED:
        return 1;
    default:
        return 0;
    }
}

void storage_media_gate_set_state(int partition, storage_media_state_t state)
{
    storage_media_gate_entry_t *e = media_gate_entry(partition);
    if (!e) {
        return;
    }
    if (!media_state_valid(state)) {
        state = STORAGE_MEDIA_UNTRUSTED;
    }
    e->state = (uint8_t)state;
    e->state_inv = (uint8_t)~(uint8_t)state;
}

storage_media_state_t storage_media_gate_state(int partition)
{
    storage_media_gate_entry_t *e = media_gate_entry(partition);
    if (!e) {
        return STORAGE_MEDIA_UNTRUSTED;
    }
    if (e->state != (uint8_t)~e->state_inv) {
        return STORAGE_MEDIA_UNTRUSTED;
    }
    if (!media_state_valid((storage_media_state_t)e->state)) {
        return STORAGE_MEDIA_UNTRUSTED;
    }
    return (storage_media_state_t)e->state;
}

int storage_media_gate_writable(int partition)
{
    storage_media_state_t state = storage_media_gate_state(partition);
    return (state == STORAGE_MEDIA_BLANK || state == STORAGE_MEDIA_HEALTHY) ? 1 : 0;
}

void storage_media_gate_register_region(uint32_t offset, size_t size, int partition)
{
    if (g_media_region_count >= STORAGE_MEDIA_GATE_REGION_MAX) {
        return;
    }
    if (partition < 0 || partition >= STORAGE_MEDIA_GATE_PART_MAX || size == 0U) {
        return;
    }
    g_media_regions[g_media_region_count].offset = offset;
    g_media_regions[g_media_region_count].size = size;
    g_media_regions[g_media_region_count].partition = partition;
    g_media_region_count++;
}

const storage_media_region_t *storage_media_gate_region(int index)
{
    if (index < 0 || index >= STORAGE_MEDIA_GATE_REGION_MAX) {
        return NULL;
    }
    if (index >= (int)g_media_region_count) {
        return NULL;
    }
    return &g_media_regions[index];
}

int storage_media_gate_range_writable(uint32_t offset, size_t size)
{
    if (size == 0U) {
        return 1;
    }
    for (int i = 0; i < (int)g_media_region_count; i++) {
        const storage_media_region_t *r = &g_media_regions[i];
        uint32_t region_end = r->offset + (uint32_t)r->size;
        uint32_t range_end = offset + (uint32_t)size;
        if (offset < region_end && r->offset < range_end) {
            storage_media_state_t state = storage_media_gate_state(r->partition);
            if (state == STORAGE_MEDIA_UNTRUSTED || state == STORAGE_MEDIA_DEGRADED) {
                return 0;
            }
        }
    }
    return 1;
}

int storage_media_blank_probe(storage_media_read_fn read, uint32_t base, size_t size,
                              uint8_t erase_value, int *is_blank)
{
    static uint8_t scratch[STORAGE_MEDIA_BLANK_SCRATCH_SIZE];
    size_t pos = 0U;

    if (!read || !is_blank || size == 0U) {
        if (is_blank) {
            *is_blank = 0;
        }
        return -1;
    }

    *is_blank = 1;
    while (pos < size) {
        size_t remain = size - pos;
        size_t want = (remain < sizeof(scratch)) ? remain : sizeof(scratch);
        int rc = read(base + (uint32_t)pos, scratch, want);
        if (rc != 0) {
            *is_blank = 0;
            return -2;
        }
        for (size_t i = 0U; i < want; i++) {
            if (scratch[i] != erase_value) {
                *is_blank = 0;
                break;
            }
        }
        pos += want;
    }
    return 0;
}
