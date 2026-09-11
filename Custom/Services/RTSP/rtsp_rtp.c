/**
 * @file rtsp_rtp.c
 * @brief RTP packaging and sending for H.264 video (RFC 6184)
 */

#include "rtsp_rtp.h"
#include "rtsp_service.h"
#include "debug.h"
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

/* ==================== Static TX buffer (saves ~1.4KB stack per call) ==================== */

/*
 * THREAD SAFETY: s_rtp_tx_buf and the static NAL arrays in rtsp_rtp_send_frame
 * are protected by the RTSP service mutex.  rtsp_on_frame acquires the mutex
 * before calling any of these functions, serializing all access.
 * DO NOT call these functions without holding g_rtsp_ctx.mutex.
 */
static uint8_t s_rtp_tx_buf[RTP_HEADER_SIZE + RTP_MAX_PAYLOAD + 2];

/* ==================== Helper Functions ==================== */

static void rtp_header_init(rtp_header_t *hdr, uint8_t marker,
                             uint16_t seq, uint32_t timestamp, uint32_t ssrc)
{
    hdr->flags = 0x80;  /* V=2, P=0, X=0, CC=0 */
    hdr->pt = (marker << 7) | RTP_DYNAMIC_PT;
    hdr->seq_num = htons(seq);
    hdr->timestamp = htonl(timestamp);
    hdr->ssrc = htonl(ssrc);
}

