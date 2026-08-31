#include "enc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "stm32n6xx_hal.h"
#include "stm32n6xx_ll_venc.h"
#include "common_utils.h"
#include "ewl.h"
#include "camera.h"
#include "debug.h"
#include "mem.h"
#include "cmsis_os2.h"


#if !USE_H264_VENC
void JpegEnc_Trace(const char *msg)
{
    printf("%s \r\n", msg);
}

typedef struct jpegInfo_t
{
  uint32_t  frameSeqNum;
  uint32_t  frameHeader;
}jpegInfo_t;


typedef struct venc_jpeg_cfg_t
{
  JpegEncCfg                    cfgJpeg;
  jpegInfo_t                    cfgJpegInfo;
}venc_jpeg_cfg_t;

static JpegEncIn   jpegEncIn= {0};
static JpegEncOut  jpegEncOut= {0};
#endif

static uint8_t venc_out_buffer[VENC_OUT_BUFFER_SIZE] ALIGN_32 IN_PSRAM;

static enc_t g_enc = {0};

const osThreadAttr_t encTask_attributes = {
    .name = "encTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_size = 4 * 1024
};

static struct VENC_Context {
    H264EncInst hdl;
    JpegEncInst jdl;
    int is_sps_pps_done;
    uint64_t pic_cnt;
    int gop_len;
} VENC_Instance;

#if !USE_H264_VENC
venc_jpeg_cfg_t hVencJpegPluginInstance={
	 .cfgJpeg.qLevel                           = 9,                                   // Quantization level (0 - 9)
//	 .cfgJpeg.inputWidth                       = 1920,                                // Number of pixels/line in input image
//	 .cfgJpeg.inputHeight                      = 1080,                                // Number of lines in input image
	 .cfgJpeg.frameType                        = JPEGENC_RGB565,                      // Input frame YUV / RGB format
//	 .cfgJpeg.codingWidth                      = 1920,                                // Width of encoded image
//	 .cfgJpeg.codingHeight                     = 1080,                                // Height of encoded image
	 .cfgJpeg.xOffset                          = 0,                                   // Pixels from top-left corner of input image
	 .cfgJpeg.yOffset                          = 0,                                   // to top-left corner of encoded image
	 .cfgJpeg.restartInterval                  = 0,                                   // Restart interval (MCU lines)
	 .cfgJpegInfo.frameHeader                  = 1,                                   // Enable/disable creation of frame headers 
	 .cfgJpegInfo.frameSeqNum                  = 1,                                   // Number of images to encode sequentially 0=Disabled
	 .cfgJpeg.rotation                         = JPEGENC_ROTATE_0,                    //  RGB to YUV conversion
	 .cfgJpeg.codingType                       = JPEGENC_WHOLE_FRAME,                 // Whole frame / restart interval
	 .cfgJpeg.codingMode                       = JPEGENC_420_MODE,                    //  4:2:0 / 4:2:2 coding
	 .cfgJpeg.unitsType                        = JPEGENC_DOTS_PER_INCH,               // Units for X & Y density in APP0
	 .cfgJpeg.markerType                       = JPEGENC_SINGLE_MARKER,               // Table marker type
	 .cfgJpeg.xDensity                         = 72,                                  // Horizontal pixel density
	 .cfgJpeg.yDensity                         = 72,                                  // Vertical pixel density
};
#endif

#pragma pack(2)
typedef struct _tagBITMAPINFOHEADER{
	unsigned long      biSize;
	long       biWidth;
	long       biHeight;
	unsigned short       biPlanes;
	unsigned short       biBitCount;
	unsigned long      biCompression;
	unsigned long      biSizeImage;
	long       biXPelsPerMeter;
	long       biYPelsPerMeter;
	unsigned long      biClrUsed;
	unsigned long      biClrImportant;
} T_BITMAPINFOHEADER;

typedef struct _tagBITMAPFILEHEADER {
	unsigned short    bfType;
	unsigned long   bfSize;
	unsigned short    bfReserved1;
	unsigned short    bfReserved2;
	unsigned long   bfOffBits;
} T_BITMAPFILEHEADER;
#pragma pack()

