/*
 * Copyright 2021-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "morse_transport.h"
#include "mmhal_wlan.h"
#include "mmutils.h"
#include "driver/morse_crc/morse_crc.h"


#ifdef ENABLE_SDIO_CMD_FSM_TRACE
#include "mmtrace.h"
static mmtrace_channel sdio_cmd_fsm_channel_handle;
#define SDIO_CMD_FSM_TRACE_INIT() \
    sdio_cmd_fsm_channel_handle = mmtrace_register_channel("sdio_cmd_fsm")
#define SDIO_CMD_FSM_TRACE(_fmt, ...) \
    mmtrace_printf(sdio_cmd_fsm_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define SDIO_CMD_FSM_TRACE_INIT() \
    do {                          \
    } while (0)
#define SDIO_CMD_FSM_TRACE(_fmt, ...) \
    do {                              \
    } while (0)
#endif


#ifdef ENABLE_CMD53_WRITE_FSM_TRACE
#include "mmtrace.h"
static mmtrace_channel cmd53_write_fsm_channel_handle;
#define CMD53_WRITE_FSM_TRACE_INIT() \
    cmd53_write_fsm_channel_handle = mmtrace_register_channel("cmd53_write_fsm")
#define CMD53_WRITE_FSM_TRACE(_fmt, ...) \
    mmtrace_printf(cmd53_write_fsm_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define CMD53_WRITE_FSM_TRACE_INIT() \
    do {                             \
    } while (0)
#define CMD53_WRITE_FSM_TRACE(_fmt, ...) \
    do {                                 \
    } while (0)
#endif


#ifdef ENABLE_CMD53_READ_FSM_TRACE
#include "mmtrace.h"
static mmtrace_channel cmd53_read_fsm_channel_handle;
#define CMD53_READ_FSM_TRACE_INIT() \
    cmd53_read_fsm_channel_handle = mmtrace_register_channel("cmd53_read_fsm")
#define CMD53_READ_FSM_TRACE(_fmt, ...) \
    mmtrace_printf(cmd53_read_fsm_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define CMD53_READ_FSM_TRACE_INIT() \
    do {                            \
    } while (0)
#define CMD53_READ_FSM_TRACE(_fmt, ...) \
    do {                                \
    } while (0)
#endif


#define MAX_BUS_ATTEMPTS (200)


struct MM_PACKED sdio_spi_r5
{
    uint8_t status;
    uint8_t data;
};


enum sdio_spi_control_token
{

    SDIO_SPI_TKN_MULTI_WRITE = 0xFC,

    SDIO_SPI_TKN_READ_SINGLE_WRITE = 0xFE,

    SDIO_SPI_TKN_STOP_TRANSACTION = 0xFD,

    SDIO_SPI_TKN_DATA_RSP_ACCEPTED = 0xE1 | (0x02 << 1),

    SDIO_SPI_TKN_DATA_RSP_REJ_CRC = 0xE1 | (0x05 << 1),

    SDIO_SPI_TKN_DATA_RSP_REJ_WRITE = 0xE1 | (0x06 << 1),
};


enum sdio_direction
{
    SDIO_DIR_CARD_TO_HOST = 0,
    SDIO_DIR_HOST_TO_CARD = 1 << 6,
};


enum sdio_cmd_index
{

    SDIO_CMD0 = 0,

    SDIO_CMD52 = 52,

    SDIO_CMD53 = 53,

    SDIO_CMD63 = 63,
};


static uint8_t morse_receive_spi(void)
{
    uint8_t ret = mmhal_wlan_spi_rw(0xff);
    morse_xtal_init_delay();
    return ret;
}


static void morse_transmit_spi(uint8_t data)
{
    (void)mmhal_wlan_spi_rw(data);
    morse_xtal_init_delay();
}


static int morse_cmd53_get_data(const struct mmhal_wlan_sdio_cmd53_read_args *args)
{
    uint8_t *data = args->data;
    uint32_t byte_cnt;
    int ret = 0;

    if (args->block_size == 0)
    {
        byte_cnt = args->transfer_length;
    }
    else
    {
        byte_cnt = args->transfer_length * args->block_size;
    }

    mmhal_wlan_spi_cs_assert();

    while (byte_cnt > 0)
    {
        uint8_t rcv_data;
        uint32_t attempt;


        CMD53_READ_FSM_TRACE("wait_tkn");
        for (attempt = 0; attempt < MAX_BUS_ATTEMPTS; attempt++)
        {
            rcv_data = morse_receive_spi();
            if (rcv_data == SDIO_SPI_TKN_READ_SINGLE_WRITE)
            {
                break;
            }
        }

        CMD53_READ_FSM_TRACE("chk_tkn");
        if (rcv_data != SDIO_SPI_TKN_READ_SINGLE_WRITE)
        {
            CMD53_READ_FSM_TRACE("timeout");
            MMLOG_WRN("Timeout waiting for CMD53 read ready\n");
            ret = MMHAL_SDIO_DATA_TIMEOUT;
            goto exit;
        }

        const uint8_t *block_start = data;


        uint32_t size = byte_cnt;
        if (args->block_size != 0 && args->block_size < byte_cnt)
        {
            size = args->block_size;
        }

        CMD53_READ_FSM_TRACE("read_buf");

        mmhal_wlan_spi_read_buf(data, size);
        data += size;

        CMD53_READ_FSM_TRACE("read_crc");

        uint16_t rx_crc16;
        rx_crc16 = morse_receive_spi() << 8;
        rx_crc16 |= morse_receive_spi();

        CMD53_READ_FSM_TRACE("chck_crc");

        uint16_t crc16 = morse_crc16_xmodem(0, block_start, size);

        if (crc16 != rx_crc16)
        {
            CMD53_READ_FSM_TRACE("crc_err");
            MMLOG_WRN("RD: CRC failed\n");
            ret = MMHAL_SDIO_DATA_CRC_ERROR;
            goto exit;
        }

        byte_cnt -= size;
    }

exit:
    CMD53_READ_FSM_TRACE("idle");
    mmhal_wlan_spi_cs_deassert();
    return ret;
}

static int morse_test_data_rsp_token(uint8_t token)
{
    int result = MMHAL_SDIO_OTHER_ERROR;

    switch (token)
    {
        case SDIO_SPI_TKN_DATA_RSP_ACCEPTED:
            result = 0;
            goto exit;

        case SDIO_SPI_TKN_DATA_RSP_REJ_CRC:
            MMLOG_WRN("WR: rejected due to CRC\n");
            result = MMHAL_SDIO_DATA_CRC_ERROR;
            goto exit;

        case SDIO_SPI_TKN_DATA_RSP_REJ_WRITE:
            MMLOG_WRN("WR: write error\n");
            result = MMHAL_SDIO_OTHER_ERROR;
            goto exit;

        default:
            MMLOG_WRN("WR: invalid/no token received (0x%02x)\n", token);
            result = MMHAL_SDIO_OTHER_ERROR;
            goto exit;
    }

exit:
    return result;
}


static bool morse_wait_ready(void)
{
    uint8_t result = 0;
    uint32_t attempt = MAX_BUS_ATTEMPTS;

    while (attempt--)
    {
        result = morse_receive_spi();

        if (result == 0xff)
        {
            return true;
        }
    }
    MMLOG_WRN("Bus not ready after %d attempts (0x%02x)\n", MAX_BUS_ATTEMPTS, result);
    return false;
}


static int morse_cmd53_put_data(const struct mmhal_wlan_sdio_cmd53_write_args *args)
{
    int ret = MMHAL_SDIO_OTHER_ERROR;
    bool bus_ready;
    const uint8_t *data = args->data;
    uint32_t cnt = args->transfer_length;

    MMLOG_VRB("WR: %u %s %lu\n", args->block_size, args->block_size == 0 ? "BYTE" : "BLOCK", cnt);

    enum sdio_spi_control_token start_tkn = SDIO_SPI_TKN_READ_SINGLE_WRITE;

    uint32_t size;
    if (args->block_size != 0)
    {
        size = args->block_size;
        if (cnt > 1)
        {
            start_tkn = SDIO_SPI_TKN_MULTI_WRITE;
        }
    }
    else
    {
        size = cnt;
    }

    mmhal_wlan_spi_cs_assert();

    while (cnt > 0)
    {
        const uint8_t *block_start = data;



        if (args->block_size == 0)
        {
            cnt = 0;
        }
        else
        {
            cnt--;
        }

        CMD53_WRITE_FSM_TRACE("calc_crc");

        uint16_t crc16 = morse_crc16_xmodem(0, block_start, size);

        CMD53_WRITE_FSM_TRACE("wait_rdy");
        bus_ready = morse_wait_ready();
        if (!bus_ready)
        {
            MMLOG_WRN("Bus not ready\n");
            ret = MMHAL_SDIO_DATA_TIMEOUT;
            goto exit;
        }


        (void)morse_receive_spi();

        CMD53_WRITE_FSM_TRACE("snd_tkn");
        morse_transmit_spi(start_tkn);

        CMD53_WRITE_FSM_TRACE("snd_buf");

        mmhal_wlan_spi_write_buf(data, size);
        data += size;

        CMD53_WRITE_FSM_TRACE("snd_crc");

        morse_transmit_spi((uint8_t)(crc16 >> 8));
        morse_transmit_spi((uint8_t)crc16);

        CMD53_WRITE_FSM_TRACE("get_rsp");
        uint32_t attempt;
        uint8_t rcv_data;


        MMOSAL_TASK_ENTER_CRITICAL();

        for (attempt = 0; attempt < 4; attempt++)
        {
            rcv_data = morse_receive_spi();
            if (rcv_data != 0xff)
            {
                break;
            }
        }
        MMOSAL_TASK_EXIT_CRITICAL();

        CMD53_WRITE_FSM_TRACE("chk_rsp");
        ret = morse_test_data_rsp_token(rcv_data);
        if (ret != 0)
        {
            goto exit;
        }
    }

exit:

    if (start_tkn == SDIO_SPI_TKN_MULTI_WRITE)
    {
        CMD53_WRITE_FSM_TRACE("stop_tkn");
        morse_transmit_spi(SDIO_SPI_TKN_STOP_TRANSACTION);
    }


    morse_wait_ready();

    mmhal_wlan_spi_cs_deassert();
    CMD53_WRITE_FSM_TRACE("idle");
    return ret;
}

MM_WEAK int mmhal_wlan_sdio_cmd(uint8_t cmd_idx, uint32_t arg, uint32_t *rsp)
{
    struct sdio_spi_r5 response;
    uint8_t buf[6];
    uint8_t attempt;

    int ret = MMHAL_SDIO_OTHER_ERROR;

    SDIO_CMD_FSM_TRACE("wait_rdy");

    mmhal_wlan_spi_cs_assert();

    if ((cmd_idx != SDIO_CMD63) && !morse_wait_ready())
    {
        MMLOG_WRN("CMD%u: Unable to send cmd -- bus not ready\n", cmd_idx);
        ret = MMHAL_SDIO_DATA_TIMEOUT;
        goto exit;
    }

    SDIO_CMD_FSM_TRACE("Arrange CMD");

    buf[0] = cmd_idx | SDIO_DIR_HOST_TO_CARD;

    UNPACK_BE32((buf + 1), arg);


    if (cmd_idx == SDIO_CMD52 || cmd_idx == SDIO_CMD53)
    {
        buf[5] = morse_crc7_sd(0x00, (const uint8_t *)&buf[0], sizeof(buf) - 1) << 1;
        buf[5] |= 0x1;
    }
    else
    {

        buf[5] = 0xFF;
    }

    SDIO_CMD_FSM_TRACE("Send CMD %u", cmd_idx);

    mmhal_wlan_spi_write_buf(buf, sizeof(buf));

    SDIO_CMD_FSM_TRACE("Wait Response");

    response.data = morse_receive_spi();

    for (attempt = 0; attempt < MAX_BUS_ATTEMPTS; attempt++)
    {

        response.status = response.data;
        response.data = morse_receive_spi();


        if (response.status != 0xFF)
        {
            break;
        }
    }

    SDIO_CMD_FSM_TRACE("Check Resp");
    if (response.status != 0x00)
    {

        MMLOG_DBG("CMD%u: invalid ack (0x%02x)\n", cmd_idx, response.status);
        ret = MMHAL_SDIO_OTHER_ERROR;
    }
    else
    {
        ret = 0;
    }


    if (cmd_idx == SDIO_CMD53 && response.data != 0x00)
    {
        ret = MMHAL_SDIO_OTHER_ERROR;
    }

    if (rsp != NULL)
    {
        *rsp = response.data;
    }

exit:
    mmhal_wlan_spi_cs_deassert();
    SDIO_CMD_FSM_TRACE("idle");
    return ret;
}

#define STARTUP_MAX_ATTEMPTS (3)

MM_WEAK int mmhal_wlan_sdio_startup(void)
{
    uint32_t ii, ret;
    mmhal_wlan_send_training_seq();


    for (ii = 0; ii < STARTUP_MAX_ATTEMPTS; ii++)
    {
        ret = mmhal_wlan_sdio_cmd(SDIO_CMD63, 0, NULL);
        if (ret == MORSE_SUCCESS)
        {
            break;
        }
        (void)mmhal_wlan_sdio_cmd(SDIO_CMD0, 0, NULL);
    }

    return ret;
}

MM_WEAK int mmhal_wlan_sdio_cmd53_write(const struct mmhal_wlan_sdio_cmd53_write_args *args)
{
    int ret;

    ret = mmhal_wlan_sdio_cmd(SDIO_CMD53, args->sdio_arg, NULL);
    if (ret != 0)
    {
        return ret;
    }

    if (args->transfer_length > 0)
    {
        ret = morse_cmd53_put_data(args);
    }

    return ret;
}

MM_WEAK int mmhal_wlan_sdio_cmd53_read(const struct mmhal_wlan_sdio_cmd53_read_args *args)
{
    int ret;

    ret = mmhal_wlan_sdio_cmd(SDIO_CMD53, args->sdio_arg, NULL);
    if (ret != 0)
    {
        return ret;
    }

    if (args->transfer_length > 0)
    {
        ret = morse_cmd53_get_data(args);
    }

    return ret;
}

MM_WEAK bool mmhal_wlan_ext_xtal_init_is_required()
{
    return false;
}
