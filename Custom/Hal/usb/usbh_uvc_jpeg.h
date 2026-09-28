/**
 * @file    usbh_uvc_jpeg.h
 * @brief   UVC MJPEG decode helpers on the jpegc hardware decoder.
 *
 * jpegc (JPEG_USE_SOFT_CONV=0) decodes to RAW YCbCr and requires the frame
 * dimensions up-front (a mismatch aborts with JPEG_MODE_ERROR), so this
 * helper parses the SOF marker first and derives w/h + chroma subsampling
 * from the JPEG itself instead of trusting session state.
 */
#ifndef USBH_UVC_JPEG_H
#define USBH_UVC_JPEG_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t chroma_subsampling; /* JPEG_420/422/444_SUBSAMPLING */
} uvc_jpeg_info_t;

/* Parse SOI/SOF: dimensions + subsampling of a baseline JPEG. 0 ok. */
int uvc_jpeg_parse_header(const uint8_t *jpeg, uint32_t len, uvc_jpeg_info_t *info);

/* Hardware-decode one MJPEG frame into a jpegc-owned raw YCbCr buffer.
 * Valid until the next jpegc decode; free with JPEGC_CMD_RETURN_DEC_BUFFER.
 * Returns 0 ok, <0 error. */
int uvc_jpeg_decode_ycbcr(const uint8_t *jpeg, uint32_t len,
                          uint8_t **ycbcr, uint32_t *ycbcr_len,
                          uvc_jpeg_info_t *info);

/* Raster ownership lock: the jpegc decode raster is a SINGLE shared buffer,
 * so a decode must not start while another consumer still converts/draws
 * from the previous one. Use the bounded trylock: on timeout SKIP the
 * work - proceeding without the lock reconfigures jpegc under another
 * consumer and can wedge the codec. */
int uvc_jpeg_raster_trylock(uint32_t timeout_ms);
void uvc_jpeg_raster_unlock(void);

#endif /* USBH_UVC_JPEG_H */
