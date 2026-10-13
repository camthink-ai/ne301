/***************************************************************************/ /**
 * @file sl_si91x_spi_bus.h
 * @brief SiWx91x NCP SPI bus interface documentation and declarations.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifndef SL_SI91X_SPI_BUS_H
#define SL_SI91X_SPI_BUS_H

/***************************************************************************/ /**
 * @addtogroup HOST-INTERFACE Host Interface
 * @ingroup SI91X_SERVICE_APIS
 * @{
 * @brief SiWx91x NCP SPI bus interface.
 * @details Provides SPI framed transport helpers used by the WiSeConnect stack
 *          on hosted (NCP) designs: bus init, frame read/write, interrupt status,
 *          and nested SPI protocol helpers. Install `sl_si91x_spi_bus` together
 *          with host control and board-specific SPI configuration as required.
 ******************************************************************************/

/** @} (end addtogroup HOST-INTERFACE) */

#endif // SL_SI91X_SPI_BUS_H
