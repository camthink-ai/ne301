/*
 * Copyright 2021-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "ps.h"
#include "mmhal_wlan.h"
#include "mmosal.h"
#include "driver/driver.h"
#include "driver/transport/morse_transport.h"
#include "mmwlan.h"
#include "umac/core/umac_core.h"

#ifdef ENABLE_PS_TRACE
#include "mmtrace.h"
static mmtrace_channel ps_state_channel_handle;
static mmtrace_channel ps_evt_channel_handle;
#define PS_TRACE_INIT()                                                 \
    do {                                                                \
        ps_state_channel_handle = mmtrace_register_channel("ps_state"); \
        ps_evt_channel_handle = mmtrace_register_channel("ps_evt");     \
    } while (0)
#define PS_TRACE_STATE(_fmt, ...) mmtrace_printf(ps_state_channel_handle, _fmt, ##__VA_ARGS__)
#define PS_TRACE_EVT(_fmt, ...)   mmtrace_printf(ps_evt_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define PS_TRACE_INIT() \
    do {                \
    } while (0)
#define PS_TRACE_STATE(_fmt, ...) \
    do {                          \
    } while (0)
#define PS_TRACE_EVT(_fmt, ...) \
    do {                        \
    } while (0)
#endif


#define MORSE_PS_WAKE_INDICATED_MULTIPLIER 3


#define DEFAULT_BUS_TIMEOUT_MS (5)


static struct driver_data *volatile ps_mors;

static void morse_ps_wait_after_wake_pin_raise(struct driver_data *driverd)
{
    bool hw_signals_wake = driverd->firmware_flags & MORSE_FW_FLAGS_TOGGLES_BUSY_PIN_ON_WAKE_PIN;
    uint32_t max_boot_delay_ms = driverd->cfg->get_ps_wakeup_delay_ms(driverd->chip_id);

    if (mmhal_wlan_busy_is_asserted())
    {

        return;
    }

    if (hw_signals_wake)
    {
        max_boot_delay_ms *= MORSE_PS_WAKE_INDICATED_MULTIPLIER;
    }

    bool ok = mmosal_semb_wait(driverd->ps.wake, max_boot_delay_ms);

    if (!ok && hw_signals_wake)
    {
        MMLOG_WRN("HW did not signal wake\n");
    }
}

static void morse_ps_update_timeout(struct driver_data *driverd, int timeout_ms)
{
    MMOSAL_DEV_ASSERT(mmosal_mutex_is_held_by_active_task(driverd->ps.lock));
    uint32_t new_timeout = mmosal_get_time_ms() + timeout_ms;
    if (mmosal_time_lt(driverd->ps.bus_ps_timeout, new_timeout))
    {
        PS_TRACE_EVT("bus_activity %u -> %u", driverd->ps.bus_ps_timeout, new_timeout);
        driverd->ps.bus_ps_timeout = new_timeout;
        driver_task_schedule_notification_at(driverd, DRV_EVT_PS_BUS_ACTIVITY_PEND, new_timeout);
    }
}

static void morse_ps_wakeup(struct driver_data *driverd)
{
    MMOSAL_DEV_ASSERT(mmosal_mutex_is_held_by_active_task(driverd->ps.lock));
    if (!driverd->ps.suspended)
    {
        return;
    }

    PS_TRACE_STATE("wakeup");
    MMLOG_DBG("Wakeup Pin Set\n");
    atomic_store(&driverd->ps.pending_wake, true);
    mmhal_wlan_wake_assert();
    morse_ps_wait_after_wake_pin_raise(driverd);
    morse_trns_set_irq_enabled(driverd, true);
    driverd->ps.suspended = false;
    atomic_store(&driverd->ps.pending_wake, false);


    morse_ps_update_timeout(driverd, DEFAULT_BUS_TIMEOUT_MS);
}

static void morse_ps_sleep(struct driver_data *driverd)
{
    MMOSAL_DEV_ASSERT(mmosal_mutex_is_held_by_active_task(driverd->ps.lock));
    if (driverd->ps.suspended)
    {
        return;
    }

    PS_TRACE_STATE("suspend");
    MMLOG_DBG("Wakeup Pin Clear\n");
    driverd->ps.suspended = true;
    morse_trns_set_irq_enabled(driverd, false);
    mmhal_wlan_wake_deassert();
}

static void morse_ps_irq_handle(void)
{
    if (ps_mors == NULL)
    {
        return;
    }

    PS_TRACE_EVT("ps_irq");
    if (atomic_load(&ps_mors->ps.pending_wake))
    {
        mmosal_semb_give_from_isr(ps_mors->ps.wake);
    }
    else
    {

        driver_task_notify_event_from_isr(ps_mors, DRV_EVT_PS_ASYNC_WAKEUP_PEND);
    }
}

static void morse_ps_evaluate(struct driver_data *driverd)
{
    bool needs_wake = false;

    uint32_t blocking_evt_mask = DRV_EVT_MASK_PAGESET;
    bool blocking_evt_is_pending = driver_task_notification_is_pending(driverd, blocking_evt_mask);

    if (!driverd->ps.initialized)
    {
        return;
    }

    MMOSAL_DEV_ASSERT(mmosal_mutex_is_held_by_active_task(driverd->ps.lock));
    MMOSAL_DEV_ASSERT(mmosal_task_get_active() == driverd->driver_task.task);

    const bool skbs_pending = driverd->cfg->ops->skbq_get_tx_buffered_count(driverd);
    if (!skbs_pending && driverd->stale_status.enabled)
    {

        mmosal_timer_stop(driverd->stale_status.timer);
    }
    needs_wake = (driverd->ps.wakers != 0) || blocking_evt_is_pending || skbs_pending;

    if (driver_is_data_tx_allowed(driverd) && !mmosal_time_has_passed(driverd->ps.bus_ps_timeout))
    {


        needs_wake = true;


        driver_task_schedule_notification_at(driverd,
                                             DRV_EVT_PS_BUS_ACTIVITY_PEND,
                                             driverd->ps.bus_ps_timeout);
    }

    if (needs_wake)
    {
#ifdef ENABLE_PS_TRACE
        uint32_t wake_reason =
            driverd->ps.wakers |
            (driverd->driver_task.pending_evts << 4) |
            (blocking_evt_is_pending ? 0x00010000 : 0) |
            ((driverd->cfg->ops->skbq_get_tx_buffered_count(driverd) > 0) ? 0x00020000 : 0) |
            (!mmosal_time_has_passed(driverd->ps.bus_ps_timeout) ? 0x00040000 : 0);
        PS_TRACE_EVT("eval wake %x", wake_reason);
#endif
        morse_ps_wakeup(driverd);
    }
    else if (mmhal_wlan_busy_is_asserted())
    {
        PS_TRACE_EVT("eval sleep blocked");

        morse_ps_update_timeout(driverd, DEFAULT_BUS_TIMEOUT_MS);
    }
    else
    {
        PS_TRACE_EVT("eval sleep");
        morse_ps_sleep(driverd);
    }
}

static void morse_ps_standby_exit_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(evt);

    umac_offload_standby_exit(umacd);
}

void morse_ps_network_activity(struct driver_data *driverd)
{
    MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
    morse_ps_update_timeout(driverd, driverd->ps.dynamic_ps_timout_ms);
    MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
}

int morse_ps_set_dynamic_ps_timeout(struct driver_data *driverd, uint32_t timeout_ms)
{
    int ret =
        mmdrv_set_param(UNKNOWN_VIF_ID, MORSE_PARAM_ID_DYNAMIC_PS_TIMEOUT_MS, htole32(timeout_ms));
    if (ret)
    {
        return ret;
    }


    driverd->ps.dynamic_ps_timout_ms = timeout_ms;

    MMLOG_INF("Dynamic PS timout set %lu\n", timeout_ms);

    return ret;
}

void morse_ps_work(struct driver_data *driverd)
{
    bool async_wakeup;
    bool delayed_eval;
    bool bus_activity;

    if (!driverd->ps.initialized)
    {
        return;
    }

    async_wakeup = driver_task_notification_check_and_clear(driverd, DRV_EVT_PS_ASYNC_WAKEUP_PEND);
    delayed_eval = driver_task_notification_check_and_clear(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
    bus_activity = driver_task_notification_check_and_clear(driverd, DRV_EVT_PS_BUS_ACTIVITY_PEND);

    if (async_wakeup)
    {
        PS_TRACE_EVT("async_wakeup");

        MMLOG_DBG("Aysnc wakeup Request from IRQ, waking up.\n");
        MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
        morse_ps_wakeup(driverd);
        MMOSAL_MUTEX_RELEASE(driverd->ps.lock);

        if (driverd->standby_waiting_for_wakeup)
        {
            struct umac_evt evt = UMAC_EVT_INIT(morse_ps_standby_exit_evt_handler);
            struct umac_data *umacd = umac_data_get_umacd();
            if (umac_core_evt_queue(umacd, &evt))
            {
                driverd->standby_waiting_for_wakeup = false;
            }
            else
            {
                MMLOG_WRN("Could not queue STANDBY_EXIT event!\n");
            }
        }
    }

    if (bus_activity)
    {
        if (!mmosal_time_has_passed(driverd->ps.bus_ps_timeout))
        {
            PS_TRACE_EVT("bus_activity pend %u", driverd->ps.bus_ps_timeout);
            driver_task_schedule_notification_at(driverd,
                                                 DRV_EVT_PS_BUS_ACTIVITY_PEND,
                                                 driverd->ps.bus_ps_timeout);
            bus_activity = false;
        }
    }

    if (delayed_eval || bus_activity)
    {
        if (delayed_eval && bus_activity)
        {
            PS_TRACE_EVT("delayed_eval && bus_activity");
        }
        else if (delayed_eval)
        {
            PS_TRACE_EVT("delayed_eval");
        }
        else
        {
            PS_TRACE_EVT("bus_activity");
        }

        MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
        MMLOG_VRB("Wakers: %lu\n", driverd->ps.wakers);
        morse_ps_evaluate(driverd);
        MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
    }
}

int morse_ps_enable(struct driver_data *driverd, enum ps_waker_id waker_id)
{
    if (!driverd->ps.initialized)
    {
        return 0;
    }

    PS_TRACE_EVT("enable %u", waker_id);

    MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
    driverd->ps.wakers &= ~(1ul << waker_id);
    MMLOG_VRB("Wakers: %lx\n", driverd->ps.wakers);
    morse_ps_evaluate(driverd);
    MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
    return 0;
}

int morse_ps_enable_async(struct driver_data *driverd, enum ps_waker_id waker_id)
{
    if (!driverd->ps.initialized)
    {
        return 0;
    }

    PS_TRACE_EVT("enable_async %u", waker_id);

    MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
    driverd->ps.wakers &= ~(1ul << waker_id);
    MMLOG_VRB("Wakers: %lx\n", driverd->ps.wakers);
    driver_task_notify_event(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
    MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
    return 0;
}

int morse_ps_disable(struct driver_data *driverd, enum ps_waker_id waker_id)
{
    if (!driverd->ps.initialized)
    {
        return 0;
    }

    PS_TRACE_EVT("disable %u", waker_id);

    MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
    driverd->ps.wakers |= 1ul << waker_id;
    MMLOG_VRB("Wakers: %lx\n", driverd->ps.wakers);
    morse_ps_evaluate(driverd);
    MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
    return 0;
}

int morse_ps_disable_async(struct driver_data *driverd, enum ps_waker_id waker_id)
{
    if (!driverd->ps.initialized)
    {
        return 0;
    }

    PS_TRACE_EVT("disable_async %u", waker_id);

    MMOSAL_MUTEX_GET_INF(driverd->ps.lock);
    driverd->ps.wakers |= 1ul << waker_id;
    MMLOG_VRB("Wakers: %lx\n", driverd->ps.wakers);
    driver_task_notify_event(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
    MMOSAL_MUTEX_RELEASE(driverd->ps.lock);
    return 0;
}

int morse_ps_init(struct driver_data *driverd)
{
    MMOSAL_DEV_ASSERT(driverd->ps.initialized == false);
    PS_TRACE_INIT();

    driverd->ps.bus_ps_timeout = 0;
    driverd->ps.dynamic_ps_timout_ms = MMWLAN_DEFAULT_DYNAMIC_PS_TIMEOUT_MS;
    driverd->ps.suspended = false;

    driverd->ps.wakers = (1ul << PS_WAKER_UMAC);
    driverd->ps.lock = mmosal_mutex_create("ps");
    MMOSAL_ASSERT(driverd->ps.lock);
    driverd->ps.wake = mmosal_semb_create("ps_wake");
    MMOSAL_ASSERT(driverd->ps.wake);

    ps_mors = driverd;

    driverd->ps.initialized = true;


    mmhal_wlan_wake_assert();
    PS_TRACE_STATE("init_awake");

    mmhal_wlan_register_busy_irq_handler(morse_ps_irq_handle);
    mmhal_wlan_set_busy_irq_enabled(true);

    return 0;
}

void morse_ps_deinit(struct driver_data *driverd)
{
    if (!driverd->ps.initialized)
    {
        return;
    }


    mmhal_wlan_set_busy_irq_enabled(false);
    ps_mors = NULL;

    driverd->ps.initialized = false;

    mmosal_mutex_delete(driverd->ps.lock);
    driverd->ps.lock = NULL;

    mmosal_semb_delete(driverd->ps.wake);
    driverd->ps.wake = NULL;

    mmhal_wlan_wake_deassert();
    PS_TRACE_STATE("deinit_suspend");
}
