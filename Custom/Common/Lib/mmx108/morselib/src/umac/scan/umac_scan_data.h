/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_scan.h"
#include "hw_scan_fsm.h"

struct hw_scan_data
{

    struct hw_scan_fsm_instance fsm_inst;

    uint16_t vif_id;

    bool abort_all;
};

struct umac_scan_data
{

    const struct umac_scan_req *active_scan_req;

    const struct umac_scan_req *pending_scan_req;

    uint32_t prev_scan_completion_time;

    struct hw_scan_data hw_scan_data;
};
