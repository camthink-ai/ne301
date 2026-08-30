/**
 * @file rtmp_publisher.c
 * @brief Simple RTMP Publisher Library Implementation (No Encryption)
 */

#include "rtmp_publisher.h"
#include "librtmp/rtmp.h"
#include "librtmp/log.h"
#include "librtmp/amf.h"
#include "Hal/mem.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stddef.h>

/* Forward declarations */
extern uint32_t osKernelGetTickCount(void);

/* ==================== Internal Structures ==================== */

struct rtmp_publisher {
    RTMP *rtmp;
    rtmp_pub_config_t config;
    rtmp_pub_stats_t stats;
    bool is_connected;
    bool sps_pps_sent;
    uint32_t stream_start_time;
    uint8_t *sps_data;
    uint32_t sps_size;
    uint8_t *pps_data;
    uint32_t pps_size;
};

/* ==================== Helper Functions ==================== */

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void)
{
    return osKernelGetTickCount();
}

/**
 * @brief Find NAL unit start code (0x00000001 or 0x000001)
 */
static const uint8_t* find_nal_start(const uint8_t *data, uint32_t size, uint32_t *nal_size)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    
    while (p < end - 3) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            // Found 4-byte start code
            const uint8_t *nal_start = p + 4;
            const uint8_t *next_start = nal_start;
            
            // Find next start code
            while (end - next_start >= 4) {
                if (next_start[0] == 0 && next_start[1] == 0 &&
                    next_start[2] == 0 && next_start[3] == 1) {
                    break;
                }
                next_start++;
            }

            // None found: this NAL runs to the end of the buffer
            if (end - next_start < 4) {
                next_start = end;
            }

            *nal_size = (uint32_t)(next_start - nal_start);
            return nal_start;
        } else if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            // Found 3-byte start code
            const uint8_t *nal_start = p + 3;
            const uint8_t *next_start = nal_start;
            
            // Find next start code; the length guard keeps next_start[3]
            // inside the buffer
            while (end - next_start >= 3) {
                if (next_start[0] == 0 && next_start[1] == 0 &&
                    (next_start[2] == 1 ||
                     (end - next_start >= 4 && next_start[2] == 0 && next_start[3] == 1))) {
                    break;
                }
                next_start++;
            }

            if (end - next_start < 3) {
                next_start = end;
            }

            *nal_size = (uint32_t)(next_start - nal_start);
            return nal_start;
        }
        p++;
    }
    
    return NULL;
}

/**
 * @brief Extract SPS and PPS from H.264 data
 */
static int extract_sps_pps(const uint8_t *data, uint32_t size,
                           uint8_t **sps, uint32_t *sps_size,
                           uint8_t **pps, uint32_t *pps_size)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    
    *sps = NULL;
    *sps_size = 0;
    *pps = NULL;
    *pps_size = 0;
    
    while (p < end - 3) {
        uint32_t nal_size = 0;
        const uint8_t *nal = find_nal_start(p, end - p, &nal_size);
        
        if (nal == NULL || nal_size == 0) {
            break;
        }
        
        uint8_t nal_type = nal[0] & 0x1F;
        
        if (nal_type == 7) { // SPS
            *sps = (uint8_t*)hal_mem_alloc_large(nal_size);
            if (*sps) {
                memcpy(*sps, nal, nal_size);
                *sps_size = nal_size;
            }
        } else if (nal_type == 8) { // PPS
            *pps = (uint8_t*)hal_mem_alloc_large(nal_size);
            if (*pps) {
                memcpy(*pps, nal, nal_size);
                *pps_size = nal_size;
            }
        }
        
        p = nal + nal_size;
    }
    
    return (*sps && *pps) ? 0 : -1;
}

/* ==================== API Implementation ==================== */

