#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include "fake_flash.h"
#include "nvs.h"
#include "storage_media_gate.h"
#include "storage_nvs_session.h"
#include "json_config_boot_gate.h"
#include "aicam_types.h"

#define NVS_SECTOR_SIZE 4096
#define NVS_SECTORS 4
#define NVS_TOTAL   (NVS_SECTOR_SIZE * NVS_SECTORS)
#define ATE_SIZE    32
#define CLOSE_SLOT  (NVS_SECTOR_SIZE - ATE_SIZE)
#define FIRST_ATE   (NVS_SECTOR_SIZE - 2 * ATE_SIZE)
#define SECOND_ATE  (NVS_SECTOR_SIZE - 3 * ATE_SIZE)
#define CRAFT_SECTORS 3
#define LFS_BLOCKS  32
#define LFS_TOTAL   (LFS_BLOCKS * 256)
#define MEM_SIZE    (NVS_TOTAL > LFS_TOTAL ? NVS_TOTAL : LFS_TOTAL)

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static const uint8_t crc8_table[16] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

static uint8_t craft_crc8(uint8_t val, const void *buf, size_t cnt)
{
    size_t i;
    const uint8_t *p = (const uint8_t *)buf;
    for (i = 0; i < cnt; i++) {
        val ^= p[i];
        val = (uint8_t)((val << 4) ^ crc8_table[val >> 4]);
        val = (uint8_t)((val << 4) ^ crc8_table[val >> 4]);
    }
    return val;
}

static void craft_ate(uint8_t *slot, const char *key, uint16_t off, uint16_t len)
{
    struct nvs_ate ate;
    memset(&ate, 0xFF, sizeof(ate));
    memset(ate.key, 0, NVS_KEY_SIZE);
    strncpy(ate.key, key, NVS_KEY_SIZE);
    ate.offset = off;
    ate.len = len;
    ate.part = 0xFF;
    ate.crc8 = craft_crc8(0xFF, &ate, offsetof(struct nvs_ate, crc8));
    memcpy(slot, &ate, sizeof(ate));
}

static void craft_close(uint8_t *slot, uint16_t last_ate_off)
{
    struct nvs_ate ate;
    memset(&ate, 0xFF, sizeof(ate));
    ate.len = 0;
    ate.offset = last_ate_off;
    ate.crc8 = craft_crc8(0xFF, &ate, offsetof(struct nvs_ate, crc8));
    memcpy(slot, &ate, sizeof(ate));
}

static uint32_t sat(uint32_t sector, uint32_t off)
{
    return sector * NVS_SECTOR_SIZE + off;
}

static void mutex_noop(void *m)
{
    (void)m;
}

static void fs_setup(nvs_fs_t *fs, uint16_t sector_count)
{
    memset(fs, 0, sizeof(*fs));
    fs->offset = 0;
    fs->sector_size = NVS_SECTOR_SIZE;
    fs->sector_count = sector_count;
    fs->flash_parameters.write_block_size = 4;
    fs->flash_parameters.erase_value = 0xFF;
    fs->flash_ops.flash_read = ff_nvs_read;
    fs->flash_ops.flash_write = ff_nvs_write;
    fs->flash_ops.flash_erase = ff_nvs_erase;
    fs->mutex_ops.lock = mutex_noop;
    fs->mutex_ops.unlock = mutex_noop;
    fs->mutex = NULL;
}

static json_config_key_status_t map_status(int raw)
{
    if (raw >= 0) {
        return JSON_CONFIG_KEY_PRESENT;
    }
    if (raw == -ENOENT) {
        return JSON_CONFIG_KEY_MISSING;
    }
    return JSON_CONFIG_KEY_UNKNOWN;
}

static storage_nvs_session_io_t session_io(void);

static int lfs_region_read(uint32_t offset, void *data, size_t len)
{
    return ff_nvs_read(offset, data, len);
}

static uint8_t g_mem[MEM_SIZE];
static fake_flash_t g_ff;

static void test_gate_poweron_failclosed(void)
{
    storage_media_state_t st0 = storage_media_gate_state(STORAGE_MEDIA_GATE_FACTORY);
    storage_media_state_t st1 = storage_media_gate_state(STORAGE_MEDIA_GATE_USER);
    CHECK(st0 == STORAGE_MEDIA_UNTRUSTED);
    CHECK(st1 == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_FACTORY) == 0);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 4) == 1);
}

static void test_gate_states(void)
{
    storage_media_gate_reset();
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_BLANK);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_BLANK);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_FACTORY) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_FACTORY) == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, (storage_media_state_t)77);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_state(99) == STORAGE_MEDIA_UNTRUSTED);
}

