/***************************************************************************/ /**
 * @file sli_buffer_manager.c
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
#include "sli_buffer_manager.h"
#include "sl_status.h"
#include "sl_constants.h"
#include "stdlib.h"
#include "string.h"
#include "cmsis_os2.h"
#include "sl_core.h"
#include "sl_slist.h"
#include "sl_cmsis_utility.h"
#define SLI_MEM_POOL_BLOCK_SIZE(x)               \
  (x                                             \
   + sizeof(sli_buffer_manager_mempool_handler_t \
              *)) ///< Calculate the block size based on metadata present in the internal buffer.

#define SLI_MAX_MEMPOOL_HANDLERS_COUNT SLI_BUFFER_MANAGER_MAX_POOL ///< Maximum number of memory pools.

#define SLI_MINIUM_ELEMENTS_IN_COMMON_MEMPOOL_QUEUE \
  1 ///< This macro determines minimum number of common mempools present in the common mempool queue.

#define SLI_ZERO_TIMEOUT 0

/***************************************************************************************************************** 
 * @brief Internal structures
*********************************************************************************************************************/
typedef struct {
  sl_slist_node_t next;          //< Next node(used only in case of common mempool).
  void *mempool_memory;          ///< Memory pool memory.
  sli_mem_pool_handle_t mempool; ///< Memory pool handler.

  uint16_t max_buffer_count;       ///< Maximum buffer count.
  uint16_t allocated_buffer_count; ///< Allocated buffer count.
  bool is_common_pool;             ///< Whether the buffer has been allocated from common mempool.
} sli_buffer_manager_mempool_handler_t;

#pragma pack(1)
typedef struct {
  sli_buffer_manager_mempool_handler_t
    *buffer_manager_mempool_handler; ///< pointer of the mempool from which the data has been allocated.
  uint8_t data[];                    ///< Data.
} sli_internal_buffer_t;
#pragma pack()

typedef struct {
  sli_buffer_manager_mempool_handler_t *head; ///< Head of the queue.
  sli_buffer_manager_mempool_handler_t *tail; ///< Tail of the queue.

  sli_buffer_manager_mempool_handler_t *last_used_handler; ///< Pointer to the last used common mempool.
  uint8_t size;                                            ///< Number of common mempools in the queue.
} sli_buffer_manager_mempool_queue_t;

/***************************************************************************************************************** 
 * Static variables
 * ****************************************************************************************************************/
static sli_buffer_manager_mempool_handler_t dedicated_mempool_handlers[SLI_MAX_MEMPOOL_HANDLERS_COUNT] = { 0 };
static sli_buffer_manager_mempool_queue_t common_mempool_queue                                         = { 0 };
static sli_buffer_manager_pool_info_t common_mempool_configuration                                     = { 0 };
/***************************************************************************************************************** 
 * Static functions
 * ****************************************************************************************************************/

/**
 * @brief Function to check if the timeout has expired.
 *
 * @param start_time Start time.
 * @param wait_time Wait time.
 * @return true if the timeout has expired.
 */
inline static bool sli_buffer_manager_has_timeout_expired(uint32_t start_time, uint32_t wait_time)
{
  return ((osKernelGetTickCount() - start_time) > wait_time);
}

/**
 * @brief Function to create and assign a SiSDK's mempool to sli_buffer_manager_mempool_handler_t.
 *
 * @param configuration Memory pool configuration.
 * @param mempool_handler Memory pool handler.
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_create_and_assign_mempool(sli_buffer_manager_pool_info_t *configuration,
                                                                sli_buffer_manager_mempool_handler_t *mempool_handler,
                                                                bool is_common_pool)
{
  CORE_irqState_t state = CORE_EnterAtomic();
  size_t buffer_size    = (size_t)configuration->block_count * SLI_MEM_POOL_BLOCK_SIZE(configuration->block_size);
  mempool_handler->mempool_memory = SLI_MALLOC(buffer_size);
  if (mempool_handler->mempool_memory == NULL) {
    CORE_ExitAtomic(state);
    return SL_STATUS_ALLOCATION_FAILED;
  }

  sli_mem_pool_create(&mempool_handler->mempool,
                      SLI_MEM_POOL_BLOCK_SIZE(configuration->block_size),
                      configuration->block_count,
                      mempool_handler->mempool_memory,
                      buffer_size);

  mempool_handler->max_buffer_count       = configuration->block_count;
  mempool_handler->allocated_buffer_count = 0;
  mempool_handler->is_common_pool         = is_common_pool;

  CORE_ExitAtomic(state);

  return SL_STATUS_OK;
}

/*
 * @brief Function to check if all buffer pools are deallocated.
 * @return true if all buffer pools are deallocated, false otherwise.
 */

