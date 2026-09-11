/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_core.h"
#include "umac_core_data.h"

#define MAX_EVTS_DISPATCHED_AT_ONCE     (5)
#define MAX_TIMEOUTS_DISPATCHED_AT_ONCE (5)




void umac_evtq_init(struct umac_core_evtq *evtq);


void umac_evt_free(struct umac_core_evtq *evtq, struct umac_evt *evt);


enum umac_evtq_position
{
    EVTQ_HEAD,
    EVTQ_TAIL,
};


bool umac_evt_queue(struct umac_core_evtq *evtq,
                    const struct umac_evt *evt,
                    enum umac_evtq_position position);


struct umac_evt *umac_evt_dequeue(struct umac_core_evtq *evtq);


static inline bool umac_evtq_is_empty(struct umac_core_evtq *evtq)
{
    return evtq->head == NULL;
}


void umac_evtq_dump(struct umac_core_evtq *evtq);




void umac_timeoutq_init(struct umac_core_timeoutq *toq);


void umac_timeoutq_deinit(struct umac_core_timeoutq *toq);


enum mmwlan_status umac_timeoutq_alloc_extra(struct umac_core_timeoutq *toq);


uint32_t umac_timeoutq_dispatch(struct umac_core_data *core);


uint32_t umac_timeoutq_time_to_next_timeout(struct umac_core_data *core);


void umac_timeoutq_dump(struct umac_core_timeoutq *toq);




void umac_evt_handler(struct umac_data *umacd, const struct umac_evt *evt);