#if !USE_H264_VENC
static uint8_t encoder_jpeg_init(ENC_Conf_t *p_conf)
{
    int32_t  ret;

    hVencJpegPluginInstance.cfgJpeg.inputWidth = p_conf->width;
    hVencJpegPluginInstance.cfgJpeg.inputHeight = p_conf->height;
    hVencJpegPluginInstance.cfgJpeg.codingWidth = p_conf->width;
    hVencJpegPluginInstance.cfgJpeg.codingHeight = p_conf->height;
    
    hVencJpegPluginInstance.cfgJpeg.frameType    = (JpegEncFrameType)p_conf->type;
    
    
    ret = JpegEncInit(&hVencJpegPluginInstance.cfgJpeg, &VENC_Instance.jdl);
    if (ret != JPEGENC_OK)
    {
        printf("Error: JpegEncInit\n");
        return 0;
    }
    
    
    ret = JpegEncSetPictureSize(VENC_Instance.jdl, &hVencJpegPluginInstance.cfgJpeg);
    
    if(ret  != JPEGENC_OK)
    {
        printf("Error : JpegEncSetPictureSize \n");
        return 0;
    }
    memset(&jpegEncIn,0,sizeof(jpegEncIn));

    return 1;
}

int32_t encode_jpeg_frame(const void *pFrame2Encode, void *pOut, uint32_t szOut)
{
    int32_t ret=0;
    uint32_t *pOutpout;
    uint32_t  szOutpout;
    /* ATTENTION: the frame to encode must be in memory uncached */
    pOutpout = pOut;
    szOutpout = szOut;


    jpegEncIn.pOutBuf = (u8 *)pOutpout;
    jpegEncIn.busOutBuf = (uint32_t) pOutpout;
    jpegEncIn.outBufSize = szOutpout;

    jpegEncIn.busLum = (uint32_t)pFrame2Encode; 
    jpegEncIn.busCb = jpegEncIn.busLum + hVencJpegPluginInstance.cfgJpeg.inputWidth * hVencJpegPluginInstance.cfgJpeg.inputHeight;
    jpegEncIn.busCr = jpegEncIn.busCb + (hVencJpegPluginInstance.cfgJpeg.inputWidth/2) * (hVencJpegPluginInstance.cfgJpeg.inputHeight/2);
    jpegEncIn.frameHeader = hVencJpegPluginInstance.cfgJpegInfo.frameHeader;
    
    
    /* Loop until the frame is ready, seveal loop in case of slicing configuration */
    do
    {
        ret = JpegEncEncode(VENC_Instance.jdl, &jpegEncIn, &jpegEncOut, NULL, NULL);
        switch (ret)
        {
        case JPEGENC_RESTART_INTERVAL:
        case JPEGENC_FRAME_READY:
        return jpegEncOut.jfifSize;
        case JPEGENC_SYSTEM_ERROR:
        default:
        printf("Error: JpegEncEncode :%ld\r\n", ret);
        return 0;
        }
    }
    while (ret == JPEGENC_RESTART_INTERVAL);
    return 0;
}

static void encoder_jpeg_end(void)
{
    LL_VENC_DeInit();
    JpegEncRelease(&VENC_Instance.jdl);
}
#endif

static void VENC_H264_SetupConstantQp(H264EncRateCtrl *rate, int qp)
{
    rate->pictureRc = 0;
    rate->mbRc = 0;
    rate->pictureSkip = 0;
    rate->hrd = 0;
    rate->qpHdr = qp;
    rate->qpMin = qp;
    rate->qpMax = qp;
}

static void VENC_H264_SetupVbr(H264EncRateCtrl *rate, int bitrate, int gopLen, int qp)
{
    rate->pictureRc = 1;
    rate->mbRc = 1;
    rate->pictureSkip = 0;
    rate->hrd = 0;
    rate->qpHdr = qp;
    rate->qpMin = 10;
    rate->qpMax = 51;
    rate->gopLen = gopLen;
    rate->bitPerSecond = bitrate;
    rate->intraQpDelta = 0;
}

static int VENC_h264_AppendPadding(struct VENC_Context *p_ctx, uint8_t *p_out, size_t out_len, size_t *p_out_len)
{
    uint32_t out_addr = (uint32_t) p_out;
    int pad_size = 8 - (out_addr % 8);
    int pad_len = 0;

    *p_out_len = 0;

    /* No need of padding */
    if (out_addr % 8 == 0)
        return 0;

    /* adjust pad size */
    if (pad_size < 6)
        pad_size += 8;

    /* Do we add enought space for padding ? */
    if (pad_size > out_len)
        return -1;

    /* Ok now we add nal pad */
    p_out[pad_len++] = 0x00;
    p_out[pad_len++] = 0x00;
    p_out[pad_len++] = 0x00;
    p_out[pad_len++] = 0x01;
    p_out[pad_len++] = 0x2c; /* FIXME : adapt for nal_ref_idc ? */
    pad_size -= 5;
    while (pad_size--)
        p_out[pad_len++] = 0xff;

    *p_out_len = pad_len;

    return 0;
}

