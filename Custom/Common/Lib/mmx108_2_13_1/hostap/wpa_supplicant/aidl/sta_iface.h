/*
 * WPA Supplicant - Sta Iface Aidl interface
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_STA_IFACE_H
#define WPA_SUPPLICANT_AIDL_STA_IFACE_H

#include <array>
#include <vector>

#include <android-base/macros.h>

#include <aidl/android/hardware/wifi/supplicant/AnqpInfoId.h>
#include <aidl/android/hardware/wifi/supplicant/BnSupplicantStaIface.h>
#include <aidl/android/hardware/wifi/supplicant/BtCoexistenceMode.h>
#include <aidl/android/hardware/wifi/supplicant/Hs20AnqpSubtypes.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantStaIfaceCallback.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantStaNetwork.h>
#include <aidl/android/hardware/wifi/supplicant/MloLinksInfo.h>
#include <aidl/android/hardware/wifi/supplicant/QosPolicyStatus.h>
#include <aidl/android/hardware/wifi/supplicant/RxFilterType.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "wpa_supplicant_i.h"
#include "config.h"
#include "driver_i.h"
#include "wpa.h"
}

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {

/**
 * Implementation of StaIface aidl object. Each unique aidl
 * object is used for control operations on a specific interface
 * controlled by wpa_supplicant.
 */
class StaIface : public BnSupplicantStaIface
{
public:
	StaIface(struct wpa_global* wpa_global, const char ifname[]);
	~StaIface() override = default;
	// AIDL does not provide a built-in mechanism to let the server
	// invalidate a AIDL interface object after creation. If any client
	// process holds onto a reference to the object in their context,
	// any method calls on that reference will continue to be directed to
	// the server.
	// However Supplicant HAL needs to control the lifetime of these
	// objects. So, add a public |invalidate| method to all |Iface| and
	// |Network| objects.
	// This will be used to mark an object invalid when the corresponding
	// iface or network is removed.
	// All AIDL method implementations should check if the object is still
	// marked valid before processing them.
	void invalidate();
	bool isValid();

