/**
 * @file rtsp_server.c
 * @brief RTSP protocol server implementation
 */

#include "rtsp_server.h"
#include "rtsp_digest_auth.h"
#include "rtsp_service.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== Parsing Helpers ==================== */

static const char* find_header_value(const char *request, const char *header_name)
{
    static char value_buf[512];
    uint32_t name_len = strlen(header_name);

    const char *p = request;
    while (*p) {
        if (strncasecmp(p, header_name, name_len) == 0 && p[name_len] == ':') {
            const char *val_start = p + name_len + 1;
            while (*val_start == ' ') val_start++;
            const char *val_end = strstr(val_start, "\r\n");
            if (!val_end) val_end = val_start + strlen(val_start);

            uint32_t copy_len = val_end - val_start;
            if (copy_len >= sizeof(value_buf)) copy_len = sizeof(value_buf) - 1;
            memcpy(value_buf, val_start, copy_len);
            value_buf[copy_len] = '\0';
            return value_buf;
        }
        /* Advance to next line */
        p = strstr(p, "\r\n");
        if (!p) break;
        p += 2;
    }
    return NULL;
}

/* ==================== Public API ==================== */

rtsp_method_t rtsp_parse_method(const char *request, uint32_t len)
{
    (void)len;
    if (strncmp(request, "OPTIONS", 7) == 0)      return RTSP_METHOD_OPTIONS;
    if (strncmp(request, "DESCRIBE", 8) == 0)      return RTSP_METHOD_DESCRIBE;
    if (strncmp(request, "SETUP", 5) == 0)         return RTSP_METHOD_SETUP;
    if (strncmp(request, "PLAY", 4) == 0)          return RTSP_METHOD_PLAY;
    if (strncmp(request, "PAUSE", 5) == 0)         return RTSP_METHOD_PAUSE;
    if (strncmp(request, "TEARDOWN", 8) == 0)      return RTSP_METHOD_TEARDOWN;
    if (strncmp(request, "GET_PARAMETER", 13) == 0) return RTSP_METHOD_GET_PARAMETER;

    LOG_SVC_WARN("RTSP: Unknown method in request: %.20s", request);
    return RTSP_METHOD_UNKNOWN;
}

const char* rtsp_parse_cseq(const char *request)
{
    return find_header_value(request, "CSeq");
}

const char* rtsp_parse_transport(const char *request)
{
    return find_header_value(request, "Transport");
}

const char* rtsp_parse_session(const char *request)
{
    return find_header_value(request, "Session");
}

const char* rtsp_parse_authorization(const char *request)
{
    return find_header_value(request, "Authorization");
}

aicam_result_t rtsp_parse_uri(const char *request, char *uri, uint32_t uri_size)
{
    /* Request line: METHOD rtsp://... RTSP/1.0\r\n */
    const char *sp1 = strchr(request, ' ');
    if (!sp1) return AICAM_ERROR;
    sp1++;

    const char *sp2 = strchr(sp1, ' ');
    if (!sp2) return AICAM_ERROR;

    uint32_t len = sp2 - sp1;
    if (len >= uri_size) len = uri_size - 1;
    memcpy(uri, sp1, len);
    uri[len] = '\0';
    return AICAM_OK;
}

