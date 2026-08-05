/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


bool frame_is_disassociation(const struct dot11_hdr *header);
