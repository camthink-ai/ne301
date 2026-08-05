/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "mmwlan.h"
#include "umac/scan/umac_scan_data.h"

struct umac_root_data
{

    bool shutdown_in_progress;

    bool is_initialised;
    struct umac_scan_req scan_request;
    mmwlan_scan_rx_cb_t scan_rx_cb;
    mmwlan_scan_complete_cb_t scan_complete_cb;
    void *scan_cb_arg;
};
