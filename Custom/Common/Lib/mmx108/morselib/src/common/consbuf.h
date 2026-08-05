/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/common.h"
#include "mmpkt.h"


struct consbuf
{

    uint8_t *buf;

    uint32_t buf_size;

    uint32_t offset;
};


#define CONSBUF_INIT_WITHOUT_BUF { NULL, 0, 0 }


#define CONSBUF_INIT_WITH_BUF(buf, buf_size) { (buf), (buf_size), 0 }


static inline void consbuf_reinit(struct consbuf *cbuf, uint8_t *buf, uint32_t buf_size)
{
    cbuf->buf = buf;
    cbuf->buf_size = buf_size;
    cbuf->offset = 0;
}


static inline void consbuf_reinit_from_mmpkt(struct consbuf *cbuf, struct mmpktview *view)
{
    cbuf->buf = mmpkt_append(view, 0);
    cbuf->buf_size = mmpkt_available_space_at_end(view);
    cbuf->offset = 0;
}


static inline void consbuf_append(struct consbuf *buf, const uint8_t *data, uint32_t len)
{
    if (buf->buf != NULL)
    {
        MMOSAL_ASSERT(len <= buf->buf_size - buf->offset);
        memcpy(buf->buf + buf->offset, data, len);
    }

    buf->offset += len;
}


static inline void consbuf_append_be16(struct consbuf *buf, uint16_t data)
{
    if (buf->buf != NULL)
    {
        MMOSAL_ASSERT(sizeof(data) <= buf->buf_size - buf->offset);
        (buf->buf + buf->offset)[0] = data >> 8;
        (buf->buf + buf->offset)[1] = data & 0xff;
    }

    buf->offset += sizeof(data);
}


static inline uint8_t *consbuf_reserve(struct consbuf *buf, uint32_t len)
{
    uint8_t *ret = NULL;
    if (buf->buf != NULL)
    {
        MMOSAL_ASSERT(len <= buf->buf_size - buf->offset);
        ret = buf->buf + buf->offset;
    }

    buf->offset += len;

    return ret;
}


