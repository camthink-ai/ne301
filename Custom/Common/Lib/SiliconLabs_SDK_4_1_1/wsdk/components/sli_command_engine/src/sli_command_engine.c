/***************************************************************************/ /**
 * @file sli_command_engine.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
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

#include <stdint.h>
#include <string.h>
#include "cmsis_os2.h"
#include "sl_cmsis_utility.h"
#include "sl_constants.h"
#include "sl_core.h"
#include "sli_command_engine.h"
#include "sli_queue_manager.h"
#include "sli_routing_utility.h"

/******************************************************
  *               Macro Definitions
  ******************************************************/
// Event flag for thread termination notification
#define SLI_COMMAND_ENGINE_THREAD_TERMINATE_EVENT (1 << 23)

// Event flag for thread termination acknowledgment
#define SLI_COMMAND_ENGINE_THREAD_TERMINATE_ACK_EVENT (1 << 22)

// Event flag for packet TX acknowledgment
#define SLI_COMMAND_ENGINE_PACKET_TX_ACK_EVENT (1 << 21)

// Event flag for packet RX
#define SLI_COMMAND_ENGINE_PACKET_RX_EVENT (1 << 20)

// Event flag for packet configuration
#define SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT (1 << 19)

// Event flag for dynamic packet TX (single bit: worker scans dynamic_packet_type list for all types)
#define SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT (1 << 17)

// Event flags for command engine to wait on
#define SLI_COMMAND_ENGINE_EVENTS_TO_WAIT_ON                                                           \
  (SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT          /* dynamic packet type TX pending */       \
   | SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT /* packet type (un)registration request */ \
   | SLI_COMMAND_ENGINE_PACKET_RX_EVENT                     /* RX packet available */                  \
   | SLI_COMMAND_ENGINE_PACKET_TX_ACK_EVENT                 /* TX completion/ack available */          \
   | SLI_COMMAND_ENGINE_THREAD_TERMINATE_EVENT)             /* thread terminate request */

// Macro to identify if a packet expects a synchronous or sequential asynchronous response
#define SLI_COMMAND_ENGINE_EXPECT_PACKET_RESPONSE(x)                        \
  ((SLI_COMMAND_ENGINE_COMMAND_PACKET & x->tx_info.flags)                   \
   && ((SLI_COMMAND_ENGINE_SYNC_RESPONSE_STATUS_PACKET & x->tx_info.flags)  \
       || (SLI_COMMAND_ENGINE_SYNC_RESPONSE_DATA_PACKET & x->tx_info.flags) \
       || (SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET & x->tx_info.flags)))

/// Evaluates to true if the async response flag is set in metadata->tx_info.flags.
#define SLI_COMMAND_ENGINE_IS_ASYNC_RESPONSE_PACKET(metadata) \
  (SLI_COMMAND_ENGINE_ASYNC_RESPONSE_PACKET == (metadata->tx_info.flags & SLI_COMMAND_ENGINE_ASYNC_RESPONSE_PACKET))

/******************************************************
  *               Local Type Definitions
  ******************************************************/
//------------------------------------------------------------------------------
// @brief  Packet type configuration request kinds for dynamic packet handling.
//         These values are pushed into the command engine control queue to
//         (un)register additional packet types at runtime.
//------------------------------------------------------------------------------
typedef enum {
  SLI_COMMAND_ENGINE_REGISTER_PACKET_TYPE,   // Request to add/register a new dynamic packet type
  SLI_COMMAND_ENGINE_UNREGISTER_PACKET_TYPE, // Request to remove/unregister an existing dynamic packet type
  SLI_COMMAND_ENGINE_PACKET_TYPE_REQUEST_MAX // Sentinel / upper bound (not used as a request)
} sli_command_engine_packet_configuration_request_type_t;

//------------------------------------------------------------------------------
// @brief  Request object placed on the command engine control queue to
//         (un)register a dynamic packet type at runtime.
//------------------------------------------------------------------------------
typedef struct {
  sli_command_engine_packet_configuration_request_type_t request_type; // Kind of request (register / unregister)
  uint8_t packet_type;                                                 // Packet type value to (un)register
  osThreadId_t thread_id;                                              // Requesting thread (to signal completion)
  sli_command_engine_packet_type_configuration_node_t
    *packet_type_config; // Pointer to new node when registering, NULL when unregistering
} sli_command_engine_packet_type_configuration_request_t;

/**
 * @brief Context structure for command engine flush handlers.
 * - Encapsulates the state needed to perform a flush operation within the command engine,
 *   pairing a specific engine instance with the packet-type configuration to apply during flushing.
 */
typedef struct {
  sli_command_engine_t *instance; // Command engine instance owning the queues being flushed
  sli_command_engine_packet_type_configuration_t *packet_type_config; // Packet-type config of this packet type
  uint16_t packet_type;                                               // Packet type being flushed
} sli_command_engine_packet_flush_context_t;

/******************************************************
 *               Variable Definitions
 ******************************************************/

/******************************************************
 *              Static Function Declarations
 ******************************************************/
//------------------------------------------------------------------------------
// @brief Wait (with timeout) for any of the specified command engine events.
// @param command_engine_eventId  Event flags object to wait on.
// @param event_mask              Bitmask of events of interest (any one will satisfy the wait).
// @param timeout                 Timeout in RTOS ticks (osWaitForever for indefinite wait).
// @return Bitmask of events that became active, or 0 on timeout / error.
//------------------------------------------------------------------------------
static uint32_t sli_command_engine_wait_for_event(osEventFlagsId_t command_engine_eventId,
                                                  uint32_t event_mask,
                                                  uint32_t timeout)
{
  // Wait for ANY of the bits in event_mask to be set.
  // CMSIS-RTOS2 returns the complete current flags value on success,
  // or a (negative) error code cast to uint32_t on failure/timeout.
  if (timeout != osWaitForever) {
    timeout = SLI_SYSTEM_MS_TO_TICKS(timeout);
  }

  uint32_t result = osEventFlagsWait(command_engine_eventId, event_mask, osFlagsWaitAny, timeout);

  // ponytail: catch the 0xFFFFFFFx error family, but keep the legitimate
  // "no event" outcomes (timeout; resource on a non-blocking poll) silent
  // and instant as the original code did. Only the abnormal members (dead/
  // deleted flags object returns osFlagsErrorParameter instantly on EVERY
  // call) get logged once and a 5ms yield, so callers looping on a
  // dead object degrade to a slow poll instead of hot-spinning at this
  // thread's priority; the raw error also stays out of sticky `|=`
  // accumulators (its TERMINATE bit overlaps the error family).
  if ((result & (uint32_t)osFlagsError) != 0u) {
    if ((result != (uint32_t)osErrorTimeout) && (result != (uint32_t)osErrorResource)) {
      static bool flags_error_logged = false;
      if (!flags_error_logged) {
        flags_error_logged = true;
        SL_DEBUG_LOG_V2(ERROR, "command engine: osEventFlagsWait failed 0x%lX\r\n", result);
      }
      osDelay(5);
    }
    return 0;
  }
  return result; // Return the flags that satisfied the wait.
}

//------------------------------------------------------------------------------
// @brief  Set (OR) the specified event flags on a command engine event group.
// @param  command_engine_eventId  Event flags object handle.
// @param  event_mask              Bit mask of flags to set.
//------------------------------------------------------------------------------
static void sli_command_engine_set_event(osEventFlagsId_t command_engine_eventId, uint32_t event_mask)
{
  // No return value needed; errors (if any) are ignored intentionally.
  (void)osEventFlagsSet(command_engine_eventId, event_mask);
}

//------------------------------------------------------------------------------
// @brief  Set (OR) the specified event flags for a specific thread.
// @param  thread_id   Target thread to receive the flags.
// @param  event_mask  Bit mask of thread flags to set.
//------------------------------------------------------------------------------
static void sli_command_engine_set_thread_event(osThreadId_t thread_id, uint32_t event_mask)
{
  // Post flags to the target thread; ignoring error for lightweight usage.
  (void)osThreadFlagsSet(thread_id, event_mask);
}

