/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup EMMET Morse Micro Embedded Test Engine (Emmet)
 * @{
 *
 * Emmet is a firmware subsystem that allows various aspects of the firmware to be driven
 * by a connected computer. This is intended as an aid for development and automated test.
 *
 * @warning Emmet provides access to internal functionality and credentials that would
 *          result in a security risk if used in a production device.
 *
 * Examples of functionality that can be driven by Emmet include:
 *
 * * Connect to an AP
 * * Disconnect from an AP
 * * Read connection state
 * * Ping a remote host on the network
 * * Start an Iperf client or server
 *
 * A high-level block diagram of how Emmet fits into the overall firmware architecture is
 * shown below. Emmet provides an API to an connected computer, interfaced via the debug
 * interface of the MCU, that is able invoke functions at various layers of the firmware stack.
 *
 * @image latex emmet-blockdiag.png "Emmet block diagram"
 * @image html emmet-blockdiag.png "Emmet block diagram"
 *
 * The Emmet Control API (Ace) is a Python library that abstracts away the details of the
 * protocol used by the PC to communicate with the Emmet subsystem via OpenOCD, and provides
 * an easy-to-use interface to the Emmet API. This enables control scripts to be written in
 * Python, examples of which are provided as part of this SDK.
 *
 * @section EMMET_ENABLE Enabling Emmet support in application firmware
 *
 * There are two aspects to enabling Emmet support in an application. Firstly, the application
 * must invoke @ref emmet_init() to initialize Emmet, and @ref emmet_start() to start Emmet.
 *
 * Secondly, the @c .emmet_hostif_table section must be located at address `0x20000400`. (Note that
 * a different address may be used if it is not possible to locate @c .emmet_hostif_table at this
 * address then, but it will be necessary to update the Python API.)
 *
 * The following code can be added to the platform linker script to place the
 * @c .emmet_hostif_table section at the correct location:
 *
 * @code
 * PROVIDE (_emmet_hostif_table_base_address = 0x20000400);
 * .emmet_hostif_table _emmet_hostif_table_base_address (NOLOAD) :
 * {
 *     *(.emmet_hostif_table)
 * } > RAM
 * @endcode
 *
 * Note that the linker script provided with the SDK includes the above code snippet so that the
 * Emmet section is located at the correct address.
 *
 * @section EMMET_SCRIPTS Example control scripts
 *
 * A number of Python scripts are provided in the `tools/ace/examples` directory. These can be
 * used to perform various operations from the command line. See @ref EMMET_APP_HOWTO for more.
 */
#pragma once

#include "mmwlan.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initialize Emmet.
 *
 * This will zero the host interface table but will not set the magic number, so it will not
 * accept commands from the host until @ref emmet_start() is invoked.
 */
void emmet_init(void);

/**
 * Start Emmet.
 *
 * This will set the magic number in the host interface table so that the host knows it
 * can now begin sending commands to Emmet.
 */
void emmet_start(void);

/**
 * Set the regulatory database for Emmet to use.
 *
 * @param reg_db                The regulatory database to use. Must remain valid in memory.
 *
 * @returns @c MMWLAN_SUCCESS on success, or another relevant error code.
 */
enum mmwlan_status emmet_set_reg_db(const struct mmwlan_regulatory_db *reg_db);

/** Enumeration of button states used by the Emmet HAL. */
enum emmet_button_state
{
    /** Button released state. */
    EMMET_BUTTON_RELEASED,
    /** Button pressed state. */
    EMMET_BUTTON_PRESSED,
};

/** Enumeration for different LEDs on the board used by the Emmet HAL. */
enum emmet_led_id
{
    EMMET_LED_RED,
    EMMET_LED_GREEN,
    EMMET_LED_BLUE,
    EMMET_LED_WHITE
};

/**
 * @defgroup EMMET_HAL Hardware Abstraction Layer (HAL) for Morse Micro Embedded Test Engine (Emmet)
 *
 * This API must be implemented by applications that use Emmet.
 * @{
 */

/**
 * Set LED state.
 *
 * @param led_id    The ID of the LED to set.
 * @param level     Level 0-255, where 0 if off and 255 is full on.
 */
void emmet_hal_set_led(uint8_t led_id, uint8_t level);

/**
 * Get the current button state.
 *
 * @returns the current button state.
 */
enum emmet_button_state emmet_hal_get_button_state(void);

/**
 * Trigger a button event to the application as if a user had pushed or
 * released the button.
 *
 * @param state The type of event to trigger (press or release).
 */
void emmet_hal_trigger_button_event(enum emmet_button_state state);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
