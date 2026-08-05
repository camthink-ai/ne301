/*
 * Copyright 2022-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 * Load firmware from Morse Micro firmware TLV binary format (mbin).
 */

#include <errno.h>

#include "morse.h"
#include "firmware.h"
#include "driver/puff/puff.h"
#include "mbin.h"
#include "driver/transport/morse_transport.h"
#include "mmhal_wlan.h"


static void robuf_cleanup(struct mmhal_robuf *robuf)
{
    if (robuf->free_cb != NULL)
    {
        robuf->free_cb(robuf->free_arg);
    }
    memset(robuf, 0, sizeof(*robuf));
}


static int read_into_robuf(struct mmhal_robuf *robuf,
                           morse_file_read_cb_t file_read_cb,
                           uint32_t *offset,
                           uint32_t len)
{
    file_read_cb(*offset, len, robuf);
    if (robuf->buf == NULL || robuf->len < MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH)
    {
        MMLOG_WRN("Failed to read %lu octets @ %08lx (got %lu)\n", len, *offset, robuf->len);
        robuf_cleanup(robuf);
        return -ERANGE;
    }
    else
    {
        if (robuf->len > len)
        {
            robuf->len = len;
        }
        *offset += robuf->len;
        return 0;
    }
}

static int read_tlv_hdr(morse_file_read_cb_t file_read_cb,
                        uint32_t *offset,
                        struct mbin_tlv_hdr *tlv_hdr)
{
    struct mmhal_robuf robuf = { 0 };
    const struct mbin_tlv_hdr *hdr_overlay;


    MM_STATIC_ASSERT(sizeof(*tlv_hdr) <= MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH,
                     "MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH too small");

    int ret = read_into_robuf(&robuf, file_read_cb, offset, sizeof(*tlv_hdr));
    if (ret != 0)
    {
        return ret;
    }

    hdr_overlay = (const struct mbin_tlv_hdr *)robuf.buf;
    tlv_hdr->type = le16toh(hdr_overlay->type);
    tlv_hdr->len = le16toh(hdr_overlay->len);

    robuf_cleanup(&robuf);

    return 0;
}

static int validate_mbin_magic(morse_file_read_cb_t file_read_cb,
                               uint32_t *offset,
                               uint32_t expected_magic_number)
{
    struct mbin_tlv_hdr tlv_hdr;
    int ret = read_tlv_hdr(file_read_cb, offset, &tlv_hdr);
    if (ret != 0)
    {
        MMLOG_WRN("Invalid mbin header (read failed)\n");
        return ret;
    }

    if (tlv_hdr.type != FIELD_TYPE_MAGIC)
    {
        MMLOG_WRN("Invalid mbin header (type=0x%04x)\n", tlv_hdr.type);
        return -EIO;
    }

    if (tlv_hdr.len != sizeof(uint32_t))
    {
        MMLOG_WRN("Invalid mbin header (len=%u)\n", tlv_hdr.len);
        return -EIO;
    }

    struct mmhal_robuf robuf = { 0 };


    MM_STATIC_ASSERT(sizeof(uint32_t) <= MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH,
                     "MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH too small");

    ret = read_into_robuf(&robuf, file_read_cb, offset, tlv_hdr.len);
    if (ret != 0)
    {
        return ret;
    }

    uint32_t magic = 0;
    PACK_LE32(magic, robuf.buf);

    robuf_cleanup(&robuf);

    MMLOG_DBG("Got magic number 0x%08lx\n", magic);

    if (magic != expected_magic_number)
    {
        MMLOG_WRN("Invalid mbin header (invalid magic #; expected 0x%08lx, got 0x%08lx)\n",
                  expected_magic_number,
                  magic);
        return -EFAULT;
    }

    return 0;
}

static int process_bcf_addr(struct driver_data *driverd,
                            morse_file_read_cb_t file_read_cb,
                            uint32_t *file_read_offset,
                            struct mbin_tlv_hdr tlv_hdr)
{
    struct mmhal_robuf robuf = { 0 };

    if (tlv_hdr.len != sizeof(uint32_t))
    {
        MMLOG_WRN("mbin BCF_ADDR section wrong length\n");
        return -EINVAL;
    }


    MM_STATIC_ASSERT(sizeof(uint32_t) <= MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH,
                     "MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH too small");

    int ret = read_into_robuf(&robuf, file_read_cb, file_read_offset, tlv_hdr.len);
    if (ret != 0)
    {
        return ret;
    }

    PACK_LE32(driverd->bcf_address, robuf.buf);

