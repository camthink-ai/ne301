/*
 * Copyright 2021-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "driver/morse_driver/morse.h"


int driver_task_start(struct driver_data *driverd);


void driver_task_stop(struct driver_data *driverd);


void driver_task_notify_event(struct driver_data *driverd, enum driver_task_event evt);


void driver_task_notify_event_from_isr(struct driver_data *driverd, enum driver_task_event evt);


bool driver_task_notification_is_pending(struct driver_data *driverd, uint32_t mask);


bool driver_task_notification_check_and_clear(struct driver_data *driverd,
                                              enum driver_task_event evt);


bool driver_task_notification_check(struct driver_data *driverd, enum driver_task_event evt);


void driver_task_schedule_notification_at(struct driver_data *driverd,
                                          enum driver_task_event evt,
                                          uint32_t timeout_at_ms);


static inline void driver_task_schedule_notification(struct driver_data *driverd,
                                                     enum driver_task_event evt,
                                                     uint32_t timeout_ms)
{
    driver_task_schedule_notification_at(driverd, evt, mmosal_get_time_ms() + timeout_ms);
}


static inline bool driver_is_data_tx_allowed(struct driver_data *driverd)
{
    return !atomic_test_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED,
                            (volatile atomic_ulong *)&driverd->state_flags) &&
           !driver_task_notification_check(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND);
}


