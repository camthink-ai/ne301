/***************************************************************************/ /**
 * @file sli_cmsis_os2_thread_flags.c
 * @brief osThreadFlags* implementation for cmsis_rtos_threadx, which does not
 *        provide thread flags (see its README: "Not yet implemented").
 *
 * WiseConnect 4.1.x command/event engines use osThreadFlagsSet/Wait for
 * in-thread synchronisation. cmsis_rtos_threadx (ST's ThreadX CMSIS-RTOS2
 * layer) never implemented the thread-flags family, so we emulate it with a
 * per-thread osEventFlagsId_t lookup table. osEventFlags* IS implemented by
 * cmsis_rtos_threadx.
 *
 * The lookup table is guarded by a PRIMASK critical section so osThreadFlagsSet
 * is safe from ISR context (CMSIS permits it). Callers in the SDK are thread
 * context in practice.
 ******************************************************************************/
#include "cmsis_os2.h"
#include "cmsis_gcc.h"

#ifndef SLI_THREAD_FLAGS_MAX_THREADS
#define SLI_THREAD_FLAGS_MAX_THREADS 16
#endif

typedef struct {
  osThreadId_t      thread_id;
  osEventFlagsId_t  flags_id;
} sli_thread_flags_entry_t;

static sli_thread_flags_entry_t s_thread_flags_table[SLI_THREAD_FLAGS_MAX_THREADS];

/* Return the event-flags object bound to a thread, creating it on first use. */
static osEventFlagsId_t sli_thread_flags_lookup(osThreadId_t thread_id)
{
  osEventFlagsId_t flags_id = NULL;
  uint32_t primask;
  uint32_t i, free_slot = SLI_THREAD_FLAGS_MAX_THREADS;

  if (thread_id == NULL) {
    return NULL;
  }

  primask = __get_PRIMASK();
  __disable_irq();

  for (i = 0; i < SLI_THREAD_FLAGS_MAX_THREADS; i++) {
    if (s_thread_flags_table[i].thread_id == thread_id) {
      flags_id = s_thread_flags_table[i].flags_id;
      break;
    }
    if (s_thread_flags_table[i].thread_id == NULL && free_slot == SLI_THREAD_FLAGS_MAX_THREADS) {
      free_slot = i;
    }
  }

  if (flags_id == NULL && free_slot < SLI_THREAD_FLAGS_MAX_THREADS) {
    /* IRQs are off here; osEventFlagsNew must not rely on a higher-priority
     * IRQ. cmsis_rtos_threadx allocates from a static pool, so this is safe. */
    flags_id = osEventFlagsNew(NULL);
    if (flags_id != NULL) {
      s_thread_flags_table[free_slot].thread_id = thread_id;
      s_thread_flags_table[free_slot].flags_id  = flags_id;
    }
  }

  __set_PRIMASK(primask);
  return flags_id;
}

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
{
  osEventFlagsId_t flags_id = sli_thread_flags_lookup(thread_id);
  if (flags_id == NULL) {
    return (uint32_t)osErrorParameter;
  }
  return osEventFlagsSet(flags_id, flags);
}

uint32_t osThreadFlagsClear(uint32_t flags)
{
  osEventFlagsId_t flags_id = sli_thread_flags_lookup(osThreadGetId());
  if (flags_id == NULL) {
    return (uint32_t)osErrorParameter;
  }
  return osEventFlagsClear(flags_id, flags);
}

uint32_t osThreadFlagsGet(void)
{
  osEventFlagsId_t flags_id = sli_thread_flags_lookup(osThreadGetId());
  if (flags_id == NULL) {
    return (uint32_t)osErrorParameter;
  }
  return osEventFlagsGet(flags_id);
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
  osEventFlagsId_t flags_id = sli_thread_flags_lookup(osThreadGetId());
  if (flags_id == NULL) {
    return (uint32_t)osErrorParameter;
  }
  return osEventFlagsWait(flags_id, flags, options, timeout);
}
