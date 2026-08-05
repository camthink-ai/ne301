/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_core_private.h"
#include "mmlog.h"

#ifdef ENABLE_EVTQ_TRACE
#include "mmtrace.h"
static mmtrace_channel evtq_channel_handle;
#define EVTQ_TRACE_INIT()     evtq_channel_handle = mmtrace_register_channel("evtq")
#define EVTQ_TRACE(_fmt, ...) mmtrace_printf(evtq_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define EVTQ_TRACE_INIT() \
    do {                  \
    } while (0)
#define EVTQ_TRACE(_fmt, ...) \
    do {                      \
    } while (0)
#endif

void umac_evtq_init(struct umac_core_evtq *evtq)
{

    unsigned ii;
    for (ii = 0; ii < UMAC_EVTQ_MAXLEN; ii++)
    {
        struct umac_evt *evt = &(evtq->pool[ii]);
        evt->next = evtq->free;
        evtq->free = evt;
    }

    EVTQ_TRACE_INIT();
}

static struct umac_evt *umac_evt_alloc_protected(struct umac_core_evtq *evtq)
{
    struct umac_evt *evt = evtq->free;
    if (evt != NULL)
    {
        evtq->free = evt->next;
    }
    return evt;
}

static void umac_evt_free_protected(struct umac_core_evtq *evtq, struct umac_evt *evt)
{
    evt->next = evtq->free;
    evtq->free = evt;
}

void umac_evt_free(struct umac_core_evtq *evtq, struct umac_evt *evt)
{
    MMOSAL_TASK_ENTER_CRITICAL();
    umac_evt_free_protected(evtq, evt);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static bool umac_evt_queue_protected(struct umac_core_evtq *evtq,
                                     const struct umac_evt *evt,
                                     enum umac_evtq_position position)
{
    struct umac_evt *new_evt = umac_evt_alloc_protected(evtq);
    if (new_evt == NULL)
    {
        return false;
    }

    *new_evt = *evt;

    if (position == EVTQ_HEAD)
    {
        new_evt->next = evtq->head;
        if (evtq->tail == NULL)
        {
            MMOSAL_ASSERT(evtq->head == NULL);
            evtq->tail = new_evt;
        }
        evtq->head = new_evt;
    }
    else
    {
        new_evt->next = NULL;

        if (evtq->tail == NULL)
        {
            MMOSAL_ASSERT(evtq->head == NULL);
            evtq->head = new_evt;
            evtq->tail = new_evt;
        }
        else
        {
            MMOSAL_ASSERT(evtq->tail->next == NULL);
            evtq->tail->next = new_evt;
            evtq->tail = new_evt;
        }
    }

    return true;
}

bool umac_evt_queue(struct umac_core_evtq *evtq,
                    const struct umac_evt *evt,
                    enum umac_evtq_position position)
{
    bool ok;

    MMOSAL_TASK_ENTER_CRITICAL();
    ok = umac_evt_queue_protected(evtq, evt, position);
    MMOSAL_TASK_EXIT_CRITICAL();
    if (ok)
    {
        EVTQ_TRACE("Queued handler %x\n", (intptr_t)evt->handler);
        MMLOG_VRB("Queued evt %08lx (handler %p) at %s\n",
                  (uint32_t)evt,
                  evt->handler,
                  (position == EVTQ_HEAD) ? "head" : "tail");
    }
    else
    {
        EVTQ_TRACE("Failed to queue handler %x\n", (intptr_t)evt->handler);
        MMLOG_INF("Failed to queue evt %08lx (handler %p); qfree=%p\n",
                  (uint32_t)evt,
                  evt->handler,
                  evtq->free);
#ifdef ENABLE_UMAC_EVTQ_DUMP_ON_QUEUE_FAILURE
        umac_evtq_dump(evtq);
#endif
    }

    return ok;
}

static struct umac_evt *umac_evt_dequeue_protected(struct umac_core_evtq *evtq)
{
    struct umac_evt *evt;

    if (evtq->head == NULL)
    {
        return NULL;
    }

    MMOSAL_ASSERT(evtq->tail != NULL);

    evt = evtq->head;
    evtq->head = evt->next;

    if (evtq->head == NULL)
    {
        evtq->tail = NULL;
    }

    evt->next = NULL;
    return evt;
}

struct umac_evt *umac_evt_dequeue(struct umac_core_evtq *evtq)
{
    struct umac_evt *evt;

    MMOSAL_TASK_ENTER_CRITICAL();
    evt = umac_evt_dequeue_protected(evtq);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (evt)
    {
        EVTQ_TRACE("Dequeued handler %x\n", (intptr_t)evt->handler);
        MMLOG_VRB("Dequeued evt %08lx (handler %p)\n", (uint32_t)evt, evt->handler);
    }
    else
    {
        MMLOG_VRB("Dequeued: no evt\n");
    }

    return evt;
}

void umac_evtq_dump(struct umac_core_evtq *evtq)
{
    struct umac_evt *walk;
    MMLOG_INF("UMAC Event Queue:\n");
    for (walk = evtq->head; walk != NULL; walk = walk->next)
    {
        MMLOG_INF("EVT %08lx: handler=%p\n", (uint32_t)walk, walk->handler);
    }
}
