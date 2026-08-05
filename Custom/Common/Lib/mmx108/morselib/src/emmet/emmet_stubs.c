/*
 * Copyright 2021-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "emmet.h"
#include "mmlog.h"
#include "mmutils.h"

void emmet_init(void)
{
    MMLOG_ERR("Emmet support not built into application.\n");
}

void emmet_start(void)
{
    MMLOG_ERR("Emmet support not built into application.\n");
}

enum mmwlan_status emmet_set_reg_db(const struct mmwlan_regulatory_db *reg_db)
{
    MM_UNUSED(reg_db);
    MMLOG_ERR("Emmet support not built into application.\n");
    return MMWLAN_UNAVAILABLE;
};
