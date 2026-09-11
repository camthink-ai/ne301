/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once


#define BMGET(_v, _f) (((_v) & (_f)) >> __builtin_ctz(_f))
#define BMSET(_v, _f) (((_v) << __builtin_ctz(_f)) & (_f))
