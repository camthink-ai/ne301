/*
 * WPA Supplicant - P2P network Aidl interface
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_P2P_NETWORK_H
#define WPA_SUPPLICANT_AIDL_P2P_NETWORK_H

#include <android-base/macros.h>

#include <aidl/android/hardware/wifi/supplicant/BnSupplicantP2pNetwork.h>

extern "C"
{
#include "config.h"
#include "utils/common.h"
#include "utils/includes.h"
#include "wpa_supplicant_i.h"
}

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {

/**
 * Implementation of P2pNetwork aidl object. Each unique aidl
 * object is used for control operations on a specific network
 * controlled by wpa_supplicant.
 */
class P2pNetwork : public BnSupplicantP2pNetwork
{
public:
	P2pNetwork(
		struct wpa_global* wpa_global, const char ifname[], int network_id);
	~P2pNetwork() override = default;
	// Refer to |StaIface::invalidate()|.
	void invalidate();
	bool isValid();

	// Aidl methods exposed.
  	::ndk::ScopedAStatus getId(int32_t* _aidl_return) override;
	::ndk::ScopedAStatus getInterfaceName(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getType(IfaceType* _aidl_return) override;
	::ndk::ScopedAStatus getSsid(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getBssid(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus isCurrent(bool* _aidl_return) override;
	::ndk::ScopedAStatus isPersistent(bool* _aidl_return) override;
	::ndk::ScopedAStatus isGroupOwner(bool* _aidl_return) override;
	::ndk::ScopedAStatus setClientList(
		const std::vector<MacAddress>& in_clients) override;
	::ndk::ScopedAStatus getClientList(
		std::vector<MacAddress>* _aidl_return) override;

private:
	// Corresponding worker functions for the AIDL methods.
	std::pair<uint32_t, ndk::ScopedAStatus> getIdInternal();
	std::pair<std::string, ndk::ScopedAStatus> getInterfaceNameInternal();
	std::pair<IfaceType, ndk::ScopedAStatus> getTypeInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getSsidInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getBssidInternal();
	std::pair<bool, ndk::ScopedAStatus> isCurrentInternal();
	std::pair<bool, ndk::ScopedAStatus> isPersistentInternal();
	std::pair<bool, ndk::ScopedAStatus> isGroupOwnerInternal();
	ndk::ScopedAStatus setClientListInternal(
		const std::vector<MacAddress>& clients);
	std::pair<std::vector<MacAddress>, ndk::ScopedAStatus>
		getClientListInternal();

	struct wpa_ssid* retrieveNetworkPtr();
	struct wpa_supplicant* retrieveIfacePtr();

	// Reference to the global wpa_struct. This is assumed to be valid
	// for the lifetime of the process.
	const struct wpa_global* wpa_global_;
	// Name of the iface this network belongs to.
	const std::string ifname_;
	// Id of the network this aidl object controls.
	const int network_id_;
	bool is_valid_;

	DISALLOW_COPY_AND_ASSIGN(P2pNetwork);
};

}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_P2P_NETWORK_H