//------------------------------------------------------------------------------
// @brief Send an error event to the command engine's error handler.
//
// Tries to allocate a buffer for the error status. If allocation fails, it
// falls back to a pre-reserved emergency buffer (instance->error_buffer).
// The error status is then enqueued to the configured error queue and the
// associated error event flag is set to wake the error handler.
//------------------------------------------------------------------------------
static sl_status_t sli_command_engine_send_error_event(sli_command_engine_t *instance,
                                                       sl_command_engine_error_status_t error)
{
  sl_status_t status                               = SL_STATUS_OK;
  sl_command_engine_error_status_t *error_status   = NULL;
  sl_command_engine_error_status_t *new_error_slot = NULL;
  bool fallback_used                               = false;

  // Validate error queue and event id configuration.
  if ((NULL == instance) || (NULL == instance->config.error_event_queue) || (NULL == instance->config.error_event_id)) {
    return SL_STATUS_FAIL;
  }

  // Try to allocate a buffer for the error status.
  status = sli_buffer_manager_allocate_buffer(instance->config.error_buffer_pool_type,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                              1000,
                                              (sli_buffer_t *)&error_status);
  if (SL_STATUS_OK != status) {
    // Allocation failed: use the emergency buffer if available.
    if (NULL == instance->error_buffer) {
      return status;
    }
    error_status           = instance->error_buffer;
    instance->error_buffer = NULL; // Consume the fallback buffer.
    fallback_used          = true;
  }

  // Store the error code payload.
  *error_status = error;

  // Enqueue the error object so the error handler can process it.
  status = sli_queue_manager_enqueue(instance->config.error_event_queue, (void *)error_status);
  if (SL_STATUS_OK != status) {
    // Queue did not accept ownership; reclaim memory.
    if (fallback_used) {
      instance->error_buffer = error_status;
    } else {
      sli_buffer_manager_free_buffer(error_status);
    }
    return status;
  }

  // Signal the error handler via its event flags.
  sli_command_engine_set_event(*(instance->config.error_event_id), instance->config.error_event);

  // allocate a new error buffer if the fallback buffer is used
  if (fallback_used) {
    status = sli_buffer_manager_allocate_buffer(instance->config.error_buffer_pool_type,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                                1000,
                                                (sli_buffer_t *)&new_error_slot);
    if (SL_STATUS_OK == status) {
      instance->error_buffer = new_error_slot;
    }
  }

  return SL_STATUS_OK;
}

// Identify if an in-flight metadata node matches the received packet metadata
// Match criteria: packet_type and frame_id must both be equal
static bool rx_packet_identity_handler(const sli_queue_t *handle, const void *data, const void *node_match_data)
{
  UNUSED_PARAMETER(handle); // Not used in this matcher
  if (data == NULL || node_match_data == NULL) {
    return false;
  }
  // node_match_data carries the received packet temporary metadata
  sli_command_engine_metadata_t *rx_metadata = (sli_command_engine_metadata_t *)node_match_data;
  // data is the current in-flight metadata node being checked
  sli_command_engine_metadata_t *metadata = (sli_command_engine_metadata_t *)data;

  // Return true only if both packet_type and frame_id match
  if ((rx_metadata->tx_info.packet_type == metadata->tx_info.packet_type)
      && (rx_metadata->tx_info.frame_id == metadata->tx_info.frame_id)) {
    return true;
  }
  return false; // No match
}

// Map low-level TX ACK failures to command-engine error payload values.
static sl_command_engine_error_status_t sli_command_engine_get_tx_ack_error_status(sl_status_t tx_status)
{
  switch (tx_status) {
    case SL_STATUS_TIMEOUT:
      return SLI_COMMAND_ENGINE_STATUS_COMMAND_TX_TIMEOUT;
    case SL_STATUS_IO:
    case SL_STATUS_TRANSMIT:
    case SL_STATUS_BUS_ERROR:
      return SLI_COMMAND_ENGINE_STATUS_INTERFACE_ERROR;
    default:
      return SLI_COMMAND_ENGINE_STATUS_COMMAND_TX_FAILED;
  }
}

// decrement the in-flight command counter if the counter is greater than 0
//  and set the event if the packet queue is not empty
static void sli_command_engine_decrement_in_flight_and_set_tx_event(sli_command_engine_t *instance,
                                                                    sli_command_engine_queue_info_t *queue_info)
{
  if (queue_info->in_flight_command_count == 0) {
    return;
  }
  queue_info->in_flight_command_count--;
  if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(queue_info->packet_queue))) {
    sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT);
  }
}

// TX ACK failure: error event → detach inflight → pure sync posts to sync queue; else free metadata.
static void sli_command_engine_tx_ack_handle_failure(sli_command_engine_t *instance,
                                                     sli_command_engine_metadata_t *metadata)
{
  sli_command_engine_queue_info_t *queue_info                               = NULL; // Per packet type queue state
  sli_command_engine_packet_type_configuration_t *packet_type_configuration = NULL; // Dynamic type config
  sli_command_engine_metadata_t *detached_metadata                          = NULL; // Out: node removed from inflight
  uint32_t time_elapsed                                                     = 0;
  const sl_command_engine_error_status_t error_status =
    sli_command_engine_get_tx_ack_error_status((sl_status_t)metadata->packet_status);
  sl_status_t status = SL_STATUS_OK;

  SL_DEBUG_LOG_V2(ERROR,
                  "TX ACK for FAILED packet - sending error event : %lu",
                  (unsigned long)metadata->packet_status);
  (void)sli_command_engine_send_error_event(instance, error_status);
  /* Do not return if this fails — metadata was dequeued from tx_status and must still be detached
   * from inflight and released or handed to a sync waiter. */

  status = sli_command_engine_get_dynamic_packet_info(instance,
                                                      metadata->tx_info.packet_type,
                                                      &queue_info,
                                                      &packet_type_configuration);
  if ((SL_STATUS_OK != status) || (NULL == queue_info) || (NULL == packet_type_configuration)) {
    /* No queue info: cannot detach from inflight; drop metadata only. */
    sli_buffer_manager_free_buffer(metadata);
    return;
  }

  status = sli_queue_manager_remove_node_from_queue(&(queue_info->inflight_packet_queue),
                                                    rx_packet_identity_handler,
                                                    (const void *)metadata,
                                                    (void **)&detached_metadata);
  if ((SL_STATUS_NOT_FOUND == status) || (SL_STATUS_EMPTY == status)) {
    /* detached_metadata unset; match key referred to this metadata — free it. */
    sli_buffer_manager_free_buffer(metadata);
    return;
  }
  if (SL_STATUS_OK != status) {
    (void)sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
    sli_buffer_manager_free_buffer(metadata);
    return;
  }
  /* metadata and detached_metadata are the same node, so free only one of them 
   Current flow expects detached_metadata to alias metadata for this match key.
   detached_metadata is kept to document the remove_node_from_queue out parameter. */

  if (queue_info->in_flight_command_count > 0) {
    queue_info->in_flight_command_count--;
    if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(queue_info->packet_queue))) {
      sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT);
    }
  }

  // Drop late TX failures once the original command wait window has already expired.
  time_elapsed = (osKernelGetTickCount() - metadata->packet_start_tickcount);
  if ((metadata->tx_info.timeout > 0) && (time_elapsed > metadata->tx_info.timeout)) {
    sli_buffer_manager_free_buffer(metadata);
    return;
  }

  // Only packets that truly expect a sync response should be enqueued to sync_response_queue.
  if (!SLI_COMMAND_ENGINE_EXPECT_PACKET_RESPONSE(metadata)
      || (SLI_COMMAND_ENGINE_ASYNC_RESPONSE_PACKET & metadata->tx_info.flags)
      || (SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET & metadata->tx_info.flags)) {
    sli_buffer_manager_free_buffer(metadata);
    return;
  }

  /* Snapshot waiter identity before enqueue: another thread may dequeue and free metadata first. */
  osThreadId_t resp_thread_id = metadata->sync_resp_thread_id;

  // enqueue the metadata to the sync response queue
  metadata->tx_info.data_packet        = NULL;
  metadata->tx_info.data_packet_length = 0;
  status = sli_queue_manager_enqueue(packet_type_configuration->sync_response_queue, (void *)metadata);
  if (SL_STATUS_OK != status) {
    (void)sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
    sli_buffer_manager_free_buffer(metadata);
    return;
  }
  if (NULL != resp_thread_id) {
    sli_command_engine_set_thread_event(resp_thread_id, packet_type_configuration->sync_response_event);
  } else {
    sli_command_engine_set_event(*(packet_type_configuration->sync_response_event_id),
                                 packet_type_configuration->sync_response_event);
  }
}