static void test_gate_regions(void)
{
    storage_media_gate_reset();
    storage_media_gate_register_region(0x100, 0x100, STORAGE_MEDIA_GATE_FACTORY);
    storage_media_gate_register_region(0x200, 0x100, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_media_gate_region(0) != NULL);
    CHECK(storage_media_gate_region(1) != NULL);
    CHECK(storage_media_gate_region(2) == NULL);
    CHECK(storage_media_gate_range_writable(0x100, 4) == 0);
    CHECK(storage_media_gate_range_writable(0xF0, 0x20) == 0);
    CHECK(storage_media_gate_range_writable(0x200, 4) == 0);
    CHECK(storage_media_gate_range_writable(0x2FC, 8) == 0);
    CHECK(storage_media_gate_range_writable(0x400, 0x10) == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_range_writable(0x200, 4) == 1);
    CHECK(storage_media_gate_range_writable(0x2FC, 8) == 1);
    CHECK(storage_media_gate_range_writable(0x100, 4) == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_FACTORY, STORAGE_MEDIA_BLANK);
    CHECK(storage_media_gate_range_writable(0x100, 0x100) == 1);
    CHECK(storage_media_gate_range_writable(0, 4) == 1);
}

static void test_blank_probe_unit(void)
{
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    CHECK(storage_media_blank_probe(lfs_region_read, 0, 1024, 0xFF, &blank) == 0);
    CHECK(blank == 1);
    g_mem[777] = 0x00;
    CHECK(storage_media_blank_probe(lfs_region_read, 0, 1024, 0xFF, &blank) == 0);
    CHECK(blank == 0);
    g_ff.fail_next_reads = 1;
    CHECK(storage_media_blank_probe(lfs_region_read, 0, 1024, 0xFF, &blank) != 0);
    CHECK(blank == 0);
    CHECK(storage_media_blank_probe(NULL, 0, 16, 0xFF, &blank) != 0);
    CHECK(storage_media_blank_probe(lfs_region_read, 0, 0, 0xFF, &blank) != 0);
}

static void test_nvs_blank_check_real(void)
{
    nvs_fs_t fs;
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 1);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "k", "v", 2) == 2);
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 0);
}

static void test_nvs_healthy_regress(void)
{
    nvs_fs_t fs;
    char buf[128];
    char key[8];
    char val[100];
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    int blank = -1;
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 1);
    CHECK(nvs_init(&fs) == 0);
    for (int round = 0; round < 15; round++) {
        for (int i = 0; i < 20; i++) {
            int len = 8 + ((i + round) % 12) * 4;
            snprintf(key, sizeof(key), "key%03d", i);
            memset(val, (char)('A' + (round % 26)), sizeof(val));
            CHECK((int)nvs_write(&fs, key, val, len) == len);
        }
    }
    CHECK(g_ff.erase_calls >= 2);
    memset(&fs, 0, sizeof(fs));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    for (int i = 0; i < 20; i++) {
        int len = 8 + ((i + 14) % 12) * 4;
        snprintf(key, sizeof(key), "key%03d", i);
        memset(buf, 0, sizeof(buf));
        int r = (int)nvs_read(&fs, key, buf, sizeof(buf));
        CHECK(r == len);
        if (r == len) {
            CHECK(buf[0] == 'O');
            CHECK(buf[len - 1] == 'O');
        }
    }
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
}

static void test_first_boot_chain_blank(void)
{
    nvs_fs_t fs;
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_BLANK);
    CHECK(json_config_boot_gate_first_boot(storage_media_gate_state(STORAGE_MEDIA_GATE_USER)) == 1);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "cfg_magic", "1094795585", 11) > 0);
    CHECK((int)nvs_write(&fs, "auth_password", "secret", 7) > 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(json_config_boot_gate_first_boot(storage_media_gate_state(STORAGE_MEDIA_GATE_USER)) == 0);
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 0);
}

static void test_factory_preset_not_first_boot(void)
{
    nvs_fs_t fs;
    char val[32];
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_BLANK);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "serial_number", "SN123", 6) > 0);
    CHECK((int)nvs_write(&fs, "mac_address", "AA:BB:CC:DD:EE:FF", 18) > 0);
    int writes_before = g_ff.write_calls;
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(json_config_boot_gate_first_boot(storage_media_gate_state(STORAGE_MEDIA_GATE_USER)) == 0);
    memset(&fs, 0, sizeof(fs));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_read(&fs, "serial_number", val, sizeof(val)) == 6);
    CHECK(memcmp(val, "SN123", 6) == 0);
    CHECK(map_status((int)nvs_read(&fs, "cfg_magic", val, sizeof(val))) == JSON_CONFIG_KEY_MISSING);
    CHECK(g_ff.write_calls == writes_before);
}

