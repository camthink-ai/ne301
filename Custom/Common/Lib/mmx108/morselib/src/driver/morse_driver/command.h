/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "morse.h"

#include "common/morse_commands.h"

#define MORSE_CMD_IS_REQ(cmd)    ((cmd)->hdr.flags & MORSE_CMD_TYPE_REQ)
#define MORSE_CMD_IS_RESP(cmd)   ((cmd)->hdr.flags & MORSE_CMD_TYPE_RESP)
#define MORSE_CMD_IS_EVT(cmd)    ((cmd)->hdr.flags & MORSE_CMD_TYPE_EVT)

#define MORSE_CMD_IID_SEQ_MAX    0xfff
#define MORSE_CMD_IID_RETRY_MASK 0x000f
#define MORSE_CMD_IID_SEQ_SHIFT  4
#define MORSE_CMD_IID_SEQ_MASK   0xfff0


int morse_cmd_tx(struct driver_data *driverd,
                 struct morse_cmd_resp *resp,
                 struct morse_cmd_req *cmd,
                 uint32_t length,
                 uint32_t timeout);


int morse_cmd_resp_process(struct driver_data *driverd, struct mmpkt *mmpkt, uint8_t channel);


int morse_cmd_init(struct driver_data *driverd);


void morse_cmd_deinit(struct driver_data *driverd);


int morse_cmd_health_check(struct driver_data *driverd);
