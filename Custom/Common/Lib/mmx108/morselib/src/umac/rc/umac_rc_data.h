/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_rc.h"
#include "mmrc.h"

struct umac_rc_sta_data
{

    struct mmrc_table *reference_table;


    struct mmrc_sta_capabilities active_capabilities;


    struct mmrc_sta_capabilities local_capabilities;



    uint32_t next_update_time_ms;
};