static bool sli_buffer_manager_are_all_pools_deallocated()
{
  CORE_irqState_t state = CORE_EnterAtomic();

  SL_DEBUG_LOG_V2(DEBUG, "Buffer Manager Pools Status:\r\n");

  // Dedicated pools
  for (uint8_t i = 0; i < SLI_MAX_MEMPOOL_HANDLERS_COUNT; i++) {
    sli_buffer_manager_mempool_handler_t *handler = &dedicated_mempool_handlers[i];
    if (handler->mempool_memory != NULL) {
      SL_DEBUG_LOG_V2(DEBUG, "Dedicated Pool %u: Max Buffers = %u", i, handler->max_buffer_count);
      SL_DEBUG_LOG_V2(DEBUG, "Dedicated Pool %u: Allocated = %u", i, handler->allocated_buffer_count);

      if (handler->allocated_buffer_count > 0) {
        CORE_ExitAtomic(state);
        return false;
      }
    }
  }

  SL_DEBUG_LOG_V2(DEBUG, "Common Pools (Queue Size: %u):\r\n", common_mempool_queue.size);

  /* ponytail: the SDK do-while below dereferences `current` before testing it
   * ("there shall be at least one common pool") — but on a clean first boot
   * there is NOT one: the re-init guard in sli_buffer_manager_init() calls
   * deinit over an all-zero state, head == NULL, and the walk hops through
   * the vector table / flash under PRIMASK until it happens to exit (or
   * faults on an unmapped hop). With pools present this is unreachable
   * (size > 0), so gate the whole walk on the queue being populated. */
  if ((common_mempool_queue.size == 0) || (common_mempool_queue.head == NULL)) {
    CORE_ExitAtomic(state);
    return true;
  }

  sli_buffer_manager_mempool_handler_t *current = common_mempool_queue.head;

  uint8_t pool_idx = 0;
  do {
    SL_DEBUG_LOG_V2(DEBUG, "Common Pool %u: Max Buffers = %u", pool_idx, current->max_buffer_count);
    SL_DEBUG_LOG_V2(DEBUG, "Common Pool %u: Allocated = %u", pool_idx, current->allocated_buffer_count);
    current = (sli_buffer_manager_mempool_handler_t *)current->next.node;
    pool_idx++;

    if (current->allocated_buffer_count > 0) {
      CORE_ExitAtomic(state);
      return false;
    }
    // ponytail: size bound — a broken ring (tail/head unreachable) must not
    // let this read-only walk chase next-pointers indefinitely under PRIMASK.
    // A healthy ring visits exactly `size` handlers and wraps out first.
  } while ((current != common_mempool_queue.head) && (current != NULL) && (pool_idx <= common_mempool_queue.size));

  CORE_ExitAtomic(state);
  return true;
}

