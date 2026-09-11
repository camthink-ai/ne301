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


void consbuf_reinit(struct consbuf *cbuf, uint8_t *buf, uint32_t buf_size);


void consbuf_reinit_from_mmpkt(struct consbuf *cbuf, struct mmpktview *view);


void consbuf_append(struct consbuf *buf, const uint8_t *data, uint32_t len);


void consbuf_append_be16(struct consbuf *buf, uint16_t data);


uint8_t *consbuf_reserve(struct consbuf *buf, uint32_t len);