static int VENC_H264_EncodeStart(struct VENC_Context *p_ctx, uint8_t *p_out, size_t out_len, size_t *p_out_len)
{
    H264EncOut enc_out;
    H264EncIn enc_in;
    size_t start_len;
    size_t pad_len;
    int ret;

    enc_in.pOutBuf = (u32 *) p_out;
    enc_in.busOutBuf = (ptr_t) p_out;
    enc_in.outBufSize = out_len;
    ret = H264EncStrmStart(p_ctx->hdl, &enc_in, &enc_out);
    if (ret)
        return ret;

    start_len = enc_out.streamSize;
    ret = VENC_h264_AppendPadding(p_ctx, &p_out[start_len], out_len - start_len, &pad_len);
    if (ret)
        return ret;

    *p_out_len = start_len + pad_len;

    return 0;
}

static int VENC_H264_EncodeFrame(struct VENC_Context *p_ctx, uint8_t *p_in, uint8_t *p_out, size_t out_len,
                            size_t *p_out_len, int is_intra_force, H264EncOut *p_enc_out)
{
    H264EncIn enc_in;
    int ret;

    /* In both N6_VENC_INPUT_YUV2 and N6_VENC_INPUT_RGB565 only busLuma is used */
    enc_in.busLuma = (ptr_t) p_in;
    enc_in.busChromaU = 0;
    enc_in.busChromaV = 0;
    enc_in.pOutBuf = (u32 *) p_out;
    enc_in.busOutBuf = (ptr_t) p_out;
    enc_in.outBufSize = out_len;
    enc_in.codingType = (p_ctx->pic_cnt % (p_ctx->gop_len + 1) == 0) ? H264ENC_INTRA_FRAME : H264ENC_PREDICTED_FRAME;
    enc_in.codingType = is_intra_force ? H264ENC_INTRA_FRAME : enc_in.codingType;
    if(enc_in.codingType == H264ENC_INTRA_FRAME){
        enc_in.timeIncrement = 0;
    }else{
        enc_in.timeIncrement = 1;
    }
    enc_in.ipf = H264ENC_REFERENCE_AND_REFRESH; /* FIXME : can be H264ENC_NO_REFERENCE_NO_REFRESH in I only mode */
    enc_in.ltrf = H264ENC_REFERENCE;
    enc_in.lineBufWrCnt = 0;
    enc_in.sendAUD = 0;

    ret = H264EncStrmEncode(p_ctx->hdl, &enc_in, p_enc_out, NULL, NULL, NULL);
    if (ret != H264ENC_FRAME_READY)
        return -1;

    p_ctx->pic_cnt++;
    *p_out_len = p_enc_out->streamSize;

    return 0;
}

static int VENC_H264_Encode(enc_t *enc)
{
    struct VENC_Context *p_ctx = &VENC_Instance;
    size_t start_len = 0;
    size_t frame_len;
    int ret;

    uint8_t *p_out = enc->out_frame.frame_buffer + enc->out_frame.header_size;
    if (!p_ctx->is_sps_pps_done)
    {
        ret = VENC_H264_EncodeStart(p_ctx, p_out, VENC_OUT_BUFFER_SIZE, &start_len);
        if (ret)
            return ret;
        p_ctx->is_sps_pps_done = 1;
    }

    p_out += start_len;
    ret = VENC_H264_EncodeFrame(p_ctx, enc->in_buffer, p_out, VENC_OUT_BUFFER_SIZE - start_len, &frame_len, enc->is_intra_force, &enc->out_frame.frame_info);
    if (ret)
        return ret;

    enc->out_frame.data_size = start_len + frame_len;
    SCB_InvalidateDCache_by_Addr((uint32_t *)enc->out_frame.frame_buffer, enc->out_frame.data_size + enc->out_frame.header_size);
    return 0;
}

