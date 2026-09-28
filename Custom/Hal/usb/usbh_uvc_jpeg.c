/**
 * @file    usbh_uvc_jpeg.c
 * @brief   UVC MJPEG decode helpers (header parse + jpegc hardware decode).
 *
 * The camera delivers final JPEGs; the system only ever needs to DECODE them
 * (AI model input, AI-overlay capture variant). jpegc decodes to raw YCbCr
 * and validates the pre-set dimensions against the stream, so the SOF header
 * is parsed first and drives the decode configuration.
 */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "aicam_error.h"
#include "jpegc.h"
#include "camera.h"          /* JPEG_DEVICE_NAME */
#include "usbh_uvc_jpeg.h"
#include "stm32n6xx_hal.h"

static device_t *s_jpeg_dev;
static uint32_t s_dec_w, s_dec_h; /* raster size currently configured */
static osMutexId_t s_raster_mtx;  /* decode raster read window (see header) */

/* Pick the decode-output allocation class: the exact subsampling when its
 * %16 validation passes, else the next larger class, else 444. The param
 * only sizes the raster (the decoder parses the real stream), so the
 * smallest class that still covers the actual byte need saves MBs at high
 * resolutions (4K 422: 15.8MB exact vs 23.7MB with a blanket 444). */
static uint32_t uvc_dec_alloc_subsampling(const uvc_jpeg_info_t *pi)
{
    switch (pi->chroma_subsampling) {
    case JPEG_444_SUBSAMPLING:
        return JPEG_444_SUBSAMPLING;
    case JPEG_422_SUBSAMPLING:
        if ((pi->width % 16u) == 0u) {
            return JPEG_422_SUBSAMPLING;
        }
        return JPEG_444_SUBSAMPLING;
    case JPEG_420_SUBSAMPLING:
    default:
        if ((pi->width % 16u) == 0u && (pi->height % 16u) == 0u) {
            return JPEG_420_SUBSAMPLING;
        }
        if ((pi->width % 16u) == 0u) {
            return JPEG_422_SUBSAMPLING; /* 2 B/px covers the 1.5 B/px need */
        }
        return JPEG_444_SUBSAMPLING;
    }
}

int uvc_jpeg_raster_trylock(uint32_t timeout_ms)
{
    if (s_raster_mtx == NULL) {
        s_raster_mtx = osMutexNew(NULL);
    }
    if (s_raster_mtx == NULL) {
        return -1;
    }
    if (osMutexAcquire(s_raster_mtx, timeout_ms) != osOK) {
        printf("[uvcjpg] raster lock timeout\r\n");
        return -1;
    }
    return 0;
}

void uvc_jpeg_raster_unlock(void)
{
    if (s_raster_mtx != NULL) {
        (void)osMutexRelease(s_raster_mtx);
    }
}

static device_t *uvc_jpeg_dev(void)
{
    if (s_jpeg_dev == NULL) {
        s_jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    }
    return s_jpeg_dev;
}

int uvc_jpeg_parse_header(const uint8_t *jpeg, uint32_t len, uvc_jpeg_info_t *info)
{
    if (jpeg == NULL || len < 4 || info == NULL) {
        return -1;
    }
    if (jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
        return -1; /* no SOI */
    }

    memset(info, 0, sizeof(*info));
    uint32_t i = 2;
    while ((i + 4) < len) {
        if (jpeg[i] != 0xFF) {
            return -1; /* marker stream desync */
        }
        uint8_t marker = jpeg[i + 1];
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            i += 2; /* standalone markers carry no length */
            continue;
        }
        uint32_t seg_len = ((uint32_t)jpeg[i + 2] << 8) | jpeg[i + 3];
        if (seg_len < 2 || (i + 2 + seg_len) > len) {
            return -1;
        }

        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) { /* SOF0/1/2 */
            if (seg_len < 8) {
                return -1;
            }
            info->height = ((uint32_t)jpeg[i + 5] << 8) | jpeg[i + 6];
            info->width = ((uint32_t)jpeg[i + 7] << 8) | jpeg[i + 8];
            if (info->width == 0 || info->height == 0) {
                return -1;
            }
            /* component sampling factors -> subsampling class */
            uint32_t ncomp = jpeg[i + 9];
            uint32_t max_hv = 1, sum_hv = 0;
            for (uint32_t c = 0; c < ncomp && (10 + c * 3 + 1) <= seg_len; c++) {
                uint32_t hv = jpeg[i + 10 + c * 3 + 1];
                uint32_t h = (hv >> 4) & 0xF;
                uint32_t v = hv & 0xF;
                if (h * v > max_hv) {
                    max_hv = h * v;
                }
                sum_hv += h * v;
            }
            if (sum_hv == 6) {
                info->chroma_subsampling = JPEG_420_SUBSAMPLING;
            } else if (sum_hv == 4 || sum_hv == 8) {
                info->chroma_subsampling = JPEG_422_SUBSAMPLING;
            } else {
                info->chroma_subsampling = JPEG_444_SUBSAMPLING;
            }
            (void)max_hv;
            return 0;
        }
        if (marker == 0xDA) { /* SOS: no SOF before scan data */
            return -1;
        }
        i += 2 + seg_len;
    }
    return -1;
}