aicam_result_t rtsp_parse_client_port(const char *transport_hdr,
                                       uint16_t *rtp_port, uint16_t *rtcp_port)
{
    if (!transport_hdr) return AICAM_ERROR;

    const char *cp = strstr(transport_hdr, "client_port=");
    if (!cp) return AICAM_ERROR;
    cp += strlen("client_port=");

    int rtp = 0, rtcp = 0;
    if (sscanf(cp, "%d-%d", &rtp, &rtcp) >= 1) {
        *rtp_port = (uint16_t)rtp;
        *rtcp_port = (rtcp > 0) ? (uint16_t)rtcp : (uint16_t)(rtp + 1);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

/* ==================== Response Helpers ==================== */

static int send_response(int sock, const char *response, uint32_t len)
{
    int total = 0;
    while (total < (int)len) {
        int sent = send(sock, response + total, len - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return total;
}

static void send_simple_response(int sock, int status, const char *reason,
                                  const char *cseq, const char *extra_headers)
{
    static char resp[1024];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 %d %s\r\n"
        "CSeq: %s\r\n"
        "%s"
        "\r\n",
        status, reason, cseq ? cseq : "0",
        extra_headers ? extra_headers : "");
    send_response(sock, resp, len);
}

/* ==================== Base64 Encoding ==================== */

static uint32_t base64_encode(const uint8_t *in, uint32_t in_len,
                               char *out, uint32_t out_size)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t i = 0, j = 0;

    while (i < in_len && j + 4 < out_size) {
        uint32_t a = in[i++];
        uint32_t b = (i < in_len) ? in[i++] : 0;
        uint32_t c = (i < in_len) ? in[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64[(triple >> 18) & 0x3F];
        out[j++] = b64[(triple >> 12) & 0x3F];
        out[j++] = (i > in_len + 1) ? '=' : b64[(triple >> 6) & 0x3F];
        out[j++] = (i > in_len) ? '=' : b64[triple & 0x3F];
    }
    out[j] = '\0';
    return j;
}

/* ==================== SDP Generation ==================== */

aicam_result_t rtsp_generate_sdp(char *sdp_buf, uint32_t buf_size,
                                  const char *device_ip,
                                  const uint8_t *sps_data, uint32_t sps_size,
                                  const uint8_t *pps_data, uint32_t pps_size)
{
    static char sps_b64[256];
    static char pps_b64[256];
    char profile_level_id[8] = {0};

    if (sps_data && sps_size >= 4) {
        base64_encode(sps_data + 1, sps_size - 1, sps_b64, sizeof(sps_b64));
        /* profile-level-id from SPS: 3 bytes after forbidden_zero_bit+nal_ref_idc */
        snprintf(profile_level_id, sizeof(profile_level_id),
                 "%02X%02X%02X", sps_data[1], sps_data[2], sps_data[3]);
    }

    if (pps_data && pps_size >= 1) {
        base64_encode(pps_data + 1, pps_size - 1, pps_b64, sizeof(pps_b64));
    }

    int len = snprintf(sdp_buf, buf_size,
        "v=0\r\n"
        "o=- 0 0 IN IP4 %s\r\n"
        "s=NE301 RTSP Stream\r\n"
        "t=0 0\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1"
            ";profile-level-id=%s"
            ";sprop-parameter-sets=%s,%s\r\n"
        "a=control:trackID=0\r\n",
        device_ip,
        profile_level_id[0] ? profile_level_id : "640029",
        sps_b64[0] ? sps_b64 : "Z2QAKKwVgCgC3REQAAOwAcAQBCgAAB4IAAdCAAB4IAAdA",
        pps_b64[0] ? pps_b64 : "aM44A");

    return (len > 0 && (uint32_t)len < buf_size) ? AICAM_OK : AICAM_ERROR;
}

/* ==================== Request Handling ==================== */

/**
 * @brief Check if client needs auth; send 401 challenge if so.
 * @return AICAM_TRUE if auth is OK (or not required), AICAM_FALSE if 401 was sent.
 */
static aicam_bool_t check_auth_required(rtsp_client_t *client, const char *cseq,
                                         const char *request)
{
    rtsp_service_auth_config_t auth_cfg;
    rtsp_service_get_auth_config(&auth_cfg);

    if (!auth_cfg.auth_mode[0] || strcmp(auth_cfg.auth_mode, "digest") != 0) {
        return AICAM_TRUE;
    }

    if (client->authenticated) {
        return AICAM_TRUE;
    }

    /* Client not authenticated - try to verify Authorization header */
    const char *auth_hdr = rtsp_parse_authorization(request);
    if (auth_hdr) {
        static char uri[256];
        rtsp_parse_uri(request, uri, sizeof(uri));

        const char *method_str = NULL;
        if (strncmp(request, "SETUP", 5) == 0)      method_str = "SETUP";
        else if (strncmp(request, "PLAY", 4) == 0)   method_str = "PLAY";
        else if (strncmp(request, "DESCRIBE", 8) == 0) method_str = "DESCRIBE";

        if (method_str &&
            rtsp_digest_verify(auth_hdr, method_str,
                               auth_cfg.username, auth_cfg.password,
                               client->nonce, "NE301")) {
            client->authenticated = AICAM_TRUE;
            return AICAM_TRUE;
        }
        /* Bad credentials → 403 */
        send_simple_response(client->tcp_socket, 403, "Forbidden", cseq, NULL);
        return AICAM_FALSE;
    }

    /* No Authorization header → send 401 challenge */
    static char nonce[65];
    rtsp_digest_generate_nonce(nonce, sizeof(nonce));
    rtsp_service_set_client_nonce(client, nonce);

    static char resp_401[512];
    int len = snprintf(resp_401, sizeof(resp_401),
        "RTSP/1.0 401 Unauthorized\r\n"
        "CSeq: %s\r\n"
        "WWW-Authenticate: Digest realm=\"NE301\", nonce=\"%s\"\r\n"
        "\r\n",
        cseq ? cseq : "0", nonce);
    send_response(client->tcp_socket, resp_401, len);
    return AICAM_FALSE;
}

static aicam_result_t handle_options(rtsp_client_t *client, const char *cseq)
{
    static char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n"
        "\r\n",
        cseq ? cseq : "0");
    send_response(client->tcp_socket, resp, len);
    return AICAM_OK;
}

static aicam_result_t handle_describe(rtsp_client_t *client, const char *cseq,
                                       const char *request)
{
    LOG_SVC_INFO("RTSP: DESCRIBE handler, cseq=%s", cseq ? cseq : "NULL");

    /* Check authentication if digest mode is enabled */
    rtsp_service_auth_config_t auth_cfg;
    rtsp_service_get_auth_config(&auth_cfg);

    LOG_SVC_INFO("RTSP: auth_mode='%s'", auth_cfg.auth_mode);

    if (auth_cfg.auth_mode[0] && strcmp(auth_cfg.auth_mode, "digest") == 0) {
        const char *auth_hdr = rtsp_parse_authorization(request);
        if (!auth_hdr) {
            /* Send 401 challenge */
            static char nonce[65];
            rtsp_digest_generate_nonce(nonce, sizeof(nonce));
            rtsp_service_set_client_nonce(client, nonce);

            LOG_SVC_INFO("RTSP: DESCRIBE sending 401 challenge, nonce=%s", nonce);

            static char resp_401[512];
            int len = snprintf(resp_401, sizeof(resp_401),
                "RTSP/1.0 401 Unauthorized\r\n"
                "CSeq: %s\r\n"
                "WWW-Authenticate: Digest realm=\"NE301\", nonce=\"%s\"\r\n"
                "\r\n",
                cseq ? cseq : "0", nonce);
            send_response(client->tcp_socket, resp_401, len);
            return AICAM_OK;
        }

        /* Verify digest auth */
        static char uri[256];
        rtsp_parse_uri(request, uri, sizeof(uri));

        if (!rtsp_digest_verify(auth_hdr, "DESCRIBE",
                                auth_cfg.username, auth_cfg.password,
                                client->nonce, "NE301")) {
            LOG_SVC_WARN("RTSP: DESCRIBE auth FAILED");
            send_simple_response(client->tcp_socket, 403, "Forbidden", cseq, NULL);
            return AICAM_OK;
        }
        client->authenticated = AICAM_TRUE;
        LOG_SVC_INFO("RTSP: DESCRIBE auth OK");
    }

    /* Generate SDP (static to avoid ~2KB stack allocation) */
    static char sdp[2048];
    char ip_str[64];
    /* Use the client socket's local address - this is the IP VLC actually connected to */
    rtsp_service_get_client_local_ip(client, ip_str, sizeof(ip_str));

    rtsp_sps_pps_t sps_pps;
    rtsp_service_get_cached_sps_pps(&sps_pps);

    LOG_SVC_INFO("RTSP: DESCRIBE local_ip=%s sps=%lu pps=%lu sps_pps_valid=%d",
                 ip_str, (unsigned long)sps_pps.sps_size,
                 (unsigned long)sps_pps.pps_size,
                 sps_pps.sps_data ? 1 : 0);

    aicam_result_t ret = rtsp_generate_sdp(sdp, sizeof(sdp), ip_str,
                                            sps_pps.sps_data, sps_pps.sps_size,
                                            sps_pps.pps_data, sps_pps.pps_size);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("RTSP: SDP generation failed");
        send_simple_response(client->tcp_socket, 500, "Internal Server Error", cseq, NULL);
        return AICAM_ERROR;
    }

    /* Determine content length */
    uint32_t sdp_len = strlen(sdp);

    LOG_SVC_INFO("RTSP: DESCRIBE sending SDP (%lu bytes)", (unsigned long)sdp_len);

    char *resp = (char *)malloc(512 + sdp_len);
    if (!resp) {
        send_simple_response(client->tcp_socket, 500, "Internal Server Error", cseq, NULL);
        return AICAM_ERROR;
    }

    int len = snprintf(resp, 512 + sdp_len,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Base: rtsp://%s/live/\r\n"
        "Content-Length: %lu\r\n"
        "\r\n"
        "%s",
        cseq ? cseq : "0", ip_str, sdp_len, sdp);

    send_response(client->tcp_socket, resp, len);
    free(resp);
    return AICAM_OK;
}

static aicam_result_t handle_setup(rtsp_client_t *client, const char *cseq,
                                    const char *request)
{
    LOG_SVC_INFO("RTSP: SETUP handler, cseq=%s", cseq ? cseq : "NULL");

    if (!check_auth_required(client, cseq, request)) {
        return AICAM_OK;
    }

    const char *transport = rtsp_parse_transport(request);
    if (!transport) {
        LOG_SVC_WARN("RTSP: SETUP missing Transport header");
        send_simple_response(client->tcp_socket, 400, "Bad Request", cseq, NULL);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("RTSP: SETUP Transport: %s", transport);

    /* Parse client RTP/RTCP ports */
    uint16_t client_rtp_port = 0, client_rtcp_port = 0;
    if (rtsp_parse_client_port(transport, &client_rtp_port, &client_rtcp_port) != AICAM_OK) {
        LOG_SVC_WARN("RTSP: SETUP failed to parse client_port");
        send_simple_response(client->tcp_socket, 400, "Bad Request", cseq, NULL);
        return AICAM_ERROR;
    }

    /* Generate session ID if not yet assigned */
    if (client->session_id[0] == '\0') {
        snprintf(client->session_id, sizeof(client->session_id),
                 "%08lX%08lX", (unsigned long)rand(), (unsigned long)rand());
    }

    /* Allocate server RTP port */
    client->client_rtp_port = client_rtp_port;
    client->client_rtcp_port = client_rtcp_port;

    aicam_result_t ret = rtsp_service_allocate_rtp(client);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("RTSP: SETUP failed to allocate RTP socket");
        send_simple_response(client->tcp_socket, 500, "Internal Server Error", cseq, NULL);
        return AICAM_ERROR;
    }

    /* Send SETUP response - include source, destination, ssrc, mode per RFC 2326 */
    char server_ip[64];
    rtsp_service_get_client_local_ip(client, server_ip, sizeof(server_ip));

    char *dest_ip = inet_ntoa(client->client_addr.sin_addr);

    static char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Session: %s;timeout=%d\r\n"
        "Transport: RTP/AVP;unicast;source=%s;destination=%s;client_port=%u-%u;server_port=%u-%u;ssrc=%08lX;mode=\"PLAY\"\r\n"
        "\r\n",
        cseq ? cseq : "0",
        client->session_id, RTSP_SESSION_TIMEOUT,
        server_ip, dest_ip,
        client_rtp_port, client_rtcp_port,
        client->server_rtp_port, client->server_rtp_port + 1,
        (unsigned long)client->ssrc);

    send_response(client->tcp_socket, resp, len);

    /* Log the full response for debugging */
    LOG_SVC_INFO("RTSP SETUP response (%d bytes):\n---\n%s---", len, resp);
    LOG_SVC_INFO("RTSP SETUP OK: client=%u-%u server=%u-%u session=%s dest=%s",
                 client_rtp_port, client_rtcp_port,
                 client->server_rtp_port, client->server_rtp_port + 1,
                 client->session_id, dest_ip);
    return AICAM_OK;
}

