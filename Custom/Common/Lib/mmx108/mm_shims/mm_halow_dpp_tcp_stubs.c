/*
 * Stubs for Morse DPP-over-TCP (disabled via MM_IOT_DPP_DISABLE_TCP in libmorse).
 * libmorse.a still references dpp_tcp_* from dpp_supplicant.c.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;

struct dpp_global;
struct dpp_authentication;
struct dpp_pkex;
struct hostapd_ip_addr;

enum dpp_netrole {
	DPP_NETROLE_AP = 0,
	DPP_NETROLE_STA = 1,
	DPP_NETROLE_CONFIGURATOR = 2,
};

enum dpp_status_error {
	DPP_STATUS_OK = 0,
};

int dpp_tcp_init(struct dpp_global *dpp, struct dpp_authentication *auth,
		 const struct hostapd_ip_addr *addr, int port,
		 const char *name, enum dpp_netrole netrole,
		 const char *mud_url,
		 const char *extra_conf_req_name,
		 const char *extra_conf_req_value,
		 void *msg_ctx, void *cb_ctx,
		 int (*process_conf_obj)(void *ctx,
					 struct dpp_authentication *auth),
		 bool (*tcp_msg_sent)(void *ctx,
				      struct dpp_authentication *auth))
{
	(void)dpp;
	(void)auth;
	(void)addr;
	(void)port;
	(void)name;
	(void)netrole;
	(void)mud_url;
	(void)extra_conf_req_name;
	(void)extra_conf_req_value;
	(void)msg_ctx;
	(void)cb_ctx;
	(void)process_conf_obj;
	(void)tcp_msg_sent;
	return -1;
}

int dpp_tcp_auth(struct dpp_global *dpp, void *_conn,
		 struct dpp_authentication *auth, const char *name,
		 enum dpp_netrole netrole, const char *mud_url,
		 const char *extra_conf_req_name,
		 const char *extra_conf_req_value,
		 int (*process_conf_obj)(void *ctx,
					 struct dpp_authentication *auth),
		 bool (*tcp_msg_sent)(void *ctx,
				      struct dpp_authentication *auth))
{
	(void)dpp;
	(void)_conn;
	(void)auth;
	(void)name;
	(void)netrole;
	(void)mud_url;
	(void)extra_conf_req_name;
	(void)extra_conf_req_value;
	(void)process_conf_obj;
	(void)tcp_msg_sent;
	return -1;
}

int dpp_tcp_pkex_init(struct dpp_global *dpp, struct dpp_pkex *pkex,
		      const struct hostapd_ip_addr *addr, int port,
		      void *msg_ctx, void *cb_ctx,
		      int (*pkex_done)(void *ctx, void *conn,
				       void *bi))
{
	(void)dpp;
	(void)pkex;
	(void)addr;
	(void)port;
	(void)msg_ctx;
	(void)cb_ctx;
	(void)pkex_done;
	return -1;
}

bool dpp_tcp_conn_status_requested(struct dpp_global *dpp)
{
	(void)dpp;
	return false;
}

void dpp_tcp_send_conn_status(struct dpp_global *dpp,
			      enum dpp_status_error result,
			      const u8 *ssid, size_t ssid_len,
			      const char *channel_list)
{
	(void)dpp;
	(void)result;
	(void)ssid;
	(void)ssid_len;
	(void)channel_list;
}

void dpp_tcp_init_flush(struct dpp_global *dpp)
{
	(void)dpp;
}
