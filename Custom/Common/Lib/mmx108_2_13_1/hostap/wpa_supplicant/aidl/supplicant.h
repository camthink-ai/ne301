/*
 * WPA Supplicant - Supplicant Aidl interface
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_SUPPLICANT_H
#define WPA_SUPPLICANT_AIDL_SUPPLICANT_H

#include <aidl/android/hardware/wifi/supplicant/BnSupplicant.h>
#include <aidl/android/hardware/wifi/supplicant/DebugLevel.h>
#include <aidl/android/hardware/wifi/supplicant/IfaceInfo.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantCallback.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantP2pIface.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantStaIface.h>

#include <android-base/macros.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "utils/wpa_debug.h"
#include "wpa_supplicant_i.h"
#include "scan.h"
}

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {

/**
 * Implementation of the supplicant aidl object. This aidl
 * object is used core for global control operations on
 * wpa_supplicant.
 */
class Supplicant : public BnSupplicant
{
public:
	Supplicant(struct wpa_global* global);
	~Supplicant() override = default;
	bool isValid();

	// Aidl methods exposed.
  	::ndk::ScopedAStatus addP2pInterface(
		  const std::string& in_name,
		  std::shared_ptr<ISupplicantP2pIface>* _aidl_return) override;
	::ndk::ScopedAStatus addStaInterface(
		const std::string& in_name,
		std::shared_ptr<ISupplicantStaIface>* _aidl_return) override;
	::ndk::ScopedAStatus removeInterface(
		const IfaceInfo& in_ifaceInfo) override;
	::ndk::ScopedAStatus getP2pInterface(
		const std::string& in_name,
		std::shared_ptr<ISupplicantP2pIface>* _aidl_return) override;
	::ndk::ScopedAStatus getStaInterface(
		const std::string& in_name,
		std::shared_ptr<ISupplicantStaIface>* _aidl_return) override;
	::ndk::ScopedAStatus listInterfaces(
		std::vector<IfaceInfo>* _aidl_return) override;
	::ndk::ScopedAStatus registerCallback(
		const std::shared_ptr<ISupplicantCallback>& in_callback) override;
	::ndk::ScopedAStatus setDebugParams(
		DebugLevel in_level, bool in_showTimestamp, bool in_showKeys) override;
	::ndk::ScopedAStatus getDebugLevel(DebugLevel* _aidl_return) override;
	::ndk::ScopedAStatus isDebugShowTimestampEnabled(bool* _aidl_return) override;
	::ndk::ScopedAStatus isDebugShowKeysEnabled(bool* _aidl_return) override;
	::ndk::ScopedAStatus setConcurrencyPriority(IfaceType in_type) override;
	::ndk::ScopedAStatus terminate() override;

private:
	// Corresponding worker functions for the AIDL methods.
	std::pair<std::shared_ptr<ISupplicantP2pIface>, ndk::ScopedAStatus>
		addP2pInterfaceInternal(const std::string& name);
	std::pair<std::shared_ptr<ISupplicantStaIface>, ndk::ScopedAStatus>
		addStaInterfaceInternal(const std::string& name);
	std::pair<std::shared_ptr<ISupplicantP2pIface>, ndk::ScopedAStatus>
		getP2pInterfaceInternal(const std::string& name);
	std::pair<std::shared_ptr<ISupplicantStaIface>, ndk::ScopedAStatus>
		getStaInterfaceInternal(const std::string& name);
	
	ndk::ScopedAStatus removeInterfaceInternal(const IfaceInfo& iface_info);
	std::pair<std::vector<IfaceInfo>, ndk::ScopedAStatus> listInterfacesInternal();
	ndk::ScopedAStatus registerCallbackInternal(
		const std::shared_ptr<ISupplicantCallback>& callback);
	ndk::ScopedAStatus setDebugParamsInternal(
		DebugLevel level, bool show_timestamp, bool show_keys);
	ndk::ScopedAStatus setConcurrencyPriorityInternal(IfaceType type);
	ndk::ScopedAStatus addP2pDevInterface(struct wpa_interface iface_params);

	// Raw pointer to the global structure maintained by the core.
	struct wpa_global* wpa_global_;
	// Driver name to be used for creating interfaces.
	static const char kDriverName[];
	// wpa_supplicant.conf file location on the device.
	static const char kConfigFilePath[];

	DISALLOW_COPY_AND_ASSIGN(Supplicant);
};

}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_SUPPLICANT_H