static aicam_result_t handle_play(rtsp_client_t *client, const char *cseq)
{
    LOG_SVC_INFO("RTSP: PLAY handler, session=%s, rtp_port=%u, sock=%d",
                 client->session_id, client->client_rtp_port, client->rtp_socket);

    if (!check_auth_required(client, cseq, NULL)) {
        return AICAM_OK;
    }

    if (client->session_id[0] == '\0' || client->client_rtp_port == 0) {
        LOG_SVC_WARN("RTSP: PLAY rejected - no session (session=%s rtp_port=%u)",
                     client->session_id, client->client_rtp_port);
        send_simple_response(client->tcp_socket, 454, "Session Not Found", cseq, NULL);
        return AICAM_ERROR;
    }

    /* Mark client as playing */
    aicam_result_t ret = rtsp_service_client_start_play(client);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("RTSP: PLAY start_play failed ret=%d", ret);
        send_simple_response(client->tcp_socket, 500, "Internal Server Error", cseq, NULL);
        return AICAM_ERROR;
    }

    static char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Session: %s\r\n"
        "Range: npt=0.000-\r\n"
        "\r\n",
        cseq ? cseq : "0", client->session_id);

    send_response(client->tcp_socket, resp, len);
    LOG_SVC_INFO("RTSP PLAY OK: session=%s, rtp_sock=%d, client_rtp=%u, server_rtp=%u",
                 client->session_id, client->rtp_socket,
                 client->client_rtp_port, client->server_rtp_port);
    return AICAM_OK;
}