static int ENC_H264_Init(enc_t *enc)
{
    LOG_DRV_DEBUG("ENC_H264_Init \r\n");
    struct VENC_Context *p_ctx = &VENC_Instance;
    H264EncPreProcessingCfg cfg;
    H264EncCodingCtrl ctrl;
    H264EncConfig config;
    H264EncRateCtrl rate;
    int target_bitrate;
    int ret = H264ENC_OK;

    memset(&config, 0, sizeof(config));
    memset(&VENC_Instance, 0, sizeof(VENC_Instance));
    p_ctx->gop_len = enc->params.fps - 1;
    /* init encoder */
    config.streamType = H264ENC_BYTE_STREAM;
    config.viewMode = H264ENC_BASE_VIEW_SINGLE_BUFFER;
    config.level = H264ENC_LEVEL_5_1;
    config.width = enc->params.width;
    config.height = enc->params.height;
    config.frameRateNum = enc->params.fps;
    config.frameRateDenom = 1;
    config.refFrameAmount = 1;
    ret = H264EncInit(&config, &p_ctx->hdl);
    if(ret != H264ENC_OK){
        return ret;
    }

    /* setup source format */
    ret = H264EncGetPreProcessing(p_ctx->hdl, &cfg);
    if(ret != H264ENC_OK){
        return ret;
    }
    cfg.inputType = enc->params.input_type;
    ret = H264EncSetPreProcessing(p_ctx->hdl, &cfg);
    if(ret != H264ENC_OK){
        return ret;
    }

    /* setup coding ctrl */
    ret = H264EncGetCodingCtrl(p_ctx->hdl, &ctrl);
    if(ret != H264ENC_OK){
        return ret;
    }
    ctrl.idrHeader = 1;
    ret = H264EncSetCodingCtrl(p_ctx->hdl, &ctrl);
    if(ret != H264ENC_OK){
        return ret;
    }

    /* setup rate ctrl */
    ret = H264EncGetRateCtrl(p_ctx->hdl, &rate);
    if(ret != H264ENC_OK){
        return ret;
    }
    target_bitrate = ((enc->params.width * enc->params.height * 2) * enc->params.fps) / 30;
    if (enc->params.rate_ctrl_mode == VENC_RATE_CTRL_QP_CONSTANT)
    {
        VENC_H264_SetupConstantQp(&rate, enc->params.rate_ctrl_dq);
    } else if (enc->params.rate_ctrl_mode == VENC_RATE_CTRL_VBR)
    {
        VENC_H264_SetupVbr(&rate, target_bitrate, enc->params.fps, enc->params.rate_ctrl_dq);
    } else
    {
        return H264ENC_INVALID_ARGUMENT;
    }
    ret = H264EncSetRateCtrl(p_ctx->hdl, &rate);
    if(ret != H264ENC_OK){
        return ret;
    }

    LOG_DRV_DEBUG("ENC_H264_Init end\r\n");
    return ret;
}

/*
 * Cold-start escape. While encStatus holds H264ENCSTAT_START_STREAM the
 * library forces every frame to INTRA and only a completed INTRA clears it
 * (H264EncApi.c:1921-1923, :2647); a re-init lands back in the same state.
 * So escape asks for a cheaper frame rather than retrying: the QP floor is
 * a hard clamp in the rate controller (H264RateControl.c:934).
 */
#define ENC_STARTUP_FAILURE_THRESHOLD 30  /* ~1 s at 30 fps */
#define ENC_STARTUP_QP_STEP           8
#define ENC_QP_MAX                    51

static int ENC_H264_SetQpFloor(enc_t *enc, int qp_floor)
{
    struct VENC_Context *p_ctx = &VENC_Instance;
    H264EncRateCtrl rate;
    int ret;

    ret = H264EncGetRateCtrl(p_ctx->hdl, &rate);
    if (ret != H264ENC_OK)
        return ret;

    /* qpHdr and qpMax must stay >= qpMin or the setter rejects the call;
       constant-QP mode pins qpMax to the configured QP. */
    rate.qpMin = qp_floor;
    if (rate.qpHdr < qp_floor)
        rate.qpHdr = qp_floor;
    if (rate.qpMax < qp_floor)
        rate.qpMax = qp_floor;

    return H264EncSetRateCtrl(p_ctx->hdl, &rate);
}

static void ENC_DeInit()
{
    struct VENC_Context *p_ctx = &VENC_Instance;
    int ret;

    ret = H264EncRelease(p_ctx->hdl);
    if(ret != H264ENC_OK){
        LOG_DRV_ERROR("ENC_H264_DeInit error\r\n");
    }
    p_ctx->is_sps_pps_done = 0;
}

void *EWLmalloc(u32 n)
{
    void *res = hal_mem_alloc_fast(n);
    // printf("EWLmalloc size:%d  addr:0x%x\r\n",n, (int)res);
    assert(res);

    return res;
}

void EWLfree(void *p)
{
    hal_mem_free(p);
}

void *EWLcalloc(u32 n, u32 s)
{
    void *res = hal_mem_calloc_fast(n, s);
    // printf("EWLcalloc size:%d  addr:0x%x\r\n",n * s, (int)res);
    assert(res);

    return res;
}

void EWLPoolChoiceCb(uint8_t **pool_ptr, size_t *size)
{

}

void EWLPoolReleaseCb(uint8_t **pool_ptr)
{
    ;
}


