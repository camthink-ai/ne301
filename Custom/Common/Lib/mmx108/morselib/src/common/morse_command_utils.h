/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once


#define DEFAULT_HOST_ID 0


#define MM_CMD_TIMEOUT_DEFAULT      600
#define MM_CMD_TIMEOUT_PS           2000
#define MM_CMD_TIMEOUT_HEALTH_CHECK 1000


#define MORSE_COMMAND_INIT(cmd_instance, cmd_id, v_id, ...) \
    {                                                                          \
            .hdr = {                                                               \
                .message_id = htole16(cmd_id),                                     \
                .len = htole16(sizeof(cmd_instance) - sizeof((cmd_instance).hdr)), \
                .host_id = htole16(DEFAULT_HOST_ID),                               \
                .vif_id = htole16(v_id),                                           \
            },                                                                     \
            __VA_ARGS__                                                            \
        }



enum morse_cmd_return_code
{
    MORSE_RET_SUCCESS = 0,
    MORSE_RET_EPERM = -1,
    MORSE_RET_ENOMEM = -12,
    MORSE_RET_CMD_NOT_HANDLED = -32757,
};


static inline void morse_command_reinit_header(struct morse_cmd_header *hdr,
                                               uint16_t len,
                                               uint16_t cmd_id,
                                               uint16_t vif_id)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->message_id = htole16(cmd_id);
    hdr->len = htole16(len);
    hdr->host_id = htole16(DEFAULT_HOST_ID);
    hdr->vif_id = htole16(vif_id);
}