/**
 * @brief Function to allocate a buffer from the dedicated pool.
 *
 * @param buffer Buffer.
 * @param pool_type Pool type.
 * @param start_time Start time.
 * @param wait_duration_ms Wait duration.
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_allocate_buffer_from_dedicated_pool(
  sli_internal_buffer_t **buffer,
  const sli_buffer_manager_pool_types_t pool_type,
  uint32_t start_time,
  uint32_t wait_duration_ms)
{
  CORE_irqState_t state = CORE_EnterAtomic();

  sli_buffer_manager_mempool_handler_t *mempool_handler = &dedicated_mempool_handlers[pool_type];
  // uint8_t delay                                         = 2;
  // uint8_t new_delay                                     = 2;
  if (mempool_handler->mempool_memory == NULL) {
    CORE_ExitAtomic(state);
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (mempool_handler->max_buffer_count == 0) {
    CORE_ExitAtomic(state);
    return SL_STATUS_ALLOCATION_FAILED;
  }

  *buffer = NULL;
  CORE_ExitAtomic(state);

  // ponytail: the original fixed osDelay(2) poll meant 500 sleeps/s ping-ponging
  // the caller with the System Timer thread (tx0, highest priority — it services
  // every sleep expiry) while the pool is exhausted. During a wedged pipe (0x19
  // storm) each failed frame burned its whole 1s wait at that rate and starved
  // lower-priority threads (console dead >1min at 99.8% CPU). Enable the
  // exponential backoff the SDK sketched in the comment below, with a 5ms floor
  // and the SDK's own 50ms cap: transient pool pressure still rechecks within
  // 5-20ms, a full 1s wedge wait costs ~25 sleeps instead of 500.
  uint32_t delay_ms = 5;

  do {
    CORE_irqState_t state = CORE_EnterAtomic();
    if (mempool_handler->allocated_buffer_count < mempool_handler->max_buffer_count) {
      *buffer = (sli_internal_buffer_t *)sli_mem_pool_alloc(&mempool_handler->mempool);
      if (*buffer != NULL) {
        (*buffer)->buffer_manager_mempool_handler = mempool_handler;
        mempool_handler->allocated_buffer_count++;
        CORE_ExitAtomic(state);
        break;
      }
    }
    CORE_ExitAtomic(state);
    osDelay(SLI_SYSTEM_MS_TO_TICKS(delay_ms));
    delay_ms = (delay_ms < 50) ? (delay_ms * 2) : 50;
  } while ((osKernelGetTickCount() - start_time) <= wait_duration_ms);

  return (*buffer == NULL) ? SL_STATUS_ALLOCATION_FAILED : SL_STATUS_OK;
}

/**
 * @brief Function to allocate a buffer from the common pool.
 *
 * @param buffer Buffer.
 * @param start_time Start time.
 * @param wait_duration_ms Wait duration.
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_allocate_buffer_from_common_pool(sli_internal_buffer_t **buffer,
                                                                       uint32_t start_time,
                                                                       const uint32_t wait_duration_ms)
{
  /* ponytail: this search used to run entirely inside ONE CORE_EnterAtomic — on
   * our port Atomic == PRIMASK, so the whole pool lap ran with every interrupt
   * (SysTick included) masked, and the elapsed-time exit condition read a frozen
   * tick counter. The circular-list wrap condition kept it bounded, but there is
   * no reason to hold a global irq mask across a multi-pool search: take the
   * atomic per iteration, like the dedicated-pool allocator above. */
  CORE_irqState_t state = CORE_EnterAtomic();

  // If there are no common mempools in the queue, return.
  if (common_mempool_queue.size == 0) {
    CORE_ExitAtomic(state);
    return SL_STATUS_FAIL;
  }

  sli_buffer_manager_mempool_handler_t *mempool_handler = common_mempool_queue.last_used_handler;
  *buffer                                               = NULL;
  CORE_ExitAtomic(state);

  do {
    CORE_irqState_t iter_state = CORE_EnterAtomic();

    if (mempool_handler->allocated_buffer_count >= mempool_handler->max_buffer_count) {
      mempool_handler = (sli_buffer_manager_mempool_handler_t *)mempool_handler->next.node;
      CORE_ExitAtomic(iter_state);
      continue;
    }

    *buffer = (sli_internal_buffer_t *)sli_mem_pool_alloc(&mempool_handler->mempool);
    if ((*buffer) != NULL) {
      (*buffer)->buffer_manager_mempool_handler = mempool_handler;
      mempool_handler->allocated_buffer_count++;
      common_mempool_queue.last_used_handler = mempool_handler;
      CORE_ExitAtomic(iter_state);
      return SL_STATUS_OK;
    }

    // If the buffer is not allocated, move to the next mempool handler.
    mempool_handler = (sli_buffer_manager_mempool_handler_t *)mempool_handler->next.node;
    CORE_ExitAtomic(iter_state);

  } while (((osKernelGetTickCount() - start_time) < wait_duration_ms)
           && (mempool_handler != common_mempool_queue.last_used_handler));

  return SL_STATUS_ALLOCATION_FAILED;
}