#if !USE_H264_VENC
uint8_t * ENC_Jpeg_EncodeFrame(uint8_t *p_in, int *out_len)
{
    int ret;

    ret = encode_jpeg_frame(p_in, venc_out_buffer, VENC_OUT_BUFFER_SIZE);
    if(ret != 0){
        *out_len = ret;
        return venc_out_buffer;
    }else{
        *out_len = 0;
        return NULL;
    }
}
#endif

#define EVT_ENC_DONE    (1 << 0)  // encoder done event
#define EVT_ENC_ERROR   (1 << 1)  // encoder error event 


/**
 * @brief encoder process thread
 */
static void encProcess(void *argument)
{
    enc_t *enc = (enc_t *)argument;
    osThreadId_t tid = osThreadGetId();


    while (enc->is_init) {
        if (osSemaphoreAcquire(enc->sem_work, osWaitForever) != osOK) {
            LOG_DRV_WARN("[PROC T: %p] sem_work acquire failed\r\n", tid);
            continue;
        }

        /* check exit flag first */
        if (!enc->is_init) {
            break;
        }

        /* short lock: read and check if need to process */
        uint8_t *local_in = NULL;
        osMutexAcquire(enc->state_mtx, osWaitForever);
        if (enc->state == ENC_PROCESSING && enc->in_buffer != NULL && 
            enc->out_frame.frame_buffer != NULL) {
            local_in = enc->in_buffer;
        } else {
            LOG_DRV_WARN("[PROC T: %p] State=%d, skipping job.\r\n", tid, enc->state);
        }
        osMutexRelease(enc->state_mtx);

        if (local_in == NULL) {
            /* use event flags to notify (even if the task is invalid) */
            LOG_DRV_WARN("[PROC T: %p] Setting EVT_ENC_ERROR (NULL job).\r\n", tid);
            osEventFlagsSet(enc->evt_flags, EVT_ENC_ERROR);
            continue;
        }

        /* long time encoding call: use hw_mtx to protect */
        int encode_ret = 0;
        osMutexAcquire(enc->hw_mtx, osWaitForever);
        
#if USE_H264_VENC
        encode_ret = VENC_H264_Encode(enc);

        /* Cold-start escape: step the QP floor up while no frame has
           succeeded since init, restore normal quality on first success. */
        if (encode_ret == 0) {
            enc->startup_failures = 0;
            if (!enc->first_frame_done && enc->qp_floor_step) {
                if (ENC_H264_SetQpFloor(enc, enc->params.rate_ctrl_dq) == H264ENC_OK)
                    LOG_DRV_WARN("encoder cold start cleared at QP floor %d, quality restored\r\n",
                                 enc->params.rate_ctrl_dq + enc->qp_floor_step * ENC_STARTUP_QP_STEP);
                enc->qp_floor_step = 0;
            }
            enc->first_frame_done = 1;
        } else if (!enc->first_frame_done &&
                   ++enc->startup_failures >= ENC_STARTUP_FAILURE_THRESHOLD) {
            int qp_floor = enc->params.rate_ctrl_dq +
                           (enc->qp_floor_step + 1) * ENC_STARTUP_QP_STEP;

            enc->startup_failures = 0;
            if (qp_floor <= ENC_QP_MAX) {
                enc->qp_floor_step++;
                if (ENC_H264_SetQpFloor(enc, qp_floor) == H264ENC_OK)
                    LOG_DRV_WARN("encoder produced no frame since init; QP floor raised to %d\r\n",
                                 qp_floor);
                else
                    LOG_DRV_ERROR("encoder cold start: QP floor %d rejected\r\n", qp_floor);
            }
        }
#else
        encode_ret = encode_jpeg_frame(local_in, 
                                       enc->out_frame.frame_buffer + enc->out_frame.header_size,
                                       VENC_OUT_BUFFER_SIZE - enc->out_frame.header_size);
        if (encode_ret > 0) {
            enc->out_frame.data_size = encode_ret;
            encode_ret = 0; /* success */
        } else {
            encode_ret = -1; /* failed */
        }
#endif
        
        osMutexRelease(enc->hw_mtx);

        /* update state and notify through event flags */
        osMutexAcquire(enc->state_mtx, osWaitForever);
        if (encode_ret != 0) {
            enc->is_intra_force = 1;
            enc->out_frame.data_size = 0;
            enc->state = ENC_COMPLETE; /* even if failed, mark as complete */
        } else {
            enc->is_intra_force = 0;
            enc->state = ENC_COMPLETE;
        }
        osMutexRelease(enc->state_mtx);

        /* use event flags to notify the waiters */
        uint32_t flag_to_set = (encode_ret == 0) ? EVT_ENC_DONE : EVT_ENC_ERROR;
        osEventFlagsSet(enc->evt_flags, flag_to_set);
    }

    osThreadExit();
}


