/***************************************************************************/ /**
 * @file sli_buffer_manager_config.h
 * @brief
 * This file contains the configuration for the sli_buffer_manager. for the SiWx3xx family of devices.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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

#ifndef __SLI_BUFFER_MANAGER_CONFIG_H__
#define __SLI_BUFFER_MANAGER_CONFIG_H__

/**
 * @brief Dedicated memory pool identifiers for the SiWx3xx buffer manager.
 *
 * Each enumerator selects a fixed-purpose pool configured at init via
 * @ref sli_buffer_manager_configuration_t. Pools with zero dedicated blocks
 * may still satisfy @ref SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID requests
 * from the common pool.
 */
typedef enum {
  SLI_BUFFER_MANAGER_CE_CMD_TX_POOL = 0, ///< TX command buffers for command engine to command parser.
  SLI_BUFFER_MANAGER_DATA_TX_POOL,       ///< Host TX data payload buffers passed to command parser/nhcp.
  SLI_BUFFER_MANAGER_CE_METADATA_POOL,   ///< Command Engine per-packet metadata (sli_command_engine_metadata_t).
  SLI_BUFFER_MANAGER_CP_CMD_DATA_RX_POOL, ///< Command parser bus-interface RX buffers for incoming command and data frames.
  SLI_BUFFER_MANAGER_QUEUE_NODE_POOL, ///< Queue manager linked-list nodes (sli_queue_node_t) for command/event queues.
  SLI_BUFFER_MANAGER_MAX_POOL         ///< Count of dedicated pools; not a valid allocation target.
} sli_buffer_manager_pool_types_t;

#endif