/**
 * @brief Function to create a new common memory pool and append it to the common mempool queue.
 *
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_create_new_common_mempool(void)
{

  CORE_irqState_t state                                 = CORE_EnterAtomic();
  sli_buffer_manager_mempool_handler_t *mempool_handler = SLI_MALLOC(sizeof(sli_buffer_manager_mempool_handler_t));

  if (mempool_handler == NULL) {
    CORE_ExitAtomic(state);
    return SL_STATUS_ALLOCATION_FAILED;
  }

  memset(mempool_handler, 0, sizeof(sli_buffer_manager_mempool_handler_t));

  sl_status_t status =
    sli_buffer_manager_create_and_assign_mempool(&common_mempool_configuration, mempool_handler, true);

  if (status != SL_STATUS_OK) {
    SLI_FREE(mempool_handler);

    CORE_ExitAtomic(state);
    return status;
  }

  if (common_mempool_queue.head == NULL && common_mempool_queue.tail == NULL) {
    common_mempool_queue.head = mempool_handler;
    common_mempool_queue.tail = mempool_handler;

  } else {
    common_mempool_queue.tail->next.node = (sl_slist_node_t *)mempool_handler;
    common_mempool_queue.tail            = mempool_handler;
  }

  // Assign head as the next node of the tail.
  mempool_handler->next.node = (sl_slist_node_t *)common_mempool_queue.head;

  // Assign last_used_handler pointer to the newly created mempool.
  common_mempool_queue.last_used_handler = mempool_handler;

  common_mempool_queue.size++;
  CORE_ExitAtomic(state);
  return SL_STATUS_OK;
}

/**
 * @brief Function to remove a mempool from the common mempool queue.
 *
 * @param common_pool_handler Common  mempool handler.
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_free_a_common_mempool_from_queue(
  sli_buffer_manager_mempool_handler_t *common_pool_handler)
{
  CORE_irqState_t state = CORE_EnterAtomic();

  // If the node to be removed is the head node, update the head pointer.
  if (common_pool_handler == common_mempool_queue.head) {
    common_mempool_queue.head            = (sli_buffer_manager_mempool_handler_t *)common_pool_handler->next.node;
    common_mempool_queue.tail->next.node = (sl_slist_node_t *)common_mempool_queue.head;

    if (common_mempool_queue.last_used_handler == common_pool_handler) {
      common_mempool_queue.last_used_handler = common_mempool_queue.tail;
    }

    SLI_FREE(common_pool_handler->mempool_memory);
    SLI_FREE(common_pool_handler);

    common_mempool_queue.size--;
    CORE_ExitAtomic(state);
    return SL_STATUS_OK;
  }

  sli_buffer_manager_mempool_handler_t *current_node =
    (sli_buffer_manager_mempool_handler_t *)common_mempool_queue.head->next.node;
  sli_buffer_manager_mempool_handler_t *previous_node = common_mempool_queue.head;

  // Loop until we find the required node or we reached the head again.
  while (current_node != common_pool_handler && current_node != common_mempool_queue.head) {
    previous_node = current_node;
    current_node  = (sli_buffer_manager_mempool_handler_t *)current_node->next.node;
  }

  if (current_node != common_pool_handler) {
    CORE_ExitAtomic(state);
    return SL_STATUS_NOT_FOUND;
  }

  // Update the next pointer of the previous node to the next pointer of the current node.
  previous_node->next.node = current_node->next.node;

  // If node that is to be removed is tail node, update the tail pointer.
  if (current_node == common_mempool_queue.tail) {
    common_mempool_queue.tail = (sli_buffer_manager_mempool_handler_t *)current_node->next.node;
  }

  // Update the last_used_handler pointer if the node that is to be removed is the last_used_handler node.
  if (common_mempool_queue.last_used_handler == common_pool_handler) {
    common_mempool_queue.last_used_handler = common_mempool_queue.tail;
  }

  SLI_FREE(current_node->mempool_memory);
  SLI_FREE(current_node);

  common_mempool_queue.size--;

  CORE_ExitAtomic(state);
  return SL_STATUS_OK;
}

/**
 * @brief Function to free all the common mempools.
 *
 * @return SL_STATUS_OK if the operation is successful.
 */
