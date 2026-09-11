/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#if !(defined(DISABLE_MMWLAN_HEALTH_CHECK) && DISABLE_MMWLAN_HEALTH_CHECK)

#include <stdbool.h>
#include <stdint.h>

struct umac_health_check_data
{

    bool is_started;

    uint32_t interval_ms;

    uint32_t last_activity_ms;

    uint32_t periodic_check_vetoes;

    bool check_demanded;
};

#endif
