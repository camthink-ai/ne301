/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_core_private.h"
#include "mmlog.h"

void umac_timeoutq_init(struct umac_core_timeoutq *toq)
{
    unsigned ii;
    for (ii = 0; ii < UMAC_TIMEOUTQ_MAXLEN; ii++)
    {
        toq->pool[ii].next = toq->free;
        toq->free = &(toq->pool[ii]);
    }
}

enum mmwlan_status umac_timeoutq_alloc_extra(struct umac_core_timeoutq *toq)
{
    if (toq->extra_pool != NULL)
    {
        return MMWLAN_SUCCESS;
    }


    struct umac_core_timeout *extra_pool =
        (struct umac_core_timeout *)mmosal_calloc(UMAC_TIMEOUTQ_EXTRA_LEN,
                                                  sizeof(toq->extra_pool[0]));
    if (extra_pool == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    struct umac_core_timeout *head = &extra_pool[0];
    struct umac_core_timeout *tail = &extra_pool[0];
    for (size_t ii = 1; ii < UMAC_TIMEOUTQ_EXTRA_LEN; ++ii)
    {
        extra_pool[ii].next = head;
        head = &(extra_pool[ii]);
    }
    MMOSAL_TASK_ENTER_CRITICAL();
    toq->extra_pool = extra_pool;
    tail->next = toq->free;
    toq->free = head;
    MMOSAL_TASK_EXIT_CRITICAL();

    MMLOG_DBG("Added %u core timeouts\n", UMAC_TIMEOUTQ_EXTRA_LEN);
    return MMWLAN_SUCCESS;
}

void umac_timeoutq_deinit(struct umac_core_timeoutq *toq)
{

    mmosal_free(toq->extra_pool);
    toq->extra_pool = NULL;
}

static struct umac_core_timeout *umac_timeoutq_dequeue_protected(struct umac_core_timeoutq *toq,
                                                                 uint32_t time_ms)
{
    struct umac_core_timeout *to = toq->head;

    if (to == NULL)
    {
        return NULL;
    }

    if (mmosal_time_lt(time_ms, to->timeout_abs_ms))
    {
        return NULL;
    }

    toq->head = to->next;
    return to;
}

static struct umac_core_timeout *umac_timeoutq_dequeue(struct umac_core_timeoutq *toq,
                                                       uint32_t time_ms)
{
    struct umac_core_timeout *to;

    MMOSAL_TASK_ENTER_CRITICAL();
    to = umac_timeoutq_dequeue_protected(toq, time_ms);
    MMOSAL_TASK_EXIT_CRITICAL();
    return to;
}

static struct umac_core_timeout *umac_timeoutq_alloc_protected(struct umac_core_timeoutq *toq)
{
    struct umac_core_timeout *to = toq->free;

    if (to == NULL)
    {
        return NULL;
    }

    toq->free = to->next;
    return to;
}

static struct umac_core_timeout *umac_timeoutq_alloc(struct umac_core_timeoutq *toq)
{
    struct umac_core_timeout *to;

