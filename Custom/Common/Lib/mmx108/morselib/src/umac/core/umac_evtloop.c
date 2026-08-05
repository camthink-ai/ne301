/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_core_private.h"
#include "mmlog.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/stats/umac_stats.h"

static void evtloop_dispatch(struct umac_data *umacd,
                             struct umac_core_data *core,
                             struct umac_evt *evt)
{
    evt->handler(umacd, evt);

    umac_evt_free(&core->evtq, evt);
}


static uint32_t evtloop_iteration(struct umac_data *umacd, struct umac_core_data *core)
{
    uint32_t num_timeouts_fired;
    unsigned ii;


    bool datapath_pending = umac_datapath_process(umacd);


    for (ii = 0; ii < MAX_EVTS_DISPATCHED_AT_ONCE; ii++)
    {
        struct umac_evt *evt = umac_evt_dequeue(&(core->evtq));
        if (evt == NULL)
        {
            break;
        }

        evtloop_dispatch(umacd, core, evt);
    }

    num_timeouts_fired = umac_timeoutq_dispatch(core);
    umac_stats_increment_timeouts_fired(umacd, num_timeouts_fired);

    if (datapath_pending || !umac_evtq_is_empty(&(core->evtq)))
    {

        return 0;
    }
    else
    {
        return umac_timeoutq_time_to_next_timeout(core);
    }
}

#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
static void invoke_sleep_callback(struct umac_core_data *core,
                                  enum mmwlan_sleep_state sleep_state,
                                  uint32_t next_timeout_ms)
{
    if (core->sleep_callback != NULL)
    {
        core->sleep_callback(sleep_state, next_timeout_ms, core->sleep_arg);
    }
}

static void evtloop_main(void *arg)
{
    struct umac_data *umacd = (struct umac_data *)arg;
    struct umac_core_data *core = umac_data_get_core(umacd);

    invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
    while (!core->evtloop_shutting_down)
    {
        uint32_t next_timeout = evtloop_iteration(umacd, core);
        if (next_timeout != 0)
        {
            invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_IDLE, next_timeout);
            mmosal_semb_wait(core->evtloop_semb, next_timeout);
            invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
        }
    }

    mmosal_semb_delete(core->evtloop_semb);
    core->evtloop_semb = NULL;
    core->evtloop_has_finished = true;
}

#endif



enum mmwlan_status umac_core_start(struct umac_data *umacd)
{
#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
    struct umac_core_data *core = umac_data_get_core(umacd);
    if (core->evtloop_task == NULL)
    {
        core->evtloop_shutting_down = false;
        core->evtloop_has_finished = false;

        core->evtloop_semb = mmosal_semb_create("evtlp");
        if (core->evtloop_semb == NULL)
        {
            return MMWLAN_NO_MEM;
        }


        core->evtloop_task =
            mmosal_task_create(evtloop_main, umacd, MMOSAL_TASK_PRI_HIGH, 2152, "evtloop");
        if (core->evtloop_task == NULL)
        {
            mmosal_semb_delete(core->evtloop_semb);
            core->evtloop_semb = NULL;
            return MMWLAN_NO_MEM;
        }
    }
#else
    MM_UNUSED(umacd);
#endif

    return MMWLAN_SUCCESS;
}

void umac_core_stop(struct umac_data *umacd)
{
#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
    struct umac_core_data *core = umac_data_get_core(umacd);
    if (!core->evtloop_shutting_down)
    {

        core->evtloop_shutting_down = true;
        if (core->evtloop_task != NULL)
        {
            MMLOG_DBG("Waiting for event loop to finish\n");
            invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
            mmosal_semb_give(core->evtloop_semb);
            while (!core->evtloop_has_finished)
            {
                mmosal_task_sleep(1);
            }
            core->evtloop_task = NULL;
        }
        MMLOG_INF("Shut down event loop\n");
    }
#else
    MM_UNUSED(umacd);
#endif
}

bool umac_core_is_running(struct umac_data *umacd)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    return core->evtloop_task != NULL;
}

#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
bool umac_core_evtloop_is_active(struct umac_data *umacd)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    return core->evtloop_task == mmosal_task_get_active();
}

#endif

uint32_t umac_core_dispatch_events(struct umac_data *umacd)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    return evtloop_iteration(umacd, core);
}

bool umac_core_evt_queue(struct umac_data *umacd, const struct umac_evt *evt)
{
    bool ret;
    struct umac_core_data *core = umac_data_get_core(umacd);


    if (core->evtloop_shutting_down || core->evtloop_task == NULL)
    {
        return false;
    }

    ret = umac_evt_queue(&(core->evtq), evt, EVTQ_TAIL);
#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
    invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
    mmosal_semb_give(core->evtloop_semb);
#endif

    return ret;
}

bool umac_core_evt_queue_at_start(struct umac_data *umacd, const struct umac_evt *evt)
{
    bool ret;
    struct umac_core_data *core = umac_data_get_core(umacd);


    if (core->evtloop_shutting_down || core->evtloop_task == NULL)
    {
        return false;
    }

    ret = umac_evt_queue(&(core->evtq), evt, EVTQ_HEAD);
#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
    invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
    mmosal_semb_give(core->evtloop_semb);
#endif

    return ret;
}

void umac_core_evt_wake(struct umac_data *umacd)
{
#if !(defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP)
    struct umac_core_data *core = umac_data_get_core(umacd);


    if (core->evtloop_shutting_down || core->evtloop_task == NULL)
    {
        return;
    }

    invoke_sleep_callback(core, MMWLAN_SLEEP_STATE_BUSY, 0);
    mmosal_semb_give(core->evtloop_semb);
#else
    MM_UNUSED(umacd);
#endif
}
