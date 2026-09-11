/*
 * WPA Supplicant - P2P Iface Aidl interface
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_P2P_IFACE_H
#define WPA_SUPPLICANT_AIDL_P2P_IFACE_H

#include <array>
#include <vector>

#include <android-base/macros.h>

#include <aidl/android/hardware/wifi/supplicant/BnSupplicantP2pIface.h>
#include <aidl/android/hardware/wifi/supplicant/FreqRange.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantP2pIfaceCallback.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantP2pNetwork.h>
#include <aidl/android/hardware/wifi/supplicant/MiracastMode.h>
#include <aidl/android/hardware/wifi/supplicant/WpsProvisionMethod.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "p2p/p2p.h"
#include "p2p/p2p_i.h"
#include "p2p_supplicant.h"
#include "p2p_supplicant.h"
#include "config.h"
}

#define P2P_MGMT_DEVICE_PREFIX	   "p2p-dev-"

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {

/**
 * Implementation of P2pIface aidl object. Each unique aidl
 * object is used for control operations on a specific interface
 * controlled by wpa_supplicant.
 */
class P2pIface : public BnSupplicantP2pIface
{
public:
	P2pIface(struct wpa_global* wpa_global, const char ifname[]);
	~P2pIface() override = default;
	// Refer to |StaIface::invalidate()|.
	void invalidate();
	bool isValid();

