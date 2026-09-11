#ifndef _JPEGC_H
#define _JPEGC_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "aicam_error.h"

#define ENC_DEFAULT_CHROMA_SAMPLING     JPEG_420_SUBSAMPLING   /* Select Chroma Sampling: JPEG_420_SUBSAMPLING, JPEG_422_SUBSAMPLING, JPEG_444_SUBSAMPLING   */
#define ENC_DEFAULT_COLOR_SPACE         JPEG_YCBCR_COLORSPACE  /* Select Color Space: JPEG_YCBCR_COLORSPACE, JPEG_GRAYSCALE_COLORSPACE, JPEG_CMYK_COLORSPACE */
#define ENC_DEFAULT_IMAGE_QUALITY       80                     /* Set Image Quality for Jpeg Encoding */
#define MAX_INPUT_WIDTH                 2688                   /* Set the Maximum of BMP images Width to be tested */
#define MAX_INPUT_LINES                 16                     /* Set Input buffer lines to 16 for YCbCr420, and 8 for YCbCr422 and YCbCr444 (to save RAM space) */

#define DEFAULT_IMAGE_WIDTH  ((uint32_t)1280)
#define DEFAULT_IMAGE_HEIGHT ((uint32_t)720)

/* Minimum JPEG encode output buffer size (used as a lower bound) */
#define JPEG_ENCODE_OUTPUT_BUFFER_MIN_SIZE  (200 * 1024)
/* Maximum JPEG encode output buffer size (hard upper bound to avoid excessive allocations) */
#define JPEG_ENCODE_OUTPUT_BUFFER_MAX_SIZE  (8 * 1024 * 1024)
typedef enum {
    JPEG_MODE_IDLE = 0,
    JPEG_MODE_ENC = 1,           
    JPEG_MODE_DEC = 2,
    JPEG_MODE_ENC_COMPLETE = 3,
    JPEG_MODE_DEC_COMPLETE = 4,
    JPEG_MODE_ERROR = 5,
} jpegc_mode_e;


typedef enum {
    JPEGC_CMD_GET_STATE        = JPEGC_CMD_BASE,
    JPEGC_CMD_SET_ENC_PARAM,
    JPEGC_CMD_GET_ENC_PARAM,
    JPEGC_CMD_SET_DEC_PARAM,
    JPEGC_CMD_GET_DEC_INFO,
    JPEGC_CMD_INPUT_ENC_BUFFER,
    JPEGC_CMD_OUTPUT_ENC_BUFFER,
    JPEGC_CMD_INPUT_DEC_BUFFER,
    JPEGC_CMD_OUTPUT_DEC_BUFFER,
    JPEGC_CMD_RETURN_ENC_BUFFER,
    JPEGC_CMD_RETURN_DEC_BUFFER,
    JPEGC_CMD_UNSHARE_ENC_BUFFER,       // unshare the enc buffer to the caller, need reset the enc params after use
    JPEGC_CMD_FREE_ENC_BUFFER,          // free the unshared enc buffer
} JPEGC_CMD_E;

typedef struct
{
    uint8_t State;
    uint8_t *DataBuffer;
    uint32_t DataBufferSize;

}JPEG_Data_BufferTypeDef;

typedef struct {
    uint32_t ColorSpace;               /*!< Image Color space : gray-scale, YCBCR, RGB or CMYK
                                           This parameter can be a value of @ref JPEG_ColorSpace */
    uint32_t ChromaSubsampling;        /*!< Chroma Subsampling in case of YCBCR or CMYK color space, 0-> 4:4:4 , 1-> 4:2:2, 2 -> 4:1:1, 3 -> 4:2:0
                                            This parameter can be a value of @ref JPEG_ChromaSubsampling
                                            NOTE: If the specified chroma subsampling doesn't meet resolution requirements,
                                            it will be automatically adjusted:
                                            - JPEG_420_SUBSAMPLING: requires width and height to be multiples of 16
                                            - JPEG_422_SUBSAMPLING: requires width to be multiple of 16, height multiple of 8
                                            - JPEG_444_SUBSAMPLING: requires width and height to be multiples of 8 */
    uint32_t ImageHeight;              /*!< Image height : number of lines */
    uint32_t ImageWidth;               /*!< Image width : number of pixels per line */
    uint32_t ImageQuality;             /*!< Quality of the JPEG encoding : from 1 to 100 */
}jpegc_params_t;

typedef struct {
    bool is_init;
    device_t *dev;
    jpegc_mode_e mode;
    osMutexId_t mtx_id;
    osEventFlagsId_t evt_id;
    osSemaphoreId_t sem_id;
    osSemaphoreId_t sem_enc;
    osSemaphoreId_t sem_dec;
    jpegc_params_t enc_params;
    jpegc_params_t dec_params;
    jpegc_params_t dec_info;
    osThreadId_t jpegc_processId;
    uint8_t *enc_input_buffer;
    uint8_t *enc_output_buffer;
    uint32_t enc_output_buffer_size;
    uint32_t enc_output_buffer_capacity;
    bool enc_output_buffer_handed_out; /*!< OUTPUT_ENC_BUFFER gave the pointer to a caller (caller owns it) */

    uint8_t *dec_input_buffer;
    uint32_t dec_input_buffer_size;
    uint8_t *dec_output_buffer;
    uint32_t dec_output_buffer_size;
} jpegc_t;


int jpegc_register(void);
int jpegc_unregister(void);

#endif