    MMOSAL_TASK_ENTER_CRITICAL();
    to = umac_timeoutq_alloc_protected(toq);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (to != NULL)
    {
        to->next = NULL;
    }
    return to;
}

static void umac_timeoutq_free_protected(struct umac_core_timeoutq *toq,
                                         struct umac_core_timeout *to)
{
    to->next = toq->free;
    toq->free = to;
}

static void umac_timeoutq_free(struct umac_core_timeoutq *toq, struct umac_core_timeout *to)
{
    memset(to, 0, sizeof(*to));
    MMOSAL_TASK_ENTER_CRITICAL();
    umac_timeoutq_free_protected(toq, to);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static void umac_timeoutq_enqueue_protected(struct umac_core_timeoutq *toq,
                                            struct umac_core_timeout *to)
{
    struct umac_core_timeout *walk;
    struct umac_core_timeout *next;

    if (toq->head == NULL)
    {
        toq->head = to;
        return;
    }




    if (mmosal_time_lt(to->timeout_abs_ms, toq->head->timeout_abs_ms))
    {
        to->next = toq->head;
        toq->head = to;
        return;
    }

    walk = toq->head;
    next = walk->next;
    while (next != NULL)
    {
        if (mmosal_time_le(walk->timeout_abs_ms, to->timeout_abs_ms) &&
            mmosal_time_lt(to->timeout_abs_ms, next->timeout_abs_ms))
        {
            walk->next = to;
            to->next = next;
            return;
        }

        walk = next;
        next = walk->next;
    }

    walk->next = to;
}

static void umac_timeoutq_enqueue(struct umac_core_timeoutq *toq, struct umac_core_timeout *to)
{
    to->next = NULL;

    MMOSAL_TASK_ENTER_CRITICAL();
    umac_timeoutq_enqueue_protected(toq, to);
    MMOSAL_TASK_EXIT_CRITICAL();

    MMLOG_VRB("TO + %08lx: h=%08lx arg1=%08lx arg2=%08lx\n",
              (uint32_t)to,
              (uint32_t)to->handler,
              (uint32_t)to->arg1,
              (uint32_t)to->arg2);
}

static struct umac_core_timeout *umac_timeoutq_remove_one_protected(
    struct umac_core_timeoutq *toq,
    umac_core_timeout_handler_t handler,
    void *arg1,
    void *arg2)
{
    struct umac_core_timeout *walk = toq->head;
    struct umac_core_timeout *prev;

    if (walk == NULL)
    {
        return NULL;
    }


    if (walk->handler == handler && walk->arg1 == arg1 && walk->arg2 == arg2)
    {
        toq->head = walk->next;
        return walk;
    }


    prev = walk;
    walk = walk->next;


    while (walk != NULL)
    {
        if (walk->handler == handler && walk->arg1 == arg1 && walk->arg2 == arg2)
        {
            prev->next = walk->next;
            return walk;
        }
        else
        {
            prev = walk;
            walk = walk->next;
        }
    }

    return NULL;
}

static bool umac_timeoutq_peek_next_timeout_protected(struct umac_core_timeoutq *toq,
                                                      uint32_t *next_timeout_time)
{
    if (toq->head == NULL)
    {
        return false;
    }

    *next_timeout_time = toq->head->timeout_abs_ms;
    return true;
}

static bool umac_timeoutq_peek_next_timeout(struct umac_core_timeoutq *toq,
                                            uint32_t *next_timeout_time)
{
    bool next_timeout_valid;
    MMOSAL_TASK_ENTER_CRITICAL();
    next_timeout_valid = umac_timeoutq_peek_next_timeout_protected(toq, next_timeout_time);
    MMOSAL_TASK_EXIT_CRITICAL();
    return next_timeout_valid;
}

static void umac_timeoutq_dispatch_timeout(struct umac_core_data *core,
                                           struct umac_core_timeout *to)
{
    umac_core_timeout_handler_t handler = to->handler;
    void *arg1 = to->arg1;
    void *arg2 = to->arg2;


    umac_timeoutq_free(&(core->toq), to);

    MMOSAL_ASSERT(handler != NULL);
    handler(arg1, arg2);
}

uint32_t umac_timeoutq_dispatch(struct umac_core_data *core)
{
    unsigned ii;
    uint32_t time_ms = mmosal_get_time_ms();
    uint32_t num_timeouts_fired = 0;

    for (ii = 0; ii < MAX_TIMEOUTS_DISPATCHED_AT_ONCE; ii++)
    {
        struct umac_core_timeout *to = umac_timeoutq_dequeue(&(core->toq), time_ms);
        if (to == NULL)
        {
            break;
        }

        MMLOG_VRB("TO X %08lx: h=%08lx arg1=%08lx arg2=%08lx\n",
                  (uint32_t)to,
                  (uint32_t)to->handler,
                  (uint32_t)to->arg1,
                  (uint32_t)to->arg2);
        MMLOG_VRB("HEAD: %08lx\n", (uint32_t)core->toq.head);
        umac_timeoutq_dispatch_timeout(core, to);
        num_timeouts_fired++;
    }

    return num_timeouts_fired;
}

uint32_t umac_timeoutq_time_to_next_timeout(struct umac_core_data *core)
{
    uint32_t next_timeout_time;
    bool next_timeout_valid;

    next_timeout_valid = umac_timeoutq_peek_next_timeout(&(core->toq), &next_timeout_time);
    if (next_timeout_valid)
    {

        uint32_t now = mmosal_get_time_ms();
        int32_t delta = next_timeout_time - now;
        if (delta < 0)
        {
            delta = 0;
        }
        MMLOG_VRB("Dispatch: next TO abs=%lu, now=%lu, next TO delta=%ld\n",
                  next_timeout_time,
                  now,
                  delta);
        return (uint32_t)delta;
    }
    else
    {
        return UINT32_MAX;
    }
}

static int umac_timeoutq_cancel_protected(struct umac_core_timeoutq *toq,
                                          umac_core_timeout_handler_t handler,
                                          void *arg1,
                                          void *arg2)
{
    struct umac_core_timeout *walk = toq->head;
    struct umac_core_timeout *prev;
    int count = 0;


    while (walk != NULL && walk->handler == handler && walk->arg1 == arg1 && walk->arg2 == arg2)
    {
        toq->head = walk->next;
        umac_timeoutq_free_protected(toq, walk);
        walk = toq->head;
        count++;
    }

    if (walk == NULL)
    {
        return count;
    }


    prev = walk;
    walk = walk->next;


    while (walk != NULL)
    {
        if (walk->handler == handler && walk->arg1 == arg1 && walk->arg2 == arg2)
        {
            prev->next = walk->next;

            umac_timeoutq_free_protected(toq, walk);
            walk = prev->next;
            count++;
        }
        else
        {
            prev = walk;
            walk = walk->next;
        }
    }

    return count;
}

static bool umac_timeoutq_is_timeout_registered_protected(struct umac_core_timeoutq *toq,
                                                          umac_core_timeout_handler_t handler,
                                                          void *arg1,
                                                          void *arg2)
{
    struct umac_core_timeout *walk;
    for (walk = toq->head; walk != NULL; walk = walk->next)
    {
        if (walk->handler == handler && walk->arg1 == arg1 && walk->arg2 == arg2)
        {
            return true;
        }
    }

    return false;
}

void umac_timeoutq_dump(struct umac_core_timeoutq *toq)
{
    struct umac_core_timeout *walk;
    MMLOG_INF("UMAC Timeout Queue:\n");
    for (walk = toq->head; walk != NULL; walk = walk->next)
    {
        MMLOG_INF("TO %08lx: h=%08lx arg1=%08lx arg2=%08lx @=%lu\n",
                  (uint32_t)walk,
                  (uint32_t)walk->handler,
                  (uint32_t)walk->arg1,
                  (uint32_t)walk->arg2,
                  walk->timeout_abs_ms);
    }
}

int umac_timeoutq_deplete_timeout_protected(struct umac_core_timeoutq *toq,
                                            uint32_t delta_ms,
                                            umac_core_timeout_handler_t handler,
                                            void *arg1,
                                            void *arg2)
{
    uint32_t new_timeout = mmosal_get_time_ms() + delta_ms;
    int ret = 0;


    struct umac_core_timeout *to = umac_timeoutq_remove_one_protected(toq, handler, arg1, arg2);
    if (to == NULL)
    {
        return -1;
    }

    if (mmosal_time_lt(new_timeout, to->timeout_abs_ms))
    {
        to->timeout_abs_ms = new_timeout;
        ret = 1;
    }

    umac_timeoutq_enqueue_protected(toq, to);

    return ret;
}


bool umac_core_register_timeout(struct umac_data *umacd,
                                uint32_t delta_ms,
                                umac_core_timeout_handler_t handler,
                                void *arg1,
                                void *arg2)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    struct umac_core_timeout *to;

    if (core->evtloop_shutting_down || !umac_core_is_running(umacd))
    {
        MMLOG_INF("Cannot register timeout: evtloop shutting down\n");
        return false;
    }

    to = umac_timeoutq_alloc(&(core->toq));
    if (to == NULL)
    {
        MMLOG_INF("Cannot register timeout: alloc failed\n");
        return false;
    }

    to->timeout_abs_ms = mmosal_get_time_ms() + delta_ms;

    if (delta_ms == 0)
    {
        uint32_t next_timeout;
        bool next_timeout_valid = umac_timeoutq_peek_next_timeout(&core->toq, &next_timeout);
        if (next_timeout_valid && mmosal_time_lt(next_timeout, to->timeout_abs_ms))
        {
            to->timeout_abs_ms = next_timeout - 1;
        }
    }

    to->handler = handler;
    to->arg1 = arg1;
    to->arg2 = arg2;

    umac_timeoutq_enqueue(&(core->toq), to);


    umac_core_evt_wake(umacd);

    return true;
}

int umac_core_cancel_timeout(struct umac_data *umacd,
                             umac_core_timeout_handler_t handler,
                             void *arg1,
                             void *arg2)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    int ret;

