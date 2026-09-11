/*
 * Copyright 2021-2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac/data/umac_data.h"


void umac_ps_handle_hw_restarted(struct umac_data *umacd);


void umac_ps_reset(struct umac_data *umacd);


void umac_ps_update_mode(struct umac_data *umacd);


void umac_ps_set_suspended(struct umac_data *umacd, bool suspended);