int uvc_jpeg_decode_ycbcr(const uint8_t *jpeg, uint32_t len,
                          uint8_t **ycbcr, uint32_t *ycbcr_len,
                          uvc_jpeg_info_t *info)
{
    device_t *dev = uvc_jpeg_dev();
    uvc_jpeg_info_t parsed;

    if (dev == NULL || jpeg == NULL || ycbcr == NULL || ycbcr_len == NULL) {
        printf("[uvcjpg] dev/buf null\r\n");
        return -1;
    }
    if (uvc_jpeg_parse_header(jpeg, len, &parsed) != 0) {
        printf("[uvcjpg] header parse fail (len %u head %02x %02x)\r\n",
               (unsigned)len, jpeg[0], jpeg[1]);
        return -1;
    }
    if ((parsed.width % 8u) != 0u || (parsed.height % 8u) != 0u) {
        printf("[uvcjpg] dims %ux%u not %%8\r\n",
               (unsigned)parsed.width, (unsigned)parsed.height);
        return -1; /* jpegc SET_DEC_PARAM constraint */
    }

    if (parsed.width != s_dec_w || parsed.height != s_dec_h) {
        jpegc_params_t dp = {0};
        dp.ImageWidth = parsed.width;
        dp.ImageHeight = parsed.height;
        dp.ColorSpace = JPEG_YCBCR_COLORSPACE;
        dp.ChromaSubsampling = uvc_dec_alloc_subsampling(&parsed);
        int ret = device_ioctl(dev, JPEGC_CMD_SET_DEC_PARAM, (uint8_t *)&dp, sizeof(dp));
        printf("[uvcjpg] set_dec %ux%u css=%u -> %d\r\n",
               (unsigned)parsed.width, (unsigned)parsed.height,
               (unsigned)dp.ChromaSubsampling, ret);
        if (ret != 0) {
            return -1;
        }
        s_dec_w = parsed.width;
        s_dec_h = parsed.height;
    }

    {
        int ret = device_ioctl(dev, JPEGC_CMD_INPUT_DEC_BUFFER, (uint8_t *)jpeg, len);
        if (ret != 0) {
            /* GET_STATE writes exactly ONE byte (jpegc mode fits in it) */
            unsigned char mode = 0xFF;
            (void)device_ioctl(dev, JPEGC_CMD_GET_STATE, (uint8_t *)&mode, 0);

            if (mode != 0) {
                /* another consumer owns jpegc (capture encode / overlay
                 * decode): retry on the next call, never touch its state */
                return -1;
            }

            /* mode idle but input refused: dec_output_buffer went away
             * (e.g. generate_inference_image RETURNed it after its use).
             * Re-arm once with the current frame's dimensions and retry. */
            jpegc_params_t dp = {0};
            dp.ImageWidth = parsed.width;
            dp.ImageHeight = parsed.height;
            dp.ColorSpace = JPEG_YCBCR_COLORSPACE;
            dp.ChromaSubsampling = uvc_dec_alloc_subsampling(&parsed);
            int sret = device_ioctl(dev, JPEGC_CMD_SET_DEC_PARAM, (uint8_t *)&dp, sizeof(dp));

            int retry = device_ioctl(dev, JPEGC_CMD_INPUT_DEC_BUFFER, (uint8_t *)jpeg, len);
            printf("[uvcjpg] input_dec -> %d, re-set -> %d, retry -> %d\r\n",
                   ret, sret, retry);
            if (retry != 0 || sret != 0) {
                /* re-set now fails BUSY while another session (capture
                 * encode) holds the codec - skip, next interval retries */
                return -1;
            }
            s_dec_w = parsed.width;
            s_dec_h = parsed.height;
        }
    }

    unsigned char *out = NULL;
    int n = device_ioctl(dev, JPEGC_CMD_OUTPUT_DEC_BUFFER, (unsigned char *)&out, 0);
    if (n <= 0 || out == NULL) {
        /* -3 INVALID_DATA: the HW flagged a decode data error; -6 BUSY:
         * decode-complete timeout (HW stall, reboot-only). Either way the
         * cached SET_DEC_PARAM state is suspect - drop it so the next
         * attempt re-arms the decoder from scratch. */
        if (n == AICAM_ERROR_INVALID_DATA || n == AICAM_ERROR_BUSY) {
            s_dec_w = 0;
            s_dec_h = 0;
        }
        jpegc_params_t di = {0};
        (void)device_ioctl(dev, JPEGC_CMD_GET_DEC_INFO, (uint8_t *)&di, sizeof(di));
        printf("[uvcjpg] output_dec -> %d (info %ux%u css=%u q=%u)\r\n", n,
               (unsigned)di.ImageWidth, (unsigned)di.ImageHeight,
               (unsigned)di.ChromaSubsampling, (unsigned)di.ImageQuality);
        return -1;
    }
    *ycbcr = out;
    *ycbcr_len = (uint32_t)n;
    if (info != NULL) {
        *info = parsed;
        /* The decoder parses the real stream header: let its detected
         * geometry/subsampling override ours (SOF variants are easy to
         * misread; a wrong css garbles the chroma planes on convert). */
        jpegc_params_t actual = {0};
        if (device_ioctl(dev, JPEGC_CMD_GET_DEC_INFO, (uint8_t *)&actual,
                         sizeof(actual)) == 0) {
            if (actual.ImageWidth != 0 && actual.ImageHeight != 0) {
                info->width = actual.ImageWidth;
                info->height = actual.ImageHeight;
            }
            if (actual.ChromaSubsampling <= JPEG_422_SUBSAMPLING) {
                info->chroma_subsampling = actual.ChromaSubsampling;
            }
        }
    }
    return 0;
}
