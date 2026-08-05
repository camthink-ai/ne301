/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>

#include "common/morse_error.h"
#include "mmdrv.h"
#include "driver/morse_driver/command.h"
#include "stdatomic.h"
#include "driver_health.h"

#ifdef ENABLE_DRV_HEALTH_TRACE
#include "mmtrace.h"
static mmtrace_channel drv_health_channel_handle;
#define DRV_HEALTH_TRACE_DECLARE    mmtrace_channel drv_health_channel_handle;
#define DRV_HEALTH_TRACE_INIT()     drv_health_channel_handle = mmtrace_register_channel("drv_health")
#define DRV_HEALTH_TRACE(_fmt, ...) mmtrace_printf(drv_health_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define DRV_HEALTH_TRACE_INIT() \
    do {                        \
    } while (0)
#define DRV_HEALTH_TRACE(_fmt, ...) \
    do {                            \
    } while (0)
#define DRV_HEALTH_TRACE_DECLARE
#endif

#define HEALTH_CHECK_TASK_PRIO MMOSAL_TASK_PRI_LOW

#define HEALTH_CHECK_TASK_STACK_SIZE_WORDS 400


#define DRIVER_HEALTH_SHUTDOWN_TIMEOUT_MS 3000


#define MORSE_HEALTH_CHECK_RETRIES 1


static void morse_reset_chip(void)
{
    mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_HW_RESTART, true);

    MMLOG_INF("Triggering hw restart...\n");
    DRV_HEALTH_TRACE("hw restart");

    mmdrv_host_hw_restart_required();
}


static inline bool should_skip(const bool semb_taken, const struct driver_data *driverd)
{
    if (driverd->health_check.check_demanded)
    {
        return false;
    }


    if (atomic_load(&driverd->health_check.periodic_check_vetoes) != 0)
    {

        return true;
    }

    if (!semb_taken && !mmosal_time_has_passed(
                           driverd->health_check.last_checked + driverd->health_check.interval_ms))
    {

        return true;
    }

    return false;
}


static void driver_health_task_main(void *arg)
{
    struct driver_data *driverd = (struct driver_data *)arg;
    int ret;
    uint32_t next_interval_ms;

    while (driverd->health_check.task_enabled)
    {
        if (driverd->health_check.interval_ms == 0)
        {
            next_interval_ms = UINT32_MAX;
        }
        else if (atomic_load(&driverd->health_check.periodic_check_vetoes) != 0)
        {
            next_interval_ms = UINT32_MAX;
        }
        else if (driverd->health_check.last_checked == 0)
        {
            next_interval_ms = driverd->health_check.interval_ms;
        }
        else
        {

            next_interval_ms = driverd->health_check.interval_ms -
                               mmosal_get_time_ms() +
                               driverd->health_check.last_checked;
        }

        MMLOG_VRB("Periodic health check in %lu ms.\n", next_interval_ms);
        bool semb_taken = mmosal_semb_wait(driverd->health_check.pending_semb, next_interval_ms);
        if (!driverd->health_check.task_enabled)
        {
            break;
        }

        if (should_skip(semb_taken, driverd))
        {
            continue;
        }

        driverd->health_check.check_demanded = false;


        int retries = 0;
        do {
            ret = morse_cmd_health_check(driverd);
            MMLOG_INF("Health check attempt %d returned %d\n", retries + 1, ret);
        } while (ret && retries++ < MORSE_HEALTH_CHECK_RETRIES);

        driverd->health_check.last_checked = mmosal_get_time_ms();
        if (ret == -ESTALE)
        {

            MMLOG_INF("Failed with -ESTALE. Likely due to shutdown in progress.\n");
        }
        else if (ret)
        {
            MMLOG_ERR("Health check failed (errno=%d)\n", ret);
            DRV_HEALTH_TRACE("health fail");


            morse_reset_chip();
        }
        else
        {
            MMLOG_VRB("Health Check COMPLETE...\n");
            DRV_HEALTH_TRACE("health pass");
        }
    }

    driverd->health_check.task_running = false;
}

int driver_health_init(struct driver_data *driverd)
{
    DRV_HEALTH_TRACE_INIT();

    DRV_HEALTH_TRACE("init");

    driverd->health_check.pending_semb = mmosal_semb_create("hcsem");
    if (driverd->health_check.pending_semb == NULL)
    {
        return -ENOMEM;
    }

    driverd->health_check.task_enabled = true;
    driverd->health_check.task_running = true;
    atomic_store(&driverd->health_check.periodic_check_vetoes, 0);
    driverd->health_check.check_demanded = false;

    driverd->health_check.task = mmosal_task_create(driver_health_task_main,
                                                    driverd,
                                                    HEALTH_CHECK_TASK_PRIO,
                                                    HEALTH_CHECK_TASK_STACK_SIZE_WORDS,
                                                    "health");
    if (driverd->health_check.task == NULL)
    {
        driverd->health_check.task_running = false;
        mmosal_semb_delete(driverd->health_check.pending_semb);
        driverd->health_check.pending_semb = NULL;
        return -ENOMEM;
    }

    return MORSE_SUCCESS;
}

void driver_health_deinit(struct driver_data *driverd)
{
    DRV_HEALTH_TRACE("deinit");


    if (driverd->health_check.task != NULL &&
        mmosal_task_get_active() != driverd->health_check.task)
    {
        uint32_t shutdown_timeout_ms = mmosal_get_time_ms() + DRIVER_HEALTH_SHUTDOWN_TIMEOUT_MS;
        driverd->health_check.task_enabled = false;
        mmosal_semb_give(driverd->health_check.pending_semb);

        while (driverd->health_check.task_running)
        {
            mmosal_task_sleep(1);
            MMOSAL_ASSERT(!mmosal_time_has_passed(shutdown_timeout_ms));
        }

        driverd->health_check.task = NULL;
        mmosal_semb_delete(driverd->health_check.pending_semb);
        driverd->health_check.pending_semb = NULL;
    }
}

void driver_health_demand_check(struct driver_data *driverd)
{

    driverd->health_check.check_demanded = true;

    driver_health_request_check(driverd);
}

void driver_health_request_check(struct driver_data *driverd)
{
    if (!driverd->started)
    {
        return;
    }
    if (driverd->health_check.pending_semb != NULL)
    {

        driverd->health_check.last_checked = 0;
        mmosal_semb_give(driverd->health_check.pending_semb);
    }
}