static void test_read_fault_untrusted_preserved(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "k1", "value1", 7) > 0);
    memcpy(snapshot, g_mem, MEM_SIZE);
    g_ff.fail_next_reads = 1;
    CHECK(nvs_blank_check(&fs, &blank) != 0);
    CHECK(blank == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_UNTRUSTED) == 0);
    g_ff.fail_next_reads = 0;
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_io_error_key_not_backfilled(void)
{
    nvs_fs_t fs;
    char val[16];
    uint8_t snapshot[MEM_SIZE];
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "confidence", "80", 3) > 0);
    memcpy(snapshot, g_mem, MEM_SIZE);
    g_ff.fail_next_reads = 1;
    int raw = (int)nvs_read(&fs, "confidence", val, sizeof(val));
    g_ff.fail_next_reads = 0;
    CHECK(raw < 0);
    CHECK(raw != -ENOENT);
    CHECK(map_status(raw) == JSON_CONFIG_KEY_UNKNOWN);
    CHECK(json_config_boot_gate_backfill_allowed(JSON_CONFIG_KEY_UNKNOWN) == 0);
    CHECK(json_config_boot_gate_backfill_allowed(JSON_CONFIG_KEY_PRESENT) == 0);
    CHECK(json_config_boot_gate_backfill_allowed(JSON_CONFIG_KEY_MISSING) == 1);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void craft_open_sector_image(int with_tear, int with_next_data)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(1, 0), "BBBBBBBB", 8);
    craft_ate(g_mem + sat(1, FIRST_ATE), "k2", 0, 8);
    if (with_tear) {
        memset(g_mem + sat(1, SECOND_ATE), 0x33, 10);
    }
    if (with_next_data) {
        memcpy(g_mem + sat(2, 0), "CCCCCCCC", 8);
        craft_ate(g_mem + sat(2, FIRST_ATE), "k3", 0, 8);
        craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    }
}

static void craft_interrupted_gc_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(1, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(1, FIRST_ATE), "k3", 0, 8);
    memcpy(g_mem + sat(2, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(2, FIRST_ATE), "k3", 0, 8);
    craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)FIRST_ATE);
}

static void craft_reclaim_bad_ate_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(2, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(2, FIRST_ATE), "k3", 0, 8);
    memset(g_mem + sat(2, SECOND_ATE), 0x33, 10);
    craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)SECOND_ATE);
}

static void craft_reclaim_unclosed_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(2, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(2, FIRST_ATE), "k3", 0, 8);
}

static void craft_reclaim_diff_len_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(1, 0), "NEWVAL9", 7);
    craft_ate(g_mem + sat(1, FIRST_ATE), "k9", 0, 7);
    memcpy(g_mem + sat(2, 0), "OLD", 3);
    craft_ate(g_mem + sat(2, FIRST_ATE), "k9", 0, 3);
    craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)FIRST_ATE);
}

static void craft_reclaim_diff_data_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(1, 0), "NEWVAL9", 7);
    craft_ate(g_mem + sat(1, FIRST_ATE), "k9", 0, 7);
    memcpy(g_mem + sat(2, 0), "OLDVAL1", 7);
    craft_ate(g_mem + sat(2, FIRST_ATE), "k9", 0, 7);
    craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)FIRST_ATE);
}

static void craft_reclaim_illegal_range_image(void)
{
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    memcpy(g_mem + sat(0, 0), "AAAAAAAA", 8);
    craft_ate(g_mem + sat(0, FIRST_ATE), "k1", 0, 8);
    craft_close(g_mem + sat(0, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    memcpy(g_mem + sat(1, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(1, FIRST_ATE), "k3", 0, 8);
    memcpy(g_mem + sat(2, 0), "CCCCCCCC", 8);
    craft_ate(g_mem + sat(2, SECOND_ATE), "k3", 0, 8);
    craft_ate(g_mem + sat(2, FIRST_ATE), "kr", 4200, 8);
    craft_close(g_mem + sat(2, CLOSE_SLOT), (uint16_t)SECOND_ATE);
}

static void test_tear_preserved(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    craft_open_sector_image(1, 0);
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_TEAR_SEEN);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k2", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "BBBBBBBB", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_recovery_deferred_on_tear(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    craft_open_sector_image(1, 1);
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_TEAR_SEEN);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k2", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "BBBBBBBB", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_recovery_clean_runs(void)
{
    nvs_fs_t fs;
    char buf[16];
    craft_interrupted_gc_image();
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & (NVS_STARTUP_TEAR_SEEN | NVS_STARTUP_RECOVERY_DEFERRED)) == 0);
    CHECK(g_ff.erase_calls == 2);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK((int)nvs_write(&fs, "k4", "DDDDDDDD", 8) == 8);
}