	// Aidl methods exposed.
	::ndk::ScopedAStatus getName(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getType(IfaceType* _aidl_return) override;
	::ndk::ScopedAStatus addNetwork(
		std::shared_ptr<ISupplicantP2pNetwork>* _aidl_return) override;
	::ndk::ScopedAStatus removeNetwork(int32_t in_id) override;
	::ndk::ScopedAStatus getNetwork(
		int32_t in_id, std::shared_ptr<ISupplicantP2pNetwork>* _aidl_return) override;
	::ndk::ScopedAStatus listNetworks(std::vector<int32_t>* _aidl_return) override;
	::ndk::ScopedAStatus registerCallback(
		const std::shared_ptr<ISupplicantP2pIfaceCallback>& in_callback) override;
	::ndk::ScopedAStatus getDeviceAddress(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus setSsidPostfix(const std::vector<uint8_t>& in_postfix) override;
	::ndk::ScopedAStatus setGroupIdle(
		const std::string& in_groupIfName, int32_t in_timeoutInSec) override;
	::ndk::ScopedAStatus setPowerSave(
		const std::string& in_groupIfName, bool in_enable) override;
	::ndk::ScopedAStatus find(int32_t in_timeoutInSec) override;
	::ndk::ScopedAStatus stopFind() override;
	::ndk::ScopedAStatus flush() override;
	::ndk::ScopedAStatus connect(
		const std::vector<uint8_t>& in_peerAddress, WpsProvisionMethod in_provisionMethod,
		const std::string& in_preSelectedPin, bool in_joinExistingGroup,
		bool in_persistent, int32_t in_goIntent, std::string* _aidl_return) override;
	::ndk::ScopedAStatus cancelConnect() override;
	::ndk::ScopedAStatus provisionDiscovery(
		const std::vector<uint8_t>& in_peerAddress,
		WpsProvisionMethod in_provisionMethod) override;
	::ndk::ScopedAStatus addGroup(bool in_persistent, int32_t in_persistentNetworkId) override;
	::ndk::ScopedAStatus addGroupWithConfig(
		const std::vector<uint8_t>& in_ssid, const std::string& in_pskPassphrase,
		bool in_persistent, int32_t in_freq, const std::vector<uint8_t>& in_peerAddress,
		bool in_joinExistingGroup) override;
	::ndk::ScopedAStatus removeGroup(const std::string& in_groupIfName) override;
	::ndk::ScopedAStatus reject(const std::vector<uint8_t>& in_peerAddress) override;
	::ndk::ScopedAStatus invite(
		const std::string& in_groupIfName,
		const std::vector<uint8_t>& in_goDeviceAddress,
		const std::vector<uint8_t>& in_peerAddress) override;
	::ndk::ScopedAStatus reinvoke(
		int32_t in_persistentNetworkId,
		const std::vector<uint8_t>& in_peerAddress) override;
	::ndk::ScopedAStatus configureExtListen(
		int32_t in_periodInMillis, int32_t in_intervalInMillis) override;
	::ndk::ScopedAStatus setListenChannel(
		int32_t in_channel, int32_t in_operatingClass) override;
	::ndk::ScopedAStatus setDisallowedFrequencies(
		const std::vector<FreqRange>& in_ranges) override;
	::ndk::ScopedAStatus getSsid(
		const std::vector<uint8_t>& in_peerAddress,
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getGroupCapability(
		const std::vector<uint8_t>& in_peerAddress,
		P2pGroupCapabilityMask* _aidl_return) override;
	::ndk::ScopedAStatus addBonjourService(
		const std::vector<uint8_t>& in_query,
		const std::vector<uint8_t>& in_response) override;
	::ndk::ScopedAStatus removeBonjourService(
		const std::vector<uint8_t>& in_query) override;
	::ndk::ScopedAStatus addUpnpService(
		int32_t in_version, const std::string& in_serviceName) override;
	::ndk::ScopedAStatus removeUpnpService(
		int32_t in_version, const std::string& in_serviceName) override;
	::ndk::ScopedAStatus flushServices() override;
	::ndk::ScopedAStatus requestServiceDiscovery(
		const std::vector<uint8_t>& in_peerAddress,
		const std::vector<uint8_t>& in_query, int64_t* _aidl_return) override;
	::ndk::ScopedAStatus cancelServiceDiscovery(int64_t in_identifier) override;
	::ndk::ScopedAStatus setMiracastMode(MiracastMode in_mode) override;
	::ndk::ScopedAStatus startWpsPbc(
		const std::string& in_groupIfName,
		const std::vector<uint8_t>& in_bssid) override;
	::ndk::ScopedAStatus startWpsPinKeypad(
		const std::string& in_groupIfName, const std::string& in_pin) override;
	::ndk::ScopedAStatus startWpsPinDisplay(
		const std::string& in_groupIfName,
		const std::vector<uint8_t>& in_bssid,
		std::string* _aidl_return) override;
	::ndk::ScopedAStatus cancelWps(const std::string& in_groupIfName) override;
	::ndk::ScopedAStatus setWpsDeviceName(
		const std::string& in_name) override;
	::ndk::ScopedAStatus setWpsDeviceType(
		const std::vector<uint8_t>& in_type) override;
	::ndk::ScopedAStatus setWpsManufacturer(
		const std::string& in_manufacturer) override;
	::ndk::ScopedAStatus setWpsModelName(
		const std::string& in_modelName) override;
	::ndk::ScopedAStatus setWpsModelNumber(
		const std::string& in_modelNumber) override;
	::ndk::ScopedAStatus setWpsSerialNumber(
		const std::string& in_serialNumber) override;
	::ndk::ScopedAStatus setWpsConfigMethods(
		WpsConfigMethods in_configMethods) override;
	::ndk::ScopedAStatus enableWfd(bool in_enable) override;
	::ndk::ScopedAStatus setWfdDeviceInfo(
		const std::vector<uint8_t>& in_info) override;
	::ndk::ScopedAStatus createNfcHandoverRequestMessage(
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus createNfcHandoverSelectMessage(
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus reportNfcHandoverInitiation(
		const std::vector<uint8_t>& in_select) override;
	::ndk::ScopedAStatus reportNfcHandoverResponse(
		const std::vector<uint8_t>& in_request) override;
	::ndk::ScopedAStatus saveConfig() override;
	::ndk::ScopedAStatus setMacRandomization(bool in_enable) override;
	::ndk::ScopedAStatus setEdmg(bool in_enable) override;
	::ndk::ScopedAStatus getEdmg(bool* _aidl_return) override;
	::ndk::ScopedAStatus setWfdR2DeviceInfo(
		const std::vector<uint8_t>& in_info) override;
	::ndk::ScopedAStatus removeClient(
		const std::vector<uint8_t>& peer_address, bool isLegacyClient) override;
	::ndk::ScopedAStatus findOnSocialChannels(int32_t in_timeoutInSec) override;
	::ndk::ScopedAStatus findOnSpecificFrequency(
		int32_t in_freq, int32_t in_timeoutInSec) override;
	::ndk::ScopedAStatus setVendorElements(
		P2pFrameTypeMask in_frameTypeMask,
		const std::vector<uint8_t>& in_vendorElemBytes) override;

private:
	// Corresponding worker functions for the AIDL methods.
	std::pair<std::string, ndk::ScopedAStatus> getNameInternal();
	std::pair<IfaceType, ndk::ScopedAStatus> getTypeInternal();
	std::pair<std::shared_ptr<ISupplicantP2pNetwork>, ndk::ScopedAStatus>
		addNetworkInternal();
	ndk::ScopedAStatus removeNetworkInternal(int32_t id);
	std::pair<std::shared_ptr<ISupplicantP2pNetwork>, ndk::ScopedAStatus>
		getNetworkInternal(int32_t id);
	std::pair<std::vector<int32_t>, ndk::ScopedAStatus>
		listNetworksInternal();
	ndk::ScopedAStatus registerCallbackInternal(
		const std::shared_ptr<ISupplicantP2pIfaceCallback>& callback);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getDeviceAddressInternal();
	ndk::ScopedAStatus setSsidPostfixInternal(
		const std::vector<uint8_t>& postfix);
	ndk::ScopedAStatus setGroupIdleInternal(
		const std::string& group_ifname, uint32_t timeout_in_sec);
	ndk::ScopedAStatus setPowerSaveInternal(
		const std::string& group_ifname, bool enable);
	ndk::ScopedAStatus findInternal(uint32_t timeout_in_sec);
	ndk::ScopedAStatus stopFindInternal();
	ndk::ScopedAStatus flushInternal();
	std::pair<std::string, ndk::ScopedAStatus> connectInternal(
		const std::vector<uint8_t>& peer_address,
		WpsProvisionMethod provision_method,
		const std::string& pre_selected_pin, bool join_existing_group,
		bool persistent, uint32_t go_intent);
	ndk::ScopedAStatus cancelConnectInternal();
	ndk::ScopedAStatus provisionDiscoveryInternal(
		const std::vector<uint8_t>& peer_address,
		WpsProvisionMethod provision_method);
	ndk::ScopedAStatus addGroupInternal(bool in_persistent, int32_t in_persistentNetworkId);
	ndk::ScopedAStatus addGroupWithConfigInternal(
		const std::vector<uint8_t>& ssid, const std::string& passphrase,
		bool persistent, uint32_t freq, const std::vector<uint8_t>& peer_address,
		bool joinExistingGroup);
	ndk::ScopedAStatus removeGroupInternal(const std::string& group_ifname);
	ndk::ScopedAStatus rejectInternal(
		const std::vector<uint8_t>& peer_address);
	ndk::ScopedAStatus inviteInternal(
		const std::string& group_ifname,
		const std::vector<uint8_t>& go_device_address,
		const std::vector<uint8_t>& peer_address);
	ndk::ScopedAStatus reinvokeInternal(
		int32_t persistent_network_id,
		const std::vector<uint8_t>& peer_address);
	ndk::ScopedAStatus configureExtListenInternal(
		uint32_t period_in_millis, uint32_t interval_in_millis);
	ndk::ScopedAStatus setListenChannelInternal(
		uint32_t channel, uint32_t operating_class);
	ndk::ScopedAStatus setDisallowedFrequenciesInternal(
		const std::vector<FreqRange>& ranges);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getSsidInternal(
		const std::vector<uint8_t>& peer_address);
	std::pair<P2pGroupCapabilityMask, ndk::ScopedAStatus> getGroupCapabilityInternal(
		const std::vector<uint8_t>& peer_address);
	ndk::ScopedAStatus addBonjourServiceInternal(
		const std::vector<uint8_t>& query,
		const std::vector<uint8_t>& response);
	ndk::ScopedAStatus removeBonjourServiceInternal(
		const std::vector<uint8_t>& query);
	ndk::ScopedAStatus addUpnpServiceInternal(
		uint32_t version, const std::string& service_name);
	ndk::ScopedAStatus removeUpnpServiceInternal(
		uint32_t version, const std::string& service_name);
	ndk::ScopedAStatus flushServicesInternal();
	std::pair<uint64_t, ndk::ScopedAStatus> requestServiceDiscoveryInternal(
		const std::vector<uint8_t>& peer_address,
		const std::vector<uint8_t>& query);
	ndk::ScopedAStatus cancelServiceDiscoveryInternal(uint64_t identifier);
	ndk::ScopedAStatus setMiracastModeInternal(
		MiracastMode mode);
	ndk::ScopedAStatus startWpsPbcInternal(
		const std::string& group_ifname,
		const std::vector<uint8_t>& bssid);
	ndk::ScopedAStatus startWpsPinKeypadInternal(
		const std::string& group_ifname, const std::string& pin);
	std::pair<std::string, ndk::ScopedAStatus> startWpsPinDisplayInternal(
		const std::string& group_ifname,
		const std::vector<uint8_t>& bssid);
	ndk::ScopedAStatus cancelWpsInternal(const std::string& group_ifname);
	ndk::ScopedAStatus setWpsDeviceNameInternal(const std::string& name);
	ndk::ScopedAStatus setWpsDeviceTypeInternal(
		const std::vector<uint8_t>& type);
	ndk::ScopedAStatus setWpsManufacturerInternal(
		const std::string& manufacturer);
	ndk::ScopedAStatus setWpsModelNameInternal(const std::string& model_name);
	ndk::ScopedAStatus setWpsModelNumberInternal(
		const std::string& model_number);
	ndk::ScopedAStatus setWpsSerialNumberInternal(
		const std::string& serial_number);
	ndk::ScopedAStatus setWpsConfigMethodsInternal(WpsConfigMethods config_methods);
	ndk::ScopedAStatus enableWfdInternal(bool enable);
	ndk::ScopedAStatus setWfdDeviceInfoInternal(
		const std::vector<uint8_t>& info);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		createNfcHandoverRequestMessageInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		createNfcHandoverSelectMessageInternal();
	ndk::ScopedAStatus reportNfcHandoverResponseInternal(
		const std::vector<uint8_t>& request);
	ndk::ScopedAStatus reportNfcHandoverInitiationInternal(
		const std::vector<uint8_t>& select);
	ndk::ScopedAStatus saveConfigInternal();
	ndk::ScopedAStatus setMacRandomizationInternal(bool enable);
	ndk::ScopedAStatus setEdmgInternal(bool enable);
	std::pair<bool, ndk::ScopedAStatus> getEdmgInternal();
	ndk::ScopedAStatus setWfdR2DeviceInfoInternal(
		const std::vector<uint8_t>& info);
	ndk::ScopedAStatus removeClientInternal(
		const std::vector<uint8_t>& peer_address, bool isLegacyClient);
	ndk::ScopedAStatus findOnSocialChannelsInternal(uint32_t timeout_in_sec);
	ndk::ScopedAStatus findOnSpecificFrequencyInternal(
		uint32_t freq, uint32_t timeout_in_sec);
	ndk::ScopedAStatus setVendorElementsInternal(
		P2pFrameTypeMask frameTypeMask,
		const std::vector<uint8_t>& vendorElemBytes);

	struct wpa_supplicant* retrieveIfacePtr();
	struct wpa_supplicant* retrieveGroupIfacePtr(
		const std::string& group_ifname);

	// Reference to the global wpa_struct. This is assumed to be valid for
	// the lifetime of the process.
	struct wpa_global* wpa_global_;
	// Name of the iface this aidl object controls
	const std::string ifname_;
	bool is_valid_;

	DISALLOW_COPY_AND_ASSIGN(P2pIface);
};

}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_P2P_IFACE_H
