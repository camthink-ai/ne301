/*
 * Copyright 2017-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "ext_target_host_table.h"
#include "pageset.h"

#include "driver/morse_driver/hw.h"
#include "driver/morse_driver/morse.h"
#include "driver/transport/morse_transport.h"


#define MM6108_REG_TRGR_BASE            0x100a6000
#define MM6108_REG_INT_BASE             0x100a6050

#define MM6108_REG_MSI                  0x02000000

#define MM6108_REG_MANIFEST_PTR_ADDRESS 0x10054d40

#define MM6108_REG_HOST_MAGIC_VALUE     0xDEADBEEF

#define MM6108_REG_RESET                0x10054050
#define MM6108_REG_RESET_VALUE          0xDEAD

#define MM6108_REG_CHIP_ID              0x10054d20

#define MM6108_REG_CLK_CTRL             0x1005406C
#define MM6108_REG_CLK_CTRL_VALUE       0xef
#define MM6108_REG_EARLY_CLK_CTRL_VALUE 0xe5

#define MM6108_REG_BOOT_ADDR            0x10054024
#define MM6108_REG_BOOT_ADDR_VALUE      0x100000

#define MM6108_REG_AON_ADDR             0x10058094
#define MM6108_REG_AON_LATCH_ADDR       0x1005807C
#define MM6108_REG_AON_LATCH_MASK       0x1

#define MM6108_DMEM_ADDR_START          0x80100000
#define MM6108_DMEM_ADDR_END            0x80480000
#define MM6108_IMEM_ADDR_START          0x00100000
#define MM6108_IMEM_ADDR_END            0x00200000

#define MM6108_REG_XTAL_INIT_SEQ_ADDR_1 0x10012008
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_2 0x1001200C
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_3 0x1005805C
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_4 0x10054000


#define MM6108_REG_XTAL_INIT_SEQ_AON_CLK_VAL 0x19
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_1_VAL  0x2
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_2_VAL  0x2
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_3_VAL  0x21D
#define MM6108_REG_XTAL_INIT_SEQ_ADDR_4_VAL  0x1B06


#define MM610X_BOARD_TYPE_MAX_VALUE (0xF - 1)


#define MM6108_SPI_INTER_BLOCK_DELAY_NANO_S 40000

#define MM6108_REG_EFUSE_DATA_BASE_ADDRESS  0x10054118


#define MM6108A0_ID MORSE_DEVICE_ID(0x6, 2, CHIP_TYPE_SILICON)
#define MM6108A1_ID MORSE_DEVICE_ID(0x6, 3, CHIP_TYPE_SILICON)
#define MM6108A2_ID MORSE_DEVICE_ID(0x6, 4, CHIP_TYPE_SILICON)

static uint8_t mm610x_get_wakeup_delay_ms(uint32_t chip_id)
{

    if ((chip_id == MM6108A0_ID) || (chip_id == MM6108A1_ID))
    {
        return 10;
    }
    else
    {
        return 20;
    }
}

static int mm610x_digital_reset(struct driver_data *driverd)
{
    int ret = 0;
    morse_trns_claim(driverd);

    if (MORSE_REG_RESET(driverd) != 0)
    {
        ret = morse_trns_write_le32(driverd,
                                    MORSE_REG_RESET(driverd),
                                    MORSE_REG_RESET_VALUE(driverd));
        if (ret != MORSE_SUCCESS)
        {
            goto exit;
        }
    }


    mmosal_task_sleep(driverd->cfg->warm_boot_delay_ms);

    if (MORSE_REG_EARLY_CLK_CTRL_VALUE(driverd) != 0)
    {
        ret = morse_trns_write_le32(driverd,
                                    MORSE_REG_CLK_CTRL(driverd),
                                    MORSE_REG_EARLY_CLK_CTRL_VALUE(driverd));
    }

exit:
    morse_trns_release(driverd);

    if (ret)
    {
        MMLOG_ERR("Failed\n");
    }

    return ret;
}

static int mm610x_enable_burst_mode(struct driver_data *driverd)
{
    MM_UNUSED(driverd);
    return MM6108_SPI_INTER_BLOCK_DELAY_NANO_S;
}

static int mm610x_read_board_type(struct driver_data *driverd)
{
    int ret = -EINVAL;
    uint32_t efuse_data2;

    if (MORSE_REG_EFUSE_DATA2(driverd) != 0)
    {
        morse_trns_claim(driverd);
        ret = morse_trns_read_le32(driverd, MORSE_REG_EFUSE_DATA2(driverd), &efuse_data2);
        morse_trns_release(driverd);

        if (ret < 0)
        {
            return ret;
        }

        return (efuse_data2 >> 27) & 0xF;
    }
    return ret;
}

static void mm610x_xtal_init(struct driver_data *driverd)
{

    morse_trns_write_le32(driverd, MM6108_REG_AON_LATCH_ADDR, MM6108_REG_XTAL_INIT_SEQ_AON_CLK_VAL);


    morse_trns_write_le32(driverd,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_1,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_1_VAL);
    morse_trns_write_le32(driverd,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_2,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_2_VAL);


    morse_trns_write_le32(driverd,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_3,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_3_VAL);


    morse_trns_write_le32(driverd,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_4,
                          MM6108_REG_XTAL_INIT_SEQ_ADDR_4_VAL);
}

static const char *mm610x_get_hw_version(uint32_t chip_id)
{
    switch (chip_id)
    {
        case MM6108A0_ID:
            return "MM6108A0";

        case MM6108A1_ID:
            return "MM6108A1";

        case MM6108A2_ID:
            return "MM6108A2";
    }
    return "unknown";
}

static void update_pager_bypass_tx_status_addr(
    struct driver_data *driverd,
    struct extended_host_table_pager_bypass_tx_status *bypass)
{
    driverd->chip_if->bypass.tx_status.location = le32toh(bypass->tx_status_buffer_addr);
    MMLOG_INF("TX Status pager bypass enabled: buffer addr 0x%08lx\n",
              le32toh(bypass->tx_status_buffer_addr));
}

static void update_pager_bypass_cmd_resp_addr(
    struct driver_data *driverd,
    struct extended_host_table_pager_bypass_cmd_resp *bypass)
{
    driverd->chip_if->bypass.cmd_resp.location = le32toh(bypass->cmd_resp_buffer_addr);
    MMLOG_INF("Cmd response pager bypass enabled: buffer addr 0x%08lx\n",
              le32toh(bypass->cmd_resp_buffer_addr));
}

static void update_validate_skb_checksum(
    struct driver_data *driverd,
    struct extended_host_table_insert_skb_checksum *validate_checksum)
{
    driverd->chip_if->validate_skb_checksum = validate_checksum->insert_and_validate_checksum;
    MMLOG_DBG("Validate checksum inserted by fw %s\n",
              validate_checksum->insert_and_validate_checksum ? "enabled" : "disabled");
}

static void update_pager_pkt_memory(struct driver_data *driverd,
                                    struct extended_host_table_pager_pkt_memory *pkt_memory)
{
    MMOSAL_ASSERT(driverd != NULL && driverd->chip_if != NULL);

    driverd->chip_if->pkt_memory.base_addr = le32toh(pkt_memory->base_addr);
    driverd->chip_if->pkt_memory.page_len = pkt_memory->page_len;
    driverd->chip_if->pkt_memory.page_len_reserved = pkt_memory->page_len_reserved;
    driverd->chip_if->pkt_memory.num = pkt_memory->num;
}

static bool mm610x_ext_host_parse_tlv(struct driver_data *driverd,
                                      struct extended_host_table_tlv *tlv)
{
    bool handled = true;

    switch (le16toh(tlv->hdr.tag))
    {
        case MORSE_FW_HOST_TABLE_TAG_PAGER_BYPASS_TX_STATUS:
            update_pager_bypass_tx_status_addr(
                driverd,
                (struct extended_host_table_pager_bypass_tx_status *)tlv);
            break;

        case MORSE_FW_HOST_TABLE_TAG_PAGER_BYPASS_CMD_RESP:
            update_pager_bypass_cmd_resp_addr(
                driverd,
                (struct extended_host_table_pager_bypass_cmd_resp *)tlv);
            break;

        case MORSE_FW_HOST_TABLE_TAG_INSERT_SKB_CHECKSUM:
            update_validate_skb_checksum(driverd,
                                         (struct extended_host_table_insert_skb_checksum *)tlv);
            break;

        case MORSE_FW_HOST_TABLE_TAG_PAGER_PKT_MEMORY:
            update_pager_pkt_memory(driverd, (struct extended_host_table_pager_pkt_memory *)tlv);
            break;

        default:
            handled = false;
            break;
    }

    return handled;
}

static const struct morse_hw_regs mm6108_regs = {

    .irq_base_address = MM6108_REG_INT_BASE,
    .trgr_base_address = MM6108_REG_TRGR_BASE,

    .chip_id_address = MM6108_REG_CHIP_ID,


    .cpu_reset_address = MM6108_REG_RESET,
    .cpu_reset_value = MM6108_REG_RESET_VALUE,


    .manifest_ptr_address = MM6108_REG_MANIFEST_PTR_ADDRESS,


    .msi_address = MM6108_REG_MSI,
    .msi_value = 0x1,

    .magic_num_value = MM6108_REG_HOST_MAGIC_VALUE,


    .clk_ctrl_address = MM6108_REG_CLK_CTRL,
    .clk_ctrl_value = MM6108_REG_CLK_CTRL_VALUE,
    .early_clk_ctrl_value = MM6108_REG_EARLY_CLK_CTRL_VALUE,


    .efuse_data_base_address = MM6108_REG_EFUSE_DATA_BASE_ADDRESS,


    .mem_count = 2,
    .pager_base_address = MM6108_DMEM_ADDR_START,
    .mem = {
        {MM6108_IMEM_ADDR_START, MM6108_IMEM_ADDR_END},
        {MM6108_DMEM_ADDR_START, MM6108_DMEM_ADDR_END},
    },


    .aon_latch = MM6108_REG_AON_LATCH_ADDR,
    .aon_latch_mask = MM6108_REG_AON_LATCH_MASK,
    .aon = MM6108_REG_AON_ADDR,
    .aon_count = 2,
};

const struct mmhal_chip mmhal_mm6108 = {
    .regs = &mm6108_regs,
    .ops = &morse_pageset_hw_ops,
    .get_ps_wakeup_delay_ms = mm610x_get_wakeup_delay_ms,
    .digital_reset = mm610x_digital_reset,
    .enable_sdio_burst_mode = mm610x_enable_burst_mode,
    .get_board_type = mm610x_read_board_type,
    .xtal_init = mm610x_xtal_init,
    .get_hw_version = mm610x_get_hw_version,
    .ext_host_parse_tlv = mm610x_ext_host_parse_tlv,
    .board_type_max_value = MM610X_BOARD_TYPE_MAX_VALUE,
    .warm_boot_delay_ms = 15,
    .xtal_init_sdio_trans_delay_ms = 2,
    .bus_double_read = true,
    .valid_chip_ids = { MM6108A0_ID, MM6108A1_ID, MM6108A2_ID, CHIP_ID_END },
};