    robuf_cleanup(&robuf);

    MMLOG_DBG("Got BCF Address 0x%08lx\n", driverd->bcf_address);

    return 0;
}

static int load_from_file_to_chip(struct driver_data *driverd,
                                  morse_file_read_cb_t file_read_cb,
                                  uint32_t *file_read_offset,
                                  uint32_t base_address,
                                  uint32_t length)
{
    struct mmhal_robuf robuf = { 0 };

    MMLOG_DBG("Loading %lu bytes to 0x%08lx\n", length, base_address);

    while (length > 0)
    {
        int ret = read_into_robuf(&robuf, file_read_cb, file_read_offset, length);
        if (ret != 0)
        {
            return ret;
        }

        uint32_t write_length = robuf.len;

        MMLOG_VRB("  ... write %lu bytes to 0x%08lx\n", write_length, base_address);
        morse_trns_claim(driverd);
        ret = morse_trns_write_multi_byte(driverd, base_address, robuf.buf, write_length);
        morse_trns_release(driverd);

        robuf_cleanup(&robuf);

        if (ret != 0)
        {
            MMLOG_WRN("Failed to write %lu octets to %08lx\n", write_length, base_address);
            return ret;
        }

        MMOSAL_ASSERT(write_length <= length);
        length -= write_length;
        base_address += write_length;
    }

    return 0;
}

static int process_segment(struct driver_data *driverd,
                           morse_file_read_cb_t file_read_cb,
                           uint32_t *file_read_offset,
                           struct mbin_tlv_hdr tlv_hdr)
{
    struct mmhal_robuf robuf = { 0 };
    const struct mbin_segment_hdr *seg_hdr;


    MM_STATIC_ASSERT(sizeof(*seg_hdr) <= MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH,
                     "MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH too small");

    int ret = read_into_robuf(&robuf, file_read_cb, file_read_offset, sizeof(*seg_hdr));
    if (ret != 0)
    {
        return ret;
    }

    MMOSAL_ASSERT(robuf.len >= sizeof(*seg_hdr));

    seg_hdr = (const struct mbin_segment_hdr *)robuf.buf;

    uint32_t base_address = le32toh(seg_hdr->base_address);

    ret = load_from_file_to_chip(driverd,
                                 file_read_cb,
                                 file_read_offset,
                                 base_address,
                                 tlv_hdr.len - sizeof(*seg_hdr));
    robuf_cleanup(&robuf);

    return ret;
}

static int process_segment_deflated(struct driver_data *driverd,
                                    morse_file_read_cb_t file_read_cb,
                                    uint32_t *file_read_offset,
                                    struct mbin_tlv_hdr tlv_hdr)
{
    struct mmhal_robuf robuf = { 0 };
    uint8_t *buf = NULL;
    unsigned long src_len;
    unsigned long dst_len;

    int ret = read_into_robuf(&robuf, file_read_cb, file_read_offset, tlv_hdr.len);
    if (ret != 0)
    {
        return ret;
    }

    if (robuf.len < tlv_hdr.len)
    {
        MMLOG_ERR("Deflated chunk read too short (expected %u bytes, got %lu bytes)\n",
                  tlv_hdr.len,
                  robuf.len);
        return -EFAULT;
    }

    const struct mbin_deflated_segment_hdr *seg_hdr =
        (const struct mbin_deflated_segment_hdr *)robuf.buf;

    uint32_t base_address = le32toh(seg_hdr->base_address);
    uint16_t chunk_size = le16toh(seg_hdr->chunk_size);
    uint16_t rounded_chunk_size = FAST_ROUND_UP(chunk_size, 4);

    buf = (uint8_t *)mmosal_malloc(rounded_chunk_size);
    if (buf == NULL)
    {
        MMLOG_WRN("Failed to allocate %u octets for fw chunk\n", rounded_chunk_size);
        ret = -ENOMEM;
        goto cleanup;
    }


    *((uint32_t *)buf + rounded_chunk_size - 4) = 0;

    src_len = tlv_hdr.len - sizeof(*seg_hdr);
    dst_len = chunk_size;

    MMLOG_DBG("Found compressed segment; compressed len=%lu, decompressed len=%lu %02x %02x\n",
              src_len,
              dst_len,
              *(robuf.buf + sizeof(*seg_hdr)),
              *(robuf.buf + sizeof(*seg_hdr) + 1));

    if ((seg_hdr->zlib_header[0] >> 4) != 7)
    {
        MMLOG_WRN("Firmware segment uses unsupported compression (%02x %02x)\n",
                  seg_hdr->zlib_header[0],
                  seg_hdr->zlib_header[1]);
    }

