/*
 * Copyright 2017-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "morse.h"
#include "hw.h"
#include "driver/morse_driver/mm6108/pager_if.h"
#include "driver/beacon/beacon.h"
#include "driver/health/driver_health.h"
#include "driver/transport/morse_transport.h"
#include "mmhal_wlan.h"

int morse_hw_irq_enable(struct driver_data *driverd, uint32_t irq, bool enable)
{

    MMOSAL_DEV_ASSERT(irq < 32);

    uint32_t irq_en,
        irq_en_addr = irq < 32 ? MORSE_REG_INT1_EN(driverd) : MORSE_REG_INT2_EN(driverd);
    uint32_t irq_clr_addr = irq < 32 ? MORSE_REG_INT1_CLR(driverd) : MORSE_REG_INT2_CLR(driverd);
    uint32_t mask = irq < 32 ? (1 << irq) : (1 << (irq - 32));

    morse_trns_claim(driverd);
    morse_trns_read_le32(driverd, irq_en_addr, &irq_en);
    if (enable)
    {
        irq_en |= (mask);
    }
    else
    {
        irq_en &= ~(mask);
    }
    morse_trns_write_le32(driverd, irq_clr_addr, mask);
    morse_trns_write_le32(driverd, irq_en_addr, irq_en);
    morse_trns_release(driverd);

    return 0;
}

int morse_hw_irq_handle(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t status1 = 0;

    ret = morse_trns_read_le32(driverd, MORSE_REG_INT1_STS(driverd), &status1);
    if (ret != 0)
    {

        ret = morse_trns_read_le32(driverd, MORSE_REG_INT1_STS(driverd), &status1);
        if (ret != 0)
        {
            goto exit;
        }
    }

    if (status1 & MORSE_CHIP_IF_IRQ_MASK_ALL)
    {
        ret = driverd->cfg->ops->chip_if_handle_irq(driverd, status1);
        if (ret != 0)
        {
            goto exit;
        }
    }
    if (status1 & MORSE_INT_BEACON_VIF_MASK_ALL)
    {
        morse_beacon_irq_handle(driverd, status1 & MORSE_INT_BEACON_VIF_MASK_ALL);
    }

    if (status1 & MORSE_INT_HW_STOP_NOTIFICATION)
    {
        driver_health_demand_check(driverd);
    }

    if (status1 != 0)
    {
        ret = morse_trns_write_le32(driverd, MORSE_REG_INT1_CLR(driverd), status1);
        if (ret != 0)
        {
            goto exit;
        }
    }

    mmhal_wlan_set_spi_irq_enabled(true);

exit:
    return ret;
}

int morse_hw_irq_clear(struct driver_data *driverd)
{
    morse_trns_claim(driverd);
    morse_trns_write_le32(driverd, MORSE_REG_INT1_CLR(driverd), 0xFFFFFFFF);
    morse_trns_write_le32(driverd, MORSE_REG_INT2_CLR(driverd), 0xFFFFFFFF);
    morse_trns_release(driverd);
    return 0;
}

void morse_hw_toggle_aon_latch(struct driver_data *driverd)
{
    uint32_t address = MORSE_REG_AON_LATCH_ADDR(driverd);
    uint32_t mask = MORSE_REG_AON_LATCH_MASK(driverd);
    uint32_t latch;

    if (address)
    {

        morse_trns_read_le32(driverd, address, &latch);
        morse_trns_write_le32(driverd, address, latch & ~(mask));
        mmosal_task_sleep(5);
        morse_trns_write_le32(driverd, address, latch | mask);
        mmosal_task_sleep(5);
        morse_trns_write_le32(driverd, address, latch & ~(mask));
        mmosal_task_sleep(5);
    }
}

bool is_efuse_xtal_wait_supported(struct driver_data *driverd)
{
    int ret;
    uint32_t efuse_data2;
    uint32_t efuse_xtal_wait;

    if (MORSE_REG_EFUSE_DATA0(driverd) == 0)
    {

        return true;
    }

    if (MORSE_REG_EFUSE_DATA2(driverd) != 0)
    {
        morse_trns_claim(driverd);
        ret = morse_trns_read_le32(driverd, MORSE_REG_EFUSE_DATA2(driverd), &efuse_data2);
        morse_trns_release(driverd);
        if (ret < 0)
        {
            MMLOG_ERR("EFuse data2 value read failed: %d\n", ret);
            return false;
        }
        efuse_xtal_wait = (efuse_data2 & MM610X_EFUSE_DATA2_XTAL_WAIT_POS);
        if (!efuse_xtal_wait)
        {
            MMLOG_ERR("EFuse xtal wait bits not set\n");
            return false;
        }
        return true;
    }
    return false;
}

bool morse_hw_is_memory(struct driver_data *driverd, uint32_t addr)
{
    int i;

    for (i = 0; i < driverd->cfg->regs->mem_count; i++)
    {
        if ((addr >= driverd->cfg->regs->mem[i].start) && (addr <= driverd->cfg->regs->mem[i].end))
        {
            return true;
        }
    }

    return false;
}

bool morse_hw_is_valid_chip_id(uint32_t chip_id, const uint32_t *valid_chip_ids)
{
    int i;

    MMOSAL_DEV_ASSERT(chip_id != CHIP_ID_END);

    for (i = 0; valid_chip_ids[i] != CHIP_ID_END; i++)
    {
        if (chip_id == valid_chip_ids[i])
        {
            return true;
        }
    }
    return false;
}

void morse_hw_pager_update_consec_failure_cnt(struct driver_data *driverd, int ret)
{
    if (ret == 0)
    {
        driverd->pageset_consec_failure_cnt = 0;
    }
    else if ((ret != -EAGAIN) && (ret != -ENOMEM))
    {
        driverd->pageset_consec_failure_cnt++;
        MMLOG_WRN("Err: %d; Count %d\n", ret, driverd->pageset_consec_failure_cnt);
        if (driverd->pageset_consec_failure_cnt > MAX_CONSEC_FAILURES)
        {
            driverd->pageset_consec_failure_cnt = 0;
            driver_health_demand_check(driverd);
        }
    }
}
