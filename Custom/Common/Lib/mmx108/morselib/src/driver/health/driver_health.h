/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once


int driver_health_init(struct driver_data *driverd);


void driver_health_deinit(struct driver_data *driverd);


void driver_health_demand_check(struct driver_data *driverd);


void driver_health_request_check(struct driver_data *driverd);


