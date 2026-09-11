/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "mmwlan_internal.h"
#include "umac_core.h"


#define UMAC_EVTQ_MAXLEN (25)


#define UMAC_TIMEOUTQ_MAXLEN (20)


#define UMAC_TIMEOUTQ_EXTRA_LEN (64)

struct umac_core_evtq
{
    struct umac_evt *head;
    struct umac_evt *tail;
    struct umac_evt *free;
    struct umac_evt pool[UMAC_EVTQ_MAXLEN];
};

struct umac_core_timeout
{
    struct umac_core_timeout *next;
    uint32_t timeout_abs_ms;
    umac_core_timeout_handler_t handler;
    void *arg1;
    void *arg2;
};

struct umac_core_timeoutq
{
    struct umac_core_timeout *head;
    struct umac_core_timeout *free;
    struct umac_core_timeout pool[UMAC_TIMEOUTQ_MAXLEN];

    struct umac_core_timeout *extra_pool;
};

struct umac_core_data
{
    struct umac_core_evtq evtq;
    struct umac_core_timeoutq toq;

    struct mmosal_task *evtloop_task;
    struct mmosal_semb *evtloop_semb;

    volatile bool evtloop_shutting_down;

    volatile bool evtloop_has_finished;

    mmwlan_sleep_cb_t sleep_callback;

    void *sleep_arg;
};
