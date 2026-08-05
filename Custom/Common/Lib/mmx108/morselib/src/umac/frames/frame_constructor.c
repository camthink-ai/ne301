/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "frames_common.h"
#include "common/consbuf.h"
#include "mmlog.h"
#include "umac/datapath/umac_datapath.h"
#include "mmdrv.h"

struct mmpkt *build_mgmt_frame(struct umac_data *umacd, mgmt_frame_builder_t builder, void *params)
{

    struct consbuf cbuf = CONSBUF_INIT_WITHOUT_BUF;
    builder(umacd, &cbuf, params);
    struct mmpkt *mmpkt = umac_datapath_alloc_raw_tx_mmpkt(MMDRV_PKT_CLASS_MGMT, 0, cbuf.offset);
    if (mmpkt == NULL)
    {
        MMLOG_WRN("Failed to allocate mgmt frame (len %lu)\n", cbuf.offset);
        return NULL;
    }
    struct mmpktview *view = mmpkt_open(mmpkt);
    consbuf_reinit_from_mmpkt(&cbuf, view);
    builder(umacd, &cbuf, params);
    uint8_t *ret = mmpkt_append(view, cbuf.offset);
    MMOSAL_ASSERT(ret != NULL);
    mmpkt_close(&view);
    return mmpkt;
}
