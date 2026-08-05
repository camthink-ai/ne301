/**
 * @file rtsp_server.h
 * @brief RTSP protocol server - TCP listener, request parsing, response generation
 */

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include "aicam_types.h"
#include "rtsp_service.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RTSP_REQUEST_BUF_SIZE   2048
#define RTSP_RESPONSE_BUF_SIZE  2048
#define RTSP_SESSION_TIMEOUT    60      /* seconds */
#define RTSP_MAX_SESSION_ID     32

/* ==================== Types ==================== */

typedef enum {
    RTSP_METHOD_OPTIONS,
    RTSP_METHOD_DESCRIBE,
    RTSP_METHOD_SETUP,
    RTSP_METHOD_PLAY,
    RTSP_METHOD_PAUSE,
    RTSP_METHOD_TEARDOWN,
    RTSP_METHOD_GET_PARAMETER,
    RTSP_METHOD_UNKNOWN
} rtsp_method_t;

/* ==================== API ==================== */

/**
 * @brief Parse RTSP method from request buffer
 */
rtsp_method_t rtsp_parse_method(const char *request, uint32_t len);

/**
 * @brief Extract CSeq value from RTSP request
 * @return CSeq value as string (points into request buffer)
 */
const char* rtsp_parse_cseq(const char *request);

/**
 * @brief Extract Transport header from SETUP request
 */
const char* rtsp_parse_transport(const char *request);

/**
 * @brief Extract Session header from request
 */
const char* rtsp_parse_session(const char *request);

/**
 * @brief Extract Authorization header from request
 */
const char* rtsp_parse_authorization(const char *request);

/**
 * @brief Extract request URI from RTSP request line
 */
aicam_result_t rtsp_parse_uri(const char *request, char *uri, uint32_t uri_size);

/**
 * @brief Parse client_port from Transport header
 * @param transport_hdr Transport header value
 * @param rtp_port Output RTP port
 * @param rtcp_port Output RTCP port
 */
aicam_result_t rtsp_parse_client_port(const char *transport_hdr,
                                       uint16_t *rtp_port, uint16_t *rtcp_port);

/**
 * @brief Handle a complete RTSP request for a client
 * @param client Client context
 * @param request Raw request data
 * @param len Request length
 */
aicam_result_t rtsp_handle_request(rtsp_client_t *client,
                                    const char *request, uint32_t len);

/**
 * @brief Generate SDP description
 * @param sdp_buf Output buffer
 * @param buf_size Buffer size
 * @param device_ip Device IP address string
 * @param sps_data SPS NAL data (after start code)
 * @param sps_size SPS size
 * @param pps_data PPS NAL data (after start code)
 * @param pps_size PPS size
 */
aicam_result_t rtsp_generate_sdp(char *sdp_buf, uint32_t buf_size,
                                  const char *device_ip,
                                  const uint8_t *sps_data, uint32_t sps_size,
                                  const uint8_t *pps_data, uint32_t pps_size);

#ifdef __cplusplus
}
#endif

#endif /* RTSP_SERVER_H */
