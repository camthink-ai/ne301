/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <stdint.h>

#include "mmpkt.h"
#include "common/mac_address.h"
#include "common/consbuf.h"
#include "dot11/dot11.h"
#include "umac/data/umac_data.h"


typedef void (*mgmt_frame_builder_t)(struct umac_data *umacd, struct consbuf *buf, void *params);


struct mmpkt *build_mgmt_frame(struct umac_data *umacd, mgmt_frame_builder_t builder, void *params);


bool frame_is_robust_mgmt(struct mmpktview *view);


