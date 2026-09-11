/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_health_check.h"

#if !(defined(DISABLE_MMWLAN_HEALTH_CHECK) && DISABLE_MMWLAN_HEALTH_CHECK)

#include "mmdrv.h"
#include "mmlog.h"
#include "mmosal.h"
#include "umac/core/umac_core.h"
#include "umac_health_check_data.h"


#define MORSE_HEALTH_CHECK_RETRIES 1


#define UMAC_PERIODIC_HEALTH_THRESHOLD_MS             \
    (MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS +    \
     ((MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS -  \
       MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS) / \
      2))

static bool umac_health_check_should_run(const struct umac_health_check_data *data)
{
    if (!data->is_started || data->interval_ms == 0)
    {
        return false;
    }

    if (!data->check_demanded && (data->periodic_check_vetoes != 0))
    {
        return false;
    }

    return true;
}

static void umac_health_check_timeout_handler(void *arg1, void *arg2)
{
    MM_UNUSED(arg2);

    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);

    if (!umac_health_check_should_run(data))
    {
        return;
    }

    if (!data->check_demanded)
    {
        uint32_t next_check_at = data->last_activity_ms + data->interval_ms;
        if (!mmosal_time_has_passed(next_check_at))
        {
            umac_health_check_schedule_timeout(umacd, next_check_at - mmosal_get_time_ms());
            return;
        }
    }

    data->check_demanded = false;

    enum mmwlan_status status;
    int retries = 0;
    do {
        status = mmdrv_health_check();
        MMLOG_INF("Health check attempt %d returned %u\n", retries + 1, status);
    } while (status != MMWLAN_SUCCESS && retries++ < MORSE_HEALTH_CHECK_RETRIES);

    if (status == MMWLAN_NOT_RUNNING)
    {
        MMLOG_INF("Health check unavailable, likely shutdown in progress.\n");
    }
    else if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Health check failed (%u)\n", status);
        mmdrv_host_hw_restart_required();
        return;
    }

    data->last_activity_ms = mmosal_get_time_ms();

    if ((data->interval_ms != 0) && (data->periodic_check_vetoes == 0))
    {
        umac_health_check_schedule_timeout(umacd, data->interval_ms);
    }

}

static void umac_health_check_cancel_timeout(struct umac_data *umacd)
{
    (void)umac_core_cancel_timeout(umacd, umac_health_check_timeout_handler, umacd, NULL);
}

void umac_health_check_schedule_timeout(struct umac_data *umacd, uint32_t delta_ms)
{
    if (!umac_core_is_running(umacd))
    {
        return;
    }

    (void)umac_health_check_cancel_timeout(umacd);

    bool ok =
        umac_core_register_timeout(umacd, delta_ms, umac_health_check_timeout_handler, umacd, NULL);
    if (!ok)
    {
        MMLOG_WRN("Failed to register health-check timeout\n");
    }
}

static uint32_t umac_health_check_calc_interval(uint32_t min_interval_ms, uint32_t max_interval_ms)
{
    MMOSAL_DEV_ASSERT(min_interval_ms <= max_interval_ms);

    if (min_interval_ms > UMAC_PERIODIC_HEALTH_THRESHOLD_MS)
    {
        return min_interval_ms;
    }
    else if (max_interval_ms > UMAC_PERIODIC_HEALTH_THRESHOLD_MS)
    {
        return UMAC_PERIODIC_HEALTH_THRESHOLD_MS;
    }
    else
    {
        return max_interval_ms;
    }
}

void umac_health_check_init(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    data->is_started = false;
    data->interval_ms =
        umac_health_check_calc_interval(MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS,
                                        MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS);
    data->last_activity_ms = 0;
    data->check_demanded = false;
    data->periodic_check_vetoes = 0;
}

void umac_health_check_deinit(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    data->is_started = false;
    umac_health_check_cancel_timeout(umacd);
}

void umac_health_check_start(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);


    data->is_started = true;
    data->last_activity_ms = mmosal_get_time_ms();
    umac_health_check_schedule_timeout(umacd, 0);
}

void umac_health_check_stop(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    data->is_started = false;
    data->check_demanded = false;
    umac_health_check_cancel_timeout(umacd);
}

enum mmwlan_status umac_health_check_set_interval(struct umac_data *umacd,
                                                  uint32_t min_interval_ms,
                                                  uint32_t max_interval_ms)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);

    if (min_interval_ms > max_interval_ms)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    data->interval_ms = umac_health_check_calc_interval(min_interval_ms, max_interval_ms);
    return MMWLAN_SUCCESS;
}

void umac_health_check_set_veto(struct umac_data *umacd, enum umac_health_check_veto_id veto_id)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    MMOSAL_ASSERT(veto_id < 32);

    data->periodic_check_vetoes |= (1ul << veto_id);
}

void umac_health_check_unset_veto(struct umac_data *umacd, enum umac_health_check_veto_id veto_id)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    MMOSAL_ASSERT(veto_id < 32);

    data->periodic_check_vetoes &= ~(1ul << veto_id);
    umac_health_check_schedule_timeout(umacd, 0);
}

void umac_health_check_note_activity(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    data->last_activity_ms = mmosal_get_time_ms();
}

void umac_health_check_demand_check(struct umac_data *umacd)
{
    struct umac_health_check_data *data = umac_data_get_health_check(umacd);
    data->check_demanded = true;
    umac_health_check_schedule_timeout(umacd, 0);
}

#endif
