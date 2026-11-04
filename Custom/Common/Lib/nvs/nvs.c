/*  NVS: non volatile storage in flash
 *
 */

#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "nvs.h"

static const uint8_t crc8_ccitt_small_table[16] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

static uint8_t crc8_ccitt(uint8_t val, const void *buf, size_t cnt)
{
    size_t i;
    const uint8_t *p = buf;

    for (i = 0; i < cnt; i++) {
        val ^= p[i];
        val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
        val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
    }
    return val;
}

static inline size_t nvs_al_size(nvs_fs_t *fs, size_t len)
{
    uint8_t write_block_size = fs->flash_parameters.write_block_size;

    if (write_block_size <= 1U) {
        return len;
    }
    return (len + (write_block_size - 1U)) & ~(write_block_size - 1U);
}

static int nvs_flash_al_wrt(nvs_fs_t *fs, uint32_t addr, const void *data,
                 size_t len)
{
    uint8_t *data8 = (uint8_t *)data;
    int rc = 0;
    int offset;
    size_t blen;
    uint8_t buf[NVS_BLOCK_SIZE];

    if (!len) {
        return 0;
    }

    offset = fs->offset;
    offset += fs->sector_size * (addr >> ADDR_SECT_SHIFT);
    offset += addr & ADDR_OFFS_MASK;
    if(fs->flash_ops.flash_write_protection_set != NULL) {
        rc = fs->flash_ops.flash_write_protection_set(false);
    }
    if (rc) {
        return rc;
    }
    blen = len & ~(fs->flash_parameters.write_block_size - 1U);
    if (blen > 0) {
        rc = fs->flash_ops.flash_write(offset, data8, blen);
        if (rc) {
            printf("NVS: flash write error \r\n");
            goto end;
        }
        len -= blen;
        offset += blen;
        data8 += blen;
    }
    if (len) {
        memcpy(buf, data8, len);
        (void)memset(buf + len, fs->flash_parameters.erase_value,
            fs->flash_parameters.write_block_size - len);

        rc = fs->flash_ops.flash_write(offset, buf, fs->flash_parameters.write_block_size);
        if (rc) {
            printf("NVS: flash write remaining error \r\n");
            goto end;
        }
    }

end:
    if(fs->flash_ops.flash_write_protection_set != NULL) {
        (void) fs->flash_ops.flash_write_protection_set(true);
    }
    return rc;
}

static int nvs_flash_rd(nvs_fs_t *fs, uint32_t addr, void *data,
             size_t len)
{
    int rc;
    int offset;

    offset = fs->offset;
    offset += fs->sector_size * (addr >> ADDR_SECT_SHIFT);
    offset += addr & ADDR_OFFS_MASK;

    rc = fs->flash_ops.flash_read(offset, data, len);
    if ( rc )
        printf("flash_driver read error!\r\n");
    return rc;
}

static int nvs_flash_ate_wrt(nvs_fs_t *fs, const struct nvs_ate *entry)
{
    int rc;

    rc = nvs_flash_al_wrt(fs, fs->ate_wra, entry,
                   sizeof(struct nvs_ate));
    fs->ate_wra -= nvs_al_size(fs, sizeof(struct nvs_ate));

    return rc;
}

static int nvs_flash_data_wrt(nvs_fs_t *fs, const void *data, size_t len)
{
    int rc;

    rc = nvs_flash_al_wrt(fs, fs->data_wra, data, len);
    fs->data_wra += nvs_al_size(fs, len);

    return rc;
}

