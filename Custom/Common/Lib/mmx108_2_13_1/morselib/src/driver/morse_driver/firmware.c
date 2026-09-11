/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "firmware.h"
#include "mmlog.h"
#include "morse.h"
#include "driver/transport/morse_transport.h"
#include "ext_host_table.h"

static int morse_firmware_reset(struct driver_data *driverd)
{
    MMOSAL_ASSERT(driverd->cfg->digital_reset);
    return driverd->cfg->digital_reset(driverd);
}

static void morse_firmware_clear_aon(struct driver_data *driverd)
{
    int idx;
    uint8_t count = MORSE_REG_AON_COUNT(driverd);
    uint32_t address = MORSE_REG_AON_ADDR(driverd);

    if (address)
    {
        for (idx = 0; idx < count; idx++, address += 4)
        {

            morse_trns_write_le32(driverd, address, 0x0);
        }
    }

    morse_hw_toggle_aon_latch(driverd);
}

static int morse_firmware_trigger(struct driver_data *driverd)
{
    morse_trns_claim(driverd);


    morse_firmware_clear_aon(driverd);

    if (MORSE_REG_CLK_CTRL(driverd) != 0)
    {
        morse_trns_write_le32(driverd,
                              MORSE_REG_CLK_CTRL(driverd),
                              MORSE_REG_CLK_CTRL_VALUE(driverd));
    }

    morse_trns_write_le32(driverd, MORSE_REG_MSI(driverd), MORSE_REG_MSI_HOST_INT(driverd));
    morse_trns_release(driverd);


    mmosal_task_sleep(5);
    return 0;
}

static int morse_firmware_magic_verify(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t magic = ~MORSE_REG_HOST_MAGIC_VALUE(driverd);

    morse_trns_claim(driverd);

    morse_trns_read_le32(driverd,
                         driverd->host_table_ptr + offsetof(struct host_table, magic_number),
                         &magic);

    if (magic != MORSE_REG_HOST_MAGIC_VALUE(driverd))
    {
        MMLOG_ERR("FW magic mismatch 0x%08lx:0x%08lx\n",
                  MORSE_REG_HOST_MAGIC_VALUE(driverd),
                  magic);
        ret = -EIO;
    }
    morse_trns_release(driverd);

    return ret;
}

static int morse_firmware_get_fw_flags(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t fw_flags = 0;

    morse_trns_claim(driverd);

    ret =
        morse_trns_read_le32(driverd,
                             driverd->host_table_ptr + offsetof(struct host_table, firmware_flags),
                             &fw_flags);

    driverd->firmware_flags = le32toh(fw_flags);

    morse_trns_release(driverd);

    return ret;
}

int morse_firmware_check_compatibility(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t fw_version;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;

    morse_trns_claim(driverd);

    ret = morse_trns_read_le32(
        driverd,
        driverd->host_table_ptr + offsetof(struct host_table, fw_version_number),
        &fw_version);

    morse_trns_release(driverd);

    major = MORSE_SEMVER_GET_MAJOR(fw_version);
    minor = MORSE_SEMVER_GET_MINOR(fw_version);
    patch = MORSE_SEMVER_GET_PATCH(fw_version);


    if (ret == 0 &&
        (major != MORSE_DRIVER_SEMVER_MAJOR || (minor + 1) < (MORSE_DRIVER_SEMVER_MINOR + 1)))
    {
        MMLOG_ERR("Incompatible FW version: (Driver) %d.%d.%d, (Chip) %lu.%lu.%lu\n",
                  MORSE_DRIVER_SEMVER_MAJOR,
                  MORSE_DRIVER_SEMVER_MINOR,
                  MORSE_DRIVER_SEMVER_PATCH,
                  major,
                  minor,
                  patch);
        ret = -EPERM;
    }
    return ret;
}

static int morse_firmware_invalidate_host_ptr(struct driver_data *driverd)
{
    int ret;

    driverd->host_table_ptr = 0;
    morse_trns_claim(driverd);
    ret = morse_trns_write_le32(driverd, MORSE_REG_HOST_MANIFEST_PTR(driverd), 0);
    morse_trns_release(driverd);
    return ret;
}


#define HOST_TABLE_POINTER_POLL_TIMEOUT_MS 1500

int morse_firmware_get_host_table_ptr(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t timeout;

    timeout = mmosal_get_time_ms() + HOST_TABLE_POINTER_POLL_TIMEOUT_MS;
    morse_trns_claim(driverd);
    while (1)
    {
        ret = morse_trns_read_le32(driverd,
                                   MORSE_REG_HOST_MANIFEST_PTR(driverd),
                                   &driverd->host_table_ptr);

        if (driverd->host_table_ptr != 0)
        {
            break;
        }

        if (mmosal_time_has_passed(timeout))
        {
            MMLOG_ERR("FW manifest pointer not set\n");
            ret = -EIO;
            break;
        }
        mmosal_task_sleep(5);
    }
    morse_trns_release(driverd);
    return ret;
}

