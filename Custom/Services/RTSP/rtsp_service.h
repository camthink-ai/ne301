/**
 * @file rtsp_service.h
 * @brief RTSP Service - lifecycle, video_hub subscription, client management
 */

#ifndef RTSP_SERVICE_H
#define RTSP_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "Services/Video/video_stream_hub.h"
#include <stdint.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RTSP_MAX_CLIENTS        4
#define RTSP_TASK_STACK_SIZE    (4096 * 2)
#define RTSP_MAX_URL_LENGTH     256
#define RTSP_DEFAULT_PORT       554

/* ==================== Types ==================== */

typedef struct rtsp_client {
    int tcp_socket;                     /* RTSP TCP connection, -1 if unused */
    struct sockaddr_in client_addr;     /* Client address */
    char session_id[32];                /* RTSP Session ID */
    char nonce[65];                     /* Digest auth nonce for this client */
    uint16_t client_rtp_port;           /* Client RTP port (SETUP) */
    uint16_t client_rtcp_port;          /* Client RTCP port */
    uint16_t server_rtp_port;           /* Server RTP port */
    int rtp_socket;                     /* RTP UDP socket, -1 if unused */
    aicam_bool_t playing;              /* Whether actively streaming */
    aicam_bool_t waiting_for_keyframe; /* Skip P-frames until keyframe arrives */
    uint32_t connected_time;            /* Connection time (ticks) */
    uint32_t ssrc;                      /* RTP SSRC */
    uint32_t rtp_seq;                   /* RTP sequence number */
    uint32_t rtp_timestamp;             /* RTP timestamp */
    uint32_t rtp_frame_index;           /* Frame counter for timestamp generation */
    aicam_bool_t in_use;               /* Slot in use */
    aicam_bool_t authenticated;        /* Passed DESCRIBE auth challenge */
} rtsp_client_t;

typedef struct {
    uint8_t *sps_data;
    uint32_t sps_size;
    uint8_t *pps_data;
    uint32_t pps_size;
} rtsp_sps_pps_t;

typedef struct {
    char auth_mode[16];                 /* "none" or "digest" */
    char username[64];
    char password[64];
} rtsp_service_auth_config_t;

/* ==================== Service Lifecycle ==================== */

aicam_result_t rtsp_service_init(void *config);
aicam_result_t rtsp_service_start(void);
aicam_result_t rtsp_service_stop(void);
aicam_result_t rtsp_service_deinit(void);
service_state_t rtsp_service_get_state(void);

aicam_bool_t rtsp_service_is_initialized(void);
aicam_bool_t rtsp_service_is_running(void);
uint32_t rtsp_service_get_client_count(void);
void rtsp_service_get_clients(rtsp_client_t *clients, uint32_t *count);
aicam_result_t rtsp_service_kick_client(const char *session_id);

/* ==================== Client Helpers (used by rtsp_server) ==================== */

aicam_result_t rtsp_service_allocate_rtp(rtsp_client_t *client);
aicam_result_t rtsp_service_client_start_play(rtsp_client_t *client);
aicam_result_t rtsp_service_client_stop_play(rtsp_client_t *client);
void rtsp_service_get_auth_config(rtsp_service_auth_config_t *cfg);
void rtsp_service_set_client_nonce(rtsp_client_t *client, const char *nonce);
void rtsp_service_get_device_ip(char *ip_buf, uint32_t buf_size);
void rtsp_service_get_client_local_ip(rtsp_client_t *client, char *ip_buf, uint32_t buf_size);
void rtsp_service_get_cached_sps_pps(rtsp_sps_pps_t *sps_pps);

#ifdef __cplusplus
}
#endif

#endif /* RTSP_SERVICE_H */
