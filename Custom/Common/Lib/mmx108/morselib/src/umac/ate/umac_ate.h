/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmwlan_internal.h"


enum mmwlan_status umac_ate_execute_command(struct umac_data *umacd,
                                            uint8_t *command,
                                            uint32_t command_len,
                                            uint8_t *response,
                                            uint32_t *response_len);


enum mmwlan_status umac_ate_get_key_info(struct umac_data *umacd,
                                         struct mmwlan_key_info *key_info,
                                         uint32_t *key_info_count);