static int morse_firmware_read_ext_host_table(struct driver_data *driverd,
                                              struct extended_host_table **ext_host_table)
{
    int ret = 0;
    uint32_t host_tbl_ptr = driverd->host_table_ptr;
    uint32_t ext_host_tbl_ptr;
    uint32_t ext_host_tbl_ptr_addr =
        host_tbl_ptr + offsetof(struct host_table, extended_host_table_addr);
    uint32_t ext_host_tbl_len;
    uint32_t ext_host_tbl_len_ptr_addr;
    struct extended_host_table *host_tbl = NULL;

    morse_trns_claim(driverd);
    ret = morse_trns_read_le32(driverd, ext_host_tbl_ptr_addr, &ext_host_tbl_ptr);
    if (ret)
    {
        goto exit;
    }


    if (ext_host_tbl_ptr == 0)
    {
        goto exit;
    }

    ext_host_tbl_len_ptr_addr =
        ext_host_tbl_ptr + offsetof(struct extended_host_table, extended_host_table_length);


    ret = morse_trns_read_le32(driverd, ext_host_tbl_len_ptr_addr, &ext_host_tbl_len);
    if (ret)
    {
        goto exit;
    }


    ext_host_tbl_len = (ext_host_tbl_len + 3) & 0xFFFFFFFC;

    host_tbl = (struct extended_host_table *)mmosal_malloc(ext_host_tbl_len);
    if (!host_tbl)
    {
        goto exit;
    }

    ret = morse_trns_read_multi_byte(driverd,
                                     ext_host_tbl_ptr,
                                     (uint8_t *)host_tbl,
                                     ext_host_tbl_len);

    morse_trns_release(driverd);

    if (ret)
    {
        goto exit;
    }

    *ext_host_table = host_tbl;

    return ret;

exit:
    morse_trns_release(driverd);
    if (host_tbl)
    {
        mmosal_free(host_tbl);
    }
    MMLOG_ERR("Failed %d\n", ret);
    return ret;
}


int morse_firmware_parse_extended_host_table(struct driver_data *driverd, uint8_t *mac_addr)
{
    int ret;
    uint8_t *head;
    uint8_t *end;
    struct extended_host_table *ext_host_table = NULL;

    ret = morse_firmware_get_fw_flags(driverd);
    if (ret)
    {
        goto exit;
    }

    ret = morse_firmware_read_ext_host_table(driverd, &ext_host_table);
    if (ret || (ext_host_table == NULL))
    {
        goto exit;
    }

    if (mac_addr != NULL)
    {
        memcpy(mac_addr, ext_host_table->dev_mac_addr, ETH_ALEN);
    }

    MMLOG_INF("Firmware Manifest MAC: " MM_MAC_ADDR_FMT "\n",
              MM_MAC_ADDR_VAL(ext_host_table->dev_mac_addr));

    head = ext_host_table->ext_host_table_data_tlvs;
    end = ((uint8_t *)ext_host_table) + le32toh(ext_host_table->extended_host_table_length);
    ext_host_table_parse_tlvs(driverd, head, end);

    mmosal_free(ext_host_table);
    return ret;
exit:
    MMLOG_ERR("Failed %d\n", ret);
    return ret;
}

int morse_firmware_init(struct driver_data *driverd,
                        morse_file_read_cb_t fw_callback,
                        morse_file_read_cb_t bcf_callback)
{
    int ret = 0, retries = 3;

    while (retries--)
    {

        if (driverd->cfg->pre_load_prepare)
        {
            ret = ret ? ret : driverd->cfg->pre_load_prepare(driverd);
        }

        ret = ret ? ret : morse_firmware_invalidate_host_ptr(driverd);
        ret = ret ? ret : morse_firmware_load_mbin(driverd, fw_callback);
        ret = ret ? ret : morse_bcf_load_mbin(driverd, bcf_callback, driverd->bcf_address);
        ret = ret ? ret : morse_firmware_trigger(driverd);
        ret = ret ? ret : morse_firmware_get_host_table_ptr(driverd);


        ret = ret ? ret : morse_firmware_magic_verify(driverd);
        ret = ret ? ret : morse_firmware_check_compatibility(driverd);

        if (!ret)
        {
            break;
        }


        if (retries != 0)
        {
            ret = morse_firmware_reset(driverd);
        }
    }

    return ret;
}