static aicam_result_t handle_teardown(rtsp_client_t *client, const char *cseq)
{
    if (!check_auth_required(client, cseq, NULL)) {
        return AICAM_OK;
    }

    static char resp[256];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "\r\n",
        cseq ? cseq : "0");

    send_response(client->tcp_socket, resp, len);
    LOG_SVC_INFO("RTSP TEARDOWN: session=%s", client->session_id);

    /* Mark client for cleanup */
    rtsp_service_client_stop_play(client);
    return AICAM_OK;
}

static aicam_result_t handle_get_parameter(rtsp_client_t *client, const char *cseq)
{
    static char resp[256];
    int len = snprintf(resp, sizeof(resp),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "\r\n",
        cseq ? cseq : "0");
    send_response(client->tcp_socket, resp, len);
    return AICAM_OK;
}

aicam_result_t rtsp_handle_request(rtsp_client_t *client,
                                    const char *request, uint32_t len)
{
    (void)len;

    /* Copy CSeq immediately - find_header_value() uses a static buffer that gets
     * overwritten by subsequent calls to rtsp_parse_transport/session/etc. */
    char cseq_buf[64] = "0";
    const char *cseq_parsed = rtsp_parse_cseq(request);
    if (cseq_parsed) {
        strncpy(cseq_buf, cseq_parsed, sizeof(cseq_buf) - 1);
        cseq_buf[sizeof(cseq_buf) - 1] = '\0';
    }
    const char *cseq = cseq_buf;

    rtsp_method_t method = rtsp_parse_method(request, len);

    switch (method) {
    case RTSP_METHOD_OPTIONS:
        return handle_options(client, cseq);
    case RTSP_METHOD_DESCRIBE:
        return handle_describe(client, cseq, request);
    case RTSP_METHOD_SETUP:
        return handle_setup(client, cseq, request);
    case RTSP_METHOD_PLAY:
        return handle_play(client, cseq);
    case RTSP_METHOD_TEARDOWN:
        return handle_teardown(client, cseq);
    case RTSP_METHOD_GET_PARAMETER:
        return handle_get_parameter(client, cseq);
    case RTSP_METHOD_PAUSE:
        send_simple_response(client->tcp_socket, 405, "Method Not Allowed", cseq, NULL);
        return AICAM_OK;
    default:
        send_simple_response(client->tcp_socket, 400, "Bad Request", cseq, NULL);
        return AICAM_ERROR;
    }
}