static int fail_reclaim_close_read(uint32_t offset, void *data, size_t len)
{
    if (len == sizeof(struct nvs_ate) && offset == sat(2, CLOSE_SLOT)) {
        return -5;
    }
    return ff_nvs_read(offset, data, len);
}

static int fail_sector_close_write(uint32_t offset, void *data, size_t len)
{
    (void)data;
    (void)len;
    if ((offset & (NVS_SECTOR_SIZE - 1)) == CLOSE_SLOT) {
        return -5;
    }
    return ff_nvs_write(offset, data, len);
}

static void test_startup_refuses_active_unique_data(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    craft_open_sector_image(0, 1);
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k2", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "BBBBBBBB", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_startup_refuses_reclaim_bad_ate(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    craft_reclaim_bad_ate_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_startup_refuses_reclaim_unclosed(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    craft_reclaim_unclosed_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    CHECK(map_status((int)nvs_read(&fs, "k3", buf, sizeof(buf))) == JSON_CONFIG_KEY_MISSING);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_startup_recovery_read_fault_no_erase(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    craft_interrupted_gc_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    fs.flash_ops.flash_read = fail_reclaim_close_read;
    CHECK(nvs_init(&fs) != 0);
    CHECK(fs.ready == false);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_recovery_refuses_reclaim_diff_len(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    storage_nvs_session_io_t io = session_io();
    craft_reclaim_diff_len_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k9", buf, sizeof(buf)) == 7);
    CHECK(memcmp(buf, "NEWVAL9", 7) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    craft_reclaim_diff_len_image();
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(fs.ready == true);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_recovery_refuses_reclaim_diff_data(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    storage_nvs_session_io_t io = session_io();
    craft_reclaim_diff_data_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k9", buf, sizeof(buf)) == 7);
    CHECK(memcmp(buf, "NEWVAL9", 7) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    craft_reclaim_diff_data_image();
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(fs.ready == true);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_recovery_refuses_reclaim_illegal_range(void)
{
    nvs_fs_t fs;
    char buf[16];
    uint8_t snapshot[MEM_SIZE];
    storage_nvs_session_io_t io = session_io();
    craft_reclaim_illegal_range_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    craft_reclaim_illegal_range_image();
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(fs.ready == true);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_close_ate_write_fault_no_advance(void)
{
    nvs_fs_t fs;
    char key[8];
    char val[96];
    char buf[96];
    uint32_t ate_wra_before;
    int i;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    for (i = 0; i < 32; i++) {
        snprintf(key, sizeof(key), "f%02d", i);
        memset(val, (char)('a' + (i % 26)), sizeof(val));
        CHECK((int)nvs_write(&fs, key, val, 90) == 90);
    }
    CHECK((fs.ate_wra >> 16) == 0);
    ate_wra_before = fs.ate_wra;
    fs.flash_ops.flash_write = fail_sector_close_write;
    snprintf(key, sizeof(key), "f32");
    memset(val, 'z', sizeof(val));
    CHECK((int)nvs_write(&fs, key, val, 90) < 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(fs.ate_wra == ate_wra_before);
    CHECK((fs.ate_wra >> 16) == 0);
    fs.flash_ops.flash_write = ff_nvs_write;
    CHECK((int)nvs_write(&fs, key, val, 90) == 90);
    CHECK((fs.ate_wra >> 16) == 1);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "f00", buf, sizeof(buf)) == 90);
    CHECK(buf[0] == 'a');
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "f31", buf, sizeof(buf)) == 90);
    CHECK(buf[0] == 'f');
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "f32", buf, sizeof(buf)) == 90);
    CHECK(buf[0] == 'z');
}

static void test_media_fault_mapping_and_downgrade(void)
{
    storage_media_gate_reset();
    CHECK(storage_nvs_session_result_is_media_fault(0) == 0);
    CHECK(storage_nvs_session_result_is_media_fault(90) == 0);
    CHECK(storage_nvs_session_result_is_media_fault(-EINVAL) == 0);
    CHECK(storage_nvs_session_result_is_media_fault(-EACCES) == 0);
    CHECK(storage_nvs_session_result_is_media_fault(-ENOSPC) == 0);
    CHECK(storage_nvs_session_result_is_media_fault(-1) == 1);
    CHECK(storage_nvs_session_result_is_media_fault(-5) == 1);
    CHECK(storage_nvs_session_result_is_media_fault(-EIO) == 1);
    CHECK(storage_nvs_session_result_is_media_fault(-ESPIPE) == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    storage_nvs_session_note_result(STORAGE_MEDIA_GATE_USER, -ENOSPC);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    storage_nvs_session_note_result(STORAGE_MEDIA_GATE_USER, 90);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
    storage_nvs_session_note_result(STORAGE_MEDIA_GATE_USER, -EACCES);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
    storage_nvs_session_note_result(STORAGE_MEDIA_GATE_USER, -5);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    storage_nvs_session_note_result(99, -5);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
}

static void test_edeadlk_preserved(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    for (uint32_t s = 0; s < CRAFT_SECTORS; s++) {
        memcpy(g_mem + sat(s, 0), "DDDDDDDD", 8);
        craft_ate(g_mem + sat(s, FIRST_ATE), "kx", 0, 8);
        craft_close(g_mem + sat(s, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    }
    memcpy(snapshot, g_mem, MEM_SIZE);
    fs_setup(&fs, CRAFT_SECTORS);
    CHECK(nvs_blank_check(&fs, &blank) == 0);
    CHECK(blank == 0);
    CHECK(nvs_init(&fs) != 0);
    CHECK(fs.ready == false);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_UNTRUSTED) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void lfs_cfg_setup(struct lfs_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->read = ff_lfs_read;
    cfg->prog = ff_lfs_prog;
    cfg->erase = ff_lfs_erase;
    cfg->sync = ff_lfs_sync;
    cfg->context = &g_ff;
    cfg->read_size = 64;
    cfg->prog_size = 64;
    cfg->block_size = 256;
    cfg->block_count = LFS_BLOCKS;
    cfg->cache_size = 64;
    cfg->lookahead_size = 8;
    cfg->block_cycles = 0;
}

static void test_lfs_blank_trusted_init(void)
{
    struct lfs_config cfg;
    lfs_t lfs;
    lfs_file_t file;
    char buf[8] = {0};
    int blank = -1;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    lfs_cfg_setup(&cfg);
    CHECK(lfs_mount(&lfs, &cfg) != 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(storage_media_blank_probe(lfs_region_read, 0, LFS_TOTAL, 0xFF, &blank) == 0);
    CHECK(blank == 1);
    CHECK(lfs_format(&lfs, &cfg) == 0);
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    CHECK(lfs_file_open(&lfs, &file, "boot.txt", LFS_O_WRONLY | LFS_O_CREAT) == 0);
    CHECK(lfs_file_write(&lfs, &file, "hello", 5) == 5);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
    memset(&lfs, 0, sizeof(lfs));
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    CHECK(lfs_file_open(&lfs, &file, "boot.txt", LFS_O_RDONLY) == 0);
    CHECK(lfs_file_read(&lfs, &file, buf, 5) == 5);
    CHECK(strcmp(buf, "hello") == 0);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
}

static void test_lfs_corrupt_no_auto_format(void)
{
    struct lfs_config cfg;
    lfs_t lfs;
    lfs_file_t file;
    int blank = -1;
    int erases_before;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    lfs_cfg_setup(&cfg);
    CHECK(lfs_format(&lfs, &cfg) == 0);
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    CHECK(lfs_mkdir(&lfs, "data") == 0);
    CHECK(lfs_file_open(&lfs, &file, "data/keep.txt", LFS_O_WRONLY | LFS_O_CREAT) == 0);
    CHECK(lfs_file_write(&lfs, &file, "precious", 8) == 8);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
    memset(&g_mem[8], 0x00, 8);
    memset(&g_mem[264], 0x00, 8);
    erases_before = g_ff.erase_calls;
    memset(&lfs, 0, sizeof(lfs));
    CHECK(lfs_mount(&lfs, &cfg) != 0);
    CHECK(g_ff.erase_calls == erases_before);
    CHECK(storage_media_blank_probe(lfs_region_read, 0, LFS_TOTAL, 0xFF, &blank) == 0);
    CHECK(blank == 0);
    CHECK(g_ff.erase_calls == erases_before);
}

static void test_lfs_explicit_format_after_corrupt(void)
{
    struct lfs_config cfg;
    lfs_t lfs;
    lfs_file_t file;
    char buf[8] = {0};
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    lfs_cfg_setup(&cfg);
    CHECK(lfs_format(&lfs, &cfg) == 0);
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    CHECK(lfs_file_open(&lfs, &file, "old.txt", LFS_O_WRONLY | LFS_O_CREAT) == 0);
    CHECK(lfs_file_write(&lfs, &file, "old", 3) == 3);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
    memset(&g_mem[8], 0x00, 8);
    memset(&g_mem[264], 0x00, 8);
    memset(&lfs, 0, sizeof(lfs));
    CHECK(lfs_mount(&lfs, &cfg) != 0);
    CHECK(lfs_format(&lfs, &cfg) == 0);
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    struct lfs_info info;
    CHECK(lfs_stat(&lfs, "old.txt", &info) != 0);
    CHECK(lfs_file_open(&lfs, &file, "new.txt", LFS_O_WRONLY | LFS_O_CREAT) == 0);
    CHECK(lfs_file_write(&lfs, &file, "fresh", 5) == 5);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_file_open(&lfs, &file, "new.txt", LFS_O_RDONLY) == 0);
    CHECK(lfs_file_read(&lfs, &file, buf, 5) == 5);
    CHECK(strcmp(buf, "fresh") == 0);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
}

static void test_lfs_probe_read_fault_no_format(void)
{
    struct lfs_config cfg;
    lfs_t lfs;
    lfs_file_t file;
    int blank = -1;
    int erases_before;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    lfs_cfg_setup(&cfg);
    CHECK(lfs_format(&lfs, &cfg) == 0);
    CHECK(lfs_mount(&lfs, &cfg) == 0);
    CHECK(lfs_file_open(&lfs, &file, "x", LFS_O_WRONLY | LFS_O_CREAT) == 0);
    CHECK(lfs_file_close(&lfs, &file) == 0);
    CHECK(lfs_unmount(&lfs) == 0);
    memset(&g_mem[8], 0x00, 8);
    memset(&g_mem[264], 0x00, 8);
    erases_before = g_ff.erase_calls;
    memset(&lfs, 0, sizeof(lfs));
    CHECK(lfs_mount(&lfs, &cfg) != 0);
    g_ff.fail_next_reads = 1;
    CHECK(storage_media_blank_probe(lfs_region_read, 0, LFS_TOTAL, 0xFF, &blank) != 0);
    CHECK(blank == 0);
    CHECK(g_ff.erase_calls == erases_before);
}

static void test_boot_gate_policy_matrix(void)
{
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_BLANK) == 1);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_HEALTHY) == 0);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_UNTRUSTED) == 0);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_STARTUP) == 0);
    CHECK(json_config_boot_gate_first_boot(STORAGE_MEDIA_DEGRADED) == 0);
    CHECK(json_config_boot_gate_key_status(AICAM_OK) == JSON_CONFIG_KEY_PRESENT);
    CHECK(json_config_boot_gate_key_status(AICAM_ERROR_NOT_FOUND) == JSON_CONFIG_KEY_MISSING);
    CHECK(json_config_boot_gate_key_status(AICAM_ERROR) == JSON_CONFIG_KEY_UNKNOWN);
    CHECK(json_config_boot_gate_key_status(AICAM_ERROR_IO) == JSON_CONFIG_KEY_UNKNOWN);
    CHECK(json_config_boot_gate_key_status((aicam_result_t)(-13)) == JSON_CONFIG_KEY_UNKNOWN);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_BLANK, JSON_CONFIG_KEY_MISSING) == 1);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_HEALTHY, JSON_CONFIG_KEY_MISSING) == 0);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_UNTRUSTED, JSON_CONFIG_KEY_MISSING) == 0);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_BLANK, JSON_CONFIG_KEY_PRESENT) == 0);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_BLANK, JSON_CONFIG_KEY_UNKNOWN) == 0);
    CHECK(json_config_boot_gate_credential_migrate_allowed(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_PRESENT) == 1);
    CHECK(json_config_boot_gate_credential_migrate_allowed(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING) == 0);
    CHECK(json_config_boot_gate_credential_migrate_allowed(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_UNKNOWN) == 0);
    CHECK(json_config_boot_gate_credential_migrate_allowed(JSON_CONFIG_KEY_UNKNOWN, JSON_CONFIG_KEY_PRESENT) == 0);
    CHECK(json_config_boot_gate_credential_migrate_allowed(JSON_CONFIG_KEY_PRESENT, JSON_CONFIG_KEY_PRESENT) == 0);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_PRESENT, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_HEALTHY) == 1);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_PRESENT, STORAGE_MEDIA_DEGRADED) == 1);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK) == 1);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_HEALTHY) == 0);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_UNTRUSTED) == 0);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_UNKNOWN, JSON_CONFIG_KEY_PRESENT, STORAGE_MEDIA_HEALTHY) == 0);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_UNKNOWN, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK) == 0);
    CHECK(json_config_boot_gate_credential_trusted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_UNKNOWN, STORAGE_MEDIA_HEALTHY) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_PRESENT, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK, AICAM_ERROR) == 1);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_PRESENT, STORAGE_MEDIA_HEALTHY, AICAM_ERROR) == 1);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_PRESENT, STORAGE_MEDIA_DEGRADED, AICAM_ERROR) == 1);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK, AICAM_OK) == 1);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK, AICAM_ERROR) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK, AICAM_ERROR_NOT_FOUND) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_HEALTHY, AICAM_OK) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_UNTRUSTED, AICAM_OK) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_UNKNOWN, JSON_CONFIG_KEY_MISSING, STORAGE_MEDIA_BLANK, AICAM_OK) == 0);
    CHECK(json_config_boot_gate_credential_trusted_persisted(JSON_CONFIG_KEY_MISSING, JSON_CONFIG_KEY_UNKNOWN, STORAGE_MEDIA_BLANK, AICAM_OK) == 0);
}

