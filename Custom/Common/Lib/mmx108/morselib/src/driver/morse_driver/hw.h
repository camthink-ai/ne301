/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "chip_if.h"
#include "ext_host_table.h"


#define MORSE_REG_ADDRESS_BASE           0x10000
#define MORSE_REG_ADDRESS_WINDOW_0       MORSE_REG_ADDRESS_BASE
#define MORSE_REG_ADDRESS_WINDOW_1       (MORSE_REG_ADDRESS_BASE + 1)
#define MORSE_REG_ADDRESS_CONFIG         (MORSE_REG_ADDRESS_BASE + 2)

#define MORSE_SDIO_RW_ADDR_BOUNDARY_MASK (0xFFFF0000)

#define MORSE_CONFIG_ACCESS_1BYTE        0
#define MORSE_CONFIG_ACCESS_2BYTE        1
#define MORSE_CONFIG_ACCESS_4BYTE        2


#define MORSE_REG_TRGR_BASE(driverd)                  ((driverd)->cfg->regs->trgr_base_address)
#define MORSE_REG_TRGR1_STS(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x00)
#define MORSE_REG_TRGR1_SET(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x04)
#define MORSE_REG_TRGR1_CLR(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x08)
#define MORSE_REG_TRGR1_EN(driverd)                   (MORSE_REG_TRGR_BASE(driverd) + 0x0C)
#define MORSE_REG_TRGR2_STS(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x10)
#define MORSE_REG_TRGR2_SET(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x14)
#define MORSE_REG_TRGR2_CLR(driverd)                  (MORSE_REG_TRGR_BASE(driverd) + 0x18)
#define MORSE_REG_TRGR2_EN(driverd)                   (MORSE_REG_TRGR_BASE(driverd) + 0x1C)

#define MORSE_REG_INT_BASE(driverd)                   ((driverd)->cfg->regs->irq_base_address)
#define MORSE_REG_INT1_STS(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x00)
#define MORSE_REG_INT1_SET(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x04)
#define MORSE_REG_INT1_CLR(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x08)
#define MORSE_REG_INT1_EN(driverd)                    (MORSE_REG_INT_BASE(driverd) + 0x0C)
#define MORSE_REG_INT2_STS(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x10)
#define MORSE_REG_INT2_SET(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x14)
#define MORSE_REG_INT2_CLR(driverd)                   (MORSE_REG_INT_BASE(driverd) + 0x18)
#define MORSE_REG_INT2_EN(driverd)                    (MORSE_REG_INT_BASE(driverd) + 0x1C)

#define MORSE_REG_CHIP_ID(driverd)                    ((driverd)->cfg->regs->chip_id_address)
#define MORSE_REG_EFUSE_DATA0(driverd)                ((driverd)->cfg->regs->efuse_data_base_address)
#define MORSE_REG_EFUSE_DATA1(driverd)                (MORSE_REG_EFUSE_DATA0(driverd) + 0x04)
#define MORSE_REG_EFUSE_DATA2(driverd)                (MORSE_REG_EFUSE_DATA0(driverd) + 0x08)

#define MORSE_REG_MSI(driverd)                        ((driverd)->cfg->regs->msi_address)
#define MORSE_REG_MSI_HOST_INT(driverd)               ((driverd)->cfg->regs->msi_value)

#define MORSE_REG_HOST_MAGIC_VALUE(driverd)           ((driverd)->cfg->regs->magic_num_value)

#define MORSE_REG_RESET(driverd)                      ((driverd)->cfg->regs->cpu_reset_address)
#define MORSE_REG_RESET_VALUE(driverd)                ((driverd)->cfg->regs->cpu_reset_value)

#define MORSE_REG_HOST_MANIFEST_PTR(driverd)          ((driverd)->cfg->regs->manifest_ptr_address)

#define MORSE_REG_EARLY_CLK_CTRL_VALUE(morse)         ((driverd)->cfg->regs->early_clk_ctrl_value)

#define MORSE_REG_CLK_CTRL(driverd)                   ((driverd)->cfg->regs->clk_ctrl_address)
#define MORSE_REG_CLK_CTRL_VALUE(driverd)             ((driverd)->cfg->regs->clk_ctrl_value)

#define MORSE_REG_BOOT_ADDR(driverd)                  ((driverd)->cfg->regs->boot_address)
#define MORSE_REG_BOOT_ADDR_VALUE(driverd)            ((driverd)->cfg->regs->boot_value)

#define IS_MEMORY_ADDRESS(driverd, address)           morse_hw_is_memory((driverd), (address))

#define MORSE_REG_AON_ADDR(driverd)                   ((driverd)->cfg->regs->aon)
#define MORSE_REG_AON_COUNT(driverd)                  ((driverd)->cfg->regs->aon_count)
#define MORSE_REG_AON_LATCH_ADDR(driverd)             ((driverd)->cfg->regs->aon_latch)
#define MORSE_REG_AON_LATCH_MASK(driverd)             ((driverd)->cfg->regs->aon_latch_mask)

#define MORSE_INT_BEACON_VIF_MASK_ALL                 (GENMASK(24, 17))
#define MORSE_INT_BEACON_BASE_NUM                     (17)

#define MORSE_INT_NDP_PROBE_REQ_PV0_NUM               (18)
#define MORSE_INT_NDP_PROBE_REQ_PV0_MASK              (1 << MORSE_INT_NDP_PROBE_REQ_PV0_NUM)
#define MORSE_INT_NDP_PROBE_REQ_PV1_NUM               (19)
#define MORSE_INT_NDP_PROBE_REQ_PV1_MASK              (1 << MORSE_INT_NDP_PROBE_REQ_PV1_NUM)