rtmp_publisher_t* rtmp_publisher_create(const rtmp_pub_config_t *config)
{
    if (!config || !config->url[0]) {
        return NULL;
    }
    
    rtmp_publisher_t *pub = (rtmp_publisher_t*)hal_mem_calloc_large(1, sizeof(rtmp_publisher_t));
    if (!pub) {
        return NULL;
    }
    
    pub->rtmp = RTMP_Alloc();
    if (!pub->rtmp) {
        hal_mem_free(pub);
        return NULL;
    }
    
    RTMP_Init(pub->rtmp);
    RTMP_EnableWrite(pub->rtmp);
    
    memcpy(&pub->config, config, sizeof(rtmp_pub_config_t));
    pub->is_connected = false;
    pub->sps_pps_sent = false;
    pub->stream_start_time = 0;
    
    return pub;
}

void rtmp_publisher_destroy(rtmp_publisher_t *pub)
{
    if (!pub) {
        return;
    }
    
    if (pub->is_connected) {
        rtmp_publisher_disconnect(pub);
    }
    
    if (pub->rtmp) {
        RTMP_Free(pub->rtmp);
    }
    
    if (pub->sps_data) {
        hal_mem_free(pub->sps_data);
    }
    
    if (pub->pps_data) {
        hal_mem_free(pub->pps_data);
    }
    
    hal_mem_free(pub);
}

rtmp_pub_err_t rtmp_publisher_connect(rtmp_publisher_t *pub)
{
    if (!pub || !pub->rtmp) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    if (pub->is_connected) {
        return RTMP_PUB_OK;
    }
    
    // Reset tcUrl so SetupURL properly re-initializes it on reconnect (CloseInternal does not
    // clear tcUrl.av_len when RTMP_LF_FTCU is unset, causing SetupURL to skip tcUrl on next call)
    pub->rtmp->Link.tcUrl.av_val = NULL;
    pub->rtmp->Link.tcUrl.av_len = 0;

    // Setup URL. librtmp keeps pointers (hostname, app, tcUrl) into this buffer until
    // RTMP_Connect() completes (SendConnect serializes them), so do NOT free url until after Connect.
    size_t url_len = strlen(pub->config.url) + 1;
    char *url = (char*)hal_mem_alloc_large(url_len);
    if (!url) {
        return RTMP_PUB_ERR_MEMORY;
    }
    strcpy(url, pub->config.url);
    
    if (!RTMP_SetupURL(pub->rtmp, url)) {
        hal_mem_free(url);
        return RTMP_PUB_ERR_INIT_FAILED;
    }
    
    // Ensure RTMP_FEATURE_WRITE is set after SetupURL (in case it was cleared)
    RTMP_EnableWrite(pub->rtmp);
    
    RTMP_Log(RTMP_LOGINFO, "After SetupURL: protocol flags=0x%x", pub->rtmp->Link.protocol);
    
    // Set timeout
    if (pub->config.timeout_ms > 0) {
        pub->rtmp->Link.timeout = pub->config.timeout_ms / 1000;
        if (pub->rtmp->Link.timeout == 0) {
            pub->rtmp->Link.timeout = 1; // Minimum 1 second
        }
    } else {
        pub->rtmp->Link.timeout = 5; // Default 5 seconds
    }
    
    // Ensure RTMP_FEATURE_WRITE is set before Connect
    RTMP_EnableWrite(pub->rtmp);
    
    // Connect to server. Free url after Connect: librtmp's SendConnect (called inside RTMP_Connect)
    // serializes hostname/app/tcUrl from the url buffer; after that the buffer is no longer accessed.
    if (!RTMP_Connect(pub->rtmp, NULL)) {
        RTMP_Close(pub->rtmp);
        hal_mem_free(url);
        return RTMP_PUB_ERR_CONNECT_FAILED;
    }
    hal_mem_free(url);
    
    // Verify RTMP_FEATURE_WRITE is still set after Connect
    RTMP_Log(RTMP_LOGINFO, "After RTMP_Connect: protocol flags=0x%x", pub->rtmp->Link.protocol);
    
    // Connect stream
    // Log RTMP state before ConnectStream
    RTMP_Log(RTMP_LOGINFO, "Before ConnectStream: protocol flags=0x%x, stream_id=%d", 
             pub->rtmp->Link.protocol, pub->rtmp->m_stream_id);
    RTMP_Log(RTMP_LOGINFO, "RTMP URL: %s, playpath: %.*s, app: %.*s", 
             pub->config.url,
             pub->rtmp->Link.playpath.av_len, pub->rtmp->Link.playpath.av_val ? pub->rtmp->Link.playpath.av_val : "(null)",
             pub->rtmp->Link.app.av_len, pub->rtmp->Link.app.av_val ? pub->rtmp->Link.app.av_val : "(null)");
    
    if (!RTMP_ConnectStream(pub->rtmp, 0)) {
        RTMP_Close(pub->rtmp);
        return RTMP_PUB_ERR_PUBLISH_FAILED;
    }
    
    // Log RTMP state after ConnectStream
    RTMP_Log(RTMP_LOGINFO, "After ConnectStream: stream_id=%d, is_playing=%d, protocol flags=0x%x", 
             pub->rtmp->m_stream_id, pub->rtmp->m_bPlaying, pub->rtmp->Link.protocol);
    
    pub->is_connected = true;
    pub->stream_start_time = get_time_ms();
    pub->sps_pps_sent = false;
    
    rtmp_pub_err_t ret = rtmp_publisher_send_metadata(pub);
    if (ret != RTMP_PUB_OK) {
        RTMP_Log(RTMP_LOGWARNING,
                 "rtmp_publisher_connect: send metadata failed (%d), continue without it",
                 ret);
        return ret;
    }

    ret = rtmp_publisher_set_chunk_size(pub, rtmp_publisher_get_chunk_size(pub));
    if (ret != RTMP_PUB_OK) {
        RTMP_Log(RTMP_LOGWARNING,
                 "rtmp_publisher_connect: set chunk size failed (%d), continue without it",
                 ret);
        return ret;
    }

    return RTMP_PUB_OK;
}