static void test_gate_startup_degraded_states(void)
{
    storage_media_gate_reset();
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_STARTUP);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_STARTUP);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_media_gate_range_writable(0, 16) == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_FACTORY) == 0);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    CHECK(storage_media_gate_range_writable(0, 16) == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, STORAGE_MEDIA_BLANK);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    CHECK(storage_media_gate_range_writable(0, 16) == 1);
    storage_media_gate_set_state(STORAGE_MEDIA_GATE_USER, (storage_media_state_t)5);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
}

static storage_nvs_session_io_t session_io(void)
{
    storage_nvs_session_io_t io;
    memset(&io, 0, sizeof(io));
    io.flash_ops.flash_read = ff_nvs_read;
    io.flash_ops.flash_write = ff_nvs_write;
    io.flash_ops.flash_erase = ff_nvs_erase;
    io.mutex_new = NULL;
    io.mutex_del = NULL;
    io.lock = mutex_noop;
    io.unlock = mutex_noop;
    return io;
}

static void test_session_tear_degraded_refuses_writes(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    char buf[16];
    storage_nvs_session_io_t io = session_io();
    craft_open_sector_image(1, 1);
    memcpy(snapshot, g_mem, MEM_SIZE);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(fs.ready == true);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_session_tear_second_boot_preserved(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    char buf[16];
    storage_nvs_session_io_t io = session_io();
    craft_open_sector_image(1, 1);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    int erases_after_first = g_ff.erase_calls;
    int writes_after_first = g_ff.write_calls;
    memcpy(snapshot, g_mem, MEM_SIZE);
    memset(&fs, 0, sizeof(fs));
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(g_ff.erase_calls == erases_after_first);
    CHECK(g_ff.write_calls == writes_after_first);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k2", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "BBBBBBBB", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_session_clean_recovery_grants_healthy(void)
{
    nvs_fs_t fs;
    char buf[16];
    storage_nvs_session_io_t io = session_io();
    craft_interrupted_gc_image();
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK((fs.startup_flags & (NVS_STARTUP_TEAR_SEEN | NVS_STARTUP_RECOVERY_DEFERRED)) == 0);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    CHECK(g_ff.erase_calls == 2);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
}

static void test_session_reclaim_bad_ate_degraded(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    char buf[16];
    storage_nvs_session_io_t io = session_io();
    craft_reclaim_bad_ate_image();
    memcpy(snapshot, g_mem, MEM_SIZE);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(fs.ready == true);
    CHECK((fs.startup_flags & NVS_STARTUP_TEAR_SEEN) == 0);
    CHECK(fs.startup_flags & NVS_STARTUP_RECOVERY_DEFERRED);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    CHECK(g_ff.erase_calls == 0);
    CHECK(g_ff.write_calls == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "AAAAAAAA", 8) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_session_blank_grants_blank_and_write(void)
{
    nvs_fs_t fs;
    storage_nvs_session_io_t io = session_io();
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, NVS_SECTORS, STORAGE_MEDIA_GATE_USER, &io) == 0);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_BLANK);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 1);
    CHECK((int)nvs_write(&fs, "bootkey", "bootval", 8) == 8);
    CHECK(g_ff.write_calls > 0);
}

