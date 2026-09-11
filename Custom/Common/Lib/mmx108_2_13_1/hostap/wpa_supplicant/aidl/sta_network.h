/*
 * WPA Supplicant - Sta network Aidl interface
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_STA_NETWORK_H
#define WPA_SUPPLICANT_AIDL_STA_NETWORK_H

#include <array>
#include <vector>

#include <android-base/macros.h>

#include <aidl/android/hardware/wifi/supplicant/BnSupplicantStaNetwork.h>
#include <aidl/android/hardware/wifi/supplicant/EapMethod.h>
#include <aidl/android/hardware/wifi/supplicant/EapPhase2Method.h>
#include <aidl/android/hardware/wifi/supplicant/ISupplicantStaNetworkCallback.h>
#include <aidl/android/hardware/wifi/supplicant/NetworkRequestEapSimUmtsAuthParams.h>
#include <aidl/android/hardware/wifi/supplicant/NetworkResponseEapSimUmtsAuthParams.h>
#include <aidl/android/hardware/wifi/supplicant/SaeH2eMode.h>
#include <aidl/android/hardware/wifi/supplicant/DppConnectionKeys.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "notify.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "eap_peer/eap.h"
#include "rsn_supp/wpa.h"
}

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {

/**
 * Implementation of StaNetwork aidl object. Each unique aidl
 * object is used for control operations on a specific network
 * controlled by wpa_supplicant.
 */
class StaNetwork : public BnSupplicantStaNetwork
{
public:
	StaNetwork(
		struct wpa_global* wpa_global, const char ifname[], int network_id);
	~StaNetwork() override = default;
	// Refer to |StaIface::invalidate()|.
	void invalidate();
	bool isValid();

