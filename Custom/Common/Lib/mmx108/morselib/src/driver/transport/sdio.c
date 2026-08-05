/*
 * Copyright 2021-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "morse_transport.h"
#include "driver/morse_driver/hw.h"
#include "driver/health/driver_health.h"
#include "driver/driver.h"

#include "mmosal.h"
#include "mmhal_wlan.h"


#define SPI_IRQ_TASK_PRIORITY (MMOSAL_TASK_PRI_HIGH)
#define SPI_IRQ_TASK_STACK    (768)


#define CMD53_MAX_BLOCKS 128


#ifdef ENABLE_TRANSPORT_FSM_TRACE
#include "mmtrace.h"
static mmtrace_channel transport_fsm_channel_handle;
#define TRANSPORT_FSM_TRACE_INIT() \
    transport_fsm_channel_handle = mmtrace_register_channel("transport_fsm")
#define TRANSPORT_FSM_TRACE(_fmt, ...) \
    mmtrace_printf(transport_fsm_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define TRANSPORT_FSM_TRACE_INIT() \
    do {                           \
    } while (0)
#define TRANSPORT_FSM_TRACE(_fmt, ...) \
    do {                               \
    } while (0)
#endif


#ifdef ENABLE_ADDR_BASE_FSM_TRACE
#include "mmtrace.h"
static mmtrace_channel addr_base_fsm_channel_handle;
#define ADDR_BASE_FSM_TRACE_INIT() \
    addr_base_fsm_channel_handle = mmtrace_register_channel("add_base_fsm")
#define ADDR_BASE_FSM_TRACE(_fmt, ...) \
    mmtrace_printf(addr_base_fsm_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define ADDR_BASE_FSM_TRACE_INIT() \
    do {                           \
    } while (0)
#define ADDR_BASE_FSM_TRACE(_fmt, ...) \
    do {                               \
    } while (0)
#endif


enum block_size
{
    BLOCK_SIZE_FN1 = 8,
    BLOCK_SIZE_FN1_LOG2 = 3,
    BLOCK_SIZE_FN2 = 512,
    BLOCK_SIZE_FN2_LOG2 = 9,
};

enum max_block_transfer_size
{
    MAX_BLOCK_TRANSFER_SIZE_FN1 = BLOCK_SIZE_FN1 * CMD53_MAX_BLOCKS,
    MAX_BLOCK_TRANSFER_SIZE_FN2 = BLOCK_SIZE_FN2 * CMD53_MAX_BLOCKS,
};

#define MAX_RETRY 3



#define SDIO_CCCR_IEN_ADDR 0x04u
#define SDIO_CCCR_IEN_IENM (1u)
#define SDIO_CCCR_IEN_IEN1 (1u << 1)

#define SDIO_CCCR_BIC_ADDR 0x07u
#define SDIO_CCCR_BIC_ECSI (1u << 5)


static struct mmosal_mutex *bus_lock = NULL;


static struct mmosal_semb *spi_irq_semb;

static struct mmosal_task *spi_irq_task_handle;


static volatile bool morse_trns_spi_irq_enabled = false;


volatile uint32_t mmhal_spi_irq_poll_interval = 5000;


static uint32_t address_base_fn1_cache;

static uint32_t address_base_fn2_cache;


static volatile bool spi_irq_task_run = false;

static volatile bool spi_irq_task_has_finished = false;


#define MAX_COMM_FAILURES 30

static uint8_t consecutive_comm_failures = 0;

static uint32_t xtal_init_sdio_trans_delay_ms = 0;

void morse_xtal_init_delay(void)
{
    if (xtal_init_sdio_trans_delay_ms)
    {
        mmosal_task_sleep(xtal_init_sdio_trans_delay_ms);
    }
}


static void comms_op_check(struct driver_data *driverd, morse_error_t return_code)
{
    if (return_code != MORSE_SUCCESS)
    {
        consecutive_comm_failures++;
        if (consecutive_comm_failures >= MAX_COMM_FAILURES)
        {
            MMLOG_WRN("%d consecutive comm failures\n", consecutive_comm_failures);
            morse_trns_set_irq_enabled(driverd, false);
            driver_health_demand_check(driverd);
            consecutive_comm_failures = 0;
        }
    }
    else
    {
        consecutive_comm_failures = 0;
    }
}


static morse_error_t morse_cmd52_write(uint32_t address,
                                       uint8_t data,
                                       enum mmhal_sdio_function function)
{
    uint32_t arg = mmhal_make_cmd52_arg(MMHAL_SDIO_WRITE, function, address, data);

    MMOSAL_ASSERT(address <= MMHAL_SDIO_ADDRESS_MAX);

    return mmhal_wlan_sdio_cmd(52, arg, NULL) ? MORSE_FAILED : MORSE_SUCCESS;
}


static morse_error_t morse_cmd52_read(uint32_t address,
                                      uint8_t *data,
                                      enum mmhal_sdio_function function)
{
    int ret;
    uint32_t rsp;
    uint32_t arg = mmhal_make_cmd52_arg(MMHAL_SDIO_READ, function, address, 0);

    MMOSAL_ASSERT(address <= MMHAL_SDIO_ADDRESS_MAX);

    ret = mmhal_wlan_sdio_cmd(52, arg, &rsp);
    if (ret != 0)
    {
        return MORSE_FAILED;
    }

    *data = rsp & 0xff;

    return MORSE_SUCCESS;
}


static morse_error_t morse_cmd53_read(enum mmhal_sdio_function function,
                                      uint32_t address,
                                      uint8_t *data,
                                      uint32_t len)
{
    morse_error_t result = MORSE_SUCCESS;

    enum block_size block_size = BLOCK_SIZE_FN2;
    enum block_size block_size_log2 = BLOCK_SIZE_FN2_LOG2;

    if (function == MMHAL_SDIO_FUNCTION_1)
    {
        block_size = BLOCK_SIZE_FN1;
        block_size_log2 = BLOCK_SIZE_FN1_LOG2;
    }


    uint16_t num_blocks = len >> block_size_log2;
    if (num_blocks > 0)
    {
        int ret;
        uint32_t transfer_size = num_blocks * block_size;
        uint32_t sdio_arg = mmhal_make_cmd53_arg(MMHAL_SDIO_READ,
                                                 function,
                                                 MMHAL_SDIO_MODE_BLOCK,
                                                 address & 0x0000FFFF,
                                                 num_blocks);
        struct mmhal_wlan_sdio_cmd53_read_args args = {
            .sdio_arg = sdio_arg,
            .data = data,
            .transfer_length = num_blocks,
            .block_size = block_size,
        };

        ret = mmhal_wlan_sdio_cmd53_read(&args);
        if (ret != 0)
        {
            result = MORSE_FAILED;
            goto exit;
        }

        address += transfer_size;
        data += transfer_size;
        len -= transfer_size;
    }


    if (len > 0)
    {
        int ret;
        uint32_t sdio_arg = mmhal_make_cmd53_arg(MMHAL_SDIO_READ,
                                                 function,
                                                 MMHAL_SDIO_MODE_BYTE,
                                                 address & 0x0000FFFF,
                                                 len);
        struct mmhal_wlan_sdio_cmd53_read_args args = {
            .sdio_arg = sdio_arg,
            .data = data,
            .transfer_length = len,
            .block_size = 0,
        };

        ret = mmhal_wlan_sdio_cmd53_read(&args);
        if (ret != 0)
        {
            result = MORSE_FAILED;
        }
    }

exit:
    return result;
}


static morse_error_t morse_cmd53_write(enum mmhal_sdio_function function,
                                       uint32_t address,
                                       const uint8_t *data,
                                       uint32_t len)
{
    morse_error_t result = MORSE_FAILED;

    enum block_size block_size = BLOCK_SIZE_FN2;
    enum block_size block_size_log2 = BLOCK_SIZE_FN2_LOG2;

    if (function == MMHAL_SDIO_FUNCTION_1)
    {
        block_size = BLOCK_SIZE_FN1;
        block_size_log2 = BLOCK_SIZE_FN1_LOG2;
    }


    uint16_t num_blocks = len >> block_size_log2;
    if (num_blocks > 0)
    {
        uint32_t sdio_arg = mmhal_make_cmd53_arg(MMHAL_SDIO_WRITE,
                                                 function,
                                                 MMHAL_SDIO_MODE_BLOCK,
                                                 address & 0x0000FFFF,
                                                 num_blocks);
        struct mmhal_wlan_sdio_cmd53_write_args write_args = {
            .sdio_arg = sdio_arg,
            .data = data,
            .transfer_length = num_blocks,
            .block_size = block_size,
        };

        result = mmhal_wlan_sdio_cmd53_write(&write_args) ? MORSE_FAILED : MORSE_SUCCESS;
        if (result != MORSE_SUCCESS)
        {
            goto exit;
        }

        uint32_t transfer_size = num_blocks * block_size;
        address += transfer_size;
        data += transfer_size;
        len -= transfer_size;
    }


    if (len > 0)
    {
        uint32_t sdio_arg = mmhal_make_cmd53_arg(MMHAL_SDIO_WRITE,
                                                 function,
                                                 MMHAL_SDIO_MODE_BYTE,
                                                 address & 0x0000FFFF,
                                                 len);
        struct mmhal_wlan_sdio_cmd53_write_args write_args = {
            .sdio_arg = sdio_arg,
            .data = data,
            .transfer_length = len,
            .block_size = 0,
        };

        result = mmhal_wlan_sdio_cmd53_write(&write_args) ? MORSE_FAILED : MORSE_SUCCESS;
        if (result != MORSE_SUCCESS)
        {
            goto exit;
        }
    }

exit:
    return result;
}


static void morse_address_base_clear_cache(void)
{
    address_base_fn1_cache = 0xffffffff;
    address_base_fn2_cache = 0xffffffff;
}


static bool morse_address_base_changed(uint32_t address, enum mmhal_sdio_function function)
{
    switch (function)
    {
        case MMHAL_SDIO_FUNCTION_1:
            return (address_base_fn1_cache != address);

        case MMHAL_SDIO_FUNCTION_2:
            return (address_base_fn2_cache != address);

        default:
            MMOSAL_ASSERT(false);
            break;
    }


    MMOSAL_ASSERT(false);
    return false;
}


static void morse_address_base_update_cache(uint32_t address, enum mmhal_sdio_function function)
{
    switch (function)
    {
        case MMHAL_SDIO_FUNCTION_1:
            address_base_fn1_cache = address;
            break;

        case MMHAL_SDIO_FUNCTION_2:
            address_base_fn2_cache = address;
            break;

        default:
            MMOSAL_ASSERT(false);
            break;
    }
}


static morse_error_t morse_address_base_set(uint32_t address,
                                            uint8_t access,
                                            enum mmhal_sdio_function function)
{
    morse_error_t result;

    address &= 0xFFFF0000;

    ADDR_BASE_FSM_TRACE("Test addr%08x", function);
    if (!morse_address_base_changed(address, function))
    {
        result = MORSE_SUCCESS;
        goto exit;
    }

    ADDR_BASE_FSM_TRACE("Change addr");

    MMOSAL_ASSERT(access <= MORSE_CONFIG_ACCESS_4BYTE);

    result = morse_cmd52_write(MORSE_REG_ADDRESS_WINDOW_0, (uint8_t)(address >> 16), function);
    morse_xtal_init_delay();
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    result = morse_cmd52_write(MORSE_REG_ADDRESS_WINDOW_1, (uint8_t)(address >> 24), function);
    morse_xtal_init_delay();
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    result = morse_cmd52_write(MORSE_REG_ADDRESS_CONFIG, access, function);
    morse_xtal_init_delay();
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    morse_address_base_update_cache(address, function);

exit:
    ADDR_BASE_FSM_TRACE("idle");
    return result;
}


static void morse_spi_irq_main(void *arg)
{
    struct driver_data *driverd = (struct driver_data *)arg;

    while (spi_irq_task_run)
    {
        (void)mmosal_semb_wait(spi_irq_semb, mmhal_spi_irq_poll_interval);

        morse_trns_claim(driverd);
        if (morse_trns_spi_irq_enabled)
        {

            morse_hw_irq_handle(driverd);
        }
        morse_trns_release(driverd);
    }

    spi_irq_task_has_finished = true;
}


void __attribute__((weak)) mmhal_wlan_clear_spi_irq(void)
{
}


static void morse_spi_irq_handler(void)
{

    mmhal_wlan_set_spi_irq_enabled(false);
    mmhal_wlan_clear_spi_irq();
    mmosal_semb_give_from_isr(spi_irq_semb);
}

morse_error_t morse_trns_read_multi_byte(struct driver_data *driverd,
                                         uint32_t address,
                                         uint8_t *data,
                                         uint32_t len)
{
    morse_error_t result = MORSE_FAILED;
    enum mmhal_sdio_function function = MMHAL_SDIO_FUNCTION_2;
    enum max_block_transfer_size max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN2;

    TRANSPORT_FSM_TRACE("read_multi_byte");


    if (len == 0 || (len & 0x03) != 0)
    {
        MMLOG_DBG("Invalid length %lu\n", len);
        goto exit;
    }

    MMOSAL_ASSERT(mmosal_mutex_is_held_by_active_task(bus_lock));

    if (!IS_MEMORY_ADDRESS(driverd, address))
    {
        function = MMHAL_SDIO_FUNCTION_1;
        max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN1;
    }


    while (len > 0)
    {
        result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
        if (result != MORSE_SUCCESS)
        {
            goto exit;
        }


        uint32_t size = min(len, max_transfer_size);


        uint32_t next_boundary = (address & 0xFFFF0000) + 0x10000;
        if ((address + size) > next_boundary)
        {
            size = next_boundary - address;
        }

        morse_cmd53_read(function, address, data, size);


        if ((size >= 8) && driverd->cfg->bus_double_read && !memcmp(data, data + 4, 4))
        {

            MMLOG_DBG("Corrupt Payload. Re-Read first 8 bytes\n");
            morse_cmd53_read(function, address, data, 8);
        }

        address += size;
        data += size;
        len -= size;
    }

exit:
    comms_op_check(driverd, result);
    return result;
}

morse_error_t morse_trns_write_multi_byte(struct driver_data *driverd,
                                          uint32_t address,
                                          const uint8_t *data,
                                          uint32_t len)
{
    TRANSPORT_FSM_TRACE("write_multi_byte");

    morse_error_t result = MORSE_FAILED;
    enum mmhal_sdio_function function = MMHAL_SDIO_FUNCTION_2;
    enum max_block_transfer_size max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN2;

    MMLOG_DBG("Write %lu bytes to %08lx\n", len, address);


    if (len == 0 || (len & 0x03) != 0)
    {
        MMLOG_WRN("Invalid length %lu\n", len);
        result = MORSE_INVALID_ARGUMENT;
        goto exit;
    }

    MMOSAL_ASSERT(mmosal_mutex_is_held_by_active_task(bus_lock));

    if (!IS_MEMORY_ADDRESS(driverd, address))
    {
        function = MMHAL_SDIO_FUNCTION_1;
        max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN1;
    }


    while (len > 0)
    {
        result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
        if (result != MORSE_SUCCESS)
        {
            MMLOG_WRN("Address base set failed\n");
            goto exit;
        }


        uint32_t size = min(len, max_transfer_size);


        uint32_t next_boundary = (address & 0xFFFF0000) + 0x10000;
        if ((address + size) > next_boundary)
        {
            size = next_boundary - address;
        }

        result = morse_cmd53_write(function, address, data, size);
        if (result != MORSE_SUCCESS)
        {
            goto exit;
        }

        address += size;
        data += size;
        len -= size;
    }

exit:
    comms_op_check(driverd, result);
    return result;
}

morse_error_t morse_trns_read_le32(struct driver_data *driverd, uint32_t address, uint32_t *data)
{
    TRANSPORT_FSM_TRACE("read_word");

    morse_error_t result = MORSE_FAILED;
    uint8_t receive_data[4] __attribute__((aligned(4))) = { 0xaa, 0xbb, 0xcc, 0xdd };

    MMOSAL_ASSERT(mmosal_mutex_is_held_by_active_task(bus_lock));

    enum mmhal_sdio_function function = MMHAL_SDIO_FUNCTION_2;

    if (!IS_MEMORY_ADDRESS(driverd, address))
    {
        function = MMHAL_SDIO_FUNCTION_1;
    }

    result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
    if (result != MORSE_SUCCESS)
    {
        MMLOG_WRN("Address base set failed\n");
        goto exit;
    }

    result = morse_cmd53_read(function, address, receive_data, sizeof(receive_data));
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    PACK_LE32(*data, receive_data);

exit:
    comms_op_check(driverd, result);
    return result;
}

morse_error_t morse_trns_write_le32(struct driver_data *driverd, uint32_t address, uint32_t data)
{
    TRANSPORT_FSM_TRACE("write_word");

    morse_error_t result = MORSE_FAILED;
    uint8_t send_data[4] __attribute__((aligned(4)));

    MMOSAL_ASSERT(mmosal_mutex_is_held_by_active_task(bus_lock));

    enum mmhal_sdio_function function = MMHAL_SDIO_FUNCTION_2;

    if (!IS_MEMORY_ADDRESS(driverd, address))
    {
        function = MMHAL_SDIO_FUNCTION_1;
    }

    result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
    if (result != MORSE_SUCCESS)
    {
        MMLOG_WRN("Address base set failed\n");
        goto exit;
    }

    UNPACK_LE32(send_data, data);

    result = morse_cmd53_write(function, address, send_data, sizeof(send_data));
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    if ((address == MORSE_REG_RESET(driverd)) && (data == MORSE_REG_RESET_VALUE(driverd)))
    {
        MMLOG_DBG("Reset detected, invalidating base addr\n");
        morse_address_base_clear_cache();
    }

exit:
    comms_op_check(driverd, result);
    return result;
}

void morse_trns_claim(struct driver_data *driverd)
{
    MM_UNUSED(driverd);
	
    MMOSAL_ASSERT(!mmosal_mutex_is_held_by_active_task(bus_lock));

    MMOSAL_MUTEX_GET_INF(bus_lock);
	
    TRANSPORT_FSM_TRACE("claim");
}

void morse_trns_release(struct driver_data *driverd)
{
    MM_UNUSED(driverd);
    MMOSAL_MUTEX_RELEASE(bus_lock);
    TRANSPORT_FSM_TRACE("release");
}

static morse_error_t morse_trns_reset(struct driver_data *driverd)
{
    int i, ret;
    morse_error_t result = MORSE_FAILED;

    MMLOG_INF("Transport reset\n");
    TRANSPORT_FSM_TRACE("reset");
	
    morse_trns_claim(driverd);

    mmhal_wlan_hard_reset();

    bool xtal_init_required = mmhal_wlan_ext_xtal_init_is_required();

    if (xtal_init_required)
    {
        xtal_init_sdio_trans_delay_ms = driverd->cfg->xtal_init_sdio_trans_delay_ms;
        mmosal_task_sleep(50);
    }


    morse_address_base_clear_cache();

    ret = mmhal_wlan_sdio_startup();
    if (ret != 0)
    {
        MMLOG_WRN("Initial communication with chip failed\n");
        return MORSE_FAILED;
    }

    MMOSAL_ASSERT(driverd->cfg != NULL);
    MMOSAL_ASSERT(driverd->cfg->regs != NULL);

    if (driverd->cfg->regs->clk_ctrl_address != 0)
    {
        morse_trns_write_le32(driverd,
                              driverd->cfg->regs->clk_ctrl_address,
                              driverd->cfg->regs->clk_ctrl_value);
    }

    if (driverd->cfg->xtal_init && xtal_init_required)
    {

        driverd->cfg->xtal_init(driverd);
        xtal_init_sdio_trans_delay_ms = 0;
    }

    for (i = 0; i < MAX_RETRY; i++)
    {

        result =
            morse_trns_read_le32(driverd, driverd->cfg->regs->chip_id_address, &driverd->chip_id);
        if (result == MORSE_SUCCESS)
        {
            break;
        }
    }

    morse_trns_release(driverd);

    if (morse_hw_is_valid_chip_id(driverd->chip_id, driverd->cfg->valid_chip_ids))
    {
        MMLOG_INF("Morse Chip Reset Successful\n");
        result = MORSE_SUCCESS;
    }
    else
    {
        MMLOG_ERR("Morse Chip Reset Unsuccessful\n");
        result = MORSE_FAILED;
    }
    return result;
}

static bool pktmem_flow_control_is_paused(void)
{
    return mmhal_wlan_pktmem_tx_flow_control_state() == MMWLAN_TX_PAUSED;
}

void pktmem_flow_control_handler(void)
{
    mmdrv_host_update_tx_paused(MMDRV_PAUSE_SOURCE_MASK_PKTMEM, pktmem_flow_control_is_paused);
}

void morse_trns_init(void)
{
    struct mmhal_wlan_pktmem_init_args pktmem_init_args = {
        .tx_flow_control_cb = pktmem_flow_control_handler,
    };
    mmhal_wlan_pktmem_init(&pktmem_init_args);
}

void morse_trns_deinit(void)
{
    mmhal_wlan_pktmem_deinit();
}


morse_error_t morse_trns_start(struct driver_data *driverd)
{
    uint8_t bic;
    morse_error_t result = MORSE_FAILED;

    TRANSPORT_FSM_TRACE_INIT();
    ADDR_BASE_FSM_TRACE_INIT();

    bus_lock = mmosal_mutex_create("SPI Bus Lock");
    MMOSAL_ASSERT(bus_lock != NULL);

    spi_irq_semb = mmosal_semb_create("SPI irq");
    MMOSAL_ASSERT(spi_irq_semb);

    mmhal_wlan_init();

    result = morse_trns_reset(driverd);
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    morse_trns_claim(driverd);

    result = morse_cmd52_write(SDIO_CCCR_IEN_ADDR,
                               SDIO_CCCR_IEN_IENM | SDIO_CCCR_IEN_IEN1,
                               MMHAL_SDIO_FUNCTION_0);
    if (result != MORSE_SUCCESS)
    {
        goto exit_cccr;
    }

    result = morse_cmd52_read(SDIO_CCCR_BIC_ADDR, &bic, MMHAL_SDIO_FUNCTION_0);
    if (result != MORSE_SUCCESS)
    {
        goto exit_cccr;
    }

    bic |= SDIO_CCCR_BIC_ECSI;

    result = morse_cmd52_write(SDIO_CCCR_BIC_ADDR, bic, MMHAL_SDIO_FUNCTION_0);
    if (result != MORSE_SUCCESS)
    {
        goto exit_cccr;
    }

exit_cccr:
    morse_trns_release(driverd);
    if (result != MORSE_SUCCESS)
    {
        goto exit;
    }

    spi_irq_task_run = true;
    spi_irq_task_has_finished = false;

    spi_irq_task_handle = mmosal_task_create(morse_spi_irq_main,
                                             driverd,
                                             SPI_IRQ_TASK_PRIORITY,
                                             SPI_IRQ_TASK_STACK,
                                             "spi_irq");
    if (spi_irq_task_handle == NULL)
    {
        result = MORSE_FAILED;
        goto exit;
    }

    mmhal_wlan_register_spi_irq_handler(morse_spi_irq_handler);

exit:
    return result;
}

void morse_trns_stop(struct driver_data *driverd)
{

    morse_trns_set_irq_enabled(driverd, false);
    spi_irq_task_run = false;
    mmosal_semb_give(spi_irq_semb);
    while (!spi_irq_task_has_finished)
    {
        mmosal_task_sleep(1);
    }

    mmosal_mutex_delete(bus_lock);
    bus_lock = NULL;

    if (spi_irq_semb != NULL)
    {
        mmosal_semb_delete(spi_irq_semb);
    }
    spi_irq_semb = NULL;

    mmhal_wlan_deinit();
}

void morse_trns_set_irq_enabled(struct driver_data *driverd, bool enabled)
{
    bool bus_held = mmosal_mutex_is_held_by_active_task(bus_lock);
	
    MM_UNUSED(driverd);

    if (enabled)
    {
        if (!morse_trns_spi_irq_enabled)
        {
            if (!bus_held)
            {
                morse_trns_claim(driverd);
            }
			
            TRANSPORT_FSM_TRACE("irq_enabled");
            morse_trns_spi_irq_enabled = true;
            mmhal_wlan_set_spi_irq_enabled(true);


            morse_hw_irq_handle(driverd);

            if (!bus_held)
            {
                morse_trns_release(driverd);
            }
        }
    }
    else
    {
        if (!bus_held)
        {
            morse_trns_claim(driverd);
        }

        TRANSPORT_FSM_TRACE("irq_disabled");
        morse_trns_spi_irq_enabled = false;
        mmhal_wlan_set_spi_irq_enabled(false);

        if (!bus_held)
        {
            morse_trns_release(driverd);
        }
    }
}