static void test_session_read_error_untrusted(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    storage_nvs_session_io_t io = session_io();
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "k", "v", 2) == 2);
    memcpy(snapshot, g_mem, MEM_SIZE);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    g_ff.fail_next_reads = 1;
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, NVS_SECTORS, STORAGE_MEDIA_GATE_USER, &io) != 0);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(storage_media_gate_range_writable(0, 16) == 0);
    g_ff.fail_next_reads = 0;
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_session_init_fail_untrusted(void)
{
    nvs_fs_t fs;
    uint8_t snapshot[MEM_SIZE];
    storage_nvs_session_io_t io = session_io();
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    for (uint32_t s = 0; s < CRAFT_SECTORS; s++) {
        memcpy(g_mem + sat(s, 0), "DDDDDDDD", 8);
        craft_ate(g_mem + sat(s, FIRST_ATE), "kx", 0, 8);
        craft_close(g_mem + sat(s, CLOSE_SLOT), (uint16_t)FIRST_ATE);
    }
    memcpy(snapshot, g_mem, MEM_SIZE);
    storage_media_gate_reset();
    storage_media_gate_register_region(0, NVS_TOTAL, STORAGE_MEDIA_GATE_USER);
    CHECK(storage_nvs_session_init(&fs, 0, NVS_SECTOR_SIZE, CRAFT_SECTORS, STORAGE_MEDIA_GATE_USER, &io) != 0);
    CHECK(fs.ready == false);
    CHECK(storage_media_gate_state(STORAGE_MEDIA_GATE_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_media_gate_writable(STORAGE_MEDIA_GATE_USER) == 0);
    CHECK(memcmp(snapshot, g_mem, MEM_SIZE) == 0);
}

static void test_cred_policy_chain_real_nvs(void)
{
    nvs_fs_t fs;
    char val[32];
    json_config_key_status_t new_st;
    ff_init(&g_ff, g_mem, sizeof(g_mem));
    fs_setup(&fs, NVS_SECTORS);
    CHECK(nvs_init(&fs) == 0);
    CHECK((int)nvs_write(&fs, "dev_info_password", "legacy-pass", 12) > 0);
    new_st = map_status((int)nvs_read(&fs, "auth_password", val, sizeof(val)));
    CHECK(new_st == JSON_CONFIG_KEY_MISSING);
    json_config_key_status_t old_st = map_status((int)nvs_read(&fs, "dev_info_password", val, sizeof(val)));
    CHECK(old_st == JSON_CONFIG_KEY_PRESENT);
    CHECK(json_config_boot_gate_credential_migrate_allowed(new_st, old_st) == 1);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_HEALTHY, new_st) == 0);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_BLANK, new_st) == 1);
    memset(&fs, 0, sizeof(fs));
    fs_setup(&fs, NVS_SECTORS);
    new_st = map_status((int)nvs_read(&fs, "auth_password", val, sizeof(val)));
    CHECK(new_st == JSON_CONFIG_KEY_UNKNOWN);
    CHECK(json_config_boot_gate_credential_migrate_allowed(new_st, JSON_CONFIG_KEY_PRESENT) == 0);
    CHECK(json_config_boot_gate_credential_persist_default_allowed(STORAGE_MEDIA_HEALTHY, new_st) == 0);
}

