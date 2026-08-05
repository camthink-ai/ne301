/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <stdbool.h>
#include "umac/data/umac_data.h"


enum umac_wnm_sleep_event
{

    UMAC_WNM_SLEEP_EVENT_REQUEST_ENTRY,

    UMAC_WNM_SLEEP_EVENT_REQUEST_EXIT,

    UMAC_WNM_SLEEP_EVENT_CONNECTION_LOST,

    UMAC_WNM_SLEEP_EVENT_HW_RESTARTED,

    UMAC_WNM_SLEEP_EVENT_ENTRY_CONFIRMED,

    UMAC_WNM_SLEEP_EVENT_EXIT_CONFIRMED,
};


void umac_wnm_sleep_init(struct umac_data *umacd);


void umac_wnm_sleep_set_chip_powerdown(struct umac_data *umacd, bool chip_powerdown_enabled);


enum mmwlan_status umac_wnm_sleep_register_semb(struct umac_data *umacd,
                                                struct mmosal_semb *semb,
                                                volatile enum mmwlan_status *status);


void umac_wnm_sleep_report_event(struct umac_data *umacd, enum umac_wnm_sleep_event event);


