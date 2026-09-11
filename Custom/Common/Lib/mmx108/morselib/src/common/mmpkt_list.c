/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stdint.h>

#include "mmosal.h"
#include "mmpkt.h"
#include "mmpkt_list.h"

#ifdef ENABLE_MMPKTLIST_TRACE
#include "mmtrace.h"
static mmtrace_channel mmpktlist_channel_handle;
#define MMPKTLIST_TRACE(_fmt, ...)                                            \
    do {                                                                      \
        if (!mmpktlist_channel_handle)                                        \
        {                                                                     \
            mmpktlist_channel_handle = mmtrace_register_channel("mmpktlist"); \
        }                                                                     \
        mmtrace_printf(mmpktlist_channel_handle, _fmt, ##__VA_ARGS__);        \
    } while (0)
#else
#define MMPKTLIST_TRACE(_fmt, ...) \
    do {                           \
    } while (0)
#endif

#ifdef MMPKT_SANITY
static void mmpkt_list_sanity_check(struct mmpkt_list *list)
{
    unsigned cnt = 0;
    struct mmpkt *walk;
    struct mmpkt *prev = NULL;

    for (walk = list->head; walk != NULL; walk = mmpkt_get_next(walk))
    {
        cnt++;
        prev = walk;
    }

    MMOSAL_ASSERT(cnt == list->len);
    MMOSAL_ASSERT(prev == list->tail);
}

#endif

void mmpkt_list_prepend(struct mmpkt_list *list, struct mmpkt *mmpkt)
{
    MMPKTLIST_TRACE("prepend %x %x", (uint32_t)list, (uint32_t)mmpkt);
    mmpkt_set_next(mmpkt, list->head);
    list->head = mmpkt;
    list->len++;

    if (list->tail == NULL)
    {
        list->tail = list->head;
    }

#ifdef MMPKT_SANITY
    mmpkt_list_sanity_check(list);
#endif
}

void mmpkt_list_append(struct mmpkt_list *list, struct mmpkt *mmpkt)
{
    MMPKTLIST_TRACE("append %x %x", (uint32_t)list, (uint32_t)mmpkt);
    mmpkt_set_next(mmpkt, NULL);
    if (list->head == NULL)
    {
        list->head = mmpkt;
        list->tail = mmpkt;
    }
    else
    {
        mmpkt_set_next(list->tail, mmpkt);
        list->tail = mmpkt;
    }
    list->len++;

#ifdef MMPKT_SANITY
    mmpkt_list_sanity_check(list);
#endif
}

void mmpkt_list_insert_after(struct mmpkt_list *list, struct mmpkt *ref, struct mmpkt *mmpkt)
{
    struct mmpkt *walk;
    struct mmpkt *next;

    MMPKT_LIST_WALK(list, walk, next)
    {
        if (walk == ref)
        {
            mmpkt_set_next(walk, mmpkt);
            mmpkt_set_next(mmpkt, next);
            list->len++;
            if (next == NULL)
            {
                list->tail = mmpkt;
            }
#ifdef MMPKT_SANITY
            mmpkt_list_sanity_check(list);
#endif
            return;
        }
    }


    MMOSAL_ASSERT(false);
}

static struct mmpkt *mmpkt_find_prev(struct mmpkt_list *list, struct mmpkt *mmpkt)
{
    struct mmpkt *walk, *next;
    for (walk = list->head, next = mmpkt_get_next(walk); next != NULL;
         walk = next, next = mmpkt_get_next(walk))
    {
        if (next == mmpkt)
        {
            return walk;
        }
    }
    return NULL;
}

void mmpkt_list_remove(struct mmpkt_list *list, struct mmpkt *mmpkt)
{
    MMPKTLIST_TRACE("remove %x %x", (uint32_t)list, (uint32_t)mmpkt);


    if (list->head == NULL)
    {
        return;
    }

    struct mmpkt *prev = NULL;

    if (list->head == mmpkt)
    {
        list->head = mmpkt_get_next(mmpkt);
    }
    else
    {
        prev = mmpkt_find_prev(list, mmpkt);
        MMOSAL_ASSERT(prev != NULL);
        mmpkt_set_next(prev, mmpkt_get_next(mmpkt));
    }

    if (list->tail == mmpkt)
    {
        list->tail = prev;
    }

    list->len--;
    mmpkt_set_next(mmpkt, NULL);

#ifdef MMPKT_SANITY
    mmpkt_list_sanity_check(list);
#endif
}

struct mmpkt *mmpkt_list_dequeue(struct mmpkt_list *list)
{
    if (list->head == NULL)
    {
        return NULL;
    }
    else
    {
        struct mmpkt *mmpkt = list->head;
        list->head = mmpkt_get_next(mmpkt);
        list->len--;

        if (list->tail == mmpkt)
        {
            list->tail = NULL;
        }

#ifdef MMPKT_SANITY
        mmpkt_list_sanity_check(list);
#endif
        MMPKTLIST_TRACE("dequeue %x %x", (uint32_t)list, (uint32_t)mmpkt);
        if (mmpkt != NULL)
        {
            mmpkt_set_next(mmpkt, NULL);
        }
        return mmpkt;
    }
}

struct mmpkt *mmpkt_list_dequeue_tail(struct mmpkt_list *list)
{
    if (list->tail == NULL)
    {
        return NULL;
    }

    struct mmpkt *mmpkt = list->tail;
    mmpkt_list_remove(list, mmpkt);

    return mmpkt;
}

void mmpkt_list_clear(struct mmpkt_list *list)
{
    struct mmpkt *walk;
    struct mmpkt *next;

#ifdef MMPKT_SANITY
    mmpkt_list_sanity_check(list);
#endif

    MMPKT_LIST_WALK(list, walk, next)
    {
        mmpkt_release(walk);
    }
    list->len = 0;
    list->head = NULL;
    list->tail = NULL;

    MMPKTLIST_TRACE("clear %x", (uint32_t)list);
}

void mmpkt_list_append_list(struct mmpkt_list *list, struct mmpkt_list *other)
{
    MMPKTLIST_TRACE("append_list %x %x", (uint32_t)list, (uint32_t)other);
    MMOSAL_DEV_ASSERT(list && other);
    if (other == NULL || other->head == NULL)
    {
        return;
    }
    if (list->head == NULL)
    {
        list->head = other->head;
    }
    else
    {
        mmpkt_set_next(list->tail, other->head);
    }
    list->tail = other->tail;
    list->len += other->len;

    mmpkt_list_init(other);
#ifdef MMPKT_SANITY
    mmpkt_list_sanity_check(list);
#endif
}
