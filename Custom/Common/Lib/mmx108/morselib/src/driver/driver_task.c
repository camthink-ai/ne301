/*
 * Copyright 2023-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include "mmosal.h"
#include "driver.h"
#include "driver/morse_driver/morse.h"
#include "driver/morse_driver/ps.h"
#include "driver/beacon/beacon.h"

#ifdef ENABLE_DRV_TASK_TRACE
#include "mmtrace.h"
static mmtrace_channel drv_channel_handle;
#define DRV_TASK_TRACE_INIT()     drv_channel_handle = mmtrace_register_channel("drv")
#define DRV_TASK_TRACE(_fmt, ...) mmtrace_printf(drv_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define DRV_TASK_TRACE_INIT() \
    do {                      \
    } while (0)
#define DRV_TASK_TRACE(_fmt, ...) \
    do {                          \
    } while (0)
#endif

#define DRV_TASK_PRIO             MMOSAL_TASK_PRI_HIGH
#define DRV_TASK_STACK_SIZE_WORDS 768

#define DRV_TASK_MAX_SLEEP_MS (INT32_MAX / 2)

static bool get_next_scheduled_evt_time(struct driver_data *driverd, uint32_t *next_evt_time)
{
    uint32_t ii;
    bool found = false;

    MMOSAL_TASK_ENTER_CRITICAL();
    for (ii = 0; ii < MAX_SCHEDULED_EVTS; ii++)
    {
        if (driverd->driver_task.scheduled_evts[ii].evt != DRV_EVT_NONE)
        {
            if (!found)
            {
                found = true;
                *next_evt_time = driverd->driver_task.scheduled_evts[ii].timeout_at_ms;
            }
            else if (mmosal_time_lt(driverd->driver_task.scheduled_evts[ii].timeout_at_ms,
                                    *next_evt_time))
            {
                *next_evt_time = driverd->driver_task.scheduled_evts[ii].timeout_at_ms;
            }
        }
    }
    MMOSAL_TASK_EXIT_CRITICAL();
    return found;
}

void driver_task_process_scheduled_evts(struct driver_data *driverd)
{
    uint32_t ii;
    MMOSAL_TASK_ENTER_CRITICAL();
    for (ii = 0; ii < MAX_SCHEDULED_EVTS; ii++)
    {
        if (driverd->driver_task.scheduled_evts[ii].evt != DRV_EVT_NONE)
        {
            if (mmosal_time_has_passed(driverd->driver_task.scheduled_evts[ii].timeout_at_ms))
            {
                uint32_t mask = 1ul << driverd->driver_task.scheduled_evts[ii].evt;
                DRV_TASK_TRACE("schd evt ready %u", driverd->driver_task.scheduled_evts[ii].evt);
                atomic_fetch_or(&driverd->driver_task.pending_evts, mask);
                driverd->driver_task.scheduled_evts[ii].evt = DRV_EVT_NONE;
            }
            else if (driver_task_notification_check(driverd,
                                                    driverd->driver_task.scheduled_evts[ii].evt))
            {
                DRV_TASK_TRACE("schd evt already pending %u",
                               driverd->driver_task.scheduled_evts[ii].evt);


                driverd->driver_task.scheduled_evts[ii].evt = DRV_EVT_NONE;
            }
        }
    }
    MMOSAL_TASK_EXIT_CRITICAL();
}

void driver_task_schedule_notification_at(struct driver_data *driverd,
                                          enum driver_task_event evt,
                                          uint32_t timeout_at_ms)
{
    uint32_t ii;

    MMOSAL_TASK_ENTER_CRITICAL();

    for (ii = 0; ii < MAX_SCHEDULED_EVTS; ii++)
    {
        if (driverd->driver_task.scheduled_evts[ii].evt == evt)
        {
            driverd->driver_task.scheduled_evts[ii].timeout_at_ms = timeout_at_ms;


            mmosal_semb_give(driverd->driver_task.pending_semb);

            DRV_TASK_TRACE("reschd %u", evt);
            goto exit;
        }
    }

    for (ii = 0; ii < MAX_SCHEDULED_EVTS; ii++)
    {
        if (driverd->driver_task.scheduled_evts[ii].evt == DRV_EVT_NONE)
        {
            driverd->driver_task.scheduled_evts[ii].evt = evt;
            driverd->driver_task.scheduled_evts[ii].timeout_at_ms = timeout_at_ms;


            mmosal_semb_give(driverd->driver_task.pending_semb);

            DRV_TASK_TRACE("schd %u", evt);
            goto exit;
        }
    }


    MMOSAL_ASSERT(false);

exit:
    MMOSAL_TASK_EXIT_CRITICAL();
}

void driver_task_main(void *arg)
{
    struct driver_data *driverd = (struct driver_data *)arg;
    bool shutting_down = false;

    while (true)
    {
        bool have_scheduled_evt;
        int32_t relative_next_evt_time;
        uint32_t next_scheduled_evt_time = 0;

        driver_task_process_scheduled_evts(driverd);

        while (driverd->driver_task.pending_evts != 0)
        {
            MMLOG_DBG("Pending evts: %08lx\n", driverd->driver_task.pending_evts);
            DRV_TASK_TRACE("Pending evts: %x", driverd->driver_task.pending_evts);
            if (driver_task_notification_check(driverd, DRV_EVT_SHUTDOWN))
            {
                MMLOG_INF("Driver task shutting down\n");
                shutting_down = true;
                break;
            }

            morse_ps_work(driverd);
            morse_beacon_work(driverd);
            driverd->cfg->ops->chip_if_work(driverd);
            driverd->cfg->ops->tx_stale_work(driverd);
        }

        if (shutting_down)
        {
            break;
        }

        MMLOG_DBG("No more events...\n");

        relative_next_evt_time = DRV_TASK_MAX_SLEEP_MS;
        have_scheduled_evt = get_next_scheduled_evt_time(driverd, &next_scheduled_evt_time);
        if (have_scheduled_evt)
        {
            relative_next_evt_time = (int32_t)(next_scheduled_evt_time - mmosal_get_time_ms());
        }

        if (relative_next_evt_time > 0)
        {
            mmosal_semb_wait(driverd->driver_task.pending_semb, relative_next_evt_time);
        }
    }

    driverd->driver_task.task = NULL;
    driverd->driver_task.task_running = false;
}

int driver_task_start(struct driver_data *driverd)
{
    DRV_TASK_TRACE_INIT();

    driverd->driver_task.pending_semb = mmosal_semb_create("drvsm");
    if (driverd->driver_task.pending_semb == NULL)
    {
        return -ENOMEM;
    }

    MMLOG_DBG("Starting driver task. Pending events %08lx\n", driverd->driver_task.pending_evts);

    driverd->driver_task.task_running = true;

    driverd->driver_task.task = mmosal_task_create(driver_task_main,
                                                   driverd,
                                                   DRV_TASK_PRIO,
                                                   DRV_TASK_STACK_SIZE_WORDS,
                                                   "drv");
    if (driverd->driver_task.task == NULL)
    {
        mmosal_semb_delete(driverd->driver_task.pending_semb);

        driverd->driver_task.task_running = false;
        return -ENOMEM;
    }

    return 0;
}

void driver_task_stop(struct driver_data *driverd)
{
    if (!driverd->driver_task.task_running)
    {
        return;
    }

    MMLOG_INF("Stop driver task...\n");

    driver_task_notify_event(driverd, DRV_EVT_SHUTDOWN);

    while (driverd->driver_task.task_running)
    {
        mmosal_task_sleep(1);
    }

    mmosal_semb_delete(driverd->driver_task.pending_semb);
    driverd->driver_task.pending_semb = NULL;

    MMLOG_INF("Driver task stopped\n");
}

void driver_task_notify_event(struct driver_data *driverd, enum driver_task_event evt)
{
    DRV_TASK_TRACE("Notify %d", evt);
    MMLOG_DBG("Notify: %d (%08lx)\n", evt, 1ul << evt);
    atomic_fetch_or(&driverd->driver_task.pending_evts, 1ul << evt);
    if (driverd->driver_task.task != NULL)
    {
        mmosal_semb_give(driverd->driver_task.pending_semb);
    }
}

void driver_task_notify_event_from_isr(struct driver_data *driverd, enum driver_task_event evt)
{
    DRV_TASK_TRACE("Notify ISR %d", evt);
    if (driverd->driver_task.task != NULL)
    {
        atomic_fetch_or(&driverd->driver_task.pending_evts, 1ul << evt);
        mmosal_semb_give_from_isr(driverd->driver_task.pending_semb);
    }
}

bool driver_task_notification_is_pending(struct driver_data *driverd, uint32_t mask)
{
    return (driverd->driver_task.pending_evts & mask) != 0;
}

bool driver_task_notification_check_and_clear(struct driver_data *driverd,
                                              enum driver_task_event evt)
{
    uint_least32_t mask = (1ul << evt);
    DRV_TASK_TRACE("Test+clear %d", evt);
    MMLOG_VRB("Test clear %d mask=%08lx pending=%08lx masked=%08lx\n",
              evt,
              mask,
              driverd->driver_task.pending_evts,
              driverd->driver_task.pending_evts & mask);
    return (atomic_fetch_and(&driverd->driver_task.pending_evts, ~mask) & mask) != 0;
}

bool driver_task_notification_check(struct driver_data *driverd, enum driver_task_event evt)
{
    uint_least32_t mask = (1ul << evt);
    DRV_TASK_TRACE("Test %d", evt);
    MMLOG_VRB("Test %d mask=%08lx pending=%08lx masked=%08lx\n",
              evt,
              mask,
              driverd->driver_task.pending_evts,
              driverd->driver_task.pending_evts & mask);
    return (driverd->driver_task.pending_evts & mask) != 0;
}