static sl_status_t sli_buffer_manager_free_all_common_mempools(void)
{
  CORE_irqState_t state = CORE_EnterAtomic();

  ///< If there are no common mempools, return.
  if (common_mempool_queue.size <= 0) {
    CORE_ExitAtomic(state);
    return SL_STATUS_OK;
  }

  sli_buffer_manager_mempool_handler_t *head                = common_mempool_queue.head;
  sli_buffer_manager_mempool_handler_t *mempool_to_be_freed = NULL;

  // Note: we are intentionally not updating the tail reference
  /* ponytail: two guards against a corrupted queue (0x19 churn can leave
   * size > 0 with a broken ring). head == NULL would deref the vector table
   * on the first lap (same family as the are_all_pools_deallocated
   * zero-state walk); the size bound stops the drain after `size` frees even
   * when `tail` is unreachable — the old condition could chase next-pointers
   * through freed/garbage nodes and SLI_FREE garbage into the PSRAM heap. A
   * healthy ring frees exactly `size` nodes and never reaches either guard.
   * The memsets below still run so the next init starts from a clean queue. */
  if (head != NULL) {
    uint8_t remaining = common_mempool_queue.size;
    do {
      mempool_to_be_freed = head;
      head                = (sli_buffer_manager_mempool_handler_t *)mempool_to_be_freed->next.node;

      SLI_FREE(mempool_to_be_freed->mempool_memory);
      SLI_FREE(mempool_to_be_freed);
    } while ((mempool_to_be_freed != common_mempool_queue.tail) && (--remaining != 0u));
  }

  memset(&common_mempool_queue, 0, sizeof(sli_buffer_manager_mempool_queue_t));
  memset(&common_mempool_configuration, 0, sizeof(sli_buffer_manager_pool_info_t));

  CORE_ExitAtomic(state);

  return SL_STATUS_OK;
}

static sl_status_t sli_buffer_manager_free_all_mempools(void)
{
  CORE_irqState_t state = CORE_EnterAtomic();

  // Free the dedicated mempools.
  for (uint8_t index = 0; index < SLI_MAX_MEMPOOL_HANDLERS_COUNT; index++) {
    sli_buffer_manager_mempool_handler_t *mempool_handler = &dedicated_mempool_handlers[index];

    if (mempool_handler->mempool_memory != NULL) {
      SLI_FREE(mempool_handler->mempool_memory);
      memset(mempool_handler, 0, sizeof(sli_buffer_manager_mempool_handler_t));
    }
  }

  // Free the common mempool.
  sli_buffer_manager_free_all_common_mempools();

  CORE_ExitAtomic(state);

  return SL_STATUS_OK;
}

/***************************************************************************************************************** 
 * Public functions
 * ****************************************************************************************************************/