    ret = puff(buf, &dst_len, robuf.buf + sizeof(*seg_hdr), &src_len);
    if (ret != 0)
    {
        MMLOG_WRN("Failed to decompress fw chunk for %08lx: %d\n", base_address, ret);
        goto cleanup;
    }

    if (dst_len != chunk_size)
    {
        MMLOG_WRN("Firmware decompressed size invalid (%lu, expect %u)\n", dst_len, chunk_size);
        ret = -EFAULT;
        goto cleanup;
    }

    MMLOG_DBG("Writing segment dest=0x%08lx, len=%u\n", base_address, tlv_hdr.len);

    morse_trns_claim(driverd);
    ret = morse_trns_write_multi_byte(driverd, base_address, buf, rounded_chunk_size);
    if (ret != 0)
    {
        MMLOG_WRN("Failed to write %u octets to %08lx\n", rounded_chunk_size, base_address);
    }
    morse_trns_release(driverd);

cleanup:
    mmosal_free(buf);
    robuf_cleanup(&robuf);
    return ret;
}

int morse_firmware_load_mbin(struct driver_data *driverd, morse_file_read_cb_t file_read_cb)
{
    struct mbin_tlv_hdr tlv_hdr;
    uint32_t offset = 0;
    int ret;
    bool eof = false;

    MMLOG_DBG("Beginning firmware load\n");

    ret = validate_mbin_magic(file_read_cb, &offset, MBIN_FW_MAGIC_NUMBER);
    if (ret != 0)
    {
        MMLOG_ERR("Malformed FW file (%s)\n", "invalid header");
        return ret;
    }

    while (!eof && ((ret = read_tlv_hdr(file_read_cb, &offset, &tlv_hdr)) == 0))
    {
        switch (tlv_hdr.type)
        {
            case FIELD_TYPE_FW_TLV_BCF_ADDR:
                ret = process_bcf_addr(driverd, file_read_cb, &offset, tlv_hdr);
                if (ret != 0)
                {
                    return ret;
                }
                break;

            case FIELD_TYPE_FW_SEGMENT:
                ret = process_segment(driverd, file_read_cb, &offset, tlv_hdr);
                if (ret != 0)
                {
                    return ret;
                }
                break;

            case FIELD_TYPE_FW_SEGMENT_DEFLATED:
                ret = process_segment_deflated(driverd, file_read_cb, &offset, tlv_hdr);
                if (ret != 0)
                {
                    return ret;
                }
                break;

            case FIELD_TYPE_EOF:
                MMLOG_DBG("EOF TLV found\n");
                ret = 0;
                eof = true;
                break;

            default:
                MMLOG_WRN("Skipping unrecognised fw tlv %04x\n", tlv_hdr.type);
                offset += tlv_hdr.len;
                break;
        }
    }

    if (ret != 0)
    {
        MMLOG_WRN("Firmware load failed with code %d\n", ret);
    }
    else
    {
        MMLOG_DBG("Firmware load completed successfully\n");
    }

    return ret;
}

