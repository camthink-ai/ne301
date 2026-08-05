/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmlog.h"
#include "mmpkt.h"
#include "mmosal.h"
#include "common/common.h"

static const struct mmpkt_ops mmpkt_heap_ops = { .free_mmpkt = mmosal_free };

struct mmpkt *mmpkt_alloc_on_heap(uint32_t space_at_start,
                                  uint32_t space_at_end,
                                  uint32_t metadata_size)
{
    struct mmpkt *mmpkt;
    uint8_t *buf;
    uint32_t alloc_len = FAST_ROUND_UP(sizeof(*mmpkt), 4) +
                         FAST_ROUND_UP(space_at_start + space_at_end, 4) +
                         FAST_ROUND_UP(metadata_size, 4);

    if (space_at_end == UINT32_MAX)
    {
        return NULL;
    }

    mmpkt = (struct mmpkt *)mmosal_malloc(alloc_len);
    if (mmpkt == NULL)
    {
        return NULL;
    }

    buf = ((uint8_t *)mmpkt) + FAST_ROUND_UP(sizeof(*mmpkt), 4);

    mmpkt_init(mmpkt,
               buf,
               FAST_ROUND_UP(space_at_start + space_at_end, 4),
               space_at_start,
               &mmpkt_heap_ops);

    if (metadata_size != 0)
    {
        mmpkt->metadata.opaque = buf + FAST_ROUND_UP(space_at_start + space_at_end, 4);
        memset(mmpkt->metadata.opaque, 0, FAST_ROUND_UP(metadata_size, 4));
    }

    return mmpkt;
}

void mmpkt_release(struct mmpkt *mmpkt)
{
    if (mmpkt == NULL)
    {
        return;
    }

#if defined(MMPKT_DEBUG) && MMPKT_DEBUG
    MMOSAL_ASSERT(mmpkt->debug_magic == MMPKT_DEBUG_MAGIC_CLOSED);
    mmpkt->debug_magic = MMPKT_DEBUG_MAGIC_RELEASED;
#endif

    MMOSAL_ASSERT(mmpkt->ops != NULL && mmpkt->ops->free_mmpkt != NULL);
    mmpkt->ops->free_mmpkt(mmpkt);
}
