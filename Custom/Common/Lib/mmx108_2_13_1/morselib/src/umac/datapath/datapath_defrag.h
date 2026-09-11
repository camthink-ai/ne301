/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmpkt.h"
#include "dot11/dot11_frames.h"
#include "umac/datapath/umac_datapath_data.h"


struct mmpkt *datapath_defrag(struct umac_data *umacd,
                              struct datapath_defrag_data *data,
                              const struct dot11_data_hdr **data_hdr,
                              struct mmpktview **rxbufview,
                              struct mmpkt *rxbuf,
                              uint8_t tid_idx);


void datapath_defrag_deinit(struct umac_data *umacd, struct datapath_defrag_data *data);