static aicam_result_t rtp_sendto(rtsp_client_t *client,
                                  const uint8_t *packet, uint32_t size)
{
    if (client->rtp_socket < 0) {
        LOG_SVC_ERROR("RTSP RTP: sendto failed - rtp_socket=%d (invalid)", client->rtp_socket);
        return AICAM_ERROR;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(client->client_rtp_port);
    addr.sin_addr = client->client_addr.sin_addr;

    int ret = sendto(client->rtp_socket, packet, size, 0,
                     (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_SVC_ERROR("RTSP RTP: sendto failed ret=%d size=%lu dst=%s:%u sock=%d",
                      ret, (unsigned long)size,
                      inet_ntoa(addr.sin_addr), client->client_rtp_port,
                      client->rtp_socket);
        return AICAM_ERROR;
    }

    return AICAM_OK;
}

/* ==================== Public API ==================== */

aicam_result_t rtsp_rtp_send_single_nal(rtsp_client_t *client,
                                          const uint8_t *data, uint32_t size,
                                          uint32_t timestamp, uint8_t marker)
{
    if (!client || !data || size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (size > RTP_MAX_PAYLOAD) {
        return rtsp_rtp_send_fu_a(client, data, size, timestamp, marker);
    }

    rtp_header_t *hdr = (rtp_header_t *)s_rtp_tx_buf;
    rtp_header_init(hdr, marker, client->rtp_seq++, timestamp, client->ssrc);

    memcpy(s_rtp_tx_buf + RTP_HEADER_SIZE, data, size);

    return rtp_sendto(client, s_rtp_tx_buf, RTP_HEADER_SIZE + size);
}

aicam_result_t rtsp_rtp_send_fu_a(rtsp_client_t *client,
                                    const uint8_t *data, uint32_t size,
                                    uint32_t timestamp, uint8_t marker)
{
    if (!client || !data || size <= RTP_MAX_PAYLOAD) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    /*
     * H.264 NAL header: forbidden_zero_bit(1) | nal_ref_idc(2) | type(5)
     * FU Indicator: forbidden_zero_bit(0) | nal_ref_idc(from NAL) | type(28=FU-A)
     * FU Header:    S(1bit) | E(1bit) | R(1bit) | type(5bits, from NAL)
     */
    uint8_t nal_header = data[0];
    uint8_t nal_ref_idc = nal_header & 0x60;    /* bits 5-6 */
    uint8_t nal_type = nal_header & 0x1F;        /* bits 0-4 */
    const uint8_t *payload = data + 1;           /* skip NAL header */
    uint32_t payload_size = size - 1;

    uint8_t fu_indicator = (nal_ref_idc | RTP_NALU_FU_A) & 0x7F;

    uint32_t max_chunk = RTP_MAX_PAYLOAD - 2;  /* minus FU indicator + FU header */
    uint32_t offset = 0;
    uint8_t first = 1;

    while (offset < payload_size) {
        uint32_t chunk = payload_size - offset;
        uint8_t last = 0;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        } else {
            last = 1;
        }

        uint8_t fu_header = (first ? 0x80 : 0x00) |
                            (last  ? 0x40 : 0x00) |
                            (nal_type & 0x1F);

        /* Only set marker on the last fragment if caller requested it */
        uint8_t pkt_marker = (last && marker) ? 1 : 0;
        uint32_t pkt_size = RTP_HEADER_SIZE + 2 + chunk;  /* RTP + FU ind + FU hdr + payload */

        rtp_header_t *hdr = (rtp_header_t *)s_rtp_tx_buf;
        rtp_header_init(hdr, pkt_marker, client->rtp_seq++, timestamp, client->ssrc);

        s_rtp_tx_buf[RTP_HEADER_SIZE]     = fu_indicator;
        s_rtp_tx_buf[RTP_HEADER_SIZE + 1] = fu_header;
        memcpy(s_rtp_tx_buf + RTP_HEADER_SIZE + 2, payload + offset, chunk);

        aicam_result_t ret = rtp_sendto(client, s_rtp_tx_buf, pkt_size);
        if (ret != AICAM_OK) {
            return ret;
        }

        offset += chunk;
        first = 0;
    }

    return AICAM_OK;
}

aicam_result_t rtsp_rtp_send_frame(rtsp_client_t *client,
                                    const uint8_t *data, uint32_t size,
                                    aicam_bool_t is_keyframe,
                                    uint32_t rtp_ts)
{
    if (!client || !data || size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    /* Log first 10 frames, all keyframes, and every 50th frame */
    {
        static uint32_t send_frame_count = 0;
        send_frame_count++;
        if (send_frame_count <= 10) {
            LOG_SVC_INFO("RTSP: rtp_ts_diag #%lu: rtp_ts=%lu seq=%lu",
                         (unsigned long)send_frame_count,
                         (unsigned long)rtp_ts,
                         (unsigned long)client->rtp_seq);
        }
    }

    /* Update client's RTP timestamp (all packets in a frame share the same timestamp) */
    client->rtp_timestamp = rtp_ts;

    /*
     * Parse NAL units from Annex-B data and send via RTP.
     * We need to set the RTP marker bit on the last SLICE NAL of the frame
     * (VBR/VLC uses this to detect frame boundaries).
     * Strategy: two-pass - first find the last slice NAL offset, then send all.
     */

    /* Pass 1: collect NAL offsets and find last slice NAL */
    #define MAX_NALS_PER_FRAME 16
    static uint32_t nal_offsets[MAX_NALS_PER_FRAME];
    static uint32_t nal_sizes[MAX_NALS_PER_FRAME];
    static uint8_t  nal_types[MAX_NALS_PER_FRAME];
    uint32_t nal_count = 0;
    int last_slice_idx = -1;

    {
        uint32_t offset = 0;
        uint32_t nal_start = 0;
        aicam_bool_t found_nal = AICAM_FALSE;

        while (offset < size) {
            if (offset + 3 < size &&
                data[offset] == 0 && data[offset + 1] == 0) {

                uint32_t sc_len = 0;
                if (data[offset + 2] == 1) {
                    sc_len = 3;
                } else if (offset + 4 <= size && data[offset + 2] == 0 && data[offset + 3] == 1) {
                    sc_len = 4;
                }

                if (sc_len > 0) {
                    if (found_nal) {
                        uint32_t nal_size = offset - nal_start;
                        if (nal_size > 0 && nal_count < MAX_NALS_PER_FRAME) {
                            nal_offsets[nal_count] = nal_start;
                            nal_sizes[nal_count] = nal_size;
                            nal_types[nal_count] = data[nal_start] & 0x1F;
                            if (nal_types[nal_count] != 7 && nal_types[nal_count] != 8) {
                                last_slice_idx = (int)nal_count;
                            }
                            nal_count++;
                        }
                    }
                    nal_start = offset + sc_len;
                    found_nal = AICAM_TRUE;
                    offset += sc_len;
                    continue;
                }
            }
            offset++;
        }

        /* Last NAL */
        if (found_nal && nal_start < size) {
            uint32_t nal_size = size - nal_start;
            if (nal_size > 0 && nal_count < MAX_NALS_PER_FRAME) {
                nal_offsets[nal_count] = nal_start;
                nal_sizes[nal_count] = nal_size;
                nal_types[nal_count] = data[nal_start] & 0x1F;
                if (nal_types[nal_count] != 7 && nal_types[nal_count] != 8) {
                    last_slice_idx = (int)nal_count;
                }
                nal_count++;
            }
        }
    }

    /* Log first 10 frames, all keyframes, and every 50th frame */
    {
        static uint32_t send_frame_count = 0;
        send_frame_count++;
        if (send_frame_count <= 10 || is_keyframe || (send_frame_count % 50) == 0) {
            static char nal_info[128];
            int pos = 0;
            for (uint32_t i = 0; i < nal_count && pos < 100; i++) {
                pos += snprintf(nal_info + pos, sizeof(nal_info) - pos,
                               "%s%d(%u)", (i > 0 ? "," : ""),
                               nal_types[i], (unsigned)nal_sizes[i]);
            }
            LOG_SVC_INFO("RTSP: send_frame #%lu: size=%lu key=%d nals=%lu last_slice=%d types=[%s] sock=%d",
                         (unsigned long)send_frame_count,
                         (unsigned long)size, is_keyframe,
                         (unsigned long)nal_count, last_slice_idx,
                         nal_info, client->rtp_socket);
        }
    }

    if (nal_count == 0) {
        LOG_SVC_WARN("RTSP: No NALs found in frame (size=%lu, first_bytes=%02X %02X %02X %02X)",
                     (unsigned long)size,
                     size > 0 ? data[0] : 0, size > 1 ? data[1] : 0,
                     size > 2 ? data[2] : 0, size > 3 ? data[3] : 0);
        return AICAM_OK;
    }

    /* Pass 2: send all NALs, set marker on the last slice */
    for (uint32_t i = 0; i < nal_count; i++) {
        uint8_t marker = ((int)i == last_slice_idx) ? 1 : 0;
        aicam_result_t ret = rtsp_rtp_send_single_nal(
            client, data + nal_offsets[i], nal_sizes[i], rtp_ts, marker);
        if (ret != AICAM_OK) {
            LOG_SVC_ERROR("RTSP: RTP send failed nal=%lu/%lu type=%d size=%lu",
                         (unsigned long)i, (unsigned long)nal_count,
                         nal_types[i], (unsigned long)nal_sizes[i]);
            return ret;
        }
    }

    return AICAM_OK;
}