sl_status_t sli_buffer_manager_init(sli_buffer_manager_configuration_t *configuration)
{
  SL_VERIFY_POINTER_OR_RETURN(configuration, SL_STATUS_NULL_POINTER);
  sl_status_t status = SL_STATUS_ALLOCATION_FAILED;
  if (configuration->common_pool_info.block_count == 0 || configuration->common_pool_info.block_size == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* ponytail: re-init guard. A teardown that aborted midway (driver deinit
   * bails before reaching buffer_manager_deinit) can leave pools alive while
   * the driver's bookkeeping says "not initialized" — the recovery loop then
   * calls init again and create_and_assign_mempool would layer a second set
   * of pools on top of the stale handles (leak + mixed generations). Force
   * the old generation away first; on a clean boot this is a no-op. */
  (void)sli_buffer_manager_deinit();

  // Validate dedicated pool configurations here itself to avoid allocation and free due to misconfiguration.
  for (uint8_t index = 0; index < SLI_BUFFER_MANAGER_MAX_POOL; index++) {
    if (configuration->pool_info[index] != NULL && configuration->pool_info[index]->block_count != 0
        && configuration->pool_info[index]->block_size == 0) {
      return SL_STATUS_INVALID_PARAMETER;
    }
  }

  for (uint8_t index = 0; index < SLI_BUFFER_MANAGER_MAX_POOL; index++) {
    if (configuration->pool_info[index] == NULL || configuration->pool_info[index]->block_count == 0) {
      continue;
    }

    status = sli_buffer_manager_create_and_assign_mempool(configuration->pool_info[index],
                                                          &dedicated_mempool_handlers[index],
                                                          false);
    if (status != SL_STATUS_OK) {
      sli_buffer_manager_free_all_mempools();
      return SL_STATUS_NO_MORE_RESOURCE;
    }
  }

  memcpy(&common_mempool_configuration, &configuration->common_pool_info, sizeof(sli_buffer_manager_pool_info_t));
  status = sli_buffer_manager_create_new_common_mempool();

  if (status != SL_STATUS_OK) {
    sli_buffer_manager_free_all_mempools();
    return SL_STATUS_NO_MORE_RESOURCE;
  }

  return SL_STATUS_OK;
}

sl_status_t sli_buffer_manager_deinit(void)
{
  bool are_deallocated = sli_buffer_manager_are_all_pools_deallocated();

  if (!are_deallocated) {
    // ponytail: outstanding buffers at teardown mean the chip/driver is
    // already in a malfunction state (0x19 recovery churn). Returning BUSY
    // here used to abort sl_si91x_driver_deinit() before the power cycle
    // and the device_initialized reset, leaving a zombie driver that could
    // never re-initialize (field 8h hang). The straggler buffers are
    // unreachable at this point (bus interrupt off, engines deinited) —
    // force-free the pool memory so teardown completes.
    SL_DEBUG_LOG_V2(ERROR, "buffer manager: pools still allocated at deinit, force-freeing\r\n");
  }

  sli_buffer_manager_free_all_mempools();
  return SL_STATUS_OK;
}

sl_status_t sli_buffer_manager_allocate_buffer(const sli_buffer_manager_pool_types_t pool_type,
                                               const sli_buffer_manager_allocation_types_t allocation_type,
                                               const uint32_t wait_duration_ms,
                                               sli_buffer_t *buffer)
{
  sli_internal_buffer_t *internal_buffer = NULL;
  uint32_t start                         = osKernelGetTickCount();
  // Allocate buffer from the dedicated pool incase of SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED.

  if (allocation_type == SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED) {
    sl_status_t status =
      sli_buffer_manager_allocate_buffer_from_dedicated_pool(&internal_buffer, pool_type, start, wait_duration_ms);
    VERIFY_STATUS_AND_RETURN(status);

    *buffer = internal_buffer->data;
    return SL_STATUS_OK;
  }

  sl_status_t status = sli_buffer_manager_allocate_buffer_from_common_pool(&internal_buffer, start, wait_duration_ms);
  // If buffer is to be allocated from uninitialized common pool, return error.
  if (status == SL_STATUS_FAIL) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  // If the buffer is not allocated from the common pool, allocate from the dedicated pool.
  if (status != SL_STATUS_OK && !sli_buffer_manager_has_timeout_expired(start, wait_duration_ms)) {
    status =
      sli_buffer_manager_allocate_buffer_from_dedicated_pool(&internal_buffer, pool_type, start, SLI_ZERO_TIMEOUT);
  }

  // If the buffer is not allocated from the dedicated pool, create a new common pool and allocate from it.
  if (status != SL_STATUS_OK && !sli_buffer_manager_has_timeout_expired(start, wait_duration_ms)) {
    status = sli_buffer_manager_create_new_common_mempool();
    VERIFY_STATUS_AND_RETURN(status);
    sli_buffer_manager_allocate_buffer_from_common_pool(&internal_buffer, start, wait_duration_ms);
  }

  // If the buffer is still not allocated, return error.
  if (internal_buffer == NULL) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  *buffer = internal_buffer->data;
  return SL_STATUS_OK;
}

sl_status_t sli_buffer_manager_free_buffer(sli_buffer_t buffer)
{
  if (buffer == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  CORE_irqState_t state = CORE_EnterAtomic();

  sli_internal_buffer_t *internal_buffer = NULL;
  uint8_t *temp                          = NULL;

  // Decrement the pointer to get the reference to the internal buffer.
  temp            = (uint8_t *)buffer;
  internal_buffer = (sli_internal_buffer_t *)(temp - sizeof(sli_buffer_manager_mempool_handler_t *));

  sli_buffer_manager_mempool_handler_t *mempool_handler =
    (sli_buffer_manager_mempool_handler_t *)internal_buffer->buffer_manager_mempool_handler;
  sli_mem_pool_free(&mempool_handler->mempool, internal_buffer);
  mempool_handler->allocated_buffer_count--;

  if ((mempool_handler->is_common_pool) && (mempool_handler->allocated_buffer_count == 0)
      && (common_mempool_queue.size > SLI_MINIUM_ELEMENTS_IN_COMMON_MEMPOOL_QUEUE)) {
    sli_buffer_manager_free_a_common_mempool_from_queue(mempool_handler);
  }

  CORE_ExitAtomic(state);
  return SL_STATUS_OK;
}
