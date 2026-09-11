/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */


#pragma once

#include "common/common.h"
#include "mmosal.h"
#include "mmwlan.h"

#include "dot11/dot11.h"
#include "umac/data/umac_data.h"


#define UMAC_SUPPORTED_CAPABILITY_MASK   \
    (DOT11_MASK_CAPINFO_PRIVACY |        \
     DOT11_MASK_CAPINFO_SHORT_PREAMBLE | \
     DOT11_MASK_CAPINFO_QOS |            \
     DOT11_MASK_CAPINFO_SHORT_SLOT_TIME)


bool umac_shutdown_is_in_progress(struct umac_data *umacd);


void umac_fatal_error(struct umac_data *umacd, uint32_t fileid, uint32_t line);


void umac_last_will_and_testament(struct umac_data *umacd);


#ifndef UMAC_FATAL_ERROR
#define UMAC_FATAL_ERROR(umacd) umac_fatal_error(umacd, MMOSAL_FILEID, __LINE__)
#endif


