/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>

#include "common.h"

enum mmwlan_status errno_to_status(int result)
{
    int err = (result < 0) ? -result : result;

    switch (err)
    {
        case 0:
            return MMWLAN_SUCCESS;

        case ENODEV:
            return MMWLAN_NOT_RUNNING;

        case EINVAL:
            return MMWLAN_INVALID_ARGUMENT;

        case ENOMEM:
        case ENOSPC:
            return MMWLAN_NO_MEM;

        case ETIMEDOUT:
            return MMWLAN_TIMED_OUT;

        case ENOENT:
            return MMWLAN_NOT_FOUND;

        case EAGAIN:
            return MMWLAN_UNAVAILABLE;

        default:
            return MMWLAN_ERROR;
    }
}