/**
 * @brief start encoder
 */
static int enc_start(void *priv)
{
    enc_t *enc = (enc_t *)priv;
    if (!enc->is_init)
        return AICAM_ERROR_NOT_FOUND;

    osMutexAcquire(enc->state_mtx, osWaitForever);
    if (enc->state != ENC_STOP) {
        osMutexRelease(enc->state_mtx);
        return AICAM_OK;
    }
    enc->state = ENC_IDLE;
    enc->is_intra_force = 1;
    enc->first_frame_done = 0;
    enc->startup_failures = 0;
    enc->qp_floor_step = 0;
    osMutexRelease(enc->state_mtx);

    /* hardware initialization */
    if (osMutexAcquire(enc->hw_mtx, 1000) != osOK) {
        LOG_DRV_ERROR("enc_start: hw_mtx timeout\r\n");
        return AICAM_ERROR_BUSY;
    }
#if USE_H264_VENC
    if (ENC_H264_Init(enc) != H264ENC_OK) {
        ENC_DeInit();
        osMutexRelease(enc->hw_mtx);
        return AICAM_ERROR;
    }
#else
    if (!encoder_jpeg_init(&enc->params)) {
        osMutexRelease(enc->hw_mtx);
        return AICAM_ERROR;
    }
#endif
    osMutexRelease(enc->hw_mtx);

    /* clear all event flags */
    osEventFlagsClear(enc->evt_flags, EVT_ENC_DONE | EVT_ENC_ERROR);

    return AICAM_OK;
}


/**
 * @brief stop encoder
 */
static int enc_stop(void *priv)
{
    enc_t *enc = (enc_t *)priv;
    if (!enc->is_init)
        return AICAM_ERROR_NOT_FOUND;

    osThreadId_t tid = osThreadGetId();
    osMutexAcquire(enc->state_mtx, osWaitForever);
    if (enc->state == ENC_STOP) {
        osMutexRelease(enc->state_mtx);
        return AICAM_OK;
    }
    enc->state = ENC_STOP;
    osMutexRelease(enc->state_mtx);

    /* wake up the threads that are waiting for events */
    osEventFlagsSet(enc->evt_flags, EVT_ENC_ERROR);

    /* wait for hw_mtx to safely call DeInit */
    if (osMutexAcquire(enc->hw_mtx, 5000) != osOK) {
        LOG_DRV_ERROR("[STOP T: %p] hw_mtx timeout! Cannot DeInit.\r\n", tid);
        return AICAM_ERROR_BUSY;
    }

#if USE_H264_VENC
    ENC_DeInit();
#else
    encoder_jpeg_end();
#endif
    osMutexRelease(enc->hw_mtx);

    return AICAM_OK;
}


/**
 * @brief IOCTL control
 */
