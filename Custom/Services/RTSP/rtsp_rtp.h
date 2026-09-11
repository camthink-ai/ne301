/**
 * @file rtsp_rtp.h
 * @brief RTP packaging and sending for H.264 video over UDP
 */

#ifndef RTSP_RTP_H
#define RTSP_RTP_H

#include "aicam_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RTP_HEADER_SIZE         12
#define RTP_MAX_PAYLOAD         1400    /* MTU 1500 - IP(20) - UDP(8) - RTP(12) */
#define RTP_CLOCK_RATE          90000   /* H.264 RTP clock rate */
#define RTP_DYNAMIC_PT          96      /* Dynamic payload type for H.264 */

/* FU-A fragmentation type */
#define RTP_NALU_FU_A           28

/* ==================== Types ==================== */

typedef struct {
    uint8_t  flags;         /* V=2, P=0, X=0, CC=0 -> 0x80 */
    uint8_t  pt;            /* PT=96, M bit in bit 0 (marker) */
    uint16_t seq_num;       /* Sequence number (incrementing) */
    uint32_t timestamp;     /* Timestamp in 90000Hz clock */
    uint32_t ssrc;          /* Synchronization source identifier */
} AICAM_PACKED rtp_header_t;

/* Forward declaration */
struct rtsp_client;
typedef struct rtsp_client rtsp_client_t;

/* ==================== API ==================== */

/**
 * @brief Send an H.264 frame as RTP packets to a client
 * @note  Caller MUST hold the RTSP service mutex - these functions use shared
 *        static buffers (s_rtp_tx_buf, NAL parse arrays) for stack savings.
 * @param client RTSP client with RTP socket info
 * @param data Frame data in Annex-B format
 * @param size Frame data size
 * @param is_keyframe Whether this is a keyframe
 * @param rtp_ts Pre-computed RTP timestamp (90kHz clock)
 */
aicam_result_t rtsp_rtp_send_frame(rtsp_client_t *client,
                                    const uint8_t *data, uint32_t size,
                                    aicam_bool_t is_keyframe,
                                    uint32_t rtp_ts);

/**
 * @brief Send a single NAL unit as one RTP packet
 * @note  Caller MUST hold the RTSP service mutex (see rtsp_rtp_send_frame).
 */
aicam_result_t rtsp_rtp_send_single_nal(rtsp_client_t *client,
                                          const uint8_t *data, uint32_t size,
                                          uint32_t timestamp, uint8_t marker);

/**
 * @brief Send a NAL unit using FU-A fragmentation
 * @note  Caller MUST hold the RTSP service mutex (see rtsp_rtp_send_frame).
 */
aicam_result_t rtsp_rtp_send_fu_a(rtsp_client_t *client,
                                    const uint8_t *data, uint32_t size,
                                    uint32_t timestamp, uint8_t marker);

#ifdef __cplusplus
}
#endif

#endif /* RTSP_RTP_H */
