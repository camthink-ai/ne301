/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_supp_shim_private.h"

#include "umac/data/umac_data.h"
#include "umac/core/umac_core.h"

int eloop_init(void)
{
    return 0;
}

int eloop_register_timeout(unsigned int secs,
                           unsigned int usecs,
                           eloop_timeout_handler handler,
                           void *eloop_data,
                           void *user_data)
{
    uint32_t delta_ms;


    if (secs > ((INT32_MAX / 1000) - 1))
    {
        MMLOG_WRN("Timeout too long: %u secs", secs);
        return -1;
    }

    delta_ms = secs * 1000 + usecs / 1000;

    return umac_core_register_timeout(umac_data_get_umacd(),
                                      delta_ms,
                                      handler,
                                      eloop_data,
                                      user_data);
}

int eloop_cancel_timeout(eloop_timeout_handler handler, void *eloop_data, void *user_data)
{
    return umac_core_cancel_timeout(umac_data_get_umacd(), handler, eloop_data, user_data);
}

int eloop_cancel_timeout_one(eloop_timeout_handler handler,
                             void *eloop_data,
                             void *user_data,
                             struct os_reltime *remaining)
{
    int ret;
    uint32_t remaining_ms = 0;
    ret = umac_core_cancel_timeout_one(umac_data_get_umacd(),
                                       handler,
                                       eloop_data,
                                       user_data,
                                       &remaining_ms);

    if (ret > 0 && remaining != NULL)
    {
        remaining->sec = remaining_ms / 1000;
        remaining->usec = remaining_ms * 1000;
    }

    return ret;
}

int eloop_is_timeout_registered(eloop_timeout_handler handler, void *eloop_data, void *user_data)
{
    return umac_core_is_timeout_registered(umac_data_get_umacd(), handler, eloop_data, user_data);
}

int eloop_deplete_timeout(unsigned int req_secs,
                          unsigned int req_usecs,
                          eloop_timeout_handler handler,
                          void *eloop_data,
                          void *user_data)
{
    uint32_t delta_ms;


    if (req_secs > ((INT32_MAX / 1000) - 1))
    {
        MMLOG_WRN("Timeout too long: %u secs", req_secs);
        return -1;
    }

    delta_ms = req_secs * 1000 + req_usecs / 1000;
    return umac_core_deplete_timeout(umac_data_get_umacd(),
                                     delta_ms,
                                     handler,
                                     eloop_data,
                                     user_data);
}

void eloop_run(void)
{
    MMLOG_VRB("eloop run requested (nop)\n");
}

void eloop_terminate(void)
{
    MMLOG_VRB("eloop terminate requested (nop)\n");
}

void eloop_destroy(void)
{
    MMLOG_VRB("eloop destroy requested (nop)\n");
}

int eloop_register_signal_terminate(eloop_signal_handler handler, void *user_data)
{
    (void)handler;
    (void)user_data;



    return 0;
}

int eloop_register_signal_reconfig(eloop_signal_handler handler, void *user_data)
{
    (void)handler;
    (void)user_data;



    return 0;
}

int eloop_sock_requeue(void)
{
    return 0;
}
