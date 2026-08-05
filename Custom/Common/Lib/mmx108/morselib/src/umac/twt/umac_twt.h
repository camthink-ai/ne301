/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmdrv.h"
#include "umac/umac.h"
#include "umac/data/umac_data.h"
#include "common/morse_commands.h"
#include "umac/ies/ies_common.h"


#define MORSE_INT_CEIL(_num, _div) (((_num) + (_div) - 1) / (_div))


#define TWT_ENABLE_IMPLICIT (1)

#define TWT_WAKE_DURATION_UNIT (256)

#define TWT_WAKE_INTERVAL_EXPONENT_MAX_VAL (31)

#define UMAC_TWT_NUM_AGREEMENTS (1)


enum umac_twt_cmd_type
{

    UMAC_TWT_CMD_TYPE_CONFIGURE,

    UMAC_TWT_CMD_TYPE_FORCE_INSTALL_AGREEMENT,

    UMAC_TWT_CMD_TYPE_REMOVE_AGREEMENT,

    UMAC_TWT_CMD_TYPE_CONFIGURE_EXPLICIT,
};


struct umac_twt_command
{

    enum umac_twt_cmd_type type;

    uint64_t target_wake_time;

    union
    {

        uint64_t wake_interval_us;


        struct
        {

            uint16_t wake_interval_mantissa;

            uint8_t wake_interval_exponent;
        } expl;
    };


    uint32_t min_wake_duration_us;

    uint8_t twt_setup_command;

    uint8_t flow_id;
};


struct MM_PACKED umac_twt_params
{

    uint16_t req_type;

    uint64_t twt;

    uint8_t min_twt_dur;

    uint16_t mantissa;

    uint8_t channel;
};


enum umac_twt_agreement_state
{

    UMAC_TWT_AGREEMENT_STATE_EMPTY,

    UMAC_TWT_AGREEMENT_STATE_PENDING_RESPONSE,

    UMAC_TWT_AGREEMENT_STATE_PENDING_INSTALLATION,

    UMAC_TWT_AGREEMENT_STATE_INSTALLED,
};


struct MM_PACKED umac_twt_agreement_data
{

    enum umac_twt_agreement_state state;

    uint64_t wake_time_us;

    uint64_t wake_interval_us;

    uint32_t wake_duration_us;

    uint8_t control;

    struct umac_twt_params params;
};


void umac_twt_init(struct umac_data *umacd);


void umac_twt_init_vif(struct umac_data *umacd, uint16_t *vif_id);


void umac_twt_deinit_vif(struct umac_data *umacd, uint16_t *vif_id);


enum mmwlan_status umac_twt_handle_command(struct umac_data *umacd,
                                           const struct umac_twt_command *cmd);


struct umac_twt_agreement_data *umac_twt_get_agreement(struct umac_data *umacd, uint16_t flow_id);


enum mmwlan_status umac_twt_process_ie(struct umac_data *umacd, const struct dot11_ie_twt *twt_ie);


enum mmwlan_status umac_twt_install_pending_agreements(struct umac_data *umacd, bool is_reinstall);


enum mmwlan_status umac_twt_add_configuration(struct umac_data *umacd,
                                              const struct mmwlan_twt_config_args *twt_config_args);


const struct mmwlan_twt_config_args *umac_twt_get_config(struct umac_data *umacd);


