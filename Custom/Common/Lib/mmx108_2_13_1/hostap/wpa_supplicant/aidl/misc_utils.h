/*
 * WPA Supplicant - Helper methods for Aidl
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MISC_UTILS_H_
#define MISC_UTILS_H_

#include <iostream>
#include <aidl/android/hardware/wifi/supplicant/SupplicantStatusCode.h>

extern "C"
{
#include "wpabuf.h"
}

namespace {
constexpr size_t kWpsPinNumDigits = 8;
// Custom deleter for wpabuf.
void freeWpaBuf(wpabuf *ptr) { wpabuf_free(ptr); }
}  // namespace

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace supplicant {
namespace misc_utils {
using wpabuf_unique_ptr = std::unique_ptr<wpabuf, void (*)(wpabuf *)>;

// Creates a unique_ptr for wpabuf ptr with a custom deleter.
inline wpabuf_unique_ptr createWpaBufUniquePtr(struct wpabuf *raw_ptr)
{
	return {raw_ptr, freeWpaBuf};
}

// Creates a wpabuf ptr with a custom deleter copying the data from the provided
// vector.
inline wpabuf_unique_ptr convertVectorToWpaBuf(const std::vector<uint8_t> &data)
{
	return createWpaBufUniquePtr(
		wpabuf_alloc_copy(data.data(), data.size()));
}

// Copies the provided wpabuf contents to a std::vector.
inline std::vector<uint8_t> convertWpaBufToVector(const struct wpabuf *buf)
{
	if (buf) {
		return std::vector<uint8_t>(
			wpabuf_head_u8(buf), wpabuf_head_u8(buf) + wpabuf_len(buf));
	} else {
		return std::vector<uint8_t>();
	}
}

// Returns a string holding the wps pin.
inline std::string convertWpsPinToString(int pin)
{
	char pin_str[kWpsPinNumDigits + 1];
	snprintf(pin_str, sizeof(pin_str), "%08d", pin);
	return pin_str;
}

// Wrappers to create a ScopedAStatus using a SupplicantStatusCode
inline ndk::ScopedAStatus createStatus(SupplicantStatusCode status_code) {
	return ndk::ScopedAStatus::fromServiceSpecificError(
		static_cast<int32_t>(status_code));
}

inline ndk::ScopedAStatus createStatusWithMsg(
	SupplicantStatusCode status_code, std::string msg)
{
	return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
		static_cast<int32_t>(status_code), msg.c_str());
}

// Creates an std::string from a char*, which could be null
inline std::string charBufToString(const char* buf) {
	return buf ? std::string(buf) : "";
}

inline std::stringstream& serializePmkCacheEntry(
	std::stringstream &ss, struct rsn_pmksa_cache_entry *pmksa_entry) {
	ss.write((char *) &pmksa_entry->pmk_len, sizeof(pmksa_entry->pmk_len));
	ss.write((char *) pmksa_entry->pmk, pmksa_entry->pmk_len);
	ss.write((char *) pmksa_entry->pmkid, PMKID_LEN);
	ss.write((char *) pmksa_entry->aa, ETH_ALEN);
	// Omit wpa_ssid field because the network is created on connecting to a access point.
	ss.write((char *) &pmksa_entry->akmp, sizeof(pmksa_entry->akmp));
	ss.write((char *) &pmksa_entry->reauth_time, sizeof(pmksa_entry->reauth_time));
	ss.write((char *) &pmksa_entry->expiration, sizeof(pmksa_entry->expiration));
	ss.write((char *) &pmksa_entry->opportunistic, sizeof(pmksa_entry->opportunistic));
	char byte = (pmksa_entry->fils_cache_id_set) ? 1 : 0;
	ss.write((char *) &byte, sizeof(byte));
	ss.write((char *) pmksa_entry->fils_cache_id, FILS_CACHE_ID_LEN);
	ss << std::flush;
	return ss;
}

inline std::stringstream& deserializePmkCacheEntry(
	std::stringstream &ss, struct rsn_pmksa_cache_entry *pmksa_entry) {
	ss.seekg(0);
	ss.read((char *) &pmksa_entry->pmk_len, sizeof(pmksa_entry->pmk_len));
	ss.read((char *) pmksa_entry->pmk, pmksa_entry->pmk_len);
	ss.read((char *) pmksa_entry->pmkid, PMKID_LEN);
	ss.read((char *) pmksa_entry->aa, ETH_ALEN);
	// Omit wpa_ssid field because the network is created on connecting to a access point.
	ss.read((char *) &pmksa_entry->akmp, sizeof(pmksa_entry->akmp));
	ss.read((char *) &pmksa_entry->reauth_time, sizeof(pmksa_entry->reauth_time));
	ss.read((char *) &pmksa_entry->expiration, sizeof(pmksa_entry->expiration));
	ss.read((char *) &pmksa_entry->opportunistic, sizeof(pmksa_entry->opportunistic));
	char byte = 0;
	ss.read((char *) &byte, sizeof(byte));
	pmksa_entry->fils_cache_id_set = (byte) ? 1 : 0;
	ss.read((char *) pmksa_entry->fils_cache_id, FILS_CACHE_ID_LEN);
	return ss;
}
}  // namespace misc_utils
}  // namespace supplicant
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl
#endif  // MISC_UTILS_H_
