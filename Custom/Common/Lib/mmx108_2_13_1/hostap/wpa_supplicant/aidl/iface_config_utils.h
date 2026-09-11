/*
 * WPA Supplicant - Iface configuration methods
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_IFACE_CONFIG_UTILS_H
#define WPA_SUPPLICANT_AIDL_IFACE_CONFIG_UTILS_H

#include <android-base/macros.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "wpa_supplicant_i.h"
#include "config.h"
}

/**
 * Utility functions to set various config parameters of an iface via AIDL
 * methods.
 */
namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {
namespace iface_config_utils {
ndk::ScopedAStatus setWpsDeviceName(
	struct wpa_supplicant* wpa_s, const std::string& name);
ndk::ScopedAStatus setWpsDeviceType(
	struct wpa_supplicant* wpa_s, const std::array<uint8_t, 8>& type);
ndk::ScopedAStatus setWpsManufacturer(
	struct wpa_supplicant* wpa_s, const std::string& manufacturer);
ndk::ScopedAStatus setWpsModelName(
	struct wpa_supplicant* wpa_s, const std::string& model_name);
ndk::ScopedAStatus setWpsModelNumber(
	struct wpa_supplicant* wpa_s, const std::string& model_number);
ndk::ScopedAStatus setWpsSerialNumber(
	struct wpa_supplicant* wpa_s, const std::string& serial_number);
ndk::ScopedAStatus setWpsConfigMethods(
	struct wpa_supplicant* wpa_s, uint16_t config_methods);
ndk::ScopedAStatus setExternalSim(
	struct wpa_supplicant* wpa_s, bool useExternalSim);
}  // namespace iface_config_utils
}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_IFACE_CONFIG_UTILS_H