int main(void)
{
    test_gate_poweron_failclosed();
    test_gate_states();
    test_gate_regions();
    test_blank_probe_unit();
    test_nvs_blank_check_real();
    test_nvs_healthy_regress();
    test_first_boot_chain_blank();
    test_factory_preset_not_first_boot();
    test_read_fault_untrusted_preserved();
    test_io_error_key_not_backfilled();
    test_tear_preserved();
    test_recovery_deferred_on_tear();
    test_recovery_clean_runs();
    test_startup_refuses_active_unique_data();
    test_startup_refuses_reclaim_bad_ate();
    test_startup_refuses_reclaim_unclosed();
    test_startup_recovery_read_fault_no_erase();
    test_recovery_refuses_reclaim_diff_len();
    test_recovery_refuses_reclaim_diff_data();
    test_recovery_refuses_reclaim_illegal_range();
    test_close_ate_write_fault_no_advance();
    test_media_fault_mapping_and_downgrade();
    test_edeadlk_preserved();
    test_lfs_blank_trusted_init();
    test_lfs_corrupt_no_auto_format();
    test_lfs_explicit_format_after_corrupt();
    test_lfs_probe_read_fault_no_format();
    test_boot_gate_policy_matrix();
    test_cred_policy_chain_real_nvs();
    test_gate_startup_degraded_states();
    test_session_tear_degraded_refuses_writes();
    test_session_tear_second_boot_preserved();
    test_session_clean_recovery_grants_healthy();
    test_session_reclaim_bad_ate_degraded();
    test_session_blank_grants_blank_and_write();
    test_session_read_error_untrusted();
    test_session_init_fail_untrusted();
    printf("checks=%d failures=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
