/*
 * WPA Supplicant - Aidl entry point to wpa_supplicant core
 * Copyright (c) 2021, Google Inc. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_AIDL_AIDL_H
#define WPA_SUPPLICANT_AIDL_AIDL_H

#ifdef _cplusplus
extern "C"
{
#endif  // _cplusplus

	/**
	 * This is the aidl RPC interface entry point to the wpa_supplicant
	 * core. This initializes the aidl driver & AidlManager instance and
	 * then forwards all the notifcations from the supplicant core to the
	 * AidlManager.
	 */
	struct wpas_aidl_priv;
	struct wpa_global;

	struct wpas_aidl_priv *wpas_aidl_init(struct wpa_global *global);
	void wpas_aidl_deinit(struct wpas_aidl_priv *priv);

#ifdef CONFIG_CTRL_IFACE_AIDL
	int wpas_aidl_register_interface(struct wpa_supplicant *wpa_s);
	int wpas_aidl_unregister_interface(struct wpa_supplicant *wpa_s);
	int wpas_aidl_register_network(
		struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid);
	int wpas_aidl_unregister_network(
		struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid);
	int wpas_aidl_notify_state_changed(struct wpa_supplicant *wpa_s);
	int wpas_aidl_notify_network_request(
		struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
		enum wpa_ctrl_req_type rtype, const char *default_txt);
	void wpas_aidl_notify_anqp_query_done(
		struct wpa_supplicant *wpa_s, const u8 *bssid, const char *result,
		const struct wpa_bss_anqp *anqp);
	void wpas_aidl_notify_hs20_icon_query_done(
		struct wpa_supplicant *wpa_s, const u8 *bssid,
		const char *file_name, const u8 *image, u32 image_length);
	void wpas_aidl_notify_hs20_rx_subscription_remediation(
		struct wpa_supplicant *wpa_s, const char *url, u8 osu_method);
	void wpas_aidl_notify_hs20_rx_deauth_imminent_notice(
		struct wpa_supplicant *wpa_s, u8 code, u16 reauth_delay,
		const char *url);
	void wpas_aidl_notify_hs20_rx_terms_and_conditions_acceptance(
			struct wpa_supplicant *wpa_s, const char *url);
	void wpas_aidl_notify_disconnect_reason(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_assoc_reject(struct wpa_supplicant *wpa_s, const u8 *bssid,
		u8 timed_out, const u8 *assoc_resp_ie, size_t assoc_resp_ie_len);
	void wpas_aidl_notify_auth_timeout(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_bssid_changed(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_wps_event_fail(
		struct wpa_supplicant *wpa_s, uint8_t *peer_macaddr,
		uint16_t config_error, uint16_t error_indication);
	void wpas_aidl_notify_wps_event_success(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_wps_event_pbc_overlap(
		struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_p2p_device_found(
		struct wpa_supplicant *wpa_s, const u8 *addr,
		const struct p2p_peer_info *info, const u8 *peer_wfd_device_info,
		u8 peer_wfd_device_info_len, const u8 *peer_wfd_r2_device_info,
		u8 peer_wfd_r2_device_info_len);
	void wpas_aidl_notify_p2p_device_lost(
		struct wpa_supplicant *wpa_s, const u8 *p2p_device_addr);
	void wpas_aidl_notify_p2p_find_stopped(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_p2p_go_neg_req(
		struct wpa_supplicant *wpa_s, const u8 *src_addr, u16 dev_passwd_id,
		u8 go_intent);
	void wpas_aidl_notify_p2p_go_neg_completed(
		struct wpa_supplicant *wpa_s, const struct p2p_go_neg_results *res);
	void wpas_aidl_notify_p2p_group_formation_failure(
		struct wpa_supplicant *wpa_s, const char *reason);
	void wpas_aidl_notify_p2p_group_started(
		struct wpa_supplicant *wpa_s, const struct wpa_ssid *ssid,
		int persistent, int client);
	void wpas_aidl_notify_p2p_group_removed(
		struct wpa_supplicant *wpa_s, const struct wpa_ssid *ssid,
		const char *role);
	void wpas_aidl_notify_p2p_invitation_received(
		struct wpa_supplicant *wpa_s, const u8 *sa, const u8 *go_dev_addr,
		const u8 *bssid, int id, int op_freq);
	void wpas_aidl_notify_p2p_invitation_result(
		struct wpa_supplicant *wpa_s, int status, const u8 *bssid);
	void wpas_aidl_notify_p2p_provision_discovery(
		struct wpa_supplicant *wpa_s, const u8 *dev_addr, int request,
		enum p2p_prov_disc_status status, u16 config_methods,
		unsigned int generated_pin);
	void wpas_aidl_notify_p2p_sd_response(
		struct wpa_supplicant *wpa_s, const u8 *sa, u16 update_indic,
		const u8 *tlvs, size_t tlvs_len);
	void wpas_aidl_notify_ap_sta_authorized(
		struct wpa_supplicant *wpa_s, const u8 *sta,
		const u8 *p2p_dev_addr);
	void wpas_aidl_notify_ap_sta_deauthorized(
		struct wpa_supplicant *wpa_s, const u8 *sta,
		const u8 *p2p_dev_addr);
	void wpas_aidl_notify_eap_error(
		struct wpa_supplicant *wpa_s, int error_code);
	void wpas_aidl_notify_dpp_config_received(struct wpa_supplicant *wpa_s,
			struct wpa_ssid *ssid);
	void wpas_aidl_notify_dpp_config_sent(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_auth_success(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_resp_pending(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_not_compatible(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_missing_auth(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_configuration_failure(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_invalid_uri(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_timeout(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_auth_failure(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_fail(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_config_sent_wait_response(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_config_accepted(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_config_rejected(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_dpp_conn_status(struct wpa_supplicant *wpa_s,
		enum dpp_status_error status, const char *ssid,
		const char *channel_list, unsigned short band_list[], int size);
	void wpas_aidl_notify_pmk_cache_added(
		struct wpa_supplicant *wpas, struct rsn_pmksa_cache_entry *pmksa_entry);
	void wpas_aidl_notify_bss_tm_status(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_transition_disable(
		struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid, u8 bitmap);
	void wpas_aidl_notify_network_not_found(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_frequency_changed(struct wpa_supplicant *wpa_s, int frequency);
	void wpas_aidl_notify_ceritification(struct wpa_supplicant *wpa_s,
		int depth, const char *subject,
		const char *altsubject[],
		int num_altsubject,
		const char *cert_hash,
		const struct wpabuf *cert);
	void wpas_aidl_notify_eap_method_selected(struct wpa_supplicant *wpa_s,
		const char *reason_string);
	void wpas_aidl_notify_ssid_temp_disabled(struct wpa_supplicant *wpa_s,
		const char *reason_string);
	void wpas_aidl_notify_open_ssl_failure(struct wpa_supplicant *wpa_s,
		const char *reason_string);
	void wpas_aidl_notify_qos_policy_reset(struct wpa_supplicant *wpa_s);
	void wpas_aidl_notify_qos_policy_request(struct wpa_supplicant *wpa_s,
		struct dscp_policy_data *policies, int num_policies);
#else   // CONFIG_CTRL_IFACE_AIDL
static inline int wpas_aidl_register_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}
static inline int wpas_aidl_unregister_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}
static inline int wpas_aidl_register_network(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	return 0;
}
static inline int wpas_aidl_unregister_network(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	return 0;
}
static inline int wpas_aidl_notify_state_changed(struct wpa_supplicant *wpa_s)
{
	return 0;
}
static inline int wpas_aidl_notify_network_request(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
	enum wpa_ctrl_req_type rtype, const char *default_txt)
{
	return 0;
}
static void wpas_aidl_notify_anqp_query_done(
	struct wpa_supplicant *wpa_s, const u8 *bssid, const char *result,
	const struct wpa_bss_anqp *anqp)
{}
static void wpas_aidl_notify_hs20_icon_query_done(
	struct wpa_supplicant *wpa_s, const u8 *bssid, const char *file_name,
	const u8 *image, u32 image_length)
{}
static void wpas_aidl_notify_hs20_rx_subscription_remediation(
	struct wpa_supplicant *wpa_s, const char *url, u8 osu_method)
{}
static void wpas_aidl_notify_hs20_rx_deauth_imminent_notice(
	struct wpa_supplicant *wpa_s, u8 code, u16 reauth_delay, const char *url)
{}
static void wpas_aidl_notify_hs20_rx_terms_and_conditions_acceptance(
		struct wpa_supplicant *wpa_s, const char *url)
{}
static void wpas_aidl_notify_disconnect_reason(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_assoc_reject(struct wpa_supplicant *wpa_s, const u8 *bssid,
	u8 timed_out, const u8 *assoc_resp_ie, size_t assoc_resp_ie_len)
{}
static void wpas_aidl_notify_auth_timeout(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_wps_event_fail(
	struct wpa_supplicant *wpa_s, uint8_t *peer_macaddr, uint16_t config_error,
	uint16_t error_indication)
{}
static void wpas_aidl_notify_bssid_changed(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_wps_event_success(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_wps_event_pbc_overlap(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_p2p_device_found(
	struct wpa_supplicant *wpa_s, const u8 *addr,
	const struct p2p_peer_info *info, const u8 *peer_wfd_device_info,
	u8 peer_wfd_device_info_len, const u8 *peer_wfd_r2_device_info,
	u8 peer_wfd_r2_device_info_len)
{}
static void wpas_aidl_notify_p2p_device_lost(
	struct wpa_supplicant *wpa_s, const u8 *p2p_device_addr)
{}
static void wpas_aidl_notify_p2p_find_stopped(struct wpa_supplicant *wpa_s) {}
static void wpas_aidl_notify_p2p_go_neg_req(
	struct wpa_supplicant *wpa_s, const u8 *src_addr, u16 dev_passwd_id,
	u8 go_intent)
{}
static void wpas_aidl_notify_p2p_go_neg_completed(
	struct wpa_supplicant *wpa_s, const struct p2p_go_neg_results *res)
{}
static void wpas_aidl_notify_p2p_group_formation_failure(
	struct wpa_supplicant *wpa_s, const char *reason)
{}
static void wpas_aidl_notify_p2p_group_started(
	struct wpa_supplicant *wpa_s, const struct wpa_ssid *ssid, int persistent,
	int client)
{}
static void wpas_aidl_notify_p2p_group_removed(
	struct wpa_supplicant *wpa_s, const struct wpa_ssid *ssid, const char *role)
{}
static void wpas_aidl_notify_p2p_invitation_received(
	struct wpa_supplicant *wpa_s, const u8 *sa, const u8 *go_dev_addr,
	const u8 *bssid, int id, int op_freq)
{}
static void wpas_aidl_notify_p2p_invitation_result(
	struct wpa_supplicant *wpa_s, int status, const u8 *bssid)
{}
static void wpas_aidl_notify_p2p_provision_discovery(
	struct wpa_supplicant *wpa_s, const u8 *dev_addr, int request,
	enum p2p_prov_disc_status status, u16 config_methods,
	unsigned int generated_pin)
{}
static void wpas_aidl_notify_p2p_sd_response(
	struct wpa_supplicant *wpa_s, const u8 *sa, u16 update_indic,
	const u8 *tlvs, size_t tlvs_len)
{}
static void wpas_aidl_notify_ap_sta_authorized(
	struct wpa_supplicant *wpa_s, const u8 *sta, const u8 *p2p_dev_addr)
{}
static void wpas_aidl_notify_ap_sta_deauthorized(
	struct wpa_supplicant *wpa_s, const u8 *sta, const u8 *p2p_dev_addr)
{}
static void wpas_aidl_notify_eap_error(
	struct wpa_supplicant *wpa_s, int error_code)
{}
static void wpas_aidl_notify_dpp_config_received(struct wpa_supplicant *wpa_s,
		struct wpa_ssid *ssid)
{}
static void wpas_aidl_notify_dpp_config_received(struct wpa_supplicant *wpa_s,
		struct wpa_ssid *ssid);
static void wpas_aidl_notify_dpp_config_sent(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_auth_success(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_resp_pending(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_not_compatible(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_missing_auth(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_configuration_failure(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_invalid_uri(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_timeout(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_auth_failure(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_fail(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_config_sent_wait_response(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_config_accepted(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_config_rejected(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_dpp_conn_status(struct wpa_supplicant *wpa_s,
			enum dpp_status_error status, const char *ssid,
			const char *channel_list, unsigned short band_list[], int size)
{}
static void wpas_aidl_notify_pmk_cache_added(struct wpa_supplicant *wpas,
						 struct rsn_pmksa_cache_entry *pmksa_entry)
{}
static void wpas_aidl_notify_bss_tm_status(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_transition_disable(struct wpa_supplicant *wpa_s,
						struct wpa_ssid *ssid,
						u8 bitmap)
{}
static void wpas_aidl_notify_network_not_found(struct wpa_supplicant *wpa_s)
{}
static void wpas_aidl_notify_frequency_changed(struct wpa_supplicant *wpa_s, int frequency)
{}
static void wpas_aidl_notify_ceritification(struct wpa_supplicant *wpa_s,
	int depth, const char *subject,
	const char *altsubject[],
	int num_altsubject,
	const char *cert_hash,
	const struct wpabuf *cert)
{}
static void wpas_aidl_notify_eap_method_selected(struct wpa_supplicant *wpa_s,
	const char *reason_string)
{}
static void wpas_aidl_notify_ssid_temp_disabled(struct wpa_supplicant *wpa_s,
	const char *reason_string)
{}
static void wpas_aidl_notify_open_ssl_failure(struct wpa_supplicant *wpa_s,
	const char *reason_string)
{}
static void wpas_aidl_notify_qos_policy_reset(struct wpa_supplicant *wpa_s) {}
static void wpas_aidl_notify_qos_policy_request(struct wpa_supplicant *wpa_s,
						struct dscp_policy_data *policies,
						int num_policies)
{}
#endif  // CONFIG_CTRL_IFACE_AIDL

#ifdef _cplusplus
}
#endif  // _cplusplus

#endif  // WPA_SUPPLICANT_AIDL_AIDL_H