// TX ACK success with no separate “failure” status: if the command does not wait for an
// RX-correlated sync/seq response, TX completion is terminal — detach inflight and free.
// Otherwise keep metadata on inflight for @ref rx_packet_identity_handler.
static void sli_command_engine_tx_ack_release_without_response(sli_command_engine_t *instance,
                                                               sli_command_engine_metadata_t *metadata)
{
  sli_command_engine_queue_info_t *queue_info                               = NULL; // Per packet type queue state
  sli_command_engine_packet_type_configuration_t *packet_type_configuration = NULL; // Dynamic type config
  sli_command_engine_metadata_t *detached_metadata                          = NULL; // Out: node removed from inflight
  sl_status_t status                                                        = SL_STATUS_OK;

  status = sli_command_engine_get_dynamic_packet_info(instance,
                                                      metadata->tx_info.packet_type,
                                                      &queue_info,
                                                      &packet_type_configuration);
  if ((SL_STATUS_OK != status) || (NULL == queue_info) || (NULL == packet_type_configuration)) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to get dynamic packet info : %lu", (unsigned long)status);
    (void)sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
    sli_buffer_manager_free_buffer(metadata);
    return;
  }

  if (!SLI_COMMAND_ENGINE_EXPECT_PACKET_RESPONSE(metadata)) {

    SL_DEBUG_LOG_V2(DEBUG, "Removing meta data : 0x%X", (unsigned int)metadata);
    status = sli_queue_manager_remove_node_from_queue(&(queue_info->inflight_packet_queue),
                                                      rx_packet_identity_handler,
                                                      (const void *)metadata,
                                                      (void **)&detached_metadata);
    if ((SL_STATUS_NOT_FOUND == status) || (SL_STATUS_EMPTY == status)) {
      sli_buffer_manager_free_buffer(metadata);
      return;
    }
    if (SL_STATUS_OK != status) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to remove node from inflight packet queue");
      (void)sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
    }
    // metadata and detached_metadata are the same node, so free only one of them
    sli_buffer_manager_free_buffer(metadata);
    if (queue_info->in_flight_command_count > 0) {
      queue_info->in_flight_command_count--;
      if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(queue_info->packet_queue))) {
        sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT);
      }
    }
    return;
  }
  /* Await RX correlation: TX buffer is already complete — clear handles so flush paths stay safe. */
  metadata->tx_info.data_packet        = NULL;
  metadata->tx_info.data_packet_length = 0;
  metadata->tx_status                  = SLI_COMMAND_ENGINE_PACKET_TX_DONE;
  return;
}

sl_status_t sli_command_engine_get_dynamic_packet_info(
  sli_command_engine_t *instance,
  uint16_t packet_type,
  sli_command_engine_queue_info_t **queue_info,
  sli_command_engine_packet_type_configuration_t **packet_type_configuration)
{
  sli_command_engine_packet_type_configuration_node_t *node = NULL;

  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if ((NULL == queue_info) && (NULL == packet_type_configuration)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  CORE_irqState_t state = CORE_EnterAtomic();
  // No dynamic packet types registered
  if (NULL == instance->dynamic_packet_type) {
    CORE_ExitAtomic(state);
    return SL_STATUS_NOT_FOUND;
  }

  // Traverse the singly-linked list of dynamic packet type nodes
  node = instance->dynamic_packet_type;
  while (NULL != node) {
    // Match on packet type
    if (node->packet_type == packet_type) {
      // Return queue info if caller requested it
      if (NULL != queue_info) {
        *queue_info = &(node->queue_info);
      }

      // Return packet configuration if caller requested it
      if (NULL != packet_type_configuration) {
        *packet_type_configuration = &(node->packet_config);
      }
      CORE_ExitAtomic(state);
      return SL_STATUS_OK; // Found
    }
    node = node->next; // Advance to next node
  }
  CORE_ExitAtomic(state);

  // Not found in dynamic list
  return SL_STATUS_NOT_FOUND;
}

//------------------------------------------------------------------------------
// @brief  Dequeue next packet metadata, drop any that have timed out,
//         invoke optional pre-TX hook, and route (transmit) the packet.
//         If transmission is asynchronous (returns IN_PROGRESS) we keep
//         metadata until TX completion callback moves it; otherwise on
//         immediate success we free the data buffer. For command packets
//         expecting synchronous response we move metadata to the in‑flight
//         queue so the RX path can correlate the response.
// @param  instance                  Command engine instance
// @param  queue_info                Per-packet-type queue state
// @param  packet_type_configuration Dynamic packet type configuration
// @param  packet_type               Packet type value
// @return SL_STATUS_OK (always unless internal helper reports fatal error)
//------------------------------------------------------------------------------
static sl_status_t sli_command_engine_handle_packet_tx(
  sli_command_engine_t *instance,
  sli_command_engine_queue_info_t *queue_info,
  sli_command_engine_packet_type_configuration_t *packet_type_configuration,
  uint16_t packet_type)
{
  sl_status_t status                      = SL_STATUS_OK;
  sli_command_engine_metadata_t *metadata = NULL;
  uint32_t time_elapsed                   = 0;

  do {
    // Pull next queued command
    status = sli_queue_manager_dequeue(&(queue_info->packet_queue), (void **)&metadata);
    if (SL_STATUS_OK != status) {
      // Dequeue failure: raise fatal error (queue/state corruption) and exit gracefully
      status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
      if (SL_STATUS_OK != status) {
        return status;
      }
      return SL_STATUS_OK;
    }

    // Compute time elapsed since packet queued to detect timeout
    time_elapsed = (osKernelGetTickCount() - metadata->packet_start_tickcount);
    // Drop timed out packets (unless async response expected)
    if (!(metadata->tx_info.flags
          & (SLI_COMMAND_ENGINE_ASYNC_RESPONSE_PACKET | SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET))
        && time_elapsed > metadata->tx_info.timeout) {
      // Drop timed out packet metadata and try next one (if any)
      sli_buffer_manager_free_buffer(metadata->tx_info.data_packet);
      sli_buffer_manager_free_buffer(metadata);
      metadata = NULL;
    }
    // Continue looping while:
    //  - We have not found a non-expired metadata (metadata == NULL)
    //  - And there are still entries in the queue to inspect
  } while ((NULL == metadata) && (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(queue_info->packet_queue))));

  // All entries were timed out (or queue empty after drops)
  if (NULL == metadata) {
    return SL_STATUS_OK;
  }

  // Invoke optional pre-transmit hook (e.g., header fixups, logging)
  if (NULL != packet_type_configuration->pre_tx_handler) {
    status = packet_type_configuration->pre_tx_handler(instance, packet_type, (void *)metadata);
    if (SL_STATUS_OK != status) {
      // Pre-TX hook failure: free metadata and return
      sli_buffer_manager_free_buffer(metadata->tx_info.data_packet);
      sli_buffer_manager_free_buffer(metadata);
      return SL_STATUS_OK;
    }
  }

  // Route (send) the packet via routing utility (may be async)
  status = sli_routing_utility_route_packet(instance->config.routing_table,
                                            packet_type_configuration->route_packet_type,
                                            metadata->tx_info.data_packet,
                                            metadata->tx_info.data_packet_length,
                                            metadata);
  if (SL_STATUS_IN_PROGRESS == status) {
    // Async TX started: completion path will enqueue metadata for ACK processing
    metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_TX_INPROGRESS;
  } else if (SL_STATUS_OK != status) {
    // Immediate TX failure: report command TX failure and return
    status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_COMMAND_TX_FAILED);
    if (SL_STATUS_OK != status) {
      return status;
    }
    return SL_STATUS_OK;
  } else {
    // Immediate success (synchronous send). Data buffer no longer needed.
    metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_TX_DONE;
    sli_buffer_manager_free_buffer(metadata->tx_info.data_packet);
  }

  // For command packets expecting a synchronous response Or sequential async response or Async TX we retain metadata
  if (SLI_COMMAND_ENGINE_EXPECT_PACKET_RESPONSE(metadata)
      || (SL_STATUS_IN_PROGRESS == status)) { // Or needs sequential async response
    if (SL_STATUS_IN_PROGRESS != status) {
      metadata->tx_info.data_packet        = NULL;
      metadata->tx_info.data_packet_length = 0;
    }

    SL_DEBUG_LOG_V2(DEBUG, "Adding meta data : 0x%X\r\n", (unsigned int)metadata);

    // Move metadata to in-flight queue for response correlation
    status = sli_queue_manager_enqueue(&(queue_info->inflight_packet_queue), (void *)metadata);
    if (SL_STATUS_OK != status) {
      // Could not enqueue -> fatal error
      sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
      return status;
    }

    queue_info->in_flight_command_count++;
    metadata = NULL;
  } else if ((SL_STATUS_OK == status) && SLI_COMMAND_ENGINE_IS_ASYNC_RESPONSE_PACKET(metadata)) {
    // No synchronous response or TX acknowledgement is expected: release metadata structure
    sli_buffer_manager_free_buffer(metadata);
    metadata = NULL;
  }

  return SL_STATUS_OK;
}