#define MORSE_WAKEPIN_RPI_GPIO_DEFAULT                (3)
#define MORSE_ASYNC_WAKEUP_FROM_CHIP_RPI_GPIO_DEFAULT (7)
#define MORSE_RESETPIN_RPI_GPIO_DEFAULT               (5)
#define MORSE_SPI_HW_IRQ_RPI_GPIO_DEFAULT             (25)


#define MORSE_INT_HW_STOP_NOTIFICATION_NUM (27)
#define MORSE_INT_HW_STOP_NOTIFICATION     BIT(MORSE_INT_HW_STOP_NOTIFICATION_NUM)


#define MM610X_EFUSE_DATA2_XTAL_WAIT_POS GENMASK(25, 22)


#define MM610X_EFUSE_DATA2_SUPPLEMENTAL_CHIP_ID GENMASK(23, 16)


#define MM610X_EFUSE_DATA1_8MHZ_SUPPORT BIT(18)


#define CHIP_TYPE_SILICON 0x0
#define CHIP_TYPE_FPGA    0x1


#define CHIP_ID_END 0xFFFFFFFF


#define MORSE_HW_MEMORY_MAX 8


#define MAX_CONSEC_FAILURES (5)

enum host_table_firmware_flags
{

    MORSE_FW_FLAGS_SUPPORT_S1G = BIT(0),

    MORSE_FW_FLAGS_BUSY_ACTIVE_LOW = BIT(1),

    MORSE_FW_FLAGS_REPORTS_TX_BEACON_COMPLETION = BIT(2),

    MORSE_FW_FLAGS_SUPPORT_HW_SCAN = BIT(3),

    MORSE_FW_FLAGS_SUPPORT_CHIP_HALT_IRQ = BIT(4),

    MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_BASELINE = BIT(5),

    MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_INDIV_ADDR_QOS_DATA = BIT(6),

    MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_QOS_NULL = BIT(7),

    MORSE_FW_FLAGS_TOGGLES_BUSY_PIN_ON_WAKE_PIN = BIT(8),

    MORSE_FW_FLAGS_SUPPORT_HW_REATTACH = BIT(9),
};

struct MM_PACKED host_table
{
    uint32_t magic_number;
    uint32_t fw_version_number;
    uint32_t host_flags;

    uint32_t firmware_flags;
    uint32_t memcmd_cmd_addr;
    uint32_t memcmd_resp_addr;
    uint32_t extended_host_table_addr;
    struct morse_chip_if_host_table chip_if;
};


struct morse_hw_memory
{
    uint32_t start;
    uint32_t end;
};

struct morse_hw_regs
{
    uint32_t irq_base_address;
    uint32_t trgr_base_address;
    uint32_t cpu_reset_address;
    uint32_t cpu_reset_value;
    uint32_t msi_address;
    uint32_t msi_value;
    uint32_t chip_id_address;
    uint32_t manifest_ptr_address;
    uint32_t host_table_address;
    uint32_t magic_num_value;
    uint32_t clk_ctrl_address;
    uint32_t clk_ctrl_value;
    uint32_t early_clk_ctrl_value;
    uint32_t boot_address;
    uint32_t mac_boot_value;
    uint32_t efuse_data_base_address;
    uint8_t mem_count;
    uint32_t pager_base_address;
    struct morse_hw_memory mem[MORSE_HW_MEMORY_MAX];
    uint32_t aon_latch;
    uint32_t aon_latch_mask;
    uint32_t aon;
    uint8_t aon_count;
};

struct mmhal_chip
{
    const struct morse_hw_regs *regs;
    const struct chip_if_ops *ops;


    const char *(*get_hw_version)(uint32_t chip_id);


    uint8_t (*get_ps_wakeup_delay_ms)(uint32_t chip_id);


    int (*enable_sdio_burst_mode)(struct driver_data *driverd);


    int (*get_board_type)(struct driver_data *driverd);


    int (*pre_load_prepare)(struct driver_data *driverd);


    void (*xtal_init)(struct driver_data *driverd);


    int (*digital_reset)(struct driver_data *driverd);


    bool (*ext_host_parse_tlv)(struct driver_data *driverd, struct extended_host_table_tlv *tlv);


    bool bus_double_read;

    uint32_t board_type_max_value;
    uint32_t fw_count;
    uint32_t mm_reset_gpio;
    uint32_t mm_wake_gpio;
    uint32_t mm_ps_async_gpio;
    uint32_t mm_spi_irq_gpio;

    uint32_t warm_boot_delay_ms;

    uint32_t xtal_init_sdio_trans_delay_ms;
    uint32_t valid_chip_ids[];
};

int morse_hw_irq_enable(struct driver_data *driverd, uint32_t irq, bool enable);

int morse_hw_irq_handle(struct driver_data *driverd);

int morse_hw_irq_clear(struct driver_data *driverd);


void morse_hw_toggle_aon_latch(struct driver_data *driverd);


void morse_xtal_init_delay(void);


bool morse_hw_is_memory(struct driver_data *driverd, uint32_t addr);


bool is_efuse_xtal_wait_supported(struct driver_data *driverd);


bool morse_hw_is_valid_chip_id(uint32_t chip_id, const uint32_t *valid_chip_ids);


void morse_hw_pager_update_consec_failure_cnt(struct driver_data *driverd, int ret);
