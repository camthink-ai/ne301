/*
 * WPA Supplicant - Global Aidl struct
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AIDL_I_H
#define AIDL_I_H

#ifdef _cplusplus
extern "C"
{
#endif  // _cplusplus

	struct wpas_aidl_priv {
		int aidl_fd;
		struct wpa_global *global;
		void *aidl_manager;
	};

#ifdef _cplusplus
}
#endif  // _cplusplus

#endif  // AIDL_I_H
