/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_core.h"
#include "umac_core_private.h"

void umac_core_init(struct umac_data *umacd)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    umac_timeoutq_init(&(core->toq));
    umac_evtq_init(&(core->evtq));
}

void umac_core_deinit(struct umac_data *umacd)
{
    MMOSAL_DEV_ASSERT(!umac_core_is_running(umacd));
    struct umac_core_data *core = umac_data_get_core(umacd);
    umac_timeoutq_deinit(&(core->toq));
}

enum mmwlan_status umac_core_alloc_extra_timeouts(struct umac_data *umacd)
{
    MMOSAL_DEV_ASSERT(umac_core_is_running(umacd));
    struct umac_core_data *core = umac_data_get_core(umacd);
    return umac_timeoutq_alloc_extra(&(core->toq));
}

enum mmwlan_status umac_core_register_sleep_cb(struct umac_data *umacd,
                                               mmwlan_sleep_cb_t callback,
                                               void *arg)
{
    struct umac_core_data *core = umac_data_get_core(umacd);
    core->sleep_callback = callback;
    core->sleep_arg = arg;
    return MMWLAN_SUCCESS;
}