	// Aidl methods exposed.
  	::ndk::ScopedAStatus getId(int32_t* _aidl_return) override;
	::ndk::ScopedAStatus getInterfaceName(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getType(IfaceType* _aidl_return) override;
	::ndk::ScopedAStatus registerCallback(
		const std::shared_ptr<ISupplicantStaNetworkCallback>& in_callback) override;
	::ndk::ScopedAStatus setSsid(const std::vector<uint8_t>& in_ssid) override;
	::ndk::ScopedAStatus setBssid(const std::vector<uint8_t>& in_bssid) override;
	::ndk::ScopedAStatus setDppKeys(const DppConnectionKeys& in_keys) override;
	::ndk::ScopedAStatus setScanSsid(bool in_enable) override;
	::ndk::ScopedAStatus setKeyMgmt(KeyMgmtMask in_keyMgmtMask) override;
	::ndk::ScopedAStatus setProto(ProtoMask in_protoMask) override;
	::ndk::ScopedAStatus setAuthAlg(AuthAlgMask in_authAlgMask) override;
	::ndk::ScopedAStatus setGroupCipher(GroupCipherMask in_groupCipherMask) override;
	::ndk::ScopedAStatus setPairwiseCipher(
		PairwiseCipherMask in_pairwiseCipherMask) override;
	::ndk::ScopedAStatus setPskPassphrase(const std::string& in_psk) override;
	::ndk::ScopedAStatus setPsk(const std::vector<uint8_t>& in_psk) override;
	::ndk::ScopedAStatus setWepKey(
		int32_t in_keyIdx, const std::vector<uint8_t>& in_wepKey) override;
	::ndk::ScopedAStatus setWepTxKeyIdx(int32_t in_keyIdx) override;
	::ndk::ScopedAStatus setRequirePmf(bool in_enable) override;
	::ndk::ScopedAStatus setEapMethod(EapMethod in_method) override;
	::ndk::ScopedAStatus setEapPhase2Method(EapPhase2Method in_method) override;
	::ndk::ScopedAStatus setEapIdentity(
		const std::vector<uint8_t>& in_identity) override;
	::ndk::ScopedAStatus setEapEncryptedImsiIdentity(
		const std::vector<uint8_t>& in_identity) override;
	::ndk::ScopedAStatus setEapAnonymousIdentity(
		const std::vector<uint8_t>& in_identity) override;
	::ndk::ScopedAStatus setEapPassword(
		const std::vector<uint8_t>& in_password) override;
	::ndk::ScopedAStatus setEapCACert(const std::string& in_path) override;
	::ndk::ScopedAStatus setEapCAPath(const std::string& in_path) override;
	::ndk::ScopedAStatus setEapClientCert(const std::string& in_path) override;
	::ndk::ScopedAStatus setEapPrivateKeyId(const std::string& in_id) override;
	::ndk::ScopedAStatus setEapSubjectMatch(const std::string& in_match) override;
	::ndk::ScopedAStatus setEapAltSubjectMatch(const std::string& in_match) override;
	::ndk::ScopedAStatus setEapEngine(bool in_enable) override;
	::ndk::ScopedAStatus setEapEngineID(const std::string& in_id) override;
	::ndk::ScopedAStatus setEapDomainSuffixMatch(
		const std::string& in_match) override;
	::ndk::ScopedAStatus setProactiveKeyCaching(bool in_enable) override;
	::ndk::ScopedAStatus setIdStr(const std::string& in_idStr) override;
	::ndk::ScopedAStatus setUpdateIdentifier(int32_t in_id) override;
	::ndk::ScopedAStatus setEdmg(bool in_enable) override;
	::ndk::ScopedAStatus getSsid(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getBssid(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getScanSsid(bool* _aidl_return) override;
	::ndk::ScopedAStatus getKeyMgmt(KeyMgmtMask* _aidl_return) override;
	::ndk::ScopedAStatus getProto(ProtoMask* _aidl_return) override;
	::ndk::ScopedAStatus getAuthAlg(AuthAlgMask* _aidl_return) override;
	::ndk::ScopedAStatus getGroupCipher(GroupCipherMask* _aidl_return) override;
	::ndk::ScopedAStatus getPairwiseCipher(PairwiseCipherMask* _aidl_return) override;
	::ndk::ScopedAStatus getPskPassphrase(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getPsk(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getSaePassword(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getSaePasswordId(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getWepKey(
		int32_t in_keyIdx, std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getWepTxKeyIdx(int32_t* _aidl_return) override;
	::ndk::ScopedAStatus getRequirePmf(bool* _aidl_return) override;
	::ndk::ScopedAStatus getEapMethod(EapMethod* _aidl_return) override;
	::ndk::ScopedAStatus getEapPhase2Method(EapPhase2Method* _aidl_return) override;
	::ndk::ScopedAStatus getEapIdentity(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getEapAnonymousIdentity(
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getEapPassword(std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getEapCACert(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapCAPath(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapClientCert(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapPrivateKeyId(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapSubjectMatch(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapAltSubjectMatch(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapEngine(bool* _aidl_return) override;
	::ndk::ScopedAStatus getEapEngineId(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getEapDomainSuffixMatch(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getIdStr(std::string* _aidl_return) override;
	::ndk::ScopedAStatus getWpsNfcConfigurationToken(
		std::vector<uint8_t>* _aidl_return) override;
	::ndk::ScopedAStatus getEdmg(bool* _aidl_return) override;
	::ndk::ScopedAStatus enable(bool in_noConnect) override;
	::ndk::ScopedAStatus disable() override;
	::ndk::ScopedAStatus select() override;
	::ndk::ScopedAStatus sendNetworkEapSimGsmAuthResponse(
		const std::vector<NetworkResponseEapSimGsmAuthParams>& in_params) override;
	::ndk::ScopedAStatus sendNetworkEapSimGsmAuthFailure() override;
	::ndk::ScopedAStatus sendNetworkEapSimUmtsAuthResponse(
		const NetworkResponseEapSimUmtsAuthParams& in_params) override;
	::ndk::ScopedAStatus sendNetworkEapSimUmtsAutsResponse(
		const std::vector<uint8_t>& in_auts) override;
	::ndk::ScopedAStatus sendNetworkEapSimUmtsAuthFailure() override;
	::ndk::ScopedAStatus sendNetworkEapIdentityResponse(
		const std::vector<uint8_t>& in_identity,
		const std::vector<uint8_t>& in_encryptedIdentity) override;
	::ndk::ScopedAStatus setGroupMgmtCipher(
		GroupMgmtCipherMask in_groupMgmtCipherMask) override;
	::ndk::ScopedAStatus getGroupMgmtCipher(
		GroupMgmtCipherMask* _aidl_return) override;
	::ndk::ScopedAStatus enableTlsSuiteBEapPhase1Param(
		bool in_enable) override;
	::ndk::ScopedAStatus enableSuiteBEapOpenSslCiphers() override;
	::ndk::ScopedAStatus setSaePassword(
		const std::string& in_saePassword) override;
	::ndk::ScopedAStatus setSaePasswordId(
		const std::string& in_saePasswordId) override;
	::ndk::ScopedAStatus setOcsp(OcspType in_ocspType) override;
	::ndk::ScopedAStatus getOcsp(OcspType* _aidl_return) override;
	::ndk::ScopedAStatus setPmkCache(
		const std::vector<uint8_t>& in_serializedEntry) override;
	::ndk::ScopedAStatus setWapiCertSuite(const std::string& in_suite) override;
	::ndk::ScopedAStatus getWapiCertSuite(std::string* _aidl_return) override;
	::ndk::ScopedAStatus setEapErp(bool in_enable) override;
	::ndk::ScopedAStatus setSaeH2eMode(SaeH2eMode in_mode) override;
	::ndk::ScopedAStatus enableSaePkOnlyMode(bool in_enable) override;
	::ndk::ScopedAStatus setRoamingConsortiumSelection(
		const std::vector<uint8_t>& in_selectedRcoi) override;

private:
	// Corresponding worker functions for the AIDL methods.
	std::pair<uint32_t, ndk::ScopedAStatus> getIdInternal();
	std::pair<std::string, ndk::ScopedAStatus> getInterfaceNameInternal();
	std::pair<IfaceType, ndk::ScopedAStatus> getTypeInternal();
	ndk::ScopedAStatus registerCallbackInternal(
		const std::shared_ptr<ISupplicantStaNetworkCallback>& callback);
	ndk::ScopedAStatus setSsidInternal(const std::vector<uint8_t>& ssid);
	ndk::ScopedAStatus setBssidInternal(const std::vector<uint8_t>& bssid);
	ndk::ScopedAStatus setDppKeysInternal(const DppConnectionKeys& keys);
	ndk::ScopedAStatus setScanSsidInternal(bool enable);
	ndk::ScopedAStatus setKeyMgmtInternal(
		KeyMgmtMask mask);
	ndk::ScopedAStatus setProtoInternal(
		ProtoMask mask);
	ndk::ScopedAStatus setAuthAlgInternal(
		AuthAlgMask mask);
	ndk::ScopedAStatus setGroupCipherInternal(
		GroupCipherMask mask);
	ndk::ScopedAStatus setPairwiseCipherInternal(
		PairwiseCipherMask mask);
	ndk::ScopedAStatus setPskPassphraseInternal(const std::string& psk);
	ndk::ScopedAStatus setPskInternal(const std::vector<uint8_t>& psk);
	ndk::ScopedAStatus setWepKeyInternal(
		uint32_t key_idx, const std::vector<uint8_t>& wep_key);
	ndk::ScopedAStatus setWepTxKeyIdxInternal(uint32_t key_idx);
	ndk::ScopedAStatus setRequirePmfInternal(bool enable);
	ndk::ScopedAStatus setEapMethodInternal(
		EapMethod method);
	ndk::ScopedAStatus setEapPhase2MethodInternal(
		EapPhase2Method method);
	ndk::ScopedAStatus setEapIdentityInternal(
		const std::vector<uint8_t>& identity);
	ndk::ScopedAStatus setEapEncryptedImsiIdentityInternal(
		const std::vector<uint8_t>& identity);
	ndk::ScopedAStatus setEapAnonymousIdentityInternal(
		const std::vector<uint8_t>& identity);
	ndk::ScopedAStatus setEapPasswordInternal(
		const std::vector<uint8_t>& password);
	ndk::ScopedAStatus setEapCACertInternal(const std::string& path);
	ndk::ScopedAStatus setEapCAPathInternal(const std::string& path);
	ndk::ScopedAStatus setEapClientCertInternal(const std::string& path);
	ndk::ScopedAStatus setEapPrivateKeyIdInternal(const std::string& id);
	ndk::ScopedAStatus setEapSubjectMatchInternal(const std::string& match);
	ndk::ScopedAStatus setEapAltSubjectMatchInternal(
		const std::string& match);
	ndk::ScopedAStatus setEapEngineInternal(bool enable);
	ndk::ScopedAStatus setEapEngineIDInternal(const std::string& id);
	ndk::ScopedAStatus setEapDomainSuffixMatchInternal(
		const std::string& match);
	ndk::ScopedAStatus setProactiveKeyCachingInternal(bool enable);
	ndk::ScopedAStatus setIdStrInternal(const std::string& id_str);
	ndk::ScopedAStatus setUpdateIdentifierInternal(uint32_t id);
	ndk::ScopedAStatus setEdmgInternal(bool enable);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getSsidInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getBssidInternal();
	std::pair<bool, ndk::ScopedAStatus> getScanSsidInternal();
	std::pair<KeyMgmtMask, ndk::ScopedAStatus> getKeyMgmtInternal();
	std::pair<ProtoMask, ndk::ScopedAStatus> getProtoInternal();
	std::pair<AuthAlgMask, ndk::ScopedAStatus> getAuthAlgInternal();
	std::pair<GroupCipherMask, ndk::ScopedAStatus> getGroupCipherInternal();
	std::pair<PairwiseCipherMask, ndk::ScopedAStatus> getPairwiseCipherInternal();
	std::pair<std::string, ndk::ScopedAStatus> getPskPassphraseInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getPskInternal();
	std::pair<std::string, ndk::ScopedAStatus> getSaePasswordInternal();
	std::pair<std::string, ndk::ScopedAStatus> getSaePasswordIdInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getWepKeyInternal(
		uint32_t key_idx);
	std::pair<uint32_t, ndk::ScopedAStatus> getWepTxKeyIdxInternal();
	std::pair<bool, ndk::ScopedAStatus> getRequirePmfInternal();
	std::pair<EapMethod, ndk::ScopedAStatus> getEapMethodInternal();
	std::pair<EapPhase2Method, ndk::ScopedAStatus>
		getEapPhase2MethodInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getEapIdentityInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getEapAnonymousIdentityInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getEapPasswordInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapCACertInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapCAPathInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapClientCertInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapPrivateKeyIdInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapSubjectMatchInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapAltSubjectMatchInternal();
	std::pair<bool, ndk::ScopedAStatus> getEapEngineInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapEngineIdInternal();
	std::pair<std::string, ndk::ScopedAStatus> getEapDomainSuffixMatchInternal();
	std::pair<std::string, ndk::ScopedAStatus> getIdStrInternal();
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus>
		getWpsNfcConfigurationTokenInternal();
	std::pair<bool, ndk::ScopedAStatus> getEdmgInternal();
	ndk::ScopedAStatus enableInternal(bool no_connect);
	ndk::ScopedAStatus disableInternal();
	ndk::ScopedAStatus selectInternal();
	ndk::ScopedAStatus sendNetworkEapSimGsmAuthResponseInternal(
		const std::vector<NetworkResponseEapSimGsmAuthParams>&
		vec_params);
	ndk::ScopedAStatus sendNetworkEapSimGsmAuthFailureInternal();
	ndk::ScopedAStatus sendNetworkEapSimUmtsAuthResponseInternal(
		const NetworkResponseEapSimUmtsAuthParams& params);
	ndk::ScopedAStatus sendNetworkEapSimUmtsAutsResponseInternal(
		const std::vector<uint8_t>& auts);
	ndk::ScopedAStatus sendNetworkEapSimUmtsAuthFailureInternal();
	ndk::ScopedAStatus sendNetworkEapIdentityResponseInternal(
		const std::vector<uint8_t>& identity,
		const std::vector<uint8_t>& imsi_identity);
	ndk::ScopedAStatus enableTlsSuiteBEapPhase1ParamInternal(bool enable);
	ndk::ScopedAStatus enableSuiteBEapOpenSslCiphersInternal();
	ndk::ScopedAStatus setSaePasswordInternal(
		const std::string& sae_password);
	ndk::ScopedAStatus setSaePasswordIdInternal(
		const std::string& sae_password_id);
	ndk::ScopedAStatus setGroupMgmtCipherInternal(
		GroupMgmtCipherMask mask);
	std::pair<GroupMgmtCipherMask, ndk::ScopedAStatus>
		getGroupMgmtCipherInternal();
	ndk::ScopedAStatus setOcspInternal(OcspType ocspType);
	std::pair<OcspType, ndk::ScopedAStatus> getOcspInternal();
	ndk::ScopedAStatus setPmkCacheInternal(const std::vector<uint8_t>& serializedEntry);
	ndk::ScopedAStatus setWapiCertSuiteInternal(const std::string& suite);
	std::pair<std::string, ndk::ScopedAStatus> getWapiCertSuiteInternal();
	ndk::ScopedAStatus setWapiPskInternal(const std::vector<uint8_t>& psk);
	std::pair<std::vector<uint8_t>, ndk::ScopedAStatus> getWapiPskInternal();
	ndk::ScopedAStatus setSaeH2eModeInternal(SaeH2eMode mode);
	ndk::ScopedAStatus enableSaePkOnlyModeInternal(bool enable);
	ndk::ScopedAStatus setRoamingConsortiumSelectionInternal(
		const std::vector<uint8_t>& selectedRcoi);

	struct wpa_ssid* retrieveNetworkPtr();
	struct wpa_supplicant* retrieveIfacePtr();
	int isPskPassphraseValid(const std::string& psk);
	void resetInternalStateAfterParamsUpdate();
	int setStringFieldAndResetState(
		const char* value, uint8_t** to_update_field,
		const char* hexdump_prefix);
	int setStringFieldAndResetState(
		const char* value, char** to_update_field,
		const char* hexdump_prefix);
	int setStringKeyFieldAndResetState(
		const char* value, char** to_update_field,
		const char* hexdump_prefix);
	int setByteArrayFieldAndResetState(
		const uint8_t* value, const size_t value_len,
		uint8_t** to_update_field, size_t* to_update_field_len,
		const char* hexdump_prefix);
	int setByteArrayKeyFieldAndResetState(
		const uint8_t* value, const size_t value_len,
		uint8_t** to_update_field, size_t* to_update_field_len,
		const char* hexdump_prefix);
	void setFastTransitionKeyMgmt(uint32_t &key_mgmt_mask);
	void resetFastTransitionKeyMgmt(uint32_t &key_mgmt_mask);
	ndk::ScopedAStatus setEapErpInternal(bool enable);
	int setByteArrayField(
		const uint8_t* value, const size_t value_len,
		uint8_t** to_update_field, size_t* to_update_field_len,
		const char* hexdump_prefix, bool resetState);

	// Reference to the global wpa_struct. This is assumed to be valid
	// for the lifetime of the process.
	struct wpa_global* wpa_global_;
	// Name of the iface this network belongs to.
	const std::string ifname_;
	// Id of the network this aidl object controls.
	const int network_id_;
	bool is_valid_;

	DISALLOW_COPY_AND_ASSIGN(StaNetwork);
};

}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // WPA_SUPPLICANT_AIDL_STA_NETWORK_H