// Flush handler used when de‑initializing queues or removing nodes.
// This function is called for every node remaining in a queue so that
// any allocated packet (data) buffer is released to avoid memory leaks.
static void sli_command_engine_packet_queue_flush_handler(const sli_queue_t *handle, void *data, const void *context)
{
  UNUSED_PARAMETER(handle); // Queue handle not needed for simple free
  sli_command_engine_metadata_t *metadata                  = (sli_command_engine_metadata_t *)data;
  sli_command_engine_packet_flush_context_t *flush_context = (sli_command_engine_packet_flush_context_t *)context;
  sli_command_engine_t *instance                           = metadata->instance;
  bool consumed                                            = false;

  if ((NULL != flush_context) && (NULL != instance->config.flush_handler)) {
    consumed =
      instance->config.flush_handler(instance, flush_context->packet_type, flush_context->packet_type_config, metadata);
  }

  if (consumed) {
    return;
  }

  if (metadata->tx_status == SLI_COMMAND_ENGINE_PACKET_TX_INPROGRESS) {
    metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_FLUSHED;
    return;
  }

  // Release the buffer associated with this queue node
  sli_buffer_manager_free_buffer(metadata->tx_info.data_packet);
  sli_buffer_manager_free_buffer(data);

  return; // Explicit for clarity
}

// Flush handler used when removing nodes.
// This function is called for every node remaining in a queue so that
// any allocated packet (data) buffer is released to avoid memory leaks.
static void sli_command_engine_queue_flush_handler(const sli_queue_t *handle, void *data, const void *context)
{
  UNUSED_PARAMETER(handle);  // Queue handle not needed for simple free
  UNUSED_PARAMETER(context); // No extra context required

  // Release the buffer associated with this queue node
  sli_buffer_manager_free_buffer(data);

  return; // Explicit for clarity
}

// Flush handler used when removing nodes.
// This function is called for every node remaining in a queue so that
// any allocated packet (data) buffer is released to avoid memory leaks.
static void sli_command_engine_control_queue_flush_handler(const sli_queue_t *handle, void *data, const void *context)
{
  UNUSED_PARAMETER(handle);  // Queue handle not needed for simple free
  UNUSED_PARAMETER(context); // No extra context required

  // Release the buffer associated with this queue node
  SLI_FREE(data);

  return; // Explicit for clarity
}