static int nvs_flash_ate_rd(nvs_fs_t *fs, uint32_t addr,
                 struct nvs_ate *entry)
{
    return nvs_flash_rd(fs, addr, entry, sizeof(struct nvs_ate));
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static int nvs_flash_block_cmp(nvs_fs_t *fs, uint32_t addr, const void *data,
                size_t len)
{
    const uint8_t *data8 = (const uint8_t *)data;
    int rc;
    size_t bytes_to_cmp, block_size;
    uint8_t buf[NVS_BLOCK_SIZE];

    block_size =
        NVS_BLOCK_SIZE & ~(fs->flash_parameters.write_block_size - 1U);

    while (len) {
        bytes_to_cmp = MIN(block_size, len);
        rc = nvs_flash_rd(fs, addr, buf, bytes_to_cmp);
        if (rc) {
            return rc;
        }
        rc = memcmp(data8, buf, bytes_to_cmp);
        if (rc) {
            return 1;
        }
        len -= bytes_to_cmp;
        addr += bytes_to_cmp;
        data8 += bytes_to_cmp;
    }
    return 0;
}

static int nvs_flash_cmp_const(nvs_fs_t *fs, uint32_t addr, uint8_t value,
                size_t len)
{
    int rc;
    size_t bytes_to_cmp, block_size;
    uint8_t cmp[NVS_BLOCK_SIZE];

    block_size =
        NVS_BLOCK_SIZE & ~(fs->flash_parameters.write_block_size - 1U);

    (void)memset(cmp, value, block_size);
    while (len) {
        bytes_to_cmp = MIN(block_size, len);
        rc = nvs_flash_block_cmp(fs, addr, cmp, bytes_to_cmp);
        if (rc) {
            // Debug: dump actual flash data at mismatch location
            uint8_t buf[32];
            size_t dump_len = bytes_to_cmp > 32 ? 32 : bytes_to_cmp;
            nvs_flash_rd(fs, addr, buf, dump_len);
            // printf("[NVS] cmp_const mismatch at 0x%x, expect=0x%02x, got: ", (unsigned int)addr, value);
            // for (size_t j = 0; j < dump_len; j++) {
            //     printf("%02x ", buf[j]);
            // }
            // printf("\r\n");
            return rc;
        }
        len -= bytes_to_cmp;
        addr += bytes_to_cmp;
    }
    return 0;
}

static int nvs_flash_data_cmp(nvs_fs_t *fs, uint32_t addr_a, uint32_t addr_b,
                  uint16_t len)
{
    int rc;
    size_t bytes_to_cmp, remaining;
    uint8_t buf_a[NVS_BLOCK_SIZE];
    uint8_t buf_b[NVS_BLOCK_SIZE];

    remaining = len;
    while (remaining) {
        bytes_to_cmp = MIN(NVS_BLOCK_SIZE, remaining);
        rc = nvs_flash_rd(fs, addr_a, buf_a, bytes_to_cmp);
        if (rc) {
            return rc;
        }
        rc = nvs_flash_rd(fs, addr_b, buf_b, bytes_to_cmp);
        if (rc) {
            return rc;
        }
        if (memcmp(buf_a, buf_b, bytes_to_cmp) != 0) {
            return 1;
        }
        remaining -= bytes_to_cmp;
        addr_a += bytes_to_cmp;
        addr_b += bytes_to_cmp;
    }
    return 0;
}

static int nvs_flash_block_move(nvs_fs_t *fs, uint32_t addr, size_t len)
{
    int rc;
    size_t bytes_to_copy, block_size;
    uint8_t buf[NVS_BLOCK_SIZE];

    block_size =
        NVS_BLOCK_SIZE & ~(fs->flash_parameters.write_block_size - 1U);

    while (len) {
        bytes_to_copy = MIN(block_size, len);
        rc = nvs_flash_rd(fs, addr, buf, bytes_to_copy);
        if (rc) {
            return rc;
        }
        rc = nvs_flash_data_wrt(fs, buf, bytes_to_copy);
        if (rc) {
            return rc;
        }
        len -= bytes_to_copy;
        addr += bytes_to_copy;
    }
    return 0;
}

static int nvs_flash_erase_sector(nvs_fs_t *fs, uint32_t addr)
{
    int rc;
    int offset;

    addr &= ADDR_SECT_MASK;
    rc = nvs_flash_cmp_const(fs, addr, fs->flash_parameters.erase_value,
            fs->sector_size);
    if (rc <= 0) {
        return rc;
    }

    offset = fs->offset;
    offset += fs->sector_size * (addr >> ADDR_SECT_SHIFT);

    if(fs->flash_ops.flash_write_protection_set != NULL) {
        rc = fs->flash_ops.flash_write_protection_set(false);
        if (rc) {
            return rc;
        }
    }
  

    rc = fs->flash_ops.flash_erase(offset, fs->sector_size);
    if (rc) {
        return rc;
    }
    if(fs->flash_ops.flash_write_protection_set != NULL) {
        (void) fs->flash_ops.flash_write_protection_set(true);
    }
    return 0;
}

static void nvs_ate_crc8_update(struct nvs_ate *entry)
{
    uint8_t crc8;

    crc8 = crc8_ccitt(0xff, entry, offsetof(struct nvs_ate, crc8));
    entry->crc8 = crc8;
}

static int nvs_ate_crc8_check(const struct nvs_ate *entry)
{
    uint8_t crc8;

    crc8 = crc8_ccitt(0xff, entry, offsetof(struct nvs_ate, crc8));
    if (crc8 == entry->crc8) {
        return 0;
    }
    return 1;
}

static int nvs_ate_cmp_const(const struct nvs_ate *entry, uint8_t value)
{
    const uint8_t *data8 = (const uint8_t *)entry;
    int i;

    for (i = 0; i < sizeof(struct nvs_ate); i++) {
        if (data8[i] != value) {
            return 1;
        }
    }

    return 0;
}

static int nvs_ate_data_legal(nvs_fs_t *fs, const struct nvs_ate *ate,
                  size_t ate_size)
{
    uint32_t data_top = (uint32_t)fs->sector_size - 2U * ate_size;

    if (((uint32_t)ate->offset % fs->flash_parameters.write_block_size) != 0U) {
        return 0;
    }
    if ((uint32_t)ate->offset > data_top) {
        return 0;
    }
    if (((uint32_t)ate->offset + nvs_al_size(fs, ate->len)) > data_top) {
        return 0;
    }
    return 1;
}

static int nvs_flash_wrt_entry(nvs_fs_t *fs, const char *key, const void *data,
                size_t len)
{
    int rc;
    struct nvs_ate entry;
    // size_t ate_size;

    // ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    memset(entry.key, 0, NVS_KEY_SIZE);
    strncpy(entry.key, key, NVS_KEY_SIZE);
    entry.offset = (uint16_t)(fs->data_wra & ADDR_OFFS_MASK);
    entry.len = (uint16_t)len;
    entry.part = 0xff;

    nvs_ate_crc8_update(&entry);

    rc = nvs_flash_data_wrt(fs, data, len);
    if (rc) {
        return rc;
    }
    rc = nvs_flash_ate_wrt(fs, &entry);
    if (rc) {
        return rc;
    }

    return 0;
}

static int nvs_recover_last_ate(nvs_fs_t *fs, uint32_t *addr)
{
    uint32_t data_end_addr, ate_end_addr;
    struct nvs_ate end_ate;
    size_t ate_size;
    int rc;

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    *addr -= ate_size;
    ate_end_addr = *addr;
    data_end_addr = *addr & ADDR_SECT_MASK;
    while (ate_end_addr > data_end_addr) {
        rc = nvs_flash_ate_rd(fs, ate_end_addr, &end_ate);
        if (rc) {
            return rc;
        }
        if (!nvs_ate_crc8_check(&end_ate)) {
            data_end_addr &= ADDR_SECT_MASK;
            data_end_addr += end_ate.offset + end_ate.len;
            *addr = ate_end_addr;
        }
        ate_end_addr -= ate_size;
    }

    return 0;
}

static int nvs_prev_ate(nvs_fs_t *fs, uint32_t *addr, struct nvs_ate *ate)
{
    int rc;
    struct nvs_ate close_ate;
    size_t ate_size;

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    rc = nvs_flash_ate_rd(fs, *addr, ate);
    if (rc) {
        return rc;
    }

    *addr += ate_size;
    if (((*addr) & ADDR_OFFS_MASK) != (fs->sector_size - ate_size)) {
        return 0;
    }

    if (((*addr) >> ADDR_SECT_SHIFT) == 0U) {
        *addr += ((fs->sector_count - 1) << ADDR_SECT_SHIFT);
    } else {
        *addr -= (1 << ADDR_SECT_SHIFT);
    }

    rc = nvs_flash_ate_rd(fs, *addr, &close_ate);
    if (rc) {
        return rc;
    }

    rc = nvs_ate_cmp_const(&close_ate, fs->flash_parameters.erase_value);
    if (!rc) {
        *addr = fs->ate_wra;
        return 0;
    }

    if (!nvs_ate_crc8_check(&close_ate)) {
        if (close_ate.offset < (fs->sector_size - ate_size) &&
            !(close_ate.offset % ate_size)) {
            (*addr) &= ADDR_SECT_MASK;
            (*addr) += close_ate.offset;
            return 0;
        }
    }
    return nvs_recover_last_ate(fs, addr);
}

static void nvs_sector_advance(nvs_fs_t *fs, uint32_t *addr)
{
    *addr += (1 << ADDR_SECT_SHIFT);
    if ((*addr >> ADDR_SECT_SHIFT) == fs->sector_count) {
        *addr -= (fs->sector_count << ADDR_SECT_SHIFT);
    }
}

static int nvs_sector_close(nvs_fs_t *fs)
{
    struct nvs_ate close_ate;
    size_t ate_size;
    uint32_t resume_ate_wra;
    int rc;

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));
    resume_ate_wra = fs->ate_wra;

    memset(close_ate.key, 0xFF, NVS_KEY_SIZE);
    close_ate.len = 0U;
    close_ate.offset = (uint16_t)((fs->ate_wra + ate_size) & ADDR_OFFS_MASK);

    fs->ate_wra &= ADDR_SECT_MASK;
    fs->ate_wra += (fs->sector_size - ate_size);

    nvs_ate_crc8_update(&close_ate);

    rc = nvs_flash_ate_wrt(fs, &close_ate);
    if (rc) {
        fs->ate_wra = resume_ate_wra;
        return rc;
    }

    nvs_sector_advance(fs, &fs->ate_wra);

    fs->data_wra = fs->ate_wra & ADDR_SECT_MASK;

    return 0;
}