	// Aidl methods exposed.
  	::ndk::ScopedAStatus getName(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getType(IfaceType* _aidl_return) override;
	::ndk::ScopedAStatus addNetwork(
		std::shared_ptr<ISupplicantStaNetwork>* _aidl_return) override;
	::ndk::ScopedAStatus removeNetwork(int32_t in_id) override;
	::ndk::ScopedAStatus filsHlpFlushRequest() override;
	::ndk::ScopedAStatus filsHlpAddRequest(
		const std::vector<uint8_t>& in_dst_mac,
		const std::vector<uint8_t>& in_pkt) override;
	::ndk::ScopedAStatus getNetwork(
		int32_t in_id, std::shared_ptr<ISupplicantStaNetwork>* _aidl_return) override;
	::ndk::ScopedAStatus listNetworks(std::vector<int32_t>* _aidl_return) override;
	::ndk::ScopedAStatus registerCallback(
		const std::shared_ptr<ISupplicantStaIfaceCallback>& in_callback) override;
	::ndk::ScopedAStatus reassociate() override;
	::ndk::ScopedAStatus reconnect() override;
	::ndk::ScopedAStatus disconnect() override;
	::ndk::ScopedAStatus setPowerSave(bool in_enable) override;
	::ndk::ScopedAStatus initiateTdlsDiscover(
		const std::vector<uint8_t>& in_macAddress) override;
	::ndk::ScopedAStatus initiateTdlsSetup(
		const std::vector<uint8_t>& in_macAddress) override;
	::ndk::ScopedAStatus initiateTdlsTeardown(
		const std::vector<uint8_t>& in_macAddress) override;
	::ndk::ScopedAStatus initiateAnqpQuery(
		const std::vector<uint8_t>& in_macAddress,
		const std::vector<AnqpInfoId>& in_infoElements,
		const std::vector<Hs20AnqpSubtypes>& in_subTypes) override;
	::ndk::ScopedAStatus initiateVenueUrlAnqpQuery(
		const std::vector<uint8_t>& in_macAddress) override;
	::ndk::ScopedAStatus initiateHs20IconQuery(
		const std::vector<uint8_t>& in_macAddress, const std::string& in_fileName) override;
	::ndk::ScopedAStatus getMacAddress(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus startRxFilter() override;
	::ndk::ScopedAStatus stopRxFilter() override;
	::ndk::ScopedAStatus addRxFilter(RxFilterType in_type) override;
	::ndk::ScopedAStatus removeRxFilter(RxFilterType in_type) override;
	::ndk::ScopedAStatus setBtCoexistenceMode(BtCoexistenceMode in_mode) override;
	::ndk::ScopedAStatus setBtCoexistenceScanModeEnabled(bool in_enable) override;
	::ndk::ScopedAStatus setSuspendModeEnabled(bool in_enable) override;
	::ndk::ScopedAStatus setCountryCode(const std::vector<uint8_t>& in_code) override;
	::ndk::ScopedAStatus startWpsRegistrar(
		const std::vector<uint8_t>& in_bssid, const std::string& in_pin) override;
	::ndk::ScopedAStatus startWpsPbc(const std::vector<uint8_t>& in_bssid) override;
	::ndk::ScopedAStatus startWpsPinDisplay(
		const std::vector<uint8_t>& in_bssid, std::string* _aidl_return) override;
	::ndk::ScopedAStatus startWpsPinKeypad(const std::string& in_pin) override;
	::ndk::ScopedAStatus cancelWps() override;
	::ndk::ScopedAStatus setWpsDeviceName(const std::string& in_name) override;
	::ndk::ScopedAStatus setWpsDeviceType(const std::vector<uint8_t>& in_type) override;
	::ndk::ScopedAStatus setWpsManufacturer(const std::string& in_manufacturer) override;
	::ndk::ScopedAStatus setWpsModelName(const std::string& in_modelName) override;
	::ndk::ScopedAStatus setWpsModelNumber(const std::string& in_modelNumber) override;
	::ndk::ScopedAStatus setWpsSerialNumber(const std::string& in_serialNumber) override;
	::ndk::ScopedAStatus setWpsConfigMethods(WpsConfigMethods in_configMethods) override;
	::ndk::ScopedAStatus setExternalSim(bool in_useExternalSim) override;
	::ndk::ScopedAStatus addExtRadioWork(
		const std::string& in_name, int32_t in_freqInMhz,
		int32_t in_timeoutInSec, int32_t* _aidl_return) override;
	::ndk::ScopedAStatus removeExtRadioWork(int32_t in_id) override;
	::ndk::ScopedAStatus enableAutoReconnect(bool in_enable) override;
	::ndk::ScopedAStatus getKeyMgmtCapabilities(KeyMgmtMask* _aidl_return) override;
	::ndk::ScopedAStatus addDppPeerUri(
		const std::string& in_uri, int32_t* _aidl_return) override;
	::ndk::ScopedAStatus removeDppUri(int32_t in_id) override;
	::ndk::ScopedAStatus startDppConfiguratorInitiator(
		int32_t in_peerBootstrapId, int32_t in_ownBootstrapId,
		const std::string& in_ssid, const std::string& in_password,
		const std::string& in_psk, DppNetRole in_netRole, DppAkm in_securityAkm,
		const std::vector<uint8_t>& in_privEcKey,
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus startDppEnrolleeInitiator(
		int32_t in_peerBootstrapId, int32_t in_ownBootstrapId) override;
	::ndk::ScopedAStatus stopDppInitiator() override;
	::ndk::ScopedAStatus getConnectionCapabilities(ConnectionCapabilities* _aidl_return) override;
	::ndk::ScopedAStatus getWpaDriverCapabilities(WpaDriverCapabilitiesMask* _aidl_return) override;
	::ndk::ScopedAStatus setMboCellularDataStatus(bool in_available) override;
	::ndk::ScopedAStatus generateDppBootstrapInfoForResponder(
		const std::vector<uint8_t>& in_macAddress,
		const std::string& in_deviceInfo, DppCurve in_curve,
		DppResponderBootstrapInfo* _aidl_return) override;
	::ndk::ScopedAStatus startDppEnrolleeResponder(int32_t in_listenChannel) override;
	::ndk::ScopedAStatus stopDppResponder(int32_t in_ownBootstrapId) override;
	::ndk::ScopedAStatus generateSelfDppConfiguration(
		const std::string& in_ssid, const std::vector<uint8_t>& in_privEcKey) override;
	::ndk::ScopedAStatus setQosPolicyFeatureEnabled(bool in_enable) override;
	::ndk::ScopedAStatus sendQosPolicyResponse(
		int32_t in_qosPolicyRequestId, bool in_morePolicies,
		const std::vector<QosPolicyStatus>& in_qosPolicyStatusList) override;
	::ndk::ScopedAStatus removeAllQosPolicies() override;
	::ndk::ScopedAStatus getConnectionMloLinksInfo(MloLinksInfo* _aidl_return) override;

private:
	// Corresponding worker functions for the AIDL methods.
	std::pair<std::string, ndk::ScopedAStatus> getNameInternal();
	std::pair<IfaceType, ndk::ScopedAStatus> getTypeInternal();
	std::pair<std::shared_ptr<ISupplicantStaNetwork>, ndk::ScopedAStatus>
		addNetworkInternal();
	ndk::ScopedAStatus filsHlpFlushRequestInternal();
	ndk::ScopedAStatus filsHlpAddRequestInternal(
		const std::vector<uint8_t>& dst_mac,
		const std::vector<uint8_t>& pkt);
	ndk::ScopedAStatus removeNetworkInternal(int32_t id);
	std::pair<std::shared_ptr<ISupplicantStaNetwork>, ndk::ScopedAStatus>
		getNetworkInternal(int32_t id);
	std::pair<std::vector<int32_t>, ndk::ScopedAStatus>
		listNetworksInternal();
	ndk::ScopedAStatus registerCallbackInternal(
		const std::shared_ptr<ISupplicantStaIfaceCallback>& callback);
	ndk::ScopedAStatus reassociateInternal();
	ndk::ScopedAStatus reconnectInternal();
	ndk::ScopedAStatus disconnectInternal();
	ndk::ScopedAStatus setPowerSaveInternal(bool enable);
	ndk::ScopedAStatus initiateTdlsDiscoverInternal(
		const std::vector<uint8_t>& mac_address);
	ndk::ScopedAStatus initiateTdlsSetupInternal(
		const std::vector<uint8_t>& mac_address);
	ndk::ScopedAStatus initiateTdlsTeardownInternal(
		const std::vector<uint8_t>& mac_address);
	ndk::ScopedAStatus initiateAnqpQueryInternal(
		const std::vector<uint8_t>& mac_address,
		const std::vector<AnqpInfoId>& info_elements,
		const std::vector<Hs20AnqpSubtypes>&
		sub_types);
	ndk::ScopedAStatus initiateVenueUrlAnqpQueryInternal(
		const std::vector<uint8_t>& mac_address);
	ndk::ScopedAStatus initiateHs20IconQueryInternal(
		const std::vector<uint8_t>& mac_address,
		const std::string& file_name);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getMacAddressInternal();
	ndk::ScopedAStatus startRxFilterInternal();
	ndk::ScopedAStatus stopRxFilterInternal();
	ndk::ScopedAStatus addRxFilterInternal(
		RxFilterType type);
	ndk::ScopedAStatus removeRxFilterInternal(
		RxFilterType type);
	ndk::ScopedAStatus setBtCoexistenceModeInternal(
		BtCoexistenceMode mode);
	ndk::ScopedAStatus setBtCoexistenceScanModeEnabledInternal(bool enable);
	ndk::ScopedAStatus setSuspendModeEnabledInternal(bool enable);
	ndk::ScopedAStatus setCountryCodeInternal(
		const std::vector<uint8_t>& code);
	ndk::ScopedAStatus startWpsRegistrarInternal(
		const std::vector<uint8_t>& bssid, const std::string& pin);
	ndk::ScopedAStatus startWpsPbcInternal(
		const std::vector<uint8_t>& bssid);
	ndk::ScopedAStatus startWpsPinKeypadInternal(const std::string& pin);
	std::pair<std::string, ndk::ScopedAStatus> startWpsPinDisplayInternal(
		const std::vector<uint8_t>& bssid);
	ndk::ScopedAStatus cancelWpsInternal();
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
	ndk::ScopedAStatus setExternalSimInternal(bool useExternalSim);
	std::pair<uint32_t, ndk::ScopedAStatus> addExtRadioWorkInternal(
		const std::string& name, uint32_t freq_in_mhz,
		uint32_t timeout_in_sec);
	ndk::ScopedAStatus removeExtRadioWorkInternal(uint32_t id);
	ndk::ScopedAStatus enableAutoReconnectInternal(bool enable);
	std::pair<KeyMgmtMask, ndk::ScopedAStatus> getKeyMgmtCapabilitiesInternal();
	std::pair<uint32_t, ndk::ScopedAStatus> addDppPeerUriInternal(const std::string& uri);
	ndk::ScopedAStatus removeDppUriInternal(uint32_t bootstrap_id);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> startDppConfiguratorInitiatorInternal(
		uint32_t peer_bootstrap_id, uint32_t own_bootstrap_id, const std::string& ssid,
		const std::string& password, const std::string& psk, DppNetRole net_role,
		DppAkm security_akm, const std::vector<uint8_t> &privEcKey);
	ndk::ScopedAStatus startDppEnrolleeInitiatorInternal(uint32_t peer_bootstrap_id,
			uint32_t own_bootstrap_id);
	ndk::ScopedAStatus stopDppInitiatorInternal();
	std::pair<ConnectionCapabilities, ndk::ScopedAStatus> getConnectionCapabilitiesInternal();
	std::pair<WpaDriverCapabilitiesMask, ndk::ScopedAStatus> getWpaDriverCapabilitiesInternal();
	ndk::ScopedAStatus setMboCellularDataStatusInternal(bool available);
	std::pair<DppResponderBootstrapInfo, ndk::ScopedAStatus>
			generateDppBootstrapInfoForResponderInternal(
			const std::vector<uint8_t>& mac_address, const std::string& device_info,
			DppCurve curve);
	ndk::ScopedAStatus startDppEnrolleeResponderInternal(uint32_t listen_channel);
	ndk::ScopedAStatus stopDppResponderInternal(uint32_t own_bootstrap_id);
	ndk::ScopedAStatus generateSelfDppConfigurationInternal(
		const std::string& ssid, const std::vector<uint8_t> &privEcKey);
	ndk::ScopedAStatus setQosPolicyFeatureEnabledInternal(bool enable);
	ndk::ScopedAStatus test_aidl(struct wpa_supplicant *wpa_s);
	ndk::ScopedAStatus sendQosPolicyResponseInternal(
		int32_t qos_policy_request_id, bool more_policies,
		const std::vector<QosPolicyStatus>& qos_policy_status_list);
	ndk::ScopedAStatus removeAllQosPoliciesInternal();
	std::pair<MloLinksInfo, ndk::ScopedAStatus> getConnectionMloLinksInfoInternal();
	struct wpa_supplicant* retrieveIfacePtr();

	// Reference to the global wpa_struct. This is assumed to be valid for
	// the lifetime of the process.
	struct wpa_global* wpa_global_;
	// Name of the iface this aidl object controls
	const std::string ifname_;
	bool is_valid_;

	DISALLOW_COPY_AND_ASSIGN(StaIface);
};

}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_STA_IFACE_H
