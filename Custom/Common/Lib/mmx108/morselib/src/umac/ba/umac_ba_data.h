/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_ba.h"
#include "dot11/dot11.h"


#define UMAC_BA_MAX_SESSIONS (UMAC_BA_MAX_AGGR_TID + 1)


enum umac_ba_status
{
    UMAC_BA_DISABLED,
    UMAC_BA_REQUESTED,
    UMAC_BA_REFUSED,
    UMAC_BA_SUCCESS
};

struct umac_ba_session
{

    uint8_t status;

    uint8_t dialog_token;
    uint8_t tid;

    uint16_t buffer_size;

    uint16_t timeout;

    uint16_t starting_seq_ctrl;

    uint32_t attempt_backoff;

    uint16_t next_expected_rx_seq_num;
};

struct umac_ba_sta_data
{

    struct sessions
    {

        struct umac_ba_session originator[UMAC_BA_MAX_SESSIONS];

        struct umac_ba_session recipient[UMAC_BA_MAX_SESSIONS];
    } sessions;
};