static int enc_ioctl(void *priv, unsigned int cmd, unsigned char *ubuf, unsigned long arg)
{
    enc_t *enc = (enc_t *)priv;
    ENC_CMD_E enc_cmd = (ENC_CMD_E)cmd;
    int ret = AICAM_OK;
    osThreadId_t tid = osThreadGetId();

    if (!enc->is_init)
        return AICAM_ERROR_NOT_FOUND;

    switch (enc_cmd) {
        case ENC_CMD_GET_STATE:
            osMutexAcquire(enc->state_mtx, osWaitForever);
            *ubuf = enc->state;
            osMutexRelease(enc->state_mtx);
            ret = AICAM_OK;
            break;

        case ENC_CMD_SET_PARAM:
            if (arg != sizeof(enc_param_t)) {
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            osMutexAcquire(enc->state_mtx, osWaitForever);
            memcpy(&enc->params, ubuf, sizeof(enc_param_t));
            osMutexRelease(enc->state_mtx);
            ret = AICAM_OK;
            break;

        case ENC_CMD_GET_PARAM:
            if (arg != sizeof(enc_param_t)) {
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            osMutexAcquire(enc->state_mtx, osWaitForever);
            memcpy(ubuf, &enc->params, sizeof(enc_param_t));
            osMutexRelease(enc->state_mtx);
            ret = AICAM_OK;
            break;

        case ENC_CMD_INPUT_BUFFER:
            if (arg != enc->params.width * enc->params.height * enc->params.bpp) {
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            osMutexAcquire(enc->state_mtx, osWaitForever);
            if (enc->state != ENC_IDLE && enc->state != ENC_COMPLETE) {
                osMutexRelease(enc->state_mtx);
                ret = AICAM_ERROR_BUSY;
                break;
            }
            
            /* clear old event flags */
            osEventFlagsClear(enc->evt_flags, EVT_ENC_DONE | EVT_ENC_ERROR);
            
            enc->in_buffer = ubuf;
            enc->state = ENC_PROCESSING;
            osMutexRelease(enc->state_mtx);
            
            /* notify the work thread */
            if (enc->sem_work != NULL) {
                osSemaphoreRelease(enc->sem_work);
            }
            ret = AICAM_OK;
            break;

        case ENC_CMD_OUTPUT_BUFFER: {
            
            /* fast path: check if already completed */
            osMutexAcquire(enc->state_mtx, osWaitForever);
            if (enc->state == ENC_COMPLETE) {
                *((unsigned char **)ubuf) = enc->out_frame.frame_buffer + 
                                            enc->out_frame.header_size;
                ret = enc->out_frame.data_size;
                osMutexRelease(enc->state_mtx);
                break;
            }
            
            if (enc->state != ENC_PROCESSING) {
                osMutexRelease(enc->state_mtx);
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            osMutexRelease(enc->state_mtx);

            /* use event flags to wait for completion */
            uint32_t flags = osEventFlagsWait(enc->evt_flags, 
                                              EVT_ENC_DONE | EVT_ENC_ERROR,
                                              osFlagsWaitAny,  /* any event trigger */
                                              1000);           /* 1 second timeout */
            
            if (flags == osFlagsErrorTimeout) {
                ret = AICAM_ERROR_TIMEOUT;
                break;
            }
            
            if (flags & osFlagsError) {
                LOG_DRV_WARN("[IOCTL T: %p] Event wait ERROR: 0x%x\r\n", tid, flags);
                ret = AICAM_ERROR;
                break;
            }

            /* check event type */
            if (flags & EVT_ENC_DONE) {
                osMutexAcquire(enc->state_mtx, osWaitForever);
                if (enc->state == ENC_COMPLETE && enc->out_frame.data_size > 0) {
                    *((unsigned char **)ubuf) = enc->out_frame.frame_buffer + 
                                                enc->out_frame.header_size;
                    ret = enc->out_frame.data_size;
                } else {
                    ret = AICAM_ERROR;
                }
                osMutexRelease(enc->state_mtx);
            } else if (flags & EVT_ENC_ERROR) {
                ret = AICAM_ERROR;
            }
            break;
        }

        case ENC_CMD_OUTPUT_FRAME: {
            /* fast path */
            osMutexAcquire(enc->state_mtx, osWaitForever);
            if (enc->state == ENC_COMPLETE) {
                memcpy(ubuf, &enc->out_frame, sizeof(enc_out_frame_t));
                osMutexRelease(enc->state_mtx);
                ret = AICAM_OK;
                break;
            }
            
            if (enc->state != ENC_PROCESSING) {
                osMutexRelease(enc->state_mtx);
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            osMutexRelease(enc->state_mtx);

            /* wait for event */
            uint32_t flags = osEventFlagsWait(enc->evt_flags,
                                              EVT_ENC_DONE | EVT_ENC_ERROR,
                                              osFlagsWaitAny,
                                              1000);
            
            if (flags == osFlagsErrorTimeout) {
                ret = AICAM_ERROR_TIMEOUT;
                break;
            }
            
            if (flags & osFlagsError) {
                ret = AICAM_ERROR;
                break;
            }

            if (flags & EVT_ENC_DONE) {
                osMutexAcquire(enc->state_mtx, osWaitForever);
                if (enc->state == ENC_COMPLETE) {
                    memcpy(ubuf, &enc->out_frame, sizeof(enc_out_frame_t));
                    ret = AICAM_OK;
                } else {
                    ret = AICAM_ERROR;
                }
                osMutexRelease(enc->state_mtx);
            } else {
                ret = AICAM_ERROR;
            }
            break;
        }

        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }

    return ret;
}


/**
 * @brief initialize encoder
 */
static int enc_init(void *priv)
{
    enc_t *enc = (enc_t *)priv;

    /* create locks and synchronization primitives */
    enc->state_mtx = osMutexNew(NULL);
    enc->hw_mtx = osMutexNew(NULL);
    enc->sem_work = osSemaphoreNew(1, 0, NULL);  // max=1, init=0
    
    /* create event flags group  */
    enc->evt_flags = osEventFlagsNew(NULL);

    if (!enc->state_mtx || !enc->hw_mtx || !enc->sem_work || !enc->evt_flags) {
        LOG_DRV_ERROR("Failed to create OS resources\r\n");
        
        /* clean up created resources */
        if (enc->state_mtx) osMutexDelete(enc->state_mtx);
        if (enc->hw_mtx) osMutexDelete(enc->hw_mtx);
        if (enc->sem_work) osSemaphoreDelete(enc->sem_work);
        if (enc->evt_flags) osEventFlagsDelete(enc->evt_flags);
        
        return AICAM_ERROR;
    }

    /* initialize parameters */
    enc->params.width = VENC_DEFAULT_WIDTH;
    enc->params.height = VENC_DEFAULT_HEIGHT;
    enc->params.fps = VENC_DEFAULT_FPS;
    enc->params.input_type = VENC_DEFAULT_INPUT_TYPE;
    enc->params.bpp = VENC_DEFAULT_BPP;
    enc->params.rate_ctrl_mode = VENC_RATE_CTRL_VBR;
    enc->params.rate_ctrl_dq = VENC_DEFAULT_RATE_CTRL_QP;

    enc->in_buffer = NULL;
    enc->out_frame.frame_buffer = venc_out_buffer;
    enc->out_frame.header_size = ENC_FRAME_HEADER_SIZE;
    enc->out_frame.data_size = 0;

    osMutexAcquire(enc->state_mtx, osWaitForever);
    enc->state = ENC_STOP;
    osMutexRelease(enc->state_mtx);

    LL_VENC_Init();

    enc->is_init = true;
    enc->enc_processId = osThreadNew(encProcess, enc, &encTask_attributes);
    if (enc->enc_processId == NULL) {
        enc->is_init = false;
        
        /* clean up resources */
        osMutexDelete(enc->state_mtx);
        osMutexDelete(enc->hw_mtx);
        osSemaphoreDelete(enc->sem_work);
        osEventFlagsDelete(enc->evt_flags);
        
        return AICAM_ERROR;
    }
    
    return AICAM_OK;
}


/**
 * @brief deinitialize encoder
 */
static int enc_deinit(void *priv)
{
    enc_t *enc = (enc_t *)priv;

    if (!enc->is_init) {
        LOG_DRV_WARN("[DEINIT] Already de-inited.\r\n");
        return AICAM_OK;
    }

    /* mark thread as exited */
    enc->is_init = false;

    /* wake up all waiting threads */
    if (enc->sem_work != NULL) {
        osSemaphoreRelease(enc->sem_work);
    }
    
    /* set event flags to wake up ioctl waiters */
    if (enc->evt_flags != NULL) {
        osEventFlagsSet(enc->evt_flags, EVT_ENC_ERROR);
    }

    /* wait for thread to exit */
    if (enc->enc_processId != NULL) {
        osDelay(1000);  /* wait for 1 second */
        
        if (osThreadGetState(enc->enc_processId) != osThreadTerminated) {
            osThreadTerminate(enc->enc_processId);
        }
        enc->enc_processId = NULL;
    }

    /* release hardware resources */
    if (enc->state != ENC_STOP) {
#if USE_H264_VENC
        ENC_DeInit();
#else
        encoder_jpeg_end();
#endif
        enc->state = ENC_STOP;
    }

    /* delete all OS resources */
    if (enc->sem_work != NULL) {
        osSemaphoreDelete(enc->sem_work);
        enc->sem_work = NULL;
    }
    
    /* delete event flags group */
    if (enc->evt_flags != NULL) {
        osEventFlagsDelete(enc->evt_flags);
        enc->evt_flags = NULL;
    }
    
    if (enc->hw_mtx != NULL) {
        osMutexDelete(enc->hw_mtx);
        enc->hw_mtx = NULL;
    }
    
    if (enc->state_mtx != NULL) {
        osMutexDelete(enc->state_mtx);
        enc->state_mtx = NULL;
    }

    LL_VENC_DeInit();
    
    return AICAM_OK;
}


/**
 * @brief register device
 */
int enc_register(void)
{
    static dev_ops_t enc_ops = {
        .init = enc_init,
        .deinit = enc_deinit,
        .start = enc_start,
        .stop = enc_stop,
        .ioctl = enc_ioctl
    };
    
    if (g_enc.is_init == true) {
        return AICAM_ERROR_BUSY;
    }
    
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    if (!dev) {
        LOG_DRV_ERROR("Failed to alloc device_t\r\n");
        return AICAM_ERROR;
    }
    
    g_enc.dev = dev;
    strcpy(dev->name, ENC_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &enc_ops;
    dev->priv_data = &g_enc;

    device_register(g_enc.dev);
    return AICAM_OK;
}


/**
 * @brief unregister device
 */
int enc_unregister(void)
{
    if (g_enc.dev) {
        device_unregister(g_enc.dev);
        hal_mem_free(g_enc.dev);
        g_enc.dev = NULL;
    }
    return AICAM_OK;
}