int morse_bcf_load_mbin(struct driver_data *driverd,
                        morse_file_read_cb_t file_read_cb,
                        unsigned int bcf_address)
{
    struct mbin_tlv_hdr tlv_hdr;
    uint32_t offset = 0;
    int ret;

    MMLOG_DBG("Loading BCF to 0x%08x\n", bcf_address);

    ret = validate_mbin_magic(file_read_cb, &offset, MBIN_BCF_MAGIC_NUMBER);
    if (ret != 0)
    {
        MMLOG_ERR("Malformed BCF file (%s)\n", "invalid header");
        return ret;
    }

    ret = read_tlv_hdr(file_read_cb, &offset, &tlv_hdr);
    if (ret != 0)
    {
        return ret;
    }

    if (tlv_hdr.type != FIELD_TYPE_BCF_BOARD_CONFIG)
    {
        MMLOG_ERR("Malformed BCF file (%s)\n", "missing board_config");
        return -EINVAL;
    }

    MMLOG_INF("Write BCF board_config to chip - addr %x size %d\n", bcf_address, tlv_hdr.len);
    ret = load_from_file_to_chip(driverd, file_read_cb, &offset, bcf_address, tlv_hdr.len);
    if (ret != 0)
    {
        return ret;
    }

    bcf_address += tlv_hdr.len;

    while ((ret = read_tlv_hdr(file_read_cb, &offset, &tlv_hdr)) == 0)
    {
        if (tlv_hdr.type == FIELD_TYPE_EOF)
        {
            ret = -ERANGE;
            break;
        }
        else if (tlv_hdr.type == FIELD_TYPE_BCF_REGDOM)
        {
            struct mmhal_robuf robuf = { 0 };
            const struct mbin_regdom_hdr *hdr;
            bool country_matched = false;


            MM_STATIC_ASSERT(sizeof(*hdr) <= MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH,
                             "MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH too small");

            MMOSAL_ASSERT(tlv_hdr.len > sizeof(*hdr));

            ret = read_into_robuf(&robuf, file_read_cb, &offset, sizeof(*hdr));
            if (ret != 0)
            {
                return ret;
            }

            MMOSAL_ASSERT(robuf.len >= sizeof(*hdr));
            hdr = (const struct mbin_regdom_hdr *)robuf.buf;

            country_matched = (hdr->country_code[0] == driverd->country[0] &&
                               hdr->country_code[1] == driverd->country[1]);

            robuf_cleanup(&robuf);

            if (country_matched)
            {
                MMLOG_DBG("Found matching regdom section\n");
                ret = load_from_file_to_chip(driverd,
                                             file_read_cb,
                                             &offset,
                                             bcf_address,
                                             tlv_hdr.len - sizeof(*hdr));
                break;
            }
            else
            {
                offset += tlv_hdr.len - sizeof(*hdr);
            }
        }
        else
        {
            MMLOG_WRN("Skipping unrecognised BCF tlv 0x%04x\n", tlv_hdr.type);
            offset += tlv_hdr.len;
            continue;
        }
    }

    if (ret == -ERANGE)
    {
        MMLOG_ERR("Possible malformed BCF file. Unable to find regdom section for '%s'\n",
                  driverd->country);
    }
    else if (ret != 0)
    {
        MMLOG_WRN("BCF load failed with code %d\n", ret);
    }
    else
    {
        MMLOG_DBG("BCF load copmleted successfully\n");
    }

    return ret;
}

int morse_bcf_get_metadata(struct mmwlan_bcf_metadata *metadata)
{
    morse_file_read_cb_t file_read_cb = mmhal_wlan_read_bcf_file;
    struct mbin_tlv_hdr tlv_hdr;
    uint32_t offset = 0;
    int ret;
    bool eof = false;
    bool board_config_found = false;

    memset(metadata, 0, sizeof(*metadata));

    ret = validate_mbin_magic(file_read_cb, &offset, MBIN_BCF_MAGIC_NUMBER);
    if (ret != 0)
    {
        MMLOG_ERR("Malformed BCF file (%s)\n", "invalid header");
        return -1;
    }

    while (!eof && ((ret = read_tlv_hdr(file_read_cb, &offset, &tlv_hdr)) == 0))
    {
        switch (tlv_hdr.type)
        {
            case FIELD_TYPE_BCF_BOARD_CONFIG:
            {
                if (tlv_hdr.len >= 12)
                {
                    struct mmhal_robuf robuf = { 0 };
                    file_read_cb(offset + 8, 4, &robuf);
                    metadata->version.major = robuf.buf[0] | robuf.buf[1] << 8;
                    metadata->version.minor = robuf.buf[2];
                    metadata->version.patch = robuf.buf[3];
                    board_config_found = true;
                    robuf_cleanup(&robuf);
                }
                break;
            }

            case FIELD_TYPE_BCF_BOARD_DESC:
            {
                size_t read_len = MM_MIN(tlv_hdr.len, sizeof(metadata->board_desc) - 1);
                struct mmhal_robuf robuf = { 0 };
                file_read_cb(offset, read_len, &robuf);
                memcpy(metadata->board_desc, robuf.buf, MM_MIN(robuf.len, read_len));
                robuf_cleanup(&robuf);
                break;
            }

            case FIELD_TYPE_BCF_BUILD_VER:
            {
                size_t read_len = MM_MIN(tlv_hdr.len, sizeof(metadata->build_version) - 1);
                struct mmhal_robuf robuf = { 0 };
                file_read_cb(offset, read_len, &robuf);
                memcpy(metadata->build_version, robuf.buf, MM_MIN(robuf.len, read_len));
                robuf_cleanup(&robuf);
                break;
            }

            case FIELD_TYPE_EOF:
                ret = 0;
                eof = true;
                break;

            default:
                break;
        }
        offset += tlv_hdr.len;
    }

    if (ret == 0 && !board_config_found)
    {
        ret = -ENOENT;
    }

    return ret;
}
