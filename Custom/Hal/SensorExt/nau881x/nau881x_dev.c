/**
 * @file nau881x_dev.c
 * @brief NAU881x audio codec system adaptation layer (i2c_driver backend).
 *
 * Provides write_reg / read_reg callbacks that are injected into NAU881x_t
 * at init time.  The I2C bus lifecycle (i2c_driver_init / i2c_driver_deinit)
 * is the caller's responsibility.
 *
 * NAU881x I2C wire format (2 bytes per register access):
 *   Byte 0: (reg_addr << 1) | (data[8])   -- 7-bit addr in [7:1], data MSB in [0]
 *   Byte 1: data[7:0]
 */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "nau881x_dev.h"
#include "debug.h"
#include "aicam_types.h"
#include "../i2c_driver/i2c_driver.h"

#define NAU881X_I2C_TIMEOUT_MS  200U
#define NAU881X_I2C_ADDR_7BIT   0x1Au

static i2c_instance_t *s_i2c_inst    = NULL;
static NAU881x_t       s_nau881x     = {0};
static bool            s_initialized = false;

/* ---------- write_reg / read_reg callbacks injected into NAU881x_t ---------- */

static int nau881x_dev_write_reg(uint16_t dev_addr, uint8_t reg, uint16_t val)
{
    (void)dev_addr;

    uint8_t buf[2];
    buf[0] = (uint8_t)((reg << 1) | ((val >> 8) & 0x01));
    buf[1] = (uint8_t)(val & 0xFF);

    int ret = i2c_driver_write_data(s_i2c_inst, buf, 2, NAU881X_I2C_TIMEOUT_MS);
    if (ret < 0) {
        LOG_DRV_ERROR("nau881x: I2C write reg=0x%02X val=0x%03X failed %d\r\n", reg, val, ret);
        return -1;
    }
    return 0;
}

static uint16_t nau881x_dev_read_reg(uint16_t dev_addr, uint8_t reg)
{
    (void)dev_addr;

    uint8_t addr_byte = (uint8_t)(reg << 1);
    int ret = i2c_driver_write_data(s_i2c_inst, &addr_byte, 1, NAU881X_I2C_TIMEOUT_MS);
    if (ret < 0) {
        LOG_DRV_ERROR("nau881x: I2C read addr write reg=0x%02X failed %d\r\n", reg, ret);
        return 0;
    }

    uint8_t buf[2] = {0, 0};
    ret = i2c_driver_read_data(s_i2c_inst, buf, 2, NAU881X_I2C_TIMEOUT_MS);
    if (ret < 0) {
        LOG_DRV_ERROR("nau881x: I2C read data reg=0x%02X failed %d\r\n", reg, ret);
        return 0;
    }

    return (uint16_t)(((uint16_t)(buf[0] & 0x01) << 8) | buf[1]);
}

/* ---------- Public API ---------- */

int nau881x_dev_init(void)
{
    if (s_initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }

    /* I2C port init/deinit is the caller's responsibility. */
    s_i2c_inst = i2c_driver_create(I2C_PORT_1,
                                   (uint16_t)(NAU881X_I2C_ADDR_7BIT << 1),
                                   I2C_ADDRESS_7BIT);
    if (s_i2c_inst == NULL) {
        LOG_DRV_ERROR("nau881x_dev: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }

    int ret = i2c_driver_is_ready(s_i2c_inst, 3, NAU881X_I2C_TIMEOUT_MS);
    if (ret != AICAM_OK) {
        // LOG_DRV_ERROR("nau881x_dev: device not ready on I2C (addr=0x%02X)\r\n",
        //               (unsigned)NAU881X_I2C_ADDR_7BIT);
        i2c_driver_destroy(s_i2c_inst);
        s_i2c_inst = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }

    memset(&s_nau881x, 0, sizeof(s_nau881x));
    s_nau881x.write_reg = nau881x_dev_write_reg;
    s_nau881x.read_reg  = nau881x_dev_read_reg;

    nau881x_status_t status = NAU881x_Init(&s_nau881x);
    if (status != NAU881X_STATUS_OK) {
        LOG_DRV_ERROR("nau881x_dev: NAU881x_Init failed %d\r\n", (int)status);
        i2c_driver_destroy(s_i2c_inst);
        s_i2c_inst = NULL;
        return AICAM_ERROR_PROTOCOL;
    }

    s_initialized = true;
    LOG_DRV_DEBUG("nau881x_dev: init OK\r\n");
    return AICAM_OK;
}

void nau881x_dev_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    if (s_i2c_inst != NULL) {
        i2c_driver_destroy(s_i2c_inst);
        s_i2c_inst = NULL;
    }
    /* I2C port deinit is the caller's responsibility. */
    memset(&s_nau881x, 0, sizeof(s_nau881x));
    s_initialized = false;
}

NAU881x_t *nau881x_dev_get_handle(void)
{
    return s_initialized ? &s_nau881x : NULL;
}