void rtmp_publisher_disconnect(rtmp_publisher_t *pub)
{
    if (!pub || !pub->rtmp) {
        return;
    }
    
    if (pub->is_connected) {
        RTMP_Close(pub->rtmp);
        pub->is_connected = false;
    }
}

bool rtmp_publisher_is_connected(rtmp_publisher_t *pub)
{
    if (!pub || !pub->rtmp) {
        return false;
    }
    
    return pub->is_connected && RTMP_IsConnected(pub->rtmp);
}

rtmp_pub_err_t rtmp_publisher_send_sps_pps(rtmp_publisher_t *pub,
                                            const uint8_t *sps,
                                            uint32_t sps_size,
                                            const uint8_t *pps,
                                            uint32_t pps_size)
{
    if (!pub || !sps || !pps || sps_size == 0 || pps_size == 0) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    if (!pub->is_connected) {
        return RTMP_PUB_ERR_NOT_CONNECTED;
    }
    
    // Store SPS/PPS
    if (pub->sps_data) {
        hal_mem_free(pub->sps_data);
    }
    if (pub->pps_data) {
        hal_mem_free(pub->pps_data);
    }
    
    pub->sps_data = (uint8_t*)hal_mem_alloc_large(sps_size);
    pub->pps_data = (uint8_t*)hal_mem_alloc_large(pps_size);
    
    if (!pub->sps_data || !pub->pps_data) {
        if (pub->sps_data) hal_mem_free(pub->sps_data);
        if (pub->pps_data) hal_mem_free(pub->pps_data);
        pub->sps_data = NULL;
        pub->pps_data = NULL;
        return RTMP_PUB_ERR_MEMORY;
    }
    
    memcpy(pub->sps_data, sps, sps_size);
    memcpy(pub->pps_data, pps, pps_size);
    pub->sps_size = sps_size;
    pub->pps_size = pps_size;
    
    // Send SPS/PPS as video sequence header
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    
    // Calculate body size: 
    // 5 bytes (video tag header) +
    // 1 byte (configuration version) +
    // 3 bytes (profile, compatibility, level) +
    // 1 byte (length size minus one + reserved) +
    // 1 byte (number of SPS + reserved) +
    // 2 bytes (SPS length) +
    // sps_size bytes (SPS data) +
    // 1 byte (number of PPS) +
    // 2 bytes (PPS length) +
    // pps_size bytes (PPS data)
    uint32_t body_size = 5 + 1 + 3 + 1 + 1 + 2 + sps_size + 1 + 2 + pps_size;
    
    if (!RTMPPacket_Alloc(&packet, body_size)) {
        return RTMP_PUB_ERR_MEMORY;
    }
    
    uint8_t *body = (uint8_t*)packet.m_body;
    
    // Video tag header
    body[0] = 0x17; // Frame type: keyframe, CodecID: H264
    body[1] = 0x00; // AVC sequence header
    body[2] = 0x00; // Composition time (3 bytes, 0 for sequence header)
    body[3] = 0x00;
    body[4] = 0x00;
    
    // SPS
    body[5] = 0x01; // Configuration version
    body[6] = sps[1]; // AVC profile (from SPS)
    body[7] = sps[2]; // Profile compatibility
    body[8] = sps[3]; // AVC level
    body[9] = 0xE3; // 6 bits reserved (111000) + 2 bits length size minus one (11 = 3, means 4 bytes)
    body[10] = 0xE1; // 3 bits reserved + 5 bits number of SPS
    
    // SPS length (2 bytes)
    body[11] = (sps_size >> 8) & 0xFF;
    body[12] = sps_size & 0xFF;
    memcpy(&body[13], sps, sps_size);
    
    uint32_t offset = 13 + sps_size;
    
    // PPS
    body[offset] = 0x01; // Number of PPS
    offset++;
    
    // PPS length (2 bytes)
    body[offset] = (pps_size >> 8) & 0xFF;
    body[offset + 1] = pps_size & 0xFF;
    offset += 2;
    memcpy(&body[offset], pps, pps_size);
    
    // Validate SPS/PPS packet
    if (body[0] != 0x17) {
        RTMP_Log(RTMP_LOGERROR, "Invalid SPS/PPS tag header: 0x%02x (expected 0x17)", body[0]);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    if (body_size < 20) {
        RTMP_Log(RTMP_LOGERROR, "SPS/PPS packet too small: %lu", (unsigned long)body_size);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    // Ensure packet type is set BEFORE setting header type
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO; // Must be set first!
    packet.m_nChannel = 0x04; // Video channel
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = pub->rtmp->m_stream_id;
    packet.m_nBodySize = body_size;
    packet.m_hasAbsTimestamp = 0;
    
    // Verify packet type is correct
    if (packet.m_packetType != RTMP_PACKET_TYPE_VIDEO) {
        RTMP_Log(RTMP_LOGERROR, "Invalid SPS/PPS packet type: 0x%02x (expected 0x%02x)", 
                 packet.m_packetType, RTMP_PACKET_TYPE_VIDEO);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    RTMP_Log(RTMP_LOGDEBUG, "Sending SPS/PPS: packetType=0x%02x, channel=0x%02x, bodySize=%lu, first_byte=0x%02x, headerType=%d", 
             packet.m_packetType, packet.m_nChannel, (unsigned long)packet.m_nBodySize, body[0], packet.m_headerType);
    
    if (!RTMP_SendPacket(pub->rtmp, &packet, 0)) {
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_SEND_FAILED;
    }
    
    RTMPPacket_Free(&packet);
    pub->sps_pps_sent = true;
    
    return RTMP_PUB_OK;
}

rtmp_pub_err_t rtmp_publisher_send_video_frame(rtmp_publisher_t *pub,
                                                const uint8_t *data,
                                                uint32_t size,
                                                bool is_keyframe,
                                                uint32_t timestamp_ms)
{
    if (!pub || !data || size == 0) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    if (!pub->is_connected) {
        return RTMP_PUB_ERR_NOT_CONNECTED;
    }
    
    // Check if we need to send SPS/PPS first
    if (!pub->sps_pps_sent) {
        // Try to extract SPS/PPS from data
        uint8_t *sps = NULL, *pps = NULL;
        uint32_t sps_size = 0, pps_size = 0;
        
        if (extract_sps_pps(data, size, &sps, &sps_size, &pps, &pps_size) == 0) {
            rtmp_pub_err_t ret = rtmp_publisher_send_sps_pps(pub, sps, sps_size, pps, pps_size);
            if (sps) hal_mem_free(sps);
            if (pps) hal_mem_free(pps);
            if (ret != RTMP_PUB_OK) {
                return ret;
            }
        } else {
            /* extract_sps_pps may have allocated one of sps/pps before failing; free to avoid leak */
            if (sps) hal_mem_free(sps);
            if (pps) hal_mem_free(pps);
            // If we have stored SPS/PPS, use them
            if (pub->sps_data && pub->pps_data) {
                rtmp_pub_err_t ret = rtmp_publisher_send_sps_pps(pub, 
                                                                 pub->sps_data, pub->sps_size,
                                                                 pub->pps_data, pub->pps_size);
                if (ret != RTMP_PUB_OK) {
                    return ret;
                }
            } else {
                // Cannot send frame without SPS/PPS
                return RTMP_PUB_ERR_INVALID_ARG;
            }
        }
    }
    
    // Create video packet
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    
    /*
     * Convert Annex-B format (with start codes) to AVCC format (with length prefixes)
     * FLV/RTMP requires AVCC: [4-byte NALU length][NALU data]...
     * Encoder outputs Annex-B: [0x00000001/0x000001][NALU data]...
     */
    
    // First pass: calculate total body size
    uint32_t body_size = 5; // 5 bytes video tag header
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    
    uint32_t nal_count = 0;
    while (p < end - 3) {
        uint32_t nal_size = 0;
        const uint8_t *nal = find_nal_start(p, (uint32_t)(end - p), &nal_size);
        if (!nal || nal_size == 0) {
            break;
        }
        body_size += 4 + nal_size; // 4 bytes length + NALU data
        nal_count++;
        p = nal + nal_size;
    }
    
    if (body_size <= 5) {
        // No valid NALUs found
        RTMP_Log(RTMP_LOGERROR, "No valid NALUs found in frame data: size=%lu, first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned long)size, 
                 size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0,
                 size > 4 ? data[4] : 0, size > 5 ? data[5] : 0, size > 6 ? data[6] : 0, size > 7 ? data[7] : 0);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    RTMP_Log(RTMP_LOGDEBUG, "Frame conversion: input_size=%lu, nal_count=%lu, output_body_size=%lu", 
             (unsigned long)size, (unsigned long)nal_count, (unsigned long)body_size);
    
    if (!RTMPPacket_Alloc(&packet, body_size)) {
        return RTMP_PUB_ERR_MEMORY;
    }
    
    uint8_t *body = (uint8_t*)packet.m_body;
    
    // Video tag header
    if (is_keyframe) {
        body[0] = 0x17; // Frame type: keyframe, CodecID: H264
    } else {
        body[0] = 0x27; // Frame type: inter frame, CodecID: H264
    }
    body[1] = 0x01; // AVCPacketType: AVC NALU
    body[2] = 0x00; // Composition time (3 bytes, 0 for now)
    body[3] = 0x00;
    body[4] = 0x00;
    
    // Second pass: convert Annex-B to AVCC format
    uint32_t offset = 5;
    p = data;
    uint32_t converted_nal_count = 0;
    
    while (p < end - 3 && offset + 4 < body_size) {
        uint32_t nal_size = 0;
        const uint8_t *nal = find_nal_start(p, (uint32_t)(end - p), &nal_size);
        if (!nal || nal_size == 0) {
            RTMP_Log(RTMP_LOGWARNING, "Failed to find NALU at offset %lu, remaining=%lu", 
                     (unsigned long)(p - data), (unsigned long)(end - p));
            break;
        }
        
        // Write NALU length in big-endian (4 bytes)
        body[offset + 0] = (uint8_t)((nal_size >> 24) & 0xFF);
        body[offset + 1] = (uint8_t)((nal_size >> 16) & 0xFF);
        body[offset + 2] = (uint8_t)((nal_size >> 8) & 0xFF);
        body[offset + 3] = (uint8_t)(nal_size & 0xFF);
        offset += 4;
        
        // Copy NALU data (without start code)
        if (offset + nal_size > body_size) {
            RTMP_Log(RTMP_LOGWARNING, "NALU size exceeds body_size: offset=%lu, nal_size=%lu, body_size=%lu", 
                     (unsigned long)offset, (unsigned long)nal_size, (unsigned long)body_size);
            nal_size = body_size - offset; // Safety check
        }
        memcpy(&body[offset], nal, nal_size);
        offset += nal_size;
        converted_nal_count++;
        
        p = nal + nal_size;
    }
    
    if (converted_nal_count == 0) {
        RTMP_Log(RTMP_LOGERROR, "Failed to convert any NALUs, input_size=%lu", (unsigned long)size);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    // Update actual body size (use offset, which is the actual converted size)
    packet.m_nBodySize = offset;
    
    // Validate packet before sending
    if (packet.m_nBodySize < 10) {
        RTMP_Log(RTMP_LOGWARNING, "Packet body too small: %lu (skipping frame, input_size=%lu)", 
                 (unsigned long)packet.m_nBodySize, (unsigned long)size);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    // Check first byte of body (should be 0x17 or 0x27 for H.264)
    if (body[0] != 0x17 && body[0] != 0x27) {
        RTMP_Log(RTMP_LOGERROR, "Invalid video tag header: 0x%02x (expected 0x17 or 0x27)", body[0]);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    // Ensure packet type is set BEFORE setting header type
    // RTMP library may optimize header to MINIMUM, which inherits packet type from previous packet
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO; // Must be set first!
    packet.m_nChannel = 0x04; // Video channel
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_nTimeStamp = timestamp_ms;
    packet.m_nInfoField2 = pub->rtmp->m_stream_id;
    packet.m_hasAbsTimestamp = 1;
    
    // Verify packet type is correct before sending
    if (packet.m_packetType != RTMP_PACKET_TYPE_VIDEO) {
        RTMP_Log(RTMP_LOGERROR, "Invalid packet type: 0x%02x (expected 0x%02x)", 
                 packet.m_packetType, RTMP_PACKET_TYPE_VIDEO);
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    RTMP_Log(RTMP_LOGDEBUG2, "Sending video frame: packetType=0x%02x, channel=0x%02x, bodySize=%lu, timestamp=%lu, is_keyframe=%d, first_byte=0x%02x, nal_count=%lu, headerType=%d", 
             packet.m_packetType, packet.m_nChannel, (unsigned long)packet.m_nBodySize, (unsigned long)timestamp_ms,
             is_keyframe, body[0], (unsigned long)converted_nal_count, packet.m_headerType);
    
    if (!RTMP_SendPacket(pub->rtmp, &packet, 0)) {
        RTMPPacket_Free(&packet);
        pub->stats.errors++;
        return RTMP_PUB_ERR_SEND_FAILED;
    }
    
    RTMPPacket_Free(&packet);
    
    // Update statistics
    pub->stats.frames_sent++;
    pub->stats.bytes_sent += size;
    pub->stats.last_frame_size = size;
    if (pub->stats.frames_sent > 0) {
        pub->stats.avg_frame_size = pub->stats.bytes_sent / pub->stats.frames_sent;
    }
    
    return RTMP_PUB_OK;
}

rtmp_pub_err_t rtmp_publisher_get_stats(rtmp_publisher_t *pub, rtmp_pub_stats_t *stats)
{
    if (!pub || !stats) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }
    
    memcpy(stats, &pub->stats, sizeof(rtmp_pub_stats_t));
    return RTMP_PUB_OK;
}

void rtmp_publisher_reset_stats(rtmp_publisher_t *pub)
{
    if (!pub) {
        return;
    }
    
    memset(&pub->stats, 0, sizeof(rtmp_pub_stats_t));
}

rtmp_pub_err_t rtmp_publisher_set_chunk_size(rtmp_publisher_t *pub, uint32_t chunk_size)
{
    if (!pub || !pub->rtmp) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }

    /* RTMP 规范中 chunk size 有效范围为 1–65536，这里做一下基本校验 */
    if (chunk_size == 0 || chunk_size > 65536U) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }

    /* 必须在已连接状态下发送控制包 */
    if (!RTMP_IsConnected(pub->rtmp)) {
        return RTMP_PUB_ERR_NOT_CONNECTED;
    }

    /* 构造 Set Chunk Size 控制包（packet type = 0x01，body 为 4 字节 chunk size） */
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);

    if (!RTMPPacket_Alloc(&packet, 4)) {
        return RTMP_PUB_ERR_MEMORY;
    }

    packet.m_packetType   = RTMP_PACKET_TYPE_CHUNK_SIZE;  /* 0x01 */
    packet.m_nChannel     = 0x02;                         /* 控制通道，一般使用 CSID=2 */
    packet.m_headerType   = RTMP_PACKET_SIZE_LARGE;       /* 完整 header */
    packet.m_nTimeStamp   = 0;
    packet.m_nInfoField2  = 0;                            /* 控制消息 streamId = 0 */
    packet.m_hasAbsTimestamp = 0;
    packet.m_nBodySize    = 4;

    /* 写入 4 字节 big-endian chunk size */
    packet.m_body[0] = (chunk_size >> 24) & 0xFF;
    packet.m_body[1] = (chunk_size >> 16) & 0xFF;
    packet.m_body[2] = (chunk_size >> 8)  & 0xFF;
    packet.m_body[3] = (chunk_size)       & 0xFF;

    RTMP_Log(RTMP_LOGINFO, "rtmp_publisher_set_chunk_size: sending SetChunkSize=%u",
             (unsigned int)chunk_size);

    if (!RTMP_SendPacket(pub->rtmp, &packet, 0)) {
        RTMP_Log(RTMP_LOGERROR, "rtmp_publisher_set_chunk_size: RTMP_SendPacket failed");
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_SEND_FAILED;
    }

    RTMPPacket_Free(&packet);

    /* 本地同步输入/输出 chunk size，保持与服务器一致 */
    pub->rtmp->m_outChunkSize = (int)chunk_size;
    pub->rtmp->m_inChunkSize  = (int)chunk_size;

    RTMP_Log(RTMP_LOGINFO, "rtmp_publisher_set_chunk_size: chunk_size=%u",
             (unsigned int)chunk_size);

    return RTMP_PUB_OK;
}

uint32_t rtmp_publisher_get_chunk_size(rtmp_publisher_t *pub)
{
    if (!pub || !pub->rtmp) {
        return 0;
    }

    /* 这里返回当前用于发送的 out chunk size */
    if (pub->rtmp->m_outChunkSize > 0) {
        return (uint32_t)pub->rtmp->m_outChunkSize;
    }

    /* 如果还没设置过，则返回默认值 */
    return (uint32_t)RTMP_DEFAULT_CHUNKSIZE;
}

rtmp_pub_err_t rtmp_publisher_send_metadata(rtmp_publisher_t *pub)
{
    if (!pub || !pub->rtmp) {
        return RTMP_PUB_ERR_INVALID_ARG;
    }

    if (!RTMP_IsConnected(pub->rtmp)) {
        return RTMP_PUB_ERR_NOT_CONNECTED;
    }

    /* 预留足够空间存放 onMetaData 脚本标签 */
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    if (!RTMPPacket_Alloc(&packet, 256)) {
        return RTMP_PUB_ERR_MEMORY;
    }

    char *body   = packet.m_body;
    char *end    = body + 256;
    char *enc    = body;

    /* AMF0 string: "onMetaData" */
    AVal metaName = { "onMetaData", 10 };
    enc = AMF_EncodeString(enc, end, &metaName);
    if (!enc) {
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_MEMORY;
    }

    /* AMF0 ECMA array，包含若干键值对 */
    *enc++ = AMF_ECMA_ARRAY;
    enc = AMF_EncodeInt32(enc, end, 5); /* 我们写 5 个属性 */
    if (!enc) {
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_MEMORY;
    }

    AVal name;

    /* width */
    name.av_val = "width";  name.av_len = 5;
    enc = AMF_EncodeNamedNumber(enc, end, &name, (double)pub->config.width);
    if (!enc) { RTMPPacket_Free(&packet); return RTMP_PUB_ERR_MEMORY; }

    /* height */
    name.av_val = "height"; name.av_len = 6;
    enc = AMF_EncodeNamedNumber(enc, end, &name, (double)pub->config.height);
    if (!enc) { RTMPPacket_Free(&packet); return RTMP_PUB_ERR_MEMORY; }

    /* framerate */
    name.av_val = "framerate"; name.av_len = 9;
    enc = AMF_EncodeNamedNumber(enc, end, &name, (double)pub->config.fps);
    if (!enc) { RTMPPacket_Free(&packet); return RTMP_PUB_ERR_MEMORY; }

    /* videocodecid: 7 = AVC/H.264 */
    name.av_val = "videocodecid"; name.av_len = 12;
    enc = AMF_EncodeNamedNumber(enc, end, &name, 7.0);
    if (!enc) { RTMPPacket_Free(&packet); return RTMP_PUB_ERR_MEMORY; }

    /* audiocodecid: 0 (no audio) */
    name.av_val = "audiocodecid"; name.av_len = 12;
    enc = AMF_EncodeNamedNumber(enc, end, &name, 0.0);
    if (!enc) { RTMPPacket_Free(&packet); return RTMP_PUB_ERR_MEMORY; }

    /* 结束 ECMA array: 0x00 0x00 0x09 */
    if (enc + 3 > end) {
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_MEMORY;
    }
    *enc++ = 0x00;
    *enc++ = 0x00;
    *enc++ = AMF_OBJECT_END;

    packet.m_nBodySize      = (uint32_t)(enc - body);
    packet.m_packetType     = RTMP_PACKET_TYPE_INFO;    /* Script data */
    packet.m_nChannel       = 0x05;                     /* 通常脚本数据用 channel 5 */
    packet.m_headerType     = RTMP_PACKET_SIZE_LARGE;
    packet.m_nTimeStamp     = 0;
    packet.m_hasAbsTimestamp= 1;
    packet.m_nInfoField2    = pub->rtmp->m_stream_id;   /* 绑定到当前 stream */

    RTMP_Log(RTMP_LOGINFO,
             "rtmp_publisher_send_metadata: width=%u, height=%u, fps=%u, bodySize=%u",
             (unsigned int)pub->config.width,
             (unsigned int)pub->config.height,
             (unsigned int)pub->config.fps,
             (unsigned int)packet.m_nBodySize);

    if (!RTMP_SendPacket(pub->rtmp, &packet, 0)) {
        RTMP_Log(RTMP_LOGERROR, "rtmp_publisher_send_metadata: RTMP_SendPacket failed");
        RTMPPacket_Free(&packet);
        return RTMP_PUB_ERR_SEND_FAILED;
    }

    RTMPPacket_Free(&packet);
    return RTMP_PUB_OK;
}

void rtmp_publisher_get_default_config(rtmp_pub_config_t *config)
{
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(rtmp_pub_config_t));
    config->width = 640;
    config->height = 480;
    config->fps = 30;
    config->timeout_ms = 5000;
    config->enable_audio = false;
}