// Command engine main thread loop.
// Responsibilities:
//  - Waits on command engine event flags
//  - Handles thread termination
//  - Processes dynamic packet type (un)registration requests
//  - Processes TX completion (ACK) events
//  - Processes RX packets (routes to sync / async handlers)
//  - Dequeues and transmits queued TX packets (dynamic packet types only)
//  - Enforces per‑packet-type in‑flight command limits
static void sli_command_engine_thread(void *args)
{
  sli_command_engine_t *instance                                            = (sli_command_engine_t *)args;
  uint32_t events_received                                                  = 0; // Accumulated pending events
  sl_status_t status                                                        = SL_STATUS_FAIL;
  sli_command_engine_metadata_t rx_metadata                                 = { 0 }; // Temp RX metadata (stack)
  sli_command_engine_metadata_t *metadata                                   = NULL;  // Generic metadata pointer
  sli_command_engine_queue_info_t *queue_info                               = NULL;  // Per packet type queue state
  sli_command_engine_packet_type_configuration_t *packet_type_configuration = NULL;  // Dynamic type config
  uint16_t packet_type                                                      = 0; // Current packet type being processed
  void *data                                                                = NULL; // Generic packet pointer
  osThreadId_t resp_thread_id;                                                      // Waiting thread for sync response

  SL_DEBUG_LOG_V2(DEBUG, "%s task started\r\n", (uintptr_t)instance->config.name);

  while (1) {
    uint32_t wait_time = (events_received == 0) ? osWaitForever : 0;
    // Block until at least one awaited event occurs; OR in new events with any still pending
    events_received |= sli_command_engine_wait_for_event(instance->command_engine_eventId,
                                                         SLI_COMMAND_ENGINE_EVENTS_TO_WAIT_ON,
                                                         wait_time);

    // ---------------- Thread termination handling ----------------
    if (events_received & SLI_COMMAND_ENGINE_THREAD_TERMINATE_EVENT) {
      // Clear request flag
      events_received &= ~SLI_COMMAND_ENGINE_THREAD_TERMINATE_EVENT;

      // Acknowledge so deinit can continue
      sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_THREAD_TERMINATE_ACK_EVENT);

      // Suspend the thread until it is terminated.
      while (1) {
        osThreadSuspend(osThreadGetId());
      }
    }

    // ---------------- Dynamic packet type configuration requests ----------------
    if (events_received & SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT) {
      SL_DEBUG_LOG_V2(DEBUG, "Handling : SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT.\r\n");
      events_received &= ~SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT;

      // Drain all control requests (register/unregister)
      while (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&instance->control_queue)) {
        sli_command_engine_packet_type_configuration_request_t *request = NULL;
        sli_command_engine_packet_type_configuration_node_t *new_node   = NULL;

        status = sli_queue_manager_dequeue(&instance->control_queue, (void **)&request);
        if (SL_STATUS_OK != status) {
          // ponytail: `continue` re-tests the same non-empty queue without
          // advancing — a corrupt queue state (0x19 recovery churn) turned
          // this drain loop into a zero-yield hot spin at this thread's
          // priority (field 8h console death). Bail out instead.
          SL_DEBUG_LOG_V2(ERROR, "command engine: control dequeue failed 0x%lX, aborting drain\r\n", status);
          sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          break;
        }

        if (request->request_type == SLI_COMMAND_ENGINE_REGISTER_PACKET_TYPE) {
          new_node = request->packet_type_config; // Node prepared by requester (already alloc+inited)
          if (instance->dynamic_packet_type == NULL) {
            instance->dynamic_packet_type = new_node; // First dynamic packet type in list
          } else {
            new_node->next                = instance->dynamic_packet_type; // Insert new node at head for O(1) add
            instance->dynamic_packet_type = new_node;
          }
        } else { // Unregister path
          sli_command_engine_packet_type_configuration_node_t *prev = NULL;
          sli_command_engine_packet_type_configuration_node_t *node = instance->dynamic_packet_type;
          sli_command_engine_packet_type_configuration_node_t *ntbr = NULL; // Node to be removed

          while (node != NULL) {
            if (node->packet_type == request->packet_type) { // Match target packet type
              // Unlink located node
              ntbr = node;
              if (NULL != prev) {
                prev->next = node->next; // Bypass node in middle/end
              } else {
                instance->dynamic_packet_type = node->next; // Removing head updates list start
              }

              // Drain queues like sli_command_engine_deinit: full metadata + data_packet cleanup and optional flush callback
              sli_command_engine_packet_flush_context_t flush_context = { 0 };
              flush_context.instance                                  = instance;
              flush_context.packet_type                               = ntbr->packet_type;
              flush_context.packet_type_config                        = &(ntbr->packet_config);
              sli_queue_manager_deinit(&(ntbr->queue_info.packet_queue),
                                       sli_command_engine_packet_queue_flush_handler,
                                       (void *)&flush_context);
              sli_queue_manager_deinit(&(ntbr->queue_info.inflight_packet_queue),
                                       sli_command_engine_packet_queue_flush_handler,
                                       (void *)&flush_context);
              SLI_FREE(ntbr); // Release node memory
              break;      // Removal complete
            }
            prev = node;       // Track previous for unlink
            node = node->next; // Advance traversal
          }
        }

        // Notify requesting thread its request has been processed
        sli_command_engine_set_thread_event(request->thread_id, SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT);
        SLI_FREE(request);
      }
    }

    // ---------------- TX completion (ACK) handling ----------------
    if (events_received & SLI_COMMAND_ENGINE_PACKET_TX_ACK_EVENT) {
      SL_DEBUG_LOG_V2(DEBUG, "Got TX ACK Event\r\n");
      while (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&instance->tx_status_packet_queue)) {
        status = sli_queue_manager_dequeue(&(instance->tx_status_packet_queue), (void **)&metadata);
        if (SL_STATUS_OK != status) {
          // ponytail: `continue` re-tested the same non-empty queue without
          // advancing — a failing dequeue on a non-empty queue (queue
          // corruption from the 0x19 recovery churn) hot-spun this thread.
          // Abort the drain; the thread re-blocks on the next wait.
          SL_DEBUG_LOG_V2(ERROR, "TX ACK dequeue failed 0x%lX, aborting drain\r\n", status);
          sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          break;
        }
        if (metadata->tx_status == SLI_COMMAND_ENGINE_PACKET_FLUSHED) {
          SL_DEBUG_LOG_V2(DEBUG, "TX ACK for FLUSHED packet - freeing metadata");
          sli_buffer_manager_free_buffer(metadata);
        } else if ((sl_status_t)metadata->packet_status != SL_STATUS_OK) {
          sli_command_engine_tx_ack_handle_failure(instance, metadata);
        } else {
          sli_command_engine_tx_ack_release_without_response(instance, metadata);
        }
        metadata = NULL;
      }
      // Clear TX ACK event bit after processing all pending ACKs
      events_received &= ~(SLI_COMMAND_ENGINE_PACKET_TX_ACK_EVENT);
    }

    // ---------------- RX packet handling ----------------
    if (events_received & SLI_COMMAND_ENGINE_PACKET_RX_EVENT) {
      // Dequeue one RX packet from the RX queue
      status = sli_queue_manager_dequeue(&(instance->rx_packet_queue), (void **)&data);
      if (SL_STATUS_OK != status) {
        // If dequeue fails, send a fatal error event and break if error sending fails
        status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
        if (SL_STATUS_OK != status) {
          break; // Exit loop on fatal path
        }
        continue;
      }

      // If RX queue is now empty, clear the RX event bit
      if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(instance->rx_packet_queue))) {
        events_received &= ~(SLI_COMMAND_ENGINE_PACKET_RX_EVENT);
      }

      SL_DEBUG_LOG_V2(DEBUG, "Got RX Packet\r\n");

      // Fill temporary RX metadata using user callback
      rx_metadata.tx_info.data_packet = data;
      status                          = instance->config.get_packet_metadata(instance, data, &rx_metadata);
      if (SL_STATUS_OK != status) {
        SL_DEBUG_LOG_V2(WARN, "Unidentified Packet\r\n");
        // If metadata extraction fails, free the data buffer and continue
        sli_buffer_manager_free_buffer(data);
        continue;
      }
      packet_type = rx_metadata.tx_info.packet_type;

      // Lookup dynamic packet info for dynamic packet types
      status =
        sli_command_engine_get_dynamic_packet_info(instance, packet_type, &queue_info, &packet_type_configuration);
      if ((SL_STATUS_OK != status) || (NULL == queue_info) || (NULL == packet_type_configuration)) {
        // If not found, free the data buffer and continue
        SL_DEBUG_LOG_V2(ERROR, "Unidentified Packet info\r\n");
        sli_buffer_manager_free_buffer(data);
        continue;
      }

      SL_DEBUG_LOG_V2(DEBUG, "Got Packet info\r\n");
      sl_status_t rx_handler_status = SL_STATUS_OK;

      // Call RX event handler if configured (e.g., for parsing/classification)
      if (NULL != packet_type_configuration->rx_event_handler) {
        rx_handler_status = packet_type_configuration->rx_event_handler(instance, packet_type, (void *)data);
        if (SL_STATUS_OK != rx_handler_status && SL_STATUS_IN_PROGRESS != rx_handler_status) {
          // On handler failure, send fatal error and free buffer
          status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          if (SL_STATUS_OK != status) {
            break;
          }
          sli_buffer_manager_free_buffer(data);
          SL_DEBUG_LOG_V2(ERROR, "RX packet handler error\r\n");
          continue;
        }
      }

      sli_command_engine_response_t *response = NULL;
      SL_DEBUG_LOG_V2(DEBUG, "Searching Packet metadata\r\n");

      // Try to locate matching in-flight metadata for synchronous response
      status = sli_queue_manager_remove_node_from_queue(&(queue_info->inflight_packet_queue),
                                                        rx_packet_identity_handler,
                                                        (const void *)&rx_metadata,
                                                        (void **)&metadata);
      SL_DEBUG_LOG_V2(DEBUG, "Search Packet metadata status : %lu\r\n", status);

      if ((SL_STATUS_NOT_FOUND == status) || (SL_STATUS_EMPTY == status)) {
        // If not found, treat as async response: enqueue to async queue and signal event
        SL_DEBUG_LOG_V2(DEBUG,
                        "Sending data pointer : 0x%X to async event handler for packet type : %u",
                        (unsigned int)data,
                        packet_type);

        status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                    SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                                    1000,
                                                    (sli_buffer_t *)&response);

        if (status != SL_STATUS_OK) {
          status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          if (SL_STATUS_OK != status) {
            break;
          }
        }

        response->data = data;
        response->type = SLI_COMMAND_ENGINE_PACKET_ONLY_RESPONSE;

        status = sli_queue_manager_enqueue(packet_type_configuration->async_response_queue, (void *)response);
        if (SL_STATUS_OK != status) {
          // On enqueue failure, send fatal error event
          SL_DEBUG_LOG_V2(ERROR, "Async enqueue failed : %lu", status);
          status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          if (SL_STATUS_OK != status) {
            break;
          }
          continue;
        }

        SL_DEBUG_LOG_V2(DEBUG,
                        "Triggering event 0x%lX on event Id : 0x%X.",
                        packet_type_configuration->async_response_event,
                        (unsigned int)packet_type_configuration->async_response_event_id);

        // Notify async consumer via event flag
        sli_command_engine_set_event(*(packet_type_configuration->async_response_event_id),
                                     packet_type_configuration->async_response_event);
      } else if (SL_STATUS_OK != status) {
        SL_DEBUG_LOG_V2(ERROR, "Got error while dequeueing packet metadata : %lu", status);
        // Unexpected queue error, send fatal error event
        status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
        if (SL_STATUS_OK != status) {
          break;
        }
        continue;
      } else {
        uint32_t response_event             = 0;
        osEventFlagsId_t *response_event_id = NULL;
        sli_queue_t *response_queue         = NULL;

        // Compute time elapsed since packet queued to detect timeout
        uint32_t time_elapsed = (osKernelGetTickCount() - metadata->packet_start_tickcount);

        // Check if the request has timed out by the time the response is received
        if ((time_elapsed > metadata->tx_info.timeout) && (metadata->tx_info.timeout > 0)) {
          sli_command_engine_decrement_in_flight_and_set_tx_event(instance, queue_info);
          // Drop timed out response data and metadata
          SL_DEBUG_LOG_V2(WARN,
                          "Packet timedout after : %lu ms\r\n",
                          (unsigned long)SLI_SYSTEM_TICKS_TO_MS(metadata->tx_info.timeout));
          sli_buffer_manager_free_buffer(data);
          sli_buffer_manager_free_buffer(metadata);
          continue;
        }

        // Decrement in-flight command counter as we have found the matching metadata
        // Only decrement if the RX handler status is OK
        // Rx handler return SL_STATUS_IN_PROGRESS for the packets which are
        // expecting more responses such as HTTP client get packets
        if (SL_STATUS_OK == rx_handler_status) {
          sli_command_engine_decrement_in_flight_and_set_tx_event(instance, queue_info);
        }

        if (SL_STATUS_IN_PROGRESS == rx_handler_status) {
          // Create a copy of the metadata as the existing metadata will be freed after the response is sent
          sli_command_engine_metadata_t *metadata_copy = NULL;

          status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                      SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                      1000,
                                                      (sli_buffer_t *)&metadata_copy);

          if (SL_STATUS_OK != status) {
            sli_command_engine_decrement_in_flight_and_set_tx_event(instance, queue_info);

            sli_buffer_manager_free_buffer(metadata);
            sli_buffer_manager_free_buffer(data);

            status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
            if (SL_STATUS_OK != status) {
              break;
            }

            continue;
          }

          memcpy(metadata_copy, metadata, sizeof(sli_command_engine_metadata_t));

          status = sli_queue_manager_enqueue(&(queue_info->inflight_packet_queue), (void *)metadata_copy);

          if (SL_STATUS_OK != status) {
            sli_command_engine_decrement_in_flight_and_set_tx_event(instance, queue_info);
            sli_buffer_manager_free_buffer(metadata);
            sli_buffer_manager_free_buffer(data);
            sli_buffer_manager_free_buffer(metadata_copy);

            status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
            if (SL_STATUS_OK != status) {
              break;
            }

            continue;
          }
        }

        // Sync response: Complete metadata and enqueue for waiting thread
        SL_DEBUG_LOG_V2(DEBUG, "Found meta data : 0x%X\r\n", (unsigned int)metadata);
        metadata->tx_info.data_packet        = data;
        metadata->tx_info.data_packet_length = rx_metadata.tx_info.data_packet_length;

        // Check if the response is sequential asynchronous response
        if (SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET & metadata->tx_info.flags) {

          status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                      SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                                      1000,
                                                      (sli_buffer_t *)&response);

          if (status != SL_STATUS_OK) {
            status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
            if (SL_STATUS_OK != status) {
              break;
            }
          }
          metadata->tx_info.data_packet = data;
          response->data                = metadata;
          response->type                = SLI_COMMAND_ENGINE_METADATA_RESPONSE;
          resp_thread_id                = NULL;
          response_event_id             = packet_type_configuration->async_response_event_id;
          response_event                = packet_type_configuration->async_response_event;
          response_queue                = packet_type_configuration->async_response_queue;

          status = sli_queue_manager_enqueue(response_queue, (void *)response);
        } else { // Else use synchronous response
          resp_thread_id          = metadata->sync_resp_thread_id;
          metadata->packet_status = rx_metadata.packet_status;
          response_event_id       = packet_type_configuration->sync_response_event_id;
          response_event          = packet_type_configuration->sync_response_event;
          response_queue          = packet_type_configuration->sync_response_queue;

          status = sli_queue_manager_enqueue(response_queue, (void *)metadata);
        }

        SL_DEBUG_LOG_V2(DEBUG,
                        "Adding %u command packet to queue 0x%X",
                        rx_metadata.tx_info.packet_type,
                        (unsigned int)response_queue);

        if (SL_STATUS_OK != status) {
          // On enqueue failure, send fatal error event
          status = sli_command_engine_send_error_event(instance, SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR);
          if (SL_STATUS_OK != status) {
            break;
          }
          continue;
        }
        SL_DEBUG_LOG_V2(DEBUG,
                        "Sending Event: 0x%X on event id : 0x%X for queue 0x%X",
                        (unsigned int)response_event,
                        (unsigned int)*(response_event_id),
                        (unsigned int)response_queue);

        // Signal waiting thread (if original context still alive) else global event
        if (NULL != resp_thread_id) {
          sli_command_engine_set_thread_event(resp_thread_id, response_event);
        } else {
          sli_command_engine_set_event(*(response_event_id), response_event);
        }

        metadata = NULL;
      }

      // Reset temporary RX metadata container for next use
      memset((void *)&rx_metadata, 0, sizeof(sli_command_engine_metadata_t));
    }

    // ---------------- Dynamic packet type TX scheduling ----------------
    if (events_received & SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT) {
      bool dynamic_queues_empty                                 = true;
      sli_command_engine_packet_type_configuration_node_t *node = instance->dynamic_packet_type;

      while (NULL != node) {
        if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(node->queue_info.packet_queue))
            && (node->queue_info.in_flight_command_count < node->packet_config.max_in_flight_command_count)) {

          status = sli_command_engine_handle_packet_tx(instance,
                                                       &(node->queue_info),
                                                       &(node->packet_config),
                                                       node->packet_type);
          if (SL_STATUS_OK != status) {
            break;
          }
        }

        if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(node->queue_info.packet_queue))
            && (node->queue_info.in_flight_command_count < node->packet_config.max_in_flight_command_count)) {
          dynamic_queues_empty = false;
        }

        node = node->next;
      }

      if (true == dynamic_queues_empty) {
        events_received &= ~(SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT);
      }
    }
  }

  return;
}