static int nvs_gc(nvs_fs_t *fs)
{
    int rc;
    struct nvs_ate close_ate, gc_ate, wlk_ate;
    uint32_t sec_addr, gc_addr, gc_prev_addr, wlk_addr, wlk_prev_addr,
          data_addr, stop_addr;
    size_t ate_size;

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    sec_addr = (fs->ate_wra & ADDR_SECT_MASK);
    nvs_sector_advance(fs, &sec_addr);
    gc_addr = sec_addr + fs->sector_size - ate_size;

    rc = nvs_flash_ate_rd(fs, gc_addr, &close_ate);
    if (rc < 0) {
        return rc;
    }

    rc = nvs_ate_cmp_const(&close_ate, fs->flash_parameters.erase_value);
    if (!rc) {
        rc = nvs_flash_erase_sector(fs, sec_addr);
        if (rc) {
            return rc;
        }
        return 0;
    }

    stop_addr = gc_addr - ate_size;

    if (!nvs_ate_crc8_check(&close_ate)) {
        gc_addr &= ADDR_SECT_MASK;
        gc_addr += close_ate.offset;
    } else {
        rc = nvs_recover_last_ate(fs, &gc_addr);
        if (rc) {
            return rc;
        }
    }

    do {
        gc_prev_addr = gc_addr;
        rc = nvs_prev_ate(fs, &gc_addr, &gc_ate);
        if (rc) {
            return rc;
        }

        if (nvs_ate_crc8_check(&gc_ate)) {
            continue;
        }

        wlk_addr = fs->ate_wra;
        do {
            wlk_prev_addr = wlk_addr;
            rc = nvs_prev_ate(fs, &wlk_addr, &wlk_ate);
            if (rc) {
                return rc;
            }
            if ((strncmp(wlk_ate.key, gc_ate.key, NVS_KEY_SIZE) == 0) &&
                (!nvs_ate_crc8_check(&wlk_ate))) {
                break;
            }
        } while (wlk_addr != fs->ate_wra);

        if ((wlk_prev_addr == gc_prev_addr) && gc_ate.len) {
            data_addr = (gc_prev_addr & ADDR_SECT_MASK);
            data_addr += gc_ate.offset;

            gc_ate.offset = (uint16_t)(fs->data_wra & ADDR_OFFS_MASK);
            nvs_ate_crc8_update(&gc_ate);

            rc = nvs_flash_block_move(fs, data_addr, gc_ate.len);
            if (rc) {
                return rc;
            }

            rc = nvs_flash_ate_wrt(fs, &gc_ate);
            if (rc) {
                return rc;
            }
        }
    } while (gc_prev_addr != stop_addr);

    rc = nvs_flash_erase_sector(fs, sec_addr);
    if (rc) {
        return rc;
    }
    return 0;
}

