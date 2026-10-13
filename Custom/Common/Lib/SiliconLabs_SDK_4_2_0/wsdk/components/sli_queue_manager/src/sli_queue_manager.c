/***************************************************************************/ /**
 * @file sli_queue_manager.c
 * @brief
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
#include <stdint.h>
#include <assert.h>
#include "sl_core.h"
#include "sl_constants.h"
#include "sli_queue_manager_types.h"

#ifdef SIMULATION_SDK_SPIN_ERROR
// CLI fault injection ("ifconfig wl error_test sdk_qkill|sdk_qclear") — the
// VALIDATED reproducer for the 0x19 field death (2026-08-26: qkill + one ping
// -> raw TX 0x19 x30 -> real recovery -> zombie -> full console death, no
// watchdog reset, ~90s).
//
// It force-fails dequeue WITHOUT touching the queue, so "non-empty + dequeue
// fails" holds forever — the state the 0x19 recovery churn leaves behind
// (queues re-init'ed over live nodes, half-torn-down pools). The engines
// drain with `while (!IS_QUEUE_EMPTY(q)) { dequeue; if (fail) continue; }`
// (event engine registration/scan loops, command engine control loop) — the
// `continue` re-tests the same non-empty queue without advancing, so the
// loop never terminates and never yields: whichever engine thread drains
// next (command engine tx8 on TX traffic, event engine tx6 on async events)
// hot-spins at a priority above the console (tx9) and log writer (tx48),
// while watchdog (tx1) keeps getting fed -> 8h-silence signature.
//
// One failing dequeue in the pool's free-queue allocation path also maps to
// SL_STATUS_ALLOCATION_FAILED (0x19) upstream, so arming before any traffic
// reproduces the natural field error sequence. Needs one enqueue after
// arming to enter a drain loop; sdk_qclear disarms. Fix validation: with the
// drain-loop `continue`->`break` patches applied, this injection must degrade
// to a logged error instead of killing the console.
//
// (Sibling injection sdk_flagkill — NULL the event engine's flags object —
// was REMOVED 2026-08-26: refuted on hardware. This port's
// osFlagsErrorParameter is 0xFFFFFFFC, not the CMSIS-spec 0x80000000, and
// its bit pattern overlaps the engine's TERMINATE flag (1<<23), so the
// thread self-terminates instead of spinning.)
static uint8_t sim_queue_dequeue_fail = 0;

void sli_queue_manager_sim_dequeue_fail(uint8_t mode)
{
  sim_queue_dequeue_fail = mode;
}
#endif

sl_status_t sli_queue_manager_init(sli_queue_t *handle, sli_buffer_manager_pool_types_t queue_node_pool)
{
  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (queue_node_pool >= SLI_BUFFER_MANAGER_MAX_POOL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  handle->head            = NULL;
  handle->tail            = NULL;
  handle->queue_node_pool = queue_node_pool;
  handle->lock            = NULL;

  return SL_STATUS_OK;
}

sl_status_t sli_queue_manager_enqueue_node(sli_queue_t *handle, sli_queue_node_t *node)
{
  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == node) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  CORE_irqState_t state = CORE_EnterAtomic();
  node->next            = NULL;
  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    handle->head = node;
    handle->tail = node;
  } else {
    handle->tail->next = node;
    handle->tail       = node;
  }

  CORE_ExitAtomic(state);

  return SL_STATUS_OK;
}

sl_status_t sli_queue_manager_enqueue(sli_queue_t *handle, void *data)
{
  sl_status_t status     = SL_STATUS_OK;
  sli_queue_node_t *node = NULL;

  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == data) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  status = sli_buffer_manager_allocate_buffer((const sli_buffer_manager_pool_types_t)handle->queue_node_pool,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              1000,
                                              (sli_buffer_t *)&node);
  VERIFY_STATUS_AND_RETURN(status);
  node->data = data;

  return sli_queue_manager_enqueue_node(handle, node);
}

sl_status_t sli_queue_manager_add_to_queue_head(sli_queue_t *handle, void *data)
{
  sl_status_t status     = SL_STATUS_OK;
  sli_queue_node_t *node = NULL;

  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == data) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  status = sli_buffer_manager_allocate_buffer((const sli_buffer_manager_pool_types_t)handle->queue_node_pool,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              1000,
                                              (sli_buffer_t *)&node);
  VERIFY_STATUS_AND_RETURN(status);
  node->data = data;
  node->next = NULL;

  CORE_irqState_t state = CORE_EnterAtomic();

  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    handle->head = node;
    handle->tail = node;
  } else {
    node->next   = handle->head;
    handle->head = node;
  }

  CORE_ExitAtomic(state);

  return SL_STATUS_OK;
}

sl_status_t sli_queue_manager_dequeue_node(sli_queue_t *handle, sli_queue_node_t **node)
{
  sl_status_t status = SL_STATUS_EMPTY;

  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == node) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  CORE_irqState_t state = CORE_EnterAtomic();
  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    *node  = NULL;
    status = SL_STATUS_EMPTY;
  } else {
    *node  = handle->head;
    status = SL_STATUS_OK;
    if (handle->head == handle->tail) {
      handle->head = NULL;
      handle->tail = NULL;
    } else {
      handle->head = handle->head->next;
    }
  }
  CORE_ExitAtomic(state);

  if (NULL != *node) {
    (*node)->next = NULL;
  }

  return status;
}

sl_status_t sli_queue_manager_dequeue(sli_queue_t *handle, void **data)
{
  sl_status_t status     = SL_STATUS_OK;
  sli_queue_node_t *node = NULL;

  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == data) {
    return SL_STATUS_INVALID_PARAMETER;
  }

#ifdef SIMULATION_SDK_SPIN_ERROR
  // Fault injection: fail WITHOUT touching the queue, so "non-empty +
  // dequeue fails" holds forever. See the sim block above the init function.
  if (sim_queue_dequeue_fail) {
    *data = NULL;
    return SL_STATUS_FAIL;
  }
#endif

  status = sli_queue_manager_dequeue_node(handle, &node);

  if (status == SL_STATUS_OK && node != NULL) {
    *data  = node->data;
    status = sli_buffer_manager_free_buffer((sli_buffer_t)node);
  } else {
    *data = NULL;
  }

  return status;
}

sl_status_t sli_queue_manager_remove_node_from_queue(sli_queue_t *handle,
                                                     sli_queue_manager_node_match_handler_t id_handler,
                                                     const void *node_match_data,
                                                     void **data)
{
  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == data) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (NULL == id_handler) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status     = SL_STATUS_NOT_FOUND;
  sli_queue_node_t *node = NULL;
  sli_queue_node_t *prev = NULL;

  *data = NULL;

  CORE_irqState_t state = CORE_EnterAtomic();

  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    CORE_ExitAtomic(state);
    return SL_STATUS_EMPTY;
  }

  prev = NULL;
  node = handle->head;
  while (node) {
    if (true == id_handler(handle, node->data, node_match_data)) {
      status = SL_STATUS_OK;
      break;
    }

    prev = node;
    node = node->next;
  }
  if (node) {
    if ((handle->head == node) && (handle->head == handle->tail)) {
      handle->head = NULL;
      handle->tail = NULL;
    } else if (handle->head == node) {
      handle->head = node->next;
    } else if (handle->tail == node) {
      handle->tail = prev;
    } else {
      prev->next = node->next;
    }
  }
  CORE_ExitAtomic(state);

  if (node) {
    node->next = NULL;
    *data      = node->data;
    status     = sli_buffer_manager_free_buffer((sli_buffer_t)node);
  }

  return status;
}

sl_status_t sli_queue_manager_flush_nodes_from_queue(sli_queue_t *handle,
                                                     sli_queue_manager_node_match_handler_t id_handler,
                                                     void *node_match_data,
                                                     sli_queue_manager_flush_handler_t flush_handler)
{
  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  sli_queue_node_t *node    = NULL;
  sli_queue_node_t *prev    = NULL;
  sli_queue_node_t *element = NULL;
  sl_status_t status        = SL_STATUS_OK;

  if (NULL == id_handler) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  CORE_irqState_t state = CORE_EnterAtomic();
  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    CORE_ExitAtomic(state);
    return SL_STATUS_OK;
  }

  prev = NULL;
  node = handle->head;

  while (NULL != node) {
    if (true == id_handler(handle, node->data, node_match_data)) {
      element = node;

      // Remove the matched packet from the queue
      if (NULL == prev) {
        handle->head = element->next;
        node         = element->next;
      } else {
        prev->next = element->next;
        node       = element->next;
      }

      if (handle->tail == element) {
        handle->tail = prev;
      }

      element->next = NULL;
    } else {
      prev = node;
      node = node->next;
    }

    if (element != NULL) {
      // Free the matched packet if required by the user
      if (flush_handler != NULL) {
        flush_handler(handle, element->data, node_match_data);
      }

      status = sli_buffer_manager_free_buffer((sli_buffer_t)element);
      if (status != SL_STATUS_OK) {
        CORE_ExitAtomic(state);
        return status;
      }
      element = NULL;
    }
  }

  if (NULL == handle->head) {
    handle->tail = NULL;
  }

  CORE_ExitAtomic(state);
  return SL_STATUS_OK;
}

sl_status_t sli_queue_manager_flush_queue(sli_queue_t *handle,
                                          sli_queue_manager_flush_handler_t flush_handler,
                                          void *context)
{
  if (NULL == handle) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_queue_node_t *node = NULL;
  sl_status_t status     = SL_STATUS_OK;

  CORE_irqState_t state = CORE_EnterAtomic();
  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(handle)) {
    assert(handle->tail == NULL); // Both should be NULL at the same time
    CORE_ExitAtomic(state);
    return SL_STATUS_OK;
  }

  while (handle->head) {
    node = handle->head;

    if (handle->head == handle->tail) {
      handle->head = NULL;
      handle->tail = NULL;
    } else {
      handle->head = handle->head->next;
    }

    if (flush_handler) {
      flush_handler(handle, node->data, context);
    }

    status = sli_buffer_manager_free_buffer((sli_buffer_t)node);
    if (status != SL_STATUS_OK) {
      CORE_ExitAtomic(state);
      return status;
    }
  }

  CORE_ExitAtomic(state);
  return SL_STATUS_OK;
}

sl_status_t sli_queue_manager_deinit(sli_queue_t *handle,
                                     sli_queue_manager_flush_handler_t flush_handler,
                                     void *context)
{
  return sli_queue_manager_flush_queue(handle, flush_handler, context);
}