    MMOSAL_TASK_ENTER_CRITICAL();
    ret = umac_timeoutq_cancel_protected(&core->toq, handler, arg1, arg2);
    MMOSAL_TASK_EXIT_CRITICAL();

    return ret;
}

int umac_core_cancel_timeout_one(struct umac_data *umacd,
                                 umac_core_timeout_handler_t handler,
                                 void *arg1,
                                 void *arg2,
                                 uint32_t *remaining)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    struct umac_core_timeout *to;

    MMOSAL_TASK_ENTER_CRITICAL();
    to = umac_timeoutq_remove_one_protected(&core->toq, handler, arg1, arg2);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (to == NULL)
    {
        return 0;
    }

    if (remaining != NULL)
    {
        int32_t remaining_signed;
        remaining_signed = to->timeout_abs_ms - mmosal_get_time_ms();
        if (remaining_signed < 0)
        {
            *remaining = 0;
        }
        else
        {
            *remaining = (uint32_t)remaining_signed;
        }
    }

    umac_timeoutq_free(&core->toq, to);

    return 1;
}

bool umac_core_is_timeout_registered(struct umac_data *umacd,
                                     umac_core_timeout_handler_t handler,
                                     void *arg1,
                                     void *arg2)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    bool is_reg;

    MMOSAL_TASK_ENTER_CRITICAL();
    is_reg = umac_timeoutq_is_timeout_registered_protected(&(core->toq), handler, arg1, arg2);
    MMOSAL_TASK_EXIT_CRITICAL();

    return is_reg;
}

int umac_core_deplete_timeout(struct umac_data *umacd,
                              uint32_t delta_ms,
                              umac_core_timeout_handler_t handler,
                              void *arg1,
                              void *arg2)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    int ret;

    MMOSAL_TASK_ENTER_CRITICAL();
    ret = umac_timeoutq_deplete_timeout_protected(&(core->toq), delta_ms, handler, arg1, arg2);
    MMOSAL_TASK_EXIT_CRITICAL();


    if (ret == 1)
    {
        umac_core_evt_wake(umacd);
    }

    MMLOG_VRB("DEPLETE: %4lu %08lx %08lx %08lx; ret=%d\n",
              delta_ms,
              (uint32_t)handler,
              (uint32_t)arg1,
              (uint32_t)arg2,
              ret);

    return ret;
}
