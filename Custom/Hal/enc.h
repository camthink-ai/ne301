#ifndef ENC_H
#define ENC_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "aicam_error.h"
#include "h264encapi.h"
#include "jpegencapi.h"

#define USE_H264_VENC  1

#define VENC_ALLOCATOR_SIZE (4 * 1024 * 1024)

#define VENC_DEFAULT_WIDTH 1280
#define VENC_DEFAULT_HEIGHT 720
#define VENC_DEFAULT_FPS 30
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
#define VENC_DEFAULT_INPUT_TYPE H264ENC_RGB888
#define VENC_DEFAULT_BPP 4
#else
#define VENC_DEFAULT_INPUT_TYPE H264ENC_RGB565
#define VENC_DEFAULT_BPP 2
#endif
#define VENC_DEFAULT_RATE_CTRL_QP 25
#define VENC_OUT_BUFFER_SIZE (292 * 1024)

#define ENC_FRAME_HEADER_SIZE (1*64)
enum {
    VENC_RATE_CTRL_QP_CONSTANT,
    VENC_RATE_CTRL_VBR,
};

typedef enum {
    ENC_CMD_GET_STATE        = ENC_CMD_BASE,
    ENC_CMD_SET_PARAM,
    ENC_CMD_GET_PARAM,
    ENC_CMD_INPUT_BUFFER,
    ENC_CMD_OUTPUT_BUFFER,
    ENC_CMD_OUTPUT_FRAME,
} ENC_CMD_E;

typedef enum {
    ENC_STOP = 0,
    ENC_IDLE,
    ENC_PROCESSING,
    ENC_COMPLETE,
} ENC_STATE_E;

typedef struct {
    H264EncOut frame_info;
    uint8_t *frame_buffer;  //base buffer pointer.header + data
    uint32_t header_size;
    uint32_t data_size;//venc size
} enc_out_frame_t;
typedef struct {
    int width;
    int height;
    int fps;
    JpegEncFrameType input_type;
    int bpp;
    int rate_ctrl_mode;
    int rate_ctrl_dq;
} enc_param_t;
typedef struct {
    bool is_init;
    ENC_STATE_E state;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t enc_processId;
    osMutexId_t state_mtx;   // protect state and pointer
    osMutexId_t hw_mtx;      // protect hardware/codec long time operation
    osSemaphoreId_t sem_work; // task work notification (producer->consumer)
    osEventFlagsId_t evt_flags;  
    enc_param_t params;
    uint8_t *in_buffer;
    enc_out_frame_t out_frame;
    int is_intra_force;
    int first_frame_done;    /* set once any encode has succeeded since ENC_H264_Init */
    int startup_failures;    /* consecutive failures before that first success */
    int qp_floor_step;       /* cold-start escape step; 0 = normal quality */
} enc_t;

int enc_register(void);
int enc_unregister(void);
#endif