static int nvs_recovery_reclaim_provable(nvs_fs_t *fs, uint32_t reclaim_base,
					 size_t ate_size)
{
    int rc;
    struct nvs_ate close_ate, slot_ate;
    uint32_t slot;

    rc = nvs_flash_ate_rd(fs, reclaim_base + fs->sector_size - ate_size,
			  &close_ate);
    if (rc) {
	return rc;
    }
    if (!nvs_ate_cmp_const(&close_ate, fs->flash_parameters.erase_value)) {
	return 0;
    }
    if (nvs_ate_crc8_check(&close_ate)) {
	return 0;
    }
    if ((close_ate.len != 0U) ||
	(close_ate.offset >= (uint16_t)(fs->sector_size - ate_size)) ||
	((close_ate.offset % ate_size) != 0U)) {
	return 0;
    }
    slot = close_ate.offset;
    while (slot <= (uint32_t)fs->sector_size - 2U * ate_size) {
	rc = nvs_flash_ate_rd(fs, reclaim_base + slot, &slot_ate);
	if (rc) {
	    return rc;
	}
	if (!nvs_ate_cmp_const(&slot_ate, fs->flash_parameters.erase_value)) {
	    return 0;
	}
	if (nvs_ate_crc8_check(&slot_ate)) {
	    return 0;
	}
	if (!nvs_ate_data_legal(fs, &slot_ate, ate_size)) {
	    return 0;
	}
	slot += ate_size;
    }
    return 1;
}

static int nvs_recovery_active_duplicated(nvs_fs_t *fs, uint32_t reclaim_base,
					  size_t ate_size)
{
    int rc;
    struct nvs_ate act_ate, rcl_ate;
    uint32_t act_addr, act_data_addr, rcl_addr, rcl_low, rcl_top;
    uint32_t act_data_top;

    rc = nvs_flash_ate_rd(fs, reclaim_base + fs->sector_size - ate_size,
			  &rcl_ate);
    if (rc) {
	return rc;
    }
    rcl_low = reclaim_base + rcl_ate.offset;
    rcl_top = reclaim_base + fs->sector_size - 2U * ate_size;

    act_data_top = (fs->ate_wra & ADDR_SECT_MASK) + fs->sector_size -
		   2U * ate_size;
    act_addr = act_data_top;
    while ((act_addr >= fs->data_wra) && (act_addr <= act_data_top)) {
	rc = nvs_flash_ate_rd(fs, act_addr, &act_ate);
	if (rc) {
	    return rc;
	}
	if (!nvs_ate_cmp_const(&act_ate, fs->flash_parameters.erase_value)) {
	    act_addr -= ate_size;
	    continue;
	}
	if (nvs_ate_crc8_check(&act_ate) || act_ate.len == 0U) {
	    return 0;
	}
	if (!nvs_ate_data_legal(fs, &act_ate, ate_size)) {
	    return 0;
	}
	if (rcl_low > rcl_top) {
	    return 0;
	}
	rcl_addr = rcl_low;
	while (1) {
	    rc = nvs_flash_ate_rd(fs, rcl_addr, &rcl_ate);
	    if (rc) {
		return rc;
	    }
	    if (!nvs_ate_crc8_check(&rcl_ate) &&
		(strncmp(rcl_ate.key, act_ate.key, NVS_KEY_SIZE) == 0)) {
		break;
	    }
	    if (rcl_addr >= rcl_top) {
		return 0;
	    }
	    rcl_addr += ate_size;
	}
	if (rcl_ate.len != act_ate.len) {
	    return 0;
	}
	act_data_addr = (act_addr & ADDR_SECT_MASK) + act_ate.offset;
	rc = nvs_flash_data_cmp(fs, act_data_addr,
				reclaim_base + rcl_ate.offset,
				act_ate.len);
	if (rc != 0) {
	    return (rc < 0) ? rc : 0;
	}
	act_addr -= ate_size;
    }
    return 1;
}

static int nvs_startup(nvs_fs_t *fs)
{
    int rc;
    struct nvs_ate last_ate;
    size_t ate_size, empty_len;
    uint32_t addr = 0U;
    uint16_t i, closed_sectors = 0;
    uint8_t erase_value = fs->flash_parameters.erase_value;

    fs->startup_flags = 0U;

    fs->mutex_ops.lock(fs->mutex);

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    for (i = 0; i < fs->sector_count; i++) {
        addr = (i << ADDR_SECT_SHIFT) +
               (uint16_t)(fs->sector_size - ate_size);
        rc = nvs_flash_cmp_const(fs, addr, erase_value,
                     sizeof(struct nvs_ate));
        if (rc) {
            closed_sectors++;
            nvs_sector_advance(fs, &addr);
            rc = nvs_flash_cmp_const(fs, addr, erase_value,
                         sizeof(struct nvs_ate));
            if (!rc) {
                break;
            }
        }
    }
    // printf("[NVS] scan: closed_sectors=%d, i=%d, addr=0x%x\r\n", closed_sectors, i, (unsigned int)addr);
    
    if (closed_sectors == fs->sector_count) {
        // printf("[NVS] ERROR: all sectors closed (EDEADLK)\r\n");
        rc = -EDEADLK;
        goto end;
    }

    if (i == fs->sector_count) {
        rc = nvs_flash_cmp_const(fs, addr - ate_size, erase_value,
                sizeof(struct nvs_ate));
        if (!rc) {
            nvs_sector_advance(fs, &addr);
        }
    }

    fs->ate_wra = addr - ate_size;
    fs->data_wra = addr & ADDR_SECT_MASK;
    // printf("[NVS] init pos: ate_wra=0x%x, data_wra=0x%x\r\n", (unsigned int)fs->ate_wra, (unsigned int)fs->data_wra);

    while (fs->ate_wra >= fs->data_wra) {
        rc = nvs_flash_ate_rd(fs, fs->ate_wra, &last_ate);
        if (rc) {
            // printf("[NVS] ERROR: ate_rd failed at 0x%x, rc=%d\r\n", (unsigned int)fs->ate_wra, rc);
            goto end;
        }

        rc = nvs_ate_cmp_const(&last_ate, erase_value);

        if (!rc) {
            break;
        }

        if (!nvs_ate_crc8_check(&last_ate)) {
            fs->data_wra = addr & ADDR_SECT_MASK;
            fs->data_wra += last_ate.offset;
            fs->data_wra += nvs_al_size(fs, last_ate.len);

            if (fs->ate_wra == fs->data_wra && last_ate.len) {
                rc = -ESPIPE;
                goto end;
            }
        } else {
            fs->startup_flags |= NVS_STARTUP_TEAR_SEEN;
        }

        fs->ate_wra -= ate_size;
    }

    while (fs->ate_wra > fs->data_wra) {
        empty_len = fs->ate_wra - fs->data_wra;

        rc = nvs_flash_cmp_const(fs, fs->data_wra, erase_value,
                empty_len);
        if (rc < 0) {
            // printf("[NVS] ERROR: cmp_const failed at data_wra=0x%x, rc=%d\r\n", (unsigned int)fs->data_wra, rc);
            goto end;
        }
        if (!rc) {
            break;
        }

        fs->data_wra += fs->flash_parameters.write_block_size;
    }

    addr = fs->ate_wra & ADDR_SECT_MASK;
    nvs_sector_advance(fs, &addr);
    rc = nvs_flash_cmp_const(fs, addr, erase_value, fs->sector_size);
    if (rc < 0) {
        goto end;
    }
    if (rc) {
        int proven;
        if (fs->startup_flags & NVS_STARTUP_TEAR_SEEN) {
            fs->startup_flags |= NVS_STARTUP_RECOVERY_DEFERRED;
            rc = 0;
            goto end;
        }
        proven = nvs_recovery_reclaim_provable(fs, addr, ate_size);
        if (proven < 0) {
            rc = proven;
            goto end;
        }
        if (proven > 0) {
            proven = nvs_recovery_active_duplicated(fs, addr, ate_size);
            if (proven < 0) {
                rc = proven;
                goto end;
            }
        }
        if (proven == 0) {
            fs->startup_flags |= NVS_STARTUP_RECOVERY_DEFERRED;
            rc = 0;
            goto end;
        }
        rc = nvs_flash_erase_sector(fs, fs->ate_wra);
        if (rc) {
            goto end;
        }
        fs->ate_wra &= ADDR_SECT_MASK;
        fs->ate_wra += (fs->sector_size - 2 * ate_size);
        fs->data_wra = (fs->ate_wra & ADDR_SECT_MASK);
        rc = nvs_gc(fs);
        if (rc) {
            goto end;
        }
    }

end:
    // printf("[NVS] startup end: rc=%d\r\n", rc);
    fs->mutex_ops.unlock(fs->mutex);
    return rc;
}

int nvs_blank_check(nvs_fs_t *fs, int *is_blank)
{
    static uint8_t buf[NVS_BLOCK_SIZE];
    size_t total, pos;
    int rc;

    if (!fs || !is_blank) {
        return -EINVAL;
    }

    *is_blank = 1;
    total = (size_t)fs->sector_size * (size_t)fs->sector_count;
    pos = 0U;
    while (pos < total) {
        size_t len = total - pos;
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        rc = fs->flash_ops.flash_read((uint32_t)(fs->offset + (int)pos), buf, len);
        if (rc) {
            *is_blank = 0;
            return rc;
        }
        for (size_t i = 0U; i < len; i++) {
            if (buf[i] != fs->flash_parameters.erase_value) {
                *is_blank = 0;
                break;
            }
        }
        pos += len;
    }
    return 0;
}

int nvs_clear(nvs_fs_t *fs)
{
    int rc;
    uint32_t addr;

    if (!fs->ready) {
        printf("NVS not initialized\r\n");
        return -EACCES;
    }

    for (uint16_t i = 0; i < fs->sector_count; i++) {
        addr = i << ADDR_SECT_SHIFT;
        rc = nvs_flash_erase_sector(fs, addr);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

int nvs_init(nvs_fs_t *fs)
{

    int rc;

    if (fs->flash_parameters.write_block_size > NVS_BLOCK_SIZE || fs->flash_parameters.write_block_size == 0) {
        printf("Unsupported write block size\r\n");
        return -EINVAL;
    }

    if (!fs->sector_size) {
        printf("Invalid sector size\r\n");
        return -EINVAL;
    }

    if (fs->sector_count < 2) {
        printf("Configuration error - sector count\r\n");
        return -EINVAL;
    }

    rc = nvs_startup(fs);
    if (rc) {
        return rc;
    }

    fs->ready = true;

    // printf("%d Sectors of %d bytes \r\n", fs->sector_count, fs->sector_size);
    // printf("alloc wra: %d, %x \r\n",
    //     (fs->ate_wra >> ADDR_SECT_SHIFT),
    //     (fs->ate_wra & ADDR_OFFS_MASK));
    // printf("data wra: %d, %x \r\n",
    //     (fs->data_wra >> ADDR_SECT_SHIFT),
    //     (fs->data_wra & ADDR_OFFS_MASK));

    return 0;
}

size_t nvs_write(nvs_fs_t *fs, const char *key, const void *data, size_t len)
{
    int rc, gc_count;
    size_t ate_size, data_size;
    struct nvs_ate wlk_ate;
    uint32_t wlk_addr, rd_addr;
    uint16_t required_space = 0U;
    bool prev_found = false;

    if (!fs->ready) {
        printf("NVS not initialized\r\n");
        return -EACCES;
    }

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));
    data_size = nvs_al_size(fs, len);

    if ((len > (fs->sector_size - 3 * ate_size)) ||
        ((len > 0) && (data == NULL))) {
        return -EINVAL;
    }

    wlk_addr = fs->ate_wra;
    rd_addr = wlk_addr;

    while (1) {
        rd_addr = wlk_addr;
        rc = nvs_prev_ate(fs, &wlk_addr, &wlk_ate);
        if (rc) {
            return rc;
        }
        if ((strncmp(wlk_ate.key, key, NVS_KEY_SIZE) == 0) && (!nvs_ate_crc8_check(&wlk_ate))) {
            prev_found = true;
            break;
        }
        if (wlk_addr == fs->ate_wra) {
            break;
        }
    }

    if (prev_found) {
        rd_addr &= ADDR_SECT_MASK;
        rd_addr += wlk_ate.offset;

        if (len == 0) {
            if (wlk_ate.len == 0U) {
                return 0;
            }
        } else if (len == wlk_ate.len) {
            rc = nvs_flash_block_cmp(fs, rd_addr, data, len);
            if (rc <= 0) {
                return rc;
            }
        }
    } else {
        if (len == 0) {
            return 0;
        }
    }

    if (data_size) {
        required_space = data_size + ate_size;
    }

    fs->mutex_ops.lock(fs->mutex);

    gc_count = 0;
    while (1) {
        if (gc_count == fs->sector_count) {
            rc = -ENOSPC;
            goto end;
        }

        if (fs->ate_wra >= fs->data_wra + required_space) {

            rc = nvs_flash_wrt_entry(fs, key, data, len);
            if (rc) {
                goto end;
            }
            break;
        }

        rc = nvs_sector_close(fs);
        if (rc) {
            goto end;
        }

        rc = nvs_gc(fs);
        if (rc) {
            goto end;
        }
        gc_count++;
    }
    rc = len;
end:
    fs->mutex_ops.unlock(fs->mutex);
    return rc;
}

int nvs_delete(nvs_fs_t *fs, const char *key)
{
    return nvs_write(fs, key, NULL, 0);
}

size_t nvs_read_hist(nvs_fs_t *fs, const char *key, void *data, size_t len,
              uint16_t cnt)
{
    int rc;
    uint32_t wlk_addr, rd_addr;
    uint16_t cnt_his;
    struct nvs_ate wlk_ate;
    size_t ate_size;

    if (!fs->ready) {
        printf("NVS not initialized \r\n");
        return -EACCES;
    }

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    if (len > (fs->sector_size - 2 * ate_size)) {
        return -EINVAL;
    }

    cnt_his = 0U;

    wlk_addr = fs->ate_wra;
    rd_addr = wlk_addr;

    while (cnt_his <= cnt) {
        rd_addr = wlk_addr;
        rc = nvs_prev_ate(fs, &wlk_addr, &wlk_ate);
        if (rc) {
            goto err;
        }
        if ((strncmp(wlk_ate.key, key, NVS_KEY_SIZE) == 0) &&  (!nvs_ate_crc8_check(&wlk_ate))) {
            cnt_his++;
        }
        if (wlk_addr == fs->ate_wra) {
            break;
        }
    }

    if (((wlk_addr == fs->ate_wra) && (strncmp(wlk_ate.key, key, NVS_KEY_SIZE) != 0)) ||
        (wlk_ate.len == 0U) || (cnt_his < cnt)) {
        return -ENOENT;
    }

    rd_addr &= ADDR_SECT_MASK;
    rd_addr += wlk_ate.offset;
    rc = nvs_flash_rd(fs, rd_addr, data, MIN(len, wlk_ate.len));
    if (rc) {
        goto err;
    }

    return wlk_ate.len;

err:
    return rc;
}

size_t nvs_read(nvs_fs_t *fs, const char *key, void *data, size_t len)
{
    size_t rc;

    fs->mutex_ops.lock(fs->mutex);
    rc = nvs_read_hist(fs, key, data, len, 0);
    fs->mutex_ops.unlock(fs->mutex);
    return rc;
}

size_t nvs_calc_free_space(nvs_fs_t *fs)
{

    int rc;
    struct nvs_ate step_ate, wlk_ate;
    uint32_t step_addr, wlk_addr;
    size_t ate_size, free_space;

    if (!fs->ready) {
        printf("NVS not initialized \r\n");
        return -EACCES;
    }

    ate_size = nvs_al_size(fs, sizeof(struct nvs_ate));

    free_space = 0;
    for (uint16_t i = 1; i < fs->sector_count; i++) {
        free_space += (fs->sector_size - ate_size);
    }

    step_addr = fs->ate_wra;

    while (1) {
        rc = nvs_prev_ate(fs, &step_addr, &step_ate);
        if (rc) {
            return rc;
        }

        wlk_addr = fs->ate_wra;

        while (1) {
            rc = nvs_prev_ate(fs, &wlk_addr, &wlk_ate);
            if (rc) {
                return rc;
            }
            if ((strncmp(wlk_ate.key, step_ate.key, NVS_KEY_SIZE) == 0) ||
                (wlk_addr == fs->ate_wra)) {
                break;
            }
        }

        if ((wlk_addr == step_addr) && step_ate.len &&
            (!nvs_ate_crc8_check(&step_ate))) {
            free_space -= nvs_al_size(fs, step_ate.len);
            free_space -= ate_size;
        }

        if (step_addr == fs->ate_wra) {
            break;
        }

    }
    return free_space;
}

int nvs_entry_find(nvs_fs_t *fs, nvs_iterator_t *it)
{
    if (!fs->ready) return -EACCES;
    it->fs = fs;
    it->curr_addr = fs->ate_wra;
    it->finished = 0;
    it->key_count = 0;
    memset(it->dumped_keys, 0, sizeof(it->dumped_keys));
    return 0;
}

int nvs_entry_info(nvs_iterator_t *it, struct nvs_ate *info)
{
    if (it->finished) return -ENOENT;
    *info = it->curr_ate;
    return 0;
}

int nvs_entry_next(nvs_iterator_t *it)
{
    int rc;
    while (1) {
        rc = nvs_prev_ate(it->fs, &it->curr_addr, &it->curr_ate);
        if (rc) { it->finished = 1; return rc; }
        if (!nvs_ate_crc8_check(&it->curr_ate) && it->curr_ate.len > 0) {
            int already_dumped = 0;
            for (int i = 0; i < it->key_count; i++) {
                if (strncmp(it->dumped_keys[i], it->curr_ate.key, NVS_KEY_SIZE) == 0) {
                    already_dumped = 1;
                    break;
                }
            }
            if (!already_dumped) {
                if (it->key_count >= (int)(sizeof(it->dumped_keys) / sizeof(it->dumped_keys[0]))) {
                    it->finished = 1;
                    return -ENOSPC;
                }
                strncpy(it->dumped_keys[it->key_count], it->curr_ate.key, NVS_KEY_SIZE);
                it->dumped_keys[it->key_count][NVS_KEY_SIZE] = '\0';
                it->key_count++;
                return 0;
            }
        }
        if (it->curr_addr == it->fs->ate_wra) { it->finished = 1; return -ENOENT; }
    }
}

void nvs_release_iterator(nvs_iterator_t *it)
{
    memset(it, 0, sizeof(*it));
}
