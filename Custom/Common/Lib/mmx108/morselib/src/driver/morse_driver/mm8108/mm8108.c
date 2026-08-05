/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#include <errno.h>
#include <stdint.h>

#include "mmutils.h"

#include "ext_target_host_table.h"
#include "yaps.h"

#include "driver/morse_driver/hw.h"
#include "driver/morse_driver/morse.h"
#include "driver/transport/morse_transport.h"


#define MM8108_REG_TRGR_BASE            0x00003c00
#define MM8108_REG_INT_BASE             0x00003c50

#define MM8108_REG_MSI                  0x00004100

#define MM8108_REG_MANIFEST_PTR_ADDRESS 0x00002d40

#define MM8108_REG_HOST_MAGIC_VALUE     0xDEADBEEF

#define MM8108_REG_RESET                0x000020AC
#define MM8108_REG_RESET_VALUE          0xDEAD

#define MM8108_REG_CHIP_ID              0x00002d20

#define MM8108_REG_BOOT_ADDR            0x00002084
#define MM8108_REG_BOOT_ADDR_VALUE      0x100000

#define MM8108_REG_AON_ADDR             0x00002114
#define MM8108_REG_AON_LATCH_ADDR       0x00405020
#define MM8108_REG_AON_LATCH_MASK       0x1


#define MM8108_SDIO_DEVICE_REG_ADDR 0x0000207C

#define MM8108_SDIO_DEVICE_BURST_OFFSET        9

#define MM8108_APPSMAC_APPS_DMEM_ADDR_START    0x00100000
#define MM8108_APPSMAC_APPS_DMEM_ADDR_END      0x00120000

#define MM8108_APPSMAC_MAC_DMEM_ADDR_START     0x00120000
#define MM8108_APPSMAC_MAC_DMEM_ADDR_END       0x00130000

#define MM8108_APPSMAC_MEMBANK_IMEM_ADDR_START 0x00200000
#define MM8108_APPSMAC_MEMBANK_IMEM_ADDR_END   0x002a8000

#define MM8108_PHY_UPHY_MEM_ADDR_START         0x00500000
#define MM8108_PHY_UPHY_MEM_ADDR_END           0x00520000

#define MM8108_PHY_UPHY_AON_ADDR_START         0x00538000
#define MM8108_PHY_UPHY_AON_ADDR_END           0x0053C000

#define MM8108_PHY_LPHY_MEM_ADDR_START         0x00540000
#define MM8108_PHY_LPHY_MEM_ADDR_END           0x00550000

#define MM8108_PHY_LPHY_AON_ADDR_START         0x0055c000
#define MM8108_PHY_LPHY_AON_ADDR_END           0x0055e000


#define MM8108_REG_RC_CLK_POWER_OFF_ADDR        0x00405020
#define MM8108_REG_RC_CLK_POWER_OFF_MASK        0x00000040
#define MM8108_SLOW_RC_POWER_ON_DELAY_MS        2

#define MM8108_SPI_INTER_BLOCK_DELAY_BURST16_NS 4800
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST8_NS  8000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST4_NS  15000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST2_NS  30000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST0_NS  58000

#define HOST_YAPS_STARTS                        0x00170000
#define HOST_YAPS_ENDS                          0x00180000


#define MM8108XX_ID 0x9


#define MM8108B0_REV 0x6
#define MM8108B1_REV 0x7
#define MM8108B2_REV 0x8


#define MM8108B0_ID MORSE_DEVICE_ID(MM8108XX_ID, MM8108B0_REV, CHIP_TYPE_SILICON)
#define MM8108B1_ID MORSE_DEVICE_ID(MM8108XX_ID, MM8108B1_REV, CHIP_TYPE_SILICON)
#define MM8108B2_ID MORSE_DEVICE_ID(MM8108XX_ID, MM8108B2_REV, CHIP_TYPE_SILICON)

enum mm810x_sdio_burst_mode
{

    SDIO_WORD_BURST_DISABLE = 0,
    SDIO_WORD_BURST_SIZE_0 = 0,
    SDIO_WORD_BURST_SIZE_2 = 1,
    SDIO_WORD_BURST_SIZE_4 = 2,
    SDIO_WORD_BURST_SIZE_8 = 3,
    SDIO_WORD_BURST_SIZE_16 = 4,
    SDIO_WORD_BURST_MASK = 7,
};

static uint8_t mm810x_get_wakeup_delay_ms(uint32_t chip_id)
{
    MM_UNUSED(chip_id);
    return 10;
}

static uint32_t mm810x_get_burst_mode_inter_block_delay_ns(const uint8_t burst_mode)
{
    int ret;

    switch (burst_mode)
    {
        case SDIO_WORD_BURST_SIZE_16:
            ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST16_NS;
            break;

        case SDIO_WORD_BURST_SIZE_8:
            ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST8_NS;
            break;

        case SDIO_WORD_BURST_SIZE_4:
            ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST4_NS;
            break;

        case SDIO_WORD_BURST_SIZE_2:
            ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST2_NS;
            break;

        default:
            ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST0_NS;
            break;
    }

    return ret;
}