// Undo partial init; mirrors auxiliary-queue teardown order in sli_command_engine_deinit().
static sl_status_t sli_command_engine_deinit_if_initialized(sli_command_engine_t *instance)
{
  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  (void)sli_queue_manager_deinit(&(instance->control_queue), sli_command_engine_control_queue_flush_handler, NULL);

  (void)sli_queue_manager_deinit(&(instance->tx_status_packet_queue), NULL, NULL);

  (void)sli_queue_manager_deinit(&(instance->rx_packet_queue), sli_command_engine_queue_flush_handler, NULL);

  if (NULL != instance->command_engine_eventId) {
    osEventFlagsDelete(instance->command_engine_eventId);
    instance->command_engine_eventId = NULL;
  }

  if (NULL != instance->error_buffer) {
    sli_buffer_manager_free_buffer(instance->error_buffer);
    instance->error_buffer = NULL;
  }

  if (NULL != instance->command_engine_threadId) {
    osThreadTerminate(instance->command_engine_threadId);
    instance->command_engine_threadId = NULL;
  }

  instance->lifecycle = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT;

  return SL_STATUS_OK;
}

/******************************************************
 *             Internal Function Definitions
 ******************************************************/
// Initialize the command engine
sl_status_t sli_command_engine_init(sli_command_engine_t *instance,
                                    const sli_command_engine_configuration_t *command_config)
{
  sl_status_t status = SL_STATUS_FAIL;

  // Store the configuration in the instance
  instance->config                  = *command_config;
  instance->lifecycle               = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT;
  instance->dynamic_packet_type     = NULL; // before worker thread: avoids stale head on re-init
  instance->error_buffer            = NULL;
  instance->command_engine_eventId  = NULL;
  instance->command_engine_threadId = NULL;

  // Initialize RX packet queue
  status = sli_queue_manager_init(&(instance->rx_packet_queue), SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  VERIFY_STATUS_AND_RETURN(status);

  // Initialize TX status packet queue
  status = sli_queue_manager_init(&(instance->tx_status_packet_queue), SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  if (SL_STATUS_OK != status) {
    (void)sli_queue_manager_deinit(&(instance->rx_packet_queue), sli_command_engine_queue_flush_handler, NULL);
    instance->lifecycle = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT;
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Initialize control queue for dynamic packet type requests
  status = sli_queue_manager_init(&(instance->control_queue), SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  if (SL_STATUS_OK != status) {
    (void)sli_queue_manager_deinit(&(instance->tx_status_packet_queue), NULL, NULL);
    (void)sli_queue_manager_deinit(&(instance->rx_packet_queue), sli_command_engine_queue_flush_handler, NULL);
    instance->lifecycle = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT;
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Create event flags for the command engine
  instance->command_engine_eventId = osEventFlagsNew(NULL);
  if (NULL == instance->command_engine_eventId) {
    sli_command_engine_deinit_if_initialized(instance);
    VERIFY_STATUS_AND_RETURN(SL_STATUS_NO_MORE_RESOURCE);
  }

  // Initialize the error buffer
  status = sli_buffer_manager_allocate_buffer(instance->config.error_buffer_pool_type,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              1000,
                                              (sli_buffer_t *)&instance->error_buffer);
  if (SL_STATUS_OK != status) {
    sli_command_engine_deinit_if_initialized(instance);
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Thread attributes
  const osThreadAttr_t attr = {
    .name       = command_config->name,
    .priority   = command_config->priority,
    .stack_mem  = 0,
    .stack_size = command_config->stack_size,
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
  };

  // Create a new thread for the command engine
  instance->command_engine_threadId = osThreadNew(&sli_command_engine_thread, (void *)instance, &attr);
  if (NULL == instance->command_engine_threadId) {
    sli_command_engine_deinit_if_initialized(instance);
    VERIFY_STATUS_AND_RETURN(SL_STATUS_NO_MORE_RESOURCE);
  }

  instance->lifecycle = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_READY;

  return SL_STATUS_OK;
}

// Deinitialize the command engine
sl_status_t sli_command_engine_deinit(sli_command_engine_t *instance)
{
  uint32_t events_received                                  = 0;
  sli_command_engine_packet_type_configuration_node_t *node = NULL;

  // Validate input instance pointer
  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Ensure event ID object exists before using it
  if (NULL == instance->command_engine_eventId) {
    /* ponytail: already torn down (a previous deinit deleted the flags, or a
     * partial one left the instance half-cleaned). Returning an error here
     * aborts the whole driver teardown before it can clear device_initialized,
     * pinning the driver in the half-torn state where every sync command
     * returns NOT_READY. Force-clean the remnants and report success. */
    return sli_command_engine_deinit_if_initialized(instance);
  }

  instance->lifecycle = (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT;

  // Ask the command engine thread to terminate itself (thread will set ACK then self-terminate)
  sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_THREAD_TERMINATE_EVENT);

  // Wait (with timeout) for the thread to acknowledge termination
  events_received = sli_command_engine_wait_for_event(instance->command_engine_eventId,
                                                      SLI_COMMAND_ENGINE_THREAD_TERMINATE_ACK_EVENT,
                                                      5000);
  if (0 == events_received) {
    /* ponytail: no ACK — the worker is wedged (e.g. mid-recovery churn). Do
     * NOT bail: the osThreadTerminate below reaps it either way, and bailing
     * here leaves the flags/queues alive while the caller assumes the engine
     * is gone (same half-torn-zombie class as the HAL deinit fix). */
    SL_DEBUG_LOG_V2(ERROR, "command engine terminate ACK timeout, forcing teardown");
  }

  // Retry thread termination with a simple constant delay
  osStatus_t terminate_status = osError;

  // ponytail: NULL handle means the worker already self-terminated — nothing to reap.
  for (int retry = 0; (NULL != instance->command_engine_threadId) && (retry < 3); retry++) {
    terminate_status = osThreadTerminate(instance->command_engine_threadId);
    if (terminate_status == osOK) {
      break;
    }

    osDelay(SLI_SYSTEM_MS_TO_TICKS(20)); // Simple 20ms delay between retries
  }

  if (terminate_status != osOK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to terminate command engine thread after 3 retries");
  }

  // Clear stored thread id (thread has terminated)
  instance->command_engine_threadId = NULL;

  // Delete event flags and clear handle
  osEventFlagsDelete(instance->command_engine_eventId);
  instance->command_engine_eventId = NULL;

  // Deinitialize auxiliary queues
  sli_queue_manager_deinit(&(instance->control_queue), sli_command_engine_control_queue_flush_handler, NULL);
  sli_queue_manager_deinit(&(instance->tx_status_packet_queue), NULL, NULL);
  sli_queue_manager_deinit(&(instance->rx_packet_queue), sli_command_engine_queue_flush_handler, NULL);

  sli_command_engine_packet_flush_context_t flush_context = { 0 };
  flush_context.instance                                  = instance;

  // Walk and free all dynamically registered packet types
  while (NULL != instance->dynamic_packet_type) {
    node                             = instance->dynamic_packet_type;       // Take head
    instance->dynamic_packet_type    = instance->dynamic_packet_type->next; // Advance list head
    flush_context.packet_type        = node->packet_type;
    flush_context.packet_type_config = &(node->packet_config);
    sli_queue_manager_deinit(&(node->queue_info.packet_queue),
                             sli_command_engine_packet_queue_flush_handler,
                             (void *)&flush_context);
    sli_queue_manager_deinit(&(node->queue_info.inflight_packet_queue),
                             sli_command_engine_packet_queue_flush_handler,
                             (void *)&flush_context);
    SLI_FREE(node); // Free node memory
  }

  if (NULL != instance->error_buffer) {
    sli_buffer_manager_free_buffer(instance->error_buffer);
    instance->error_buffer = NULL;
  }

  return SL_STATUS_OK;
}

// Register (add) a new dynamic packet type at runtime.
// Allocates a node + request object, initializes its queues, enqueues a control
// request to the command engine thread, and waits for completion acknowledgment.
sl_status_t sli_command_engine_add_packet_type(sli_command_engine_t *instance,
                                               uint8_t packet_type,
                                               sli_command_engine_packet_type_configuration_t *packet_config)
{
  sl_status_t status                                              = SL_STATUS_OK;
  uint32_t events_received                                        = 0;
  sli_command_engine_packet_type_configuration_node_t *new_node   = NULL;
  sli_command_engine_packet_type_configuration_request_t *request = NULL;
  osThreadId_t thread_id = osThreadGetId(); // Thread to be notified on completion

  // Validate inputs
  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == packet_config) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == thread_id) { // Should not normally be NULL
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate node for the dynamic packet type
  new_node = (sli_command_engine_packet_type_configuration_node_t *)SLI_MALLOC(
    sizeof(sli_command_engine_packet_type_configuration_node_t));
  if (NULL == new_node) {
    return SL_STATUS_NO_MORE_RESOURCE;
  }

  // Allocate control request object
  request = (sli_command_engine_packet_type_configuration_request_t *)SLI_MALLOC(
    sizeof(sli_command_engine_packet_type_configuration_request_t));
  if (NULL == request) {
    SLI_FREE(new_node);
    return SL_STATUS_NO_MORE_RESOURCE;
  }

  // Copy user configuration into node
  new_node->packet_config = *packet_config;
  new_node->packet_type   = packet_type;
  new_node->next          = NULL;

  // Initialize TX queue for this packet type
  status = sli_queue_manager_init(&new_node->queue_info.packet_queue, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  if (SL_STATUS_OK != status) {
    SLI_FREE(new_node);
    SLI_FREE(request);
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Initialize in-flight (awaiting response) queue
  status = sli_queue_manager_init(&new_node->queue_info.inflight_packet_queue, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  if (SL_STATUS_OK != status) {
    SLI_FREE(request);
    sli_queue_manager_deinit(&new_node->queue_info.packet_queue, sli_command_engine_queue_flush_handler, NULL);
    SLI_FREE(new_node);
    VERIFY_STATUS_AND_RETURN(status);
  }
  new_node->queue_info.in_flight_command_count = 0;
  new_node->queue_info.tx_counter              = 0;
  new_node->queue_info.rx_counter              = 0;
  new_node->queue_info.packet_id               = 0;
  // new_node->sequential= 0;

  // Populate request
  request->request_type       = SLI_COMMAND_ENGINE_REGISTER_PACKET_TYPE;
  request->packet_type        = packet_type;
  request->packet_type_config = new_node;
  request->thread_id          = thread_id;

  // Enqueue request to control queue
  status = sli_queue_manager_enqueue(&(instance->control_queue), (void *)request);
  VERIFY_STATUS_AND_RETURN(status);

  // Notify command engine thread
  sli_command_engine_set_event(instance->command_engine_eventId,
                               SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT);

  // Wait for acknowledgment (same flag echoed back to requesting thread)
  events_received = osThreadFlagsWait(SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT, osFlagsWaitAny, 10000);
  if (!(events_received & SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT)) {
    // Timed out / failed: cleanup (thread never took ownership)
    SLI_FREE(request);
    sli_queue_manager_deinit(&new_node->queue_info.packet_queue, sli_command_engine_queue_flush_handler, NULL);
    sli_queue_manager_deinit(&new_node->queue_info.inflight_packet_queue, sli_command_engine_queue_flush_handler, NULL);
    SLI_FREE(new_node);
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

// Unregister (remove) a previously added dynamic packet type.
// Allocates a control request, enqueues it, and waits for acknowledgment.
// The command engine thread will free the node and its queues.
sl_status_t sli_command_engine_remove_packet_type(sli_command_engine_t *instance, uint8_t packet_type)
{
  sl_status_t status                                              = SL_STATUS_OK;
  uint32_t events_received                                        = 0;
  sli_command_engine_packet_type_configuration_request_t *request = NULL;
  osThreadId_t thread_id                                          = osThreadGetId(); // Requesting thread id

  // Validate inputs
  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (NULL == thread_id) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate control request
  request = (sli_command_engine_packet_type_configuration_request_t *)SLI_MALLOC(
    sizeof(sli_command_engine_packet_type_configuration_request_t));
  if (NULL == request) {
    return SL_STATUS_NO_MORE_RESOURCE;
  }

  // Populate request (no node pointer for remove)
  request->request_type       = SLI_COMMAND_ENGINE_UNREGISTER_PACKET_TYPE;
  request->packet_type        = packet_type;
  request->packet_type_config = NULL;
  request->thread_id          = thread_id;

  // Enqueue removal request
  status = sli_queue_manager_enqueue(&(instance->control_queue), (void *)request);
  VERIFY_STATUS_AND_RETURN(status);

  // Signal command engine to process request
  sli_command_engine_set_event(instance->command_engine_eventId,
                               SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT);

  // Wait for acknowledgment
  events_received = osThreadFlagsWait(SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT, osFlagsWaitAny, 10000);
  if (!(events_received & SLI_COMMAND_ENGINE_CONFIGURE_PACKET_TYPE_REQUEST_EVENT)) {
    // Thread did not acknowledge in time
    SLI_FREE(request);
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

sl_status_t sli_command_engine_is_idle(sli_command_engine_t *instance)
{
  // Validate inputs
  if (NULL == instance) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (0 != osEventFlagsGet(instance->command_engine_eventId)) {
    return SL_STATUS_BUSY;
  }

  for (sli_command_engine_packet_type_configuration_node_t *node = instance->dynamic_packet_type; node != NULL;
       node                                                      = node->next) {
    if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&(node->queue_info.packet_queue))) {
      return SL_STATUS_BUSY;
    }
  }

  return SL_STATUS_OK;
}

// Send (enqueue) a packet for transmission through the command engine.
// Allocates a metadata container, copies TX info, selects the correct
// dynamic queue, enqueues it, and signals the worker thread.
sl_status_t sli_command_engine_send_packet(sli_command_engine_t *instance, sli_command_engine_tx_info_t *tx_info)
{
  sl_status_t status                          = SL_STATUS_OK;
  sli_command_engine_metadata_t *metadata     = NULL; // Holds per-packet state until completion
  sli_command_engine_queue_info_t *queue_info = NULL; // Queue set from dynamic_packet_type
  uint32_t event_mask                         = 0;    // Event flag to wake TX scheduler

  // Validate inputs
  if ((NULL == instance) || (NULL == tx_info)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Reject new TX while not fully operational (includes @ref SLI_COMMAND_ENGINE_LIFECYCLE_DEINIT teardown).
  if ((instance->lifecycle != (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_READY)
      || (NULL == instance->command_engine_eventId)) {
    return SL_STATUS_NOT_READY;
  }

  // Allocate metadata buffer (hybrid allocation allows pool + heap fallback)
  status = sli_buffer_manager_allocate_buffer(instance->config.metadata_buffer_pool_type,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                              1000,
                                              (sli_buffer_t *)&metadata);
  VERIFY_STATUS_AND_RETURN(status);

  // Dynamic packet type: lookup its queue info
  status = sli_command_engine_get_dynamic_packet_info(instance, tx_info->packet_type, &queue_info, NULL);
  if ((SL_STATUS_OK != status) || (NULL == queue_info)) {
    // Unknown / unregistered dynamic type
    sli_buffer_manager_free_buffer(metadata);
    return SL_STATUS_INVALID_CONFIGURATION;
  }
  event_mask = SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT; // Dynamic TX scheduler event

  // Atomically allocate a monotonically increasing packet_id per packet type
  CORE_irqState_t state = CORE_EnterAtomic();
  tx_info->packet_id    = (queue_info->packet_id)++;
  CORE_ExitAtomic(state);

  // Populate metadata structure
  metadata->instance               = instance;                          // Back reference to engine instance
  metadata->tx_info.packet_type    = tx_info->packet_type;              // Store packet type (redundant but explicit)
  metadata->tx_info.packet_id      = tx_info->packet_id;                // Store assigned packet id
  metadata->tx_info                = *tx_info;                          // Copy full TX descriptor
  metadata->packet_start_tickcount = osKernelGetTickCount();            // Timestamp for timeout handling
  metadata->sync_resp_thread_id    = osThreadGetId();                   // Thread to signal for sync responses
  metadata->tx_status              = SLI_COMMAND_ENGINE_PACKET_TX_INIT; // Initial TX status

  // Enqueue metadata onto the packet queue (ownership transfers to queue on success)
  status = sli_queue_manager_enqueue(&(queue_info->packet_queue), (sl_slist_node_t *)metadata);
  VERIFY_STATUS_AND_RETURN(status);

  // Wake command engine thread to process TX scheduling
  sli_command_engine_set_event(instance->command_engine_eventId, event_mask);

  return SL_STATUS_OK;
}

sl_status_t sli_command_engine_signal_dynamic_tx(sli_command_engine_t *instance)
{
  if ((NULL == instance) || (NULL == instance->command_engine_eventId)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (instance->lifecycle != (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_READY) {
    return SL_STATUS_NOT_READY;
  }
  sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_DYNAMIC_PACKET_TYPE_TX_EVENT);
  return SL_STATUS_OK;
}

sl_status_t sli_command_engine_get_rx_queue_info_from_packet_type(
  sli_command_engine_t *instance,
  uint16_t packet_type,
  sli_command_engine_packet_type_configuration_t *packet_type_info)
{
  if ((NULL == instance) || (NULL == packet_type_info)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_command_engine_packet_type_configuration_t *temp_info = NULL;
  sl_status_t status = sli_command_engine_get_dynamic_packet_info(instance, packet_type, NULL, &temp_info);
  if ((SL_STATUS_OK != status) || (NULL == temp_info)) {
    return SL_STATUS_INVALID_CONFIGURATION;
  }

  packet_type_info->sync_response_queue    = temp_info->sync_response_queue;
  packet_type_info->sync_response_event    = temp_info->sync_response_event;
  packet_type_info->sync_response_event_id = temp_info->sync_response_event_id;
  return SL_STATUS_OK;
}

// Send a packet through the command engine
sl_status_t sli_command_engine_receive_packet(sli_command_engine_t *instance, void *data)
{
  sl_status_t status = SL_STATUS_OK;

  // Check if the instance or data is NULL
  if ((NULL == instance) || (NULL == data)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if ((instance->lifecycle != (uint8_t)SLI_COMMAND_ENGINE_LIFECYCLE_READY)
      || (NULL == instance->command_engine_eventId)) {
    return SL_STATUS_NOT_READY;
  }

  // Enqueue the data packet into the appropriate queue
  status = sli_queue_manager_enqueue(&(instance->rx_packet_queue), data);
  VERIFY_STATUS_AND_RETURN(status);

  // Signal the thread to handle the packet
  sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_PACKET_RX_EVENT);

  // Return success status
  return SL_STATUS_OK;
}

//------------------------------------------------------------------------------
// @brief  Callback invoked after a packet transmit attempt completes.
//         Stores the transmit status in the metadata, enqueues the metadata
//         onto the TX status queue, and signals the command engine thread
//         so it can continue processing (e.g. move to in‑flight or free).
// @param  packet_type  Type of the transmitted packet (unused here)
// @param  status       Result of the transmit operation
// @param  context      Pointer to sli_command_engine_metadata_t provided at TX
//------------------------------------------------------------------------------
void sli_command_engine_send_packet_tx_status(uint16_t packet_type, sl_status_t status, void *context)
{
  UNUSED_PARAMETER(packet_type); // Packet type not needed in this callback

  // Recover metadata and owning command engine instance from context
  sli_command_engine_metadata_t *metadata = (sli_command_engine_metadata_t *)context;
  sli_command_engine_t *instance          = metadata->instance;

  if (metadata->tx_status == SLI_COMMAND_ENGINE_TEARDOWN_DONE) {
    sli_buffer_manager_free_buffer(metadata);
    return;
  }

  // Record the TX completion status in the metadata
  metadata->packet_status = status;

  // Enqueue metadata for the command engine thread to post-process
  status = sli_queue_manager_enqueue(&(instance->tx_status_packet_queue), (void *)metadata);
  if (SL_STATUS_OK != status) {
    // If enqueue fails we silently drop; fatal error path handled elsewhere
    return;
  }

  // Wake the command engine thread to handle TX ACK processing
  sli_command_engine_set_event(instance->command_engine_eventId, SLI_COMMAND_ENGINE_PACKET_TX_ACK_EVENT);
  return;
}