static int mm810x_enable_burst_mode(struct driver_data *driverd)
{
    const uint8_t burst_mode = SDIO_WORD_BURST_SIZE_16;
    uint32_t reg32_value;
    int ret = mm810x_get_burst_mode_inter_block_delay_ns(burst_mode);

    MORSE_WARN_ON(driverd == NULL);

    MMLOG_INF("Enabling SPI Burst mode, Mask = 0x%08lx\n",
              (uint32_t)(SDIO_WORD_BURST_SIZE_16 << MM8108_SDIO_DEVICE_BURST_OFFSET));

    morse_trns_claim(driverd);

    if (morse_trns_read_le32(driverd, MM8108_SDIO_DEVICE_REG_ADDR, &reg32_value))
    {
        ret = -EPERM;
        goto exit;
    }

    reg32_value &= ~(uint32_t)(SDIO_WORD_BURST_MASK << MM8108_SDIO_DEVICE_BURST_OFFSET);
    reg32_value |= (uint32_t)(burst_mode << MM8108_SDIO_DEVICE_BURST_OFFSET);

    if (morse_trns_write_le32(driverd, MM8108_SDIO_DEVICE_REG_ADDR, reg32_value))
    {
        ret = -EPERM;
        goto exit;
    }

exit:
    morse_trns_release(driverd);
    if (ret < 0)
    {
        MMLOG_ERR("Failed\n");
    }
    return ret;
}

static const struct morse_hw_regs mm8108_regs = {

    .irq_base_address = MM8108_REG_INT_BASE,
    .trgr_base_address = MM8108_REG_TRGR_BASE,

    .chip_id_address = MM8108_REG_CHIP_ID,


    .cpu_reset_address = MM8108_REG_RESET,
    .cpu_reset_value = MM8108_REG_RESET_VALUE,


    .manifest_ptr_address = MM8108_REG_MANIFEST_PTR_ADDRESS,


    .msi_address = MM8108_REG_MSI,
    .msi_value = 0x1,

    .magic_num_value = MM8108_REG_HOST_MAGIC_VALUE,


    .early_clk_ctrl_value = 0,



    .efuse_data_base_address = 0,


    .mem_count = 8,
    .pager_base_address = MM8108_APPSMAC_APPS_DMEM_ADDR_START,
    .mem = {
        {HOST_YAPS_STARTS, HOST_YAPS_ENDS},
        {MM8108_APPSMAC_APPS_DMEM_ADDR_START, MM8108_APPSMAC_APPS_DMEM_ADDR_END},
        {MM8108_APPSMAC_MAC_DMEM_ADDR_START, MM8108_APPSMAC_MAC_DMEM_ADDR_END},
        {MM8108_APPSMAC_MEMBANK_IMEM_ADDR_START, MM8108_APPSMAC_MEMBANK_IMEM_ADDR_END},
        {MM8108_PHY_UPHY_MEM_ADDR_START, MM8108_PHY_UPHY_MEM_ADDR_END},
        {MM8108_PHY_UPHY_AON_ADDR_START, MM8108_PHY_UPHY_AON_ADDR_END},
        {MM8108_PHY_LPHY_MEM_ADDR_START, MM8108_PHY_LPHY_MEM_ADDR_END},
        {MM8108_PHY_LPHY_AON_ADDR_START, MM8108_PHY_LPHY_AON_ADDR_END},
    },


    .aon_latch = MM8108_REG_AON_LATCH_ADDR,
    .aon_latch_mask = MM8108_REG_AON_LATCH_MASK,
    .aon = MM8108_REG_AON_ADDR,
    .aon_count = 2,
};


static int mm810x_enable_internal_slow_clock(struct driver_data *driverd)
{
    uint32_t rc_clock_reg_value;
    int ret = 0;

    MMLOG_INF("Enabling internal slow clock\n");


    ret = morse_trns_read_le32(driverd, MM8108_REG_RC_CLK_POWER_OFF_ADDR, &rc_clock_reg_value);
    if (ret)
    {
        goto exit;
    }

    rc_clock_reg_value &= ~MM8108_REG_RC_CLK_POWER_OFF_MASK;
    ret = morse_trns_write_le32(driverd, MM8108_REG_RC_CLK_POWER_OFF_ADDR, rc_clock_reg_value);
    if (ret)
    {
        goto exit;
    }

    morse_hw_toggle_aon_latch(driverd);


    mmosal_task_sleep(MM8108_SLOW_RC_POWER_ON_DELAY_MS);
exit:
    if (ret)
    {
        MMLOG_ERR("Failed\n");
    }

    return ret;
}

static int mm810x_digital_reset(struct driver_data *driverd)
{
    int ret = 0;
    morse_trns_claim(driverd);

    mm810x_enable_internal_slow_clock(driverd);

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

exit:
    morse_trns_release(driverd);

    if (ret)
    {
        MMLOG_ERR("Failed\n");
    }

    return ret;
}

static const char *mm810x_get_hw_version(uint32_t chip_id)
{
    switch (chip_id)
    {
        case MM8108B0_ID:
            return "MM8108B0";

        case MM8108B1_ID:
            return "MM8108B1";

        case MM8108B2_ID:
            return "MM8108B2";
    }
    return "unknown";
}

static bool mm810x_ext_host_parse_tlv(struct driver_data *driverd,
                                      struct extended_host_table_tlv *tlv)
{
    bool handled = true;

    switch (le16toh(tlv->hdr.tag))
    {
        case MORSE_FW_HOST_TABLE_TAG_YAPS_TABLE:
            morse_yaps_hw_read_table(driverd, (struct morse_yaps_hw_table *)tlv->data);
            break;

        default:
            handled = false;
            break;
    }

    return handled;
}

const struct mmhal_chip mmhal_mm8108 = {
    .regs = &mm8108_regs,
    .ops = &morse_yaps_ops,

    .warm_boot_delay_ms = 15,
    .bus_double_read = false,
    .get_hw_version = mm810x_get_hw_version,
    .enable_sdio_burst_mode = mm810x_enable_burst_mode,
    .get_ps_wakeup_delay_ms = mm810x_get_wakeup_delay_ms,
    .digital_reset = mm810x_digital_reset,
    .ext_host_parse_tlv = mm810x_ext_host_parse_tlv,
    .valid_chip_ids = { MM8108B1_ID, MM8108B2_ID, CHIP_ID_END },
};
