#include "jpegc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg.h"
#include "jpeg_utils.h"
#include "jpeg_utils_conf.h"
#include "common_utils.h"
#include "debug.h"
#include "pixel_format_map.h"
#include "mem.h"

#define JPEG_USE_SOFT_CONV 0
static jpegc_t g_jpegc = {0};

/* Event flags for jpegcProcess() (event-driven, no osDelay polling) */
#define JPEGC_EVT_KICK            (1u << 0)
#define JPEGC_EVT_ENC_IN_EMPTY    (1u << 1)
#define JPEGC_EVT_ENC_OUT_FULL    (1u << 2)
#define JPEGC_EVT_ENC_DONE        (1u << 3)
#define JPEGC_EVT_DEC_DONE        (1u << 4)
#define JPEGC_EVT_ERROR           (1u << 5)

static uint8_t jpegc_tread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
const osThreadAttr_t jpegcTask_attributes = {
    .name = "jpegcTask",
    .priority = (osPriority_t) osPriorityHigh,
    .stack_mem = jpegc_tread_stack,
    .stack_size = sizeof(jpegc_tread_stack),
};

#if (JPEG_RGB_FORMAT == JPEG_ARGB8888)
#define BYTES_PER_PIXEL    4
#elif (JPEG_RGB_FORMAT == JPEG_RGB888)
#define BYTES_PER_PIXEL    3
#elif (JPEG_RGB_FORMAT == JPEG_RGB565)
#define BYTES_PER_PIXEL    2
#endif

#define ENC_CHUNK_SIZE_IN_MAX   ((uint32_t)(MAX_INPUT_WIDTH * BYTES_PER_PIXEL * MAX_INPUT_LINES))
#define ENC_CHUNK_SIZE_OUT      ((uint32_t) (1024 * 4))

#define JPEG_BUFFER_EMPTY       0
#define JPEG_BUFFER_FULL        1

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

uint32_t MCU_TotalNb                = 0;
uint32_t MCU_BlockIndex             = 0;
__IO uint32_t Output_Is_Paused      = 0;
__IO uint32_t Input_Is_Paused       = 0;

JPEG_ConfTypeDef Conf;

//encode
JPEG_RGBToYCbCr_Convert_Function pRGBToYCbCr_Convert_Function;

uint8_t MCU_Data_InBuffer0[ENC_CHUNK_SIZE_IN_MAX] ALIGN_32 UNCACHED;

uint8_t JPEG_Data_OutBuffer0[ENC_CHUNK_SIZE_OUT] ALIGN_32 UNCACHED;

JPEG_Data_BufferTypeDef Jpeg_OUT_BufferTab = {JPEG_BUFFER_EMPTY , JPEG_Data_OutBuffer0 , 0};

JPEG_Data_BufferTypeDef Jpeg_IN_BufferTab = {JPEG_BUFFER_EMPTY , MCU_Data_InBuffer0, 0};

__IO uint32_t Jpeg_HWEncodingEnd    = 0;
uint32_t * pJpegBuffer;
uint32_t decode_size = 0;
uint32_t RGB_InputImageIndex;
uint32_t RGB_InputImageSize_Bytes;
uint32_t RGB_InputImageAddress;

//decode
JPEG_YCbCrToRGB_Convert_Function pYCbCrToRGB_pConvert_Function;
#define DEC_CHUNK_SIZE_IN  ((uint32_t)(4096)) 
#define DEC_CHUNK_SIZE_OUT ((uint32_t)(768))

#if JPEG_USE_SOFT_CONV
uint8_t decode_InputBuffer0[DEC_CHUNK_SIZE_IN] ALIGN_32 UNCACHED;
uint8_t decode_OutBuffer0[DEC_CHUNK_SIZE_OUT] ALIGN_32 UNCACHED;
JPEG_Data_BufferTypeDef DE_OUT_BufferTab = {JPEG_BUFFER_EMPTY , decode_OutBuffer0 , 0};
JPEG_Data_BufferTypeDef DE_IN_BufferTab = {JPEG_BUFFER_EMPTY , decode_InputBuffer0, 0};
#endif

__IO uint32_t Jpeg_HWDecodingEnd;
uint32_t FrameBufferAddress;
uint32_t JPEGSourceAddress;
uint32_t Input_frameSize;
__IO uint32_t Input_frameIndex;
static JPEG_ConfTypeDef       JPEG_Info;

/**
 * @brief Get the maximum input lines based on chroma subsampling
 * @param chroma_subsampling Chroma subsampling mode
 * @return Number of input lines (16 for 420, 8 for 422/444)
 */
static uint32_t jpegc_get_max_input_lines(uint32_t chroma_subsampling)
{
    if (chroma_subsampling == JPEG_420_SUBSAMPLING) {
        return 16;
    } else {
        /* JPEG_422_SUBSAMPLING and JPEG_444_SUBSAMPLING use 8 lines */
        return 8;
    }
}

/**
 * @brief Automatically select appropriate chroma subsampling based on resolution
 * @param width Image width
 * @param height Image height
 * @param color_space Color space
 * @return Selected chroma subsampling mode
 */
static uint32_t jpegc_auto_select_chroma_subsampling(uint32_t width, uint32_t height, uint32_t color_space)
{
    /* If not YCbCr color space, return default */
    if (color_space != JPEG_YCBCR_COLORSPACE) {
        return JPEG_420_SUBSAMPLING;
    }
    
    /* Check if resolution meets requirements for each subsampling mode */
    /* JPEG_420_SUBSAMPLING: width and height must be multiples of 16 */
    if ((width % 16 == 0) && (height % 16 == 0)) {
        return JPEG_420_SUBSAMPLING;
    }
    
    /* JPEG_422_SUBSAMPLING: width must be multiple of 16, height must be multiple of 8 */
    if ((width % 16 == 0) && (height % 8 == 0)) {
        return JPEG_422_SUBSAMPLING;
    }
    
    /* JPEG_444_SUBSAMPLING: width and height must be multiples of 8 */
    if ((width % 8 == 0) && (height % 8 == 0)) {
        return JPEG_444_SUBSAMPLING;
    }
    
    /* If resolution doesn't meet any requirement, round down to nearest valid size */
    /* For safety, use 444 which has the least strict requirements */
    LOG_DRV_WARN("Resolution %dx%d doesn't meet JPEG requirements, using JPEG_444_SUBSAMPLING", width, height);
    return JPEG_444_SUBSAMPLING;
}

static int RGB_GetInfo(JPEG_ConfTypeDef *pInfo, jpegc_t *jpegc)
{
    /* Read Images Sizes */
    pInfo->ImageWidth         = jpegc->enc_params.ImageWidth;
    pInfo->ImageHeight        = jpegc->enc_params.ImageHeight;

    /* Jpeg Encoding Setting to be set by users */
    pInfo->ChromaSubsampling  = jpegc->enc_params.ChromaSubsampling;
    pInfo->ColorSpace         = jpegc->enc_params.ColorSpace;
    pInfo->ImageQuality       = jpegc->enc_params.ImageQuality;

    /*Check if Image Sizes meets the requirements */
    if (((pInfo->ImageWidth % 8) != 0 ) || ((pInfo->ImageHeight % 8) != 0 ) || \
        (((pInfo->ImageWidth % 16) != 0 ) && (pInfo->ColorSpace == JPEG_YCBCR_COLORSPACE) && (pInfo->ChromaSubsampling != JPEG_444_SUBSAMPLING)) || \
        (((pInfo->ImageHeight % 16) != 0 ) && (pInfo->ColorSpace == JPEG_YCBCR_COLORSPACE) && (pInfo->ChromaSubsampling == JPEG_420_SUBSAMPLING)))
    {
        return -1;
    }

    return 0;
}

/*
 * @brief Calculate a safe JPEG encode output buffer size based on image parameters.
 *        This avoids using a fixed size buffer that can overflow on high resolutions
 *        or high quality settings.
 */
static uint32_t jpegc_calc_enc_buffer_size(const jpegc_params_t *params)
{
    /* Use an integer heuristic:
     * - Base bytes-per-pixel (Bpp) grows with quality:
     *   bpp_x100 in [20, 90] => 0.20 ~ 0.90 Bpp for quality 1~100
     *   (roughly matching typical JPEG output sizes from low to high quality)
     * - estimated_bytes = pixels * bpp_x100 / 100
     * - Clamp between a minimum size and a hard upper bound
     */
    uint32_t bpp_x100 = 0;
    uint64_t pixels = (uint64_t)params->ImageWidth * (uint64_t)params->ImageHeight;

    /* Clamp quality to HAL allowed range to avoid overflow in intermediate math */
    uint32_t q = params->ImageQuality;
    if (q < JPEG_IMAGE_QUALITY_MIN) {
        q = JPEG_IMAGE_QUALITY_MIN;
    } else if (q > JPEG_IMAGE_QUALITY_MAX) {
        q = JPEG_IMAGE_QUALITY_MAX;
    }

    if (q < 40) {
        // 0.10 ~ 0.30 Bpp
        bpp_x100 = 10U + (20U * q) / 40U;
    } else if (q < 80) {
        // 0.3 ~ 0.6 Bpp
        bpp_x100 = 30U + (30U * (q - 40U)) / 40U;
    } else {
        // 0.65 ~ 1.20 Bpp
        bpp_x100 = 65U + (55U * (q - 80U)) / 20U;
    }

    uint64_t estimated = pixels * (uint64_t)bpp_x100 / 100ULL;

    if (estimated < JPEG_ENCODE_OUTPUT_BUFFER_MIN_SIZE) {
        estimated = JPEG_ENCODE_OUTPUT_BUFFER_MIN_SIZE;
    }

    /* Hard upper cap: JPEG_ENCODE_OUTPUT_BUFFER_MAX_SIZE */
    if (estimated > (uint64_t)JPEG_ENCODE_OUTPUT_BUFFER_MAX_SIZE) {
        estimated = (uint64_t)JPEG_ENCODE_OUTPUT_BUFFER_MAX_SIZE;
    }

    return (uint32_t)estimated;
}


/**
  * @brief  Encode_DMA
  * @param hjpeg: JPEG handle pointer
  * @param  FileName    : jpg file path for decode.
  * @param  DestAddress : ARGB destination Frame Buffer Address.
  * @retval None
  */
static int JPEG_Encode_DMA(JPEG_HandleTypeDef *hjpeg, jpegc_t *jpegc)
{
    pJpegBuffer = (uint32_t *)jpegc->enc_output_buffer;
    uint32_t DataBufferSize = 0;
    jpegc->enc_output_buffer_size = 0;
    /* Reset all Global variables */
    MCU_TotalNb                = 0;
    MCU_BlockIndex             = 0;
    Jpeg_HWEncodingEnd         = 0;
    Output_Is_Paused           = 0;
    Input_Is_Paused            = 0;

    /* Get RGB Info */
    if(RGB_GetInfo(&Conf, jpegc) != 0) return -1;

    JPEG_GetEncodeColorConvertFunc(&Conf, &pRGBToYCbCr_Convert_Function, &MCU_TotalNb);

    /* Clear Output Buffer */
    Jpeg_OUT_BufferTab.DataBufferSize = 0;
    Jpeg_OUT_BufferTab.State = JPEG_BUFFER_EMPTY;

    /* Fill input Buffers */
    RGB_InputImageIndex = 0;
    RGB_InputImageAddress = (uint32_t)jpegc->enc_input_buffer;
    RGB_InputImageSize_Bytes = Conf.ImageWidth * Conf.ImageHeight * BYTES_PER_PIXEL;
    /* Get max input lines based on chroma subsampling */
    uint32_t max_input_lines = jpegc_get_max_input_lines(Conf.ChromaSubsampling);
    DataBufferSize= Conf.ImageWidth * max_input_lines * BYTES_PER_PIXEL;
    if(RGB_InputImageIndex < RGB_InputImageSize_Bytes)
    {
        /* Pre-Processing */
        MCU_BlockIndex += pRGBToYCbCr_Convert_Function((uint8_t *)(RGB_InputImageAddress + RGB_InputImageIndex), Jpeg_IN_BufferTab.DataBuffer, 0, DataBufferSize,(uint32_t*)(&Jpeg_IN_BufferTab.DataBufferSize));
        Jpeg_IN_BufferTab.State = JPEG_BUFFER_FULL;

        RGB_InputImageIndex += DataBufferSize;
    }

    /* Fill Encoding Params */
    HAL_JPEG_ConfigEncoding(hjpeg, &Conf);

    /* Start JPEG encoding with DMA method */
    HAL_JPEG_Encode_DMA(hjpeg ,Jpeg_IN_BufferTab.DataBuffer ,Jpeg_IN_BufferTab.DataBufferSize ,Jpeg_OUT_BufferTab.DataBuffer ,ENC_CHUNK_SIZE_OUT);

    return 0;
}


/**
  * @brief JPEG Output Data BackGround processing .
  * @param hjpeg: JPEG handle pointer
  * @retval 1 : if JPEG processing has finiched, 0 : if JPEG processing still ongoing
  */
static uint32_t JPEG_EncodeOutputHandler(JPEG_HandleTypeDef *hjpeg)
{

    if(Jpeg_OUT_BufferTab.State == JPEG_BUFFER_FULL)
    {
        /* Check remaining capacity to avoid overflowing the user buffer */
        uint32_t remaining = 0;
        if (g_jpegc.enc_output_buffer_capacity > g_jpegc.enc_output_buffer_size) {
            remaining = g_jpegc.enc_output_buffer_capacity - g_jpegc.enc_output_buffer_size;
        }

        uint32_t copy_size = Jpeg_OUT_BufferTab.DataBufferSize;

        if (copy_size > remaining) {
            /* Truncate to available space and stop the encoding to prevent overflow */
            copy_size = remaining;
            if (copy_size > 0) {
                memcpy(pJpegBuffer, Jpeg_OUT_BufferTab.DataBuffer, copy_size);
                pJpegBuffer += copy_size / 4;
                g_jpegc.enc_output_buffer_size += copy_size;
            }
            LOG_DRV_ERROR("jpegc encode output buffer overflow: required %lu bytes, capacity %lu bytes\r\n",
                          (unsigned long)(g_jpegc.enc_output_buffer_size + Jpeg_OUT_BufferTab.DataBufferSize),
                          (unsigned long)g_jpegc.enc_output_buffer_capacity);
            /* Safely stop JPEG HW/DMA to avoid further writes */
            HAL_JPEG_Abort(hjpeg);
            
            Jpeg_HWEncodingEnd = 1;
            Jpeg_OUT_BufferTab.State = JPEG_BUFFER_EMPTY;
            Jpeg_OUT_BufferTab.DataBufferSize = 0;
            return 1;
        }

        /* Copy encoded chunk from Jpeg_OUT_BufferTab to JpegBuffer */
        memcpy(pJpegBuffer, Jpeg_OUT_BufferTab.DataBuffer ,copy_size);
        pJpegBuffer += copy_size / 4;
        g_jpegc.enc_output_buffer_size += copy_size;
        Jpeg_OUT_BufferTab.State = JPEG_BUFFER_EMPTY;
        Jpeg_OUT_BufferTab.DataBufferSize = 0;
        if(Jpeg_HWEncodingEnd != 0)
        {
            return 1;
        }
        else if((Output_Is_Paused == 1) && (Jpeg_OUT_BufferTab.State == JPEG_BUFFER_EMPTY))
        {
            Output_Is_Paused = 0;
            HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
        }
    }

    return 0;
}



/**
  * @brief JPEG Input Data BackGround Preprocessing .
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
static void JPEG_EncodeInputHandler(JPEG_HandleTypeDef *hjpeg)
{
    /* Get max input lines based on chroma subsampling */
    uint32_t max_input_lines = jpegc_get_max_input_lines(Conf.ChromaSubsampling);
    uint32_t DataBufferSize = Conf.ImageWidth * max_input_lines * BYTES_PER_PIXEL;

    if((Jpeg_IN_BufferTab.State == JPEG_BUFFER_EMPTY) && (MCU_BlockIndex <= MCU_TotalNb))
    {
        /* Read and reorder lines from RGB input and fill data buffer */
        if(RGB_InputImageIndex < RGB_InputImageSize_Bytes)
        {
            /* Pre-Processing */
            MCU_BlockIndex += pRGBToYCbCr_Convert_Function((uint8_t *)(RGB_InputImageAddress + RGB_InputImageIndex), Jpeg_IN_BufferTab.DataBuffer, 0, DataBufferSize, (uint32_t*)(&Jpeg_IN_BufferTab.DataBufferSize));
            Jpeg_IN_BufferTab.State = JPEG_BUFFER_FULL;
            RGB_InputImageIndex += DataBufferSize;

            if(Input_Is_Paused == 1)
            {
                Input_Is_Paused = 0;
                HAL_JPEG_ConfigInputBuffer(hjpeg,Jpeg_IN_BufferTab.DataBuffer, Jpeg_IN_BufferTab.DataBufferSize);
                HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_INPUT);
            }
        }
        else
        {
            MCU_BlockIndex++;
        }
    }
}

/**
  * @brief  Decode_DMA
  * @param hjpeg: JPEG handle pointer
  * @param  FrameSourceAddress    : video buffer address.
  * @param  DestAddress : YCbCr destination Frame Buffer Address.
  * @retval None
  */
static uint32_t JPEG_Decode_DMA(JPEG_HandleTypeDef *hjpeg, jpegc_t *jpegc)
{
    JPEGSourceAddress =  (uint32_t)jpegc->dec_input_buffer ;
    FrameBufferAddress = (uint32_t)jpegc->dec_output_buffer;
    Input_frameIndex = 0;
    Input_frameSize = jpegc->dec_input_buffer_size;
    Jpeg_HWDecodingEnd = 0;
    decode_size = 0;

    LOG_DRV_DEBUG("HAL_JPEG_Decode_DMA inAddr 0x%x, outAddr 0x%x, Input_frameSize:%d\r\n", JPEGSourceAddress, FrameBufferAddress, Input_frameSize);
#if JPEG_USE_SOFT_CONV
    Output_Is_Paused           = 0;
    Input_Is_Paused            = 0;
    MCU_TotalNb                = 0;
    MCU_BlockIndex             = 0;
    // memcpy((uint8_t *)decode_InputBuffer0, (uint8_t *)JPEGSourceAddress, DEC_CHUNK_SIZE_IN);
    // // /* Start JPEG decoding with DMA method */
    // HAL_JPEG_Decode_DMA(hjpeg ,(uint8_t *)decode_InputBuffer0, DEC_CHUNK_SIZE_IN, (uint8_t *)decode_OutBuffer0 , DEC_CHUNK_SIZE_OUT);

    memcpy((uint8_t *)DE_IN_BufferTab.DataBuffer, (uint8_t *)JPEGSourceAddress, DEC_CHUNK_SIZE_IN);
    DE_IN_BufferTab.State = JPEG_BUFFER_FULL;
    DE_IN_BufferTab.DataBufferSize = DEC_CHUNK_SIZE_IN;
    HAL_JPEG_Decode_DMA(hjpeg ,DE_IN_BufferTab.DataBuffer ,DE_IN_BufferTab.DataBufferSize ,DE_OUT_BufferTab.DataBuffer ,DEC_CHUNK_SIZE_OUT);
#else
    HAL_JPEG_Decode_DMA(hjpeg ,(uint8_t *)JPEGSourceAddress, DEC_CHUNK_SIZE_IN, (uint8_t *)FrameBufferAddress , DEC_CHUNK_SIZE_OUT);
#endif
    return 0;
}

#if JPEG_USE_SOFT_CONV
/**
  * @brief JPEG Output Data BackGround processing .
  * @param hjpeg: JPEG handle pointer
  * @retval 1 : if JPEG processing has finiched, 0 : if JPEG processing still ongoing
  */
static uint32_t JPEG_DecodeOutputHandler(JPEG_HandleTypeDef *hjpeg)
{
   
    if(DE_OUT_BufferTab.State == JPEG_BUFFER_FULL)
    {  
        // MCU_BlockIndex += pYCbCrToRGB_pConvert_Function(DE_OUT_BufferTab.DataBuffer, (uint8_t *)FrameBufferAddress, MCU_BlockIndex, DE_OUT_BufferTab.DataBufferSize, &ConvertedDataCount); 
        memcpy((uint8_t *)FrameBufferAddress + decode_size, DE_OUT_BufferTab.DataBuffer, DE_OUT_BufferTab.DataBufferSize);  
        decode_size += DE_OUT_BufferTab.DataBufferSize;
        DE_OUT_BufferTab.State = JPEG_BUFFER_EMPTY;
        DE_OUT_BufferTab.DataBufferSize = 0;
    }
    else if((Output_Is_Paused == 1) && \
            (DE_OUT_BufferTab.State == JPEG_BUFFER_EMPTY))
    {
        Output_Is_Paused = 0;
        HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);            
    }

    if(Jpeg_HWDecodingEnd != 0)
    {
        return 1;
    }
    return 0;  
}



/**
  * @brief JPEG Input Data BackGround Preprocessing .
  * @param hjpeg: JPEG handle pointer   
  * @retval None
  */
static void JPEG_DecodeInputHandler(JPEG_HandleTypeDef *hjpeg)
{

    if(DE_IN_BufferTab.State == JPEG_BUFFER_EMPTY)
    {
        uint32_t inDataLength;
        Input_frameIndex += DEC_CHUNK_SIZE_IN;
        if( Input_frameIndex < Input_frameSize){
            JPEGSourceAddress = JPEGSourceAddress + DEC_CHUNK_SIZE_IN;

            if((Input_frameSize - Input_frameIndex) >= DEC_CHUNK_SIZE_IN){
                inDataLength = DEC_CHUNK_SIZE_IN;
            }else{
                inDataLength = Input_frameSize - Input_frameIndex;
            }

            if(inDataLength < DEC_CHUNK_SIZE_IN && inDataLength % 4 != 0){
                inDataLength += 4 - (inDataLength % 4);
            }
            memcpy((uint8_t *)DE_IN_BufferTab.DataBuffer, (uint8_t *)JPEGSourceAddress, inDataLength);
            DE_IN_BufferTab.State = JPEG_BUFFER_FULL;
            DE_IN_BufferTab.DataBufferSize = inDataLength;
        }else{
            inDataLength = 0;
        }
        
        if((Input_Is_Paused == 1) && DE_IN_BufferTab.State == JPEG_BUFFER_FULL)
        {
            Input_Is_Paused = 0;
            HAL_JPEG_ConfigInputBuffer(hjpeg,DE_IN_BufferTab.DataBuffer, DE_IN_BufferTab.DataBufferSize);    
        
            HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_INPUT); 
        }
                
    }
}

#endif
/**
  * @brief JPEG Get Data callback
  * @param hjpeg: JPEG handle pointer
  * @param NbData: Number of encoded (consumed) bytes from input buffer
  * @retval None
  */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t NbData)
{
    if(g_jpegc.mode == JPEG_MODE_ENC){
        if(NbData == Jpeg_IN_BufferTab.DataBufferSize){
            Jpeg_IN_BufferTab.State = JPEG_BUFFER_EMPTY;
            Jpeg_IN_BufferTab.DataBufferSize = 0;

            HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
            Input_Is_Paused = 1;
            if (g_jpegc.evt_id) {
                osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_ENC_IN_EMPTY);
            }
        }else{
            HAL_JPEG_ConfigInputBuffer(hjpeg,Jpeg_IN_BufferTab.DataBuffer + NbData, Jpeg_IN_BufferTab.DataBufferSize - NbData);
        }
    }else if(g_jpegc.mode == JPEG_MODE_DEC){
#if JPEG_USE_SOFT_CONV
        if(NbData == DE_IN_BufferTab.DataBufferSize)
        {  
            DE_IN_BufferTab.State = JPEG_BUFFER_EMPTY;
            DE_IN_BufferTab.DataBufferSize = 0;
        
            if(DE_IN_BufferTab.State == JPEG_BUFFER_EMPTY)
            {
                HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
                Input_Is_Paused = 1;
            }
        }else{
            HAL_JPEG_ConfigInputBuffer(hjpeg,DE_IN_BufferTab.DataBuffer + NbData, DE_IN_BufferTab.DataBufferSize - NbData);      
        }
#else
        uint32_t inDataLength;
        Input_frameIndex += NbData;
        if( Input_frameIndex < Input_frameSize){
            JPEGSourceAddress = JPEGSourceAddress + NbData;

            if((Input_frameSize - Input_frameIndex) >= DEC_CHUNK_SIZE_IN){
                inDataLength = DEC_CHUNK_SIZE_IN;
            }else{
                inDataLength = Input_frameSize - Input_frameIndex;
            }
        }else{
            inDataLength = 0;
        }
        // printf("Address: %p, NbData: %d,  inDataLength: %d\r\n", JPEGSourceAddress, NbData, inDataLength);
        if(inDataLength > 0){
            HAL_JPEG_ConfigInputBuffer(hjpeg,(uint8_t *)JPEGSourceAddress, inDataLength);
        }
#endif
    }
}

/**
  * @brief JPEG Data Ready callback
  * @param hjpeg: JPEG handle pointer
  * @param pDataOut: pointer to the output data buffer
  * @param OutDataLength: length of output buffer in bytes
  * @retval None
  */
void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef *hjpeg, uint8_t *pDataOut, uint32_t OutDataLength)
{
    if(g_jpegc.mode == JPEG_MODE_ENC){

        Jpeg_OUT_BufferTab.State = JPEG_BUFFER_FULL;
        Jpeg_OUT_BufferTab.DataBufferSize = OutDataLength;

        HAL_JPEG_ConfigOutputBuffer(hjpeg, Jpeg_OUT_BufferTab.DataBuffer, ENC_CHUNK_SIZE_OUT);
        HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
        Output_Is_Paused = 1;
        if (g_jpegc.evt_id) {
            osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_ENC_OUT_FULL);
        }
    }else if(g_jpegc.mode == JPEG_MODE_DEC){
#if JPEG_USE_SOFT_CONV
        DE_OUT_BufferTab.State = JPEG_BUFFER_FULL;
        DE_OUT_BufferTab.DataBufferSize = OutDataLength;

        if(DE_OUT_BufferTab.State != JPEG_BUFFER_EMPTY)
        {
            HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
            Output_Is_Paused = 1;
        }
        if (g_jpegc.evt_id) {
            osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_KICK);
        }
#else
        FrameBufferAddress += OutDataLength;
        decode_size+= OutDataLength;
        HAL_JPEG_ConfigOutputBuffer(hjpeg, (uint8_t *)FrameBufferAddress, DEC_CHUNK_SIZE_OUT);
#endif
    }

}

/**
  * @brief  JPEG Info ready callback
  * @param hjpeg: JPEG handle pointer
  * @param pInfo: JPEG Info Struct pointer
  * @retval None
  */
void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hjpeg, JPEG_ConfTypeDef *pInfo)
{
    if(g_jpegc.mode == JPEG_MODE_DEC){
        if(JPEG_GetDecodeColorConvertFunc(pInfo, &pYCbCrToRGB_pConvert_Function, &MCU_TotalNb) != HAL_OK)
        {
            Error_Handler();
        }
        if(pInfo->ImageHeight != g_jpegc.dec_params.ImageHeight || pInfo->ImageWidth != g_jpegc.dec_params.ImageWidth){
            HAL_JPEG_Abort(hjpeg);
            g_jpegc.dec_info.ColorSpace = pInfo->ColorSpace;
            g_jpegc.dec_info.ImageWidth = pInfo->ImageWidth;
            g_jpegc.dec_info.ImageHeight = pInfo->ImageHeight;
            g_jpegc.dec_info.ImageQuality = pInfo->ImageQuality;
            g_jpegc.dec_info.ChromaSubsampling = pInfo->ChromaSubsampling;
            g_jpegc.mode = JPEG_MODE_ERROR;
            osSemaphoreRelease(g_jpegc.sem_dec);
        }
        
    }
}

/*
  * @brief JPEG Decode complete callback
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
    Jpeg_HWEncodingEnd = 1;
    if (g_jpegc.evt_id) {
        osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_ENC_DONE);
    }
}

/**
  * @brief  JPEG Decode complete callback
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
    Jpeg_HWDecodingEnd = 1;
    if (g_jpegc.evt_id) {
        osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_DEC_DONE);
    }
}

/**
  * @brief  JPEG Error callback
  * @param hjpeg: JPEG handle pointer
  * @retval None
  */
void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
    printf("JPEG ErrorCallback\r\n");
    // Error_Handler();
    if (g_jpegc.evt_id) {
        osEventFlagsSet(g_jpegc.evt_id, JPEGC_EVT_ERROR);
    }
}

void jpegc_lock(void)
{
    osMutexAcquire(g_jpegc.mtx_id, osWaitForever);
}

void jpegc_unlock(void)
{
    osMutexRelease(g_jpegc.mtx_id);
}

static void jpegcProcess(void *argument)
{
    jpegc_t *jpegc = (jpegc_t *)argument;
    LOG_DRV_INFO("jpegcProcess start \r\n");
    uint32_t encode_processing_end = 0;
#if JPEG_USE_SOFT_CONV
    uint32_t decode_processing_end = 0;
#endif
    jpegc->mode = JPEG_MODE_IDLE;
    jpegc->is_init = true;
    while (jpegc->is_init) {
        /* Wait until something meaningful happens (callbacks set event flags). */
        jpegc_mode_e mode_snapshot;
        osMutexAcquire(jpegc->mtx_id, osWaitForever);
        mode_snapshot = jpegc->mode;
        osMutexRelease(jpegc->mtx_id);

        if (mode_snapshot == JPEG_MODE_IDLE) {
            (void)osEventFlagsWait(jpegc->evt_id,
                                   JPEGC_EVT_KICK,
                                   osFlagsWaitAny,
                                   osWaitForever);
            continue;
        }

        uint32_t wait_mask = 0;
        if (mode_snapshot == JPEG_MODE_ENC) {
            wait_mask = JPEGC_EVT_KICK | JPEGC_EVT_ENC_IN_EMPTY | JPEGC_EVT_ENC_OUT_FULL | JPEGC_EVT_ENC_DONE | JPEGC_EVT_ERROR;
        } else if (mode_snapshot == JPEG_MODE_DEC) {
            wait_mask = JPEGC_EVT_KICK | JPEGC_EVT_DEC_DONE | JPEGC_EVT_ERROR;
#if JPEG_USE_SOFT_CONV
            wait_mask |= JPEGC_EVT_KICK;
#endif
        } else {
            wait_mask = JPEGC_EVT_KICK | JPEGC_EVT_ERROR;
        }

        (void)osEventFlagsWait(jpegc->evt_id, wait_mask, osFlagsWaitAny, 200);

        osMutexAcquire(jpegc->mtx_id, osWaitForever);
        if(jpegc->mode == JPEG_MODE_ENC){
            /* Service pending input/output once we're woken up */
            JPEG_EncodeInputHandler(&hjpeg);
            encode_processing_end = JPEG_EncodeOutputHandler(&hjpeg);
            if(encode_processing_end == 1){
                jpegc->mode = JPEG_MODE_ENC_COMPLETE;
                osSemaphoreRelease(jpegc->sem_enc);
            }
        }else if(jpegc->mode == JPEG_MODE_DEC){
#if JPEG_USE_SOFT_CONV
            JPEG_DecodeInputHandler(&hjpeg);
            decode_processing_end = JPEG_DecodeOutputHandler(&hjpeg);
            if(decode_processing_end == 1){
#else
            if(Jpeg_HWDecodingEnd == 1){
#endif
                jpegc->mode = JPEG_MODE_DEC_COMPLETE;
                HAL_JPEG_GetInfo(&hjpeg, &JPEG_Info);
                jpegc->dec_info.ColorSpace = JPEG_Info.ColorSpace;
                jpegc->dec_info.ImageWidth = JPEG_Info.ImageWidth;
                jpegc->dec_info.ImageHeight = JPEG_Info.ImageHeight;
                jpegc->dec_info.ImageQuality = JPEG_Info.ImageQuality;
                jpegc->dec_info.ChromaSubsampling = JPEG_Info.ChromaSubsampling;

                jpegc->dec_output_buffer_size = (uint32_t)jpegc->dec_info.ImageWidth * jpegc->dec_info.ImageHeight * JPEG_BYTES_PER_PIXEL(jpegc->dec_info.ChromaSubsampling);
                LOG_DRV_DEBUG("jepgc_decode size:%d, width:%d, height:%d, Quality:%d, Subsampling:%d\r\n",decode_size, jpegc->dec_info.ImageWidth, jpegc->dec_info.ImageHeight, jpegc->dec_info.ImageQuality, jpegc->dec_info.ChromaSubsampling);
                osSemaphoreRelease(g_jpegc.sem_dec);
            }
        }
        osMutexRelease(jpegc->mtx_id);
    }
    LOG_DRV_ERROR("jpegcProcess exit \r\n");
    jpegc->jpegc_processId = NULL;
    osThreadExit();
}
static int jpegc_start(void *priv)
{
    return AICAM_OK;
}

static int jpegc_stop(void *priv)
{
    return AICAM_OK;
}
static int jpegc_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    jpegc_t *jpegc = (jpegc_t *)priv;
    JPEGC_CMD_E jpegc_cmd = (JPEGC_CMD_E)cmd;
    int ret = AICAM_OK;
    if(!jpegc->is_init)
        return AICAM_ERROR;
    // LOG_DRV_DEBUG("jpegc_ioctl cmd: 0x%x \r\n", jpegc_cmd);
    osMutexAcquire(jpegc->mtx_id, osWaitForever);
    switch (jpegc_cmd)
    {
        case JPEGC_CMD_GET_STATE:
            *ubuf = jpegc->mode;
            ret = AICAM_OK;
            break;

        case JPEGC_CMD_SET_ENC_PARAM:
            if(arg != sizeof(jpegc_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&jpegc->enc_params, ubuf, sizeof(jpegc_params_t));
            
            /* Automatically adjust chroma subsampling if current setting doesn't meet resolution requirements */
            uint32_t original_chroma = jpegc->enc_params.ChromaSubsampling;
            uint32_t auto_chroma = jpegc_auto_select_chroma_subsampling(
                jpegc->enc_params.ImageWidth,
                jpegc->enc_params.ImageHeight,
                jpegc->enc_params.ColorSpace
            );
            
            /* Check if original chroma subsampling is valid */
            JPEG_ConfTypeDef test_conf;
            test_conf.ImageWidth = jpegc->enc_params.ImageWidth;
            test_conf.ImageHeight = jpegc->enc_params.ImageHeight;
            test_conf.ChromaSubsampling = jpegc->enc_params.ChromaSubsampling;
            test_conf.ColorSpace = jpegc->enc_params.ColorSpace;
            
            int is_valid = (((test_conf.ImageWidth % 8) == 0) && ((test_conf.ImageHeight % 8) == 0) && \
                (((test_conf.ImageWidth % 16) == 0) || (test_conf.ColorSpace != JPEG_YCBCR_COLORSPACE) || (test_conf.ChromaSubsampling == JPEG_444_SUBSAMPLING)) && \
                (((test_conf.ImageHeight % 16) == 0) || (test_conf.ColorSpace != JPEG_YCBCR_COLORSPACE) || (test_conf.ChromaSubsampling != JPEG_420_SUBSAMPLING)));
            
            if (!is_valid) {
                /* Original chroma subsampling doesn't meet requirements, use auto-selected one */
                jpegc->enc_params.ChromaSubsampling = auto_chroma;
                LOG_DRV_WARN("Chroma subsampling %d doesn't meet resolution %dx%d requirements, auto-adjusted to %d",
                           original_chroma, jpegc->enc_params.ImageWidth, jpegc->enc_params.ImageHeight, auto_chroma);
            }
            
            /* Allocate encode output buffer. Always allocate a new buffer:
             * if the previous encode's buffer was not UNSHAREd, the caller
             * still holds the pointer from OUTPUT_ENC_BUFFER. Dropping our
             * reference without freeing avoids both a dangling pointer (if we
             * freed) and buffer aliasing (if we reused). Caller frees via
             * RETURN_ENC_BUFFER or FREE_ENC_BUFFER. */
            uint32_t required_size = jpegc_calc_enc_buffer_size(&jpegc->enc_params);

            /* Drop jpegc's reference to the previous encode output buffer.
             * Do NOT hal_mem_free — the caller from OUTPUT_ENC_BUFFER owns it. */
            if (jpegc->enc_output_buffer != NULL) {
                jpegc->enc_output_buffer = NULL;
                jpegc->enc_output_buffer_size = 0;
                jpegc->enc_output_buffer_capacity = 0;
            }

            jpegc->enc_output_buffer = (unsigned char *)hal_mem_alloc_aligned(required_size, 32, MEM_LARGE);
            jpegc->enc_output_buffer_capacity = (jpegc->enc_output_buffer != NULL) ? required_size : 0;
            if(jpegc->enc_output_buffer == NULL){
                ret = AICAM_ERROR_NO_MEMORY;
                break;
            }
            ret = AICAM_OK;
            break;
        
        case JPEGC_CMD_GET_ENC_PARAM:
            if(arg != sizeof(jpegc_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &jpegc->enc_params, sizeof(jpegc_params_t));
            ret = AICAM_OK;
            break;

        case JPEGC_CMD_SET_DEC_PARAM:
            if(arg != sizeof(jpegc_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&jpegc->dec_params, ubuf, sizeof(jpegc_params_t));

            if (((jpegc->dec_params.ImageWidth % 8) != 0 ) || ((jpegc->dec_params.ImageHeight % 8) != 0 ) || \
                (((jpegc->dec_params.ImageWidth % 16) != 0 ) && (jpegc->dec_params.ColorSpace == JPEG_YCBCR_COLORSPACE) && (jpegc->dec_params.ChromaSubsampling != JPEG_444_SUBSAMPLING)) || \
                (((jpegc->dec_params.ImageHeight % 16) != 0 ) && (jpegc->dec_params.ColorSpace == JPEG_YCBCR_COLORSPACE) && (jpegc->dec_params.ChromaSubsampling == JPEG_420_SUBSAMPLING))){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            if(jpegc->dec_output_buffer != NULL){
                hal_mem_free(jpegc->dec_output_buffer);
                jpegc->dec_output_buffer = NULL;
            }
            jpegc->dec_output_buffer = (unsigned char *)hal_mem_alloc_aligned(jpegc->dec_params.ImageWidth * jpegc->dec_params.ImageHeight * JPEG_BYTES_PER_PIXEL(jpegc->dec_params.ChromaSubsampling), 32, MEM_LARGE);
            LOG_DRV_DEBUG("jpegc dec output buffer addr:0x%x, size:%d \r\n", jpegc->dec_output_buffer, (uint32_t)(jpegc->dec_params.ImageWidth * jpegc->dec_params.ImageHeight * JPEG_BYTES_PER_PIXEL(jpegc->dec_params.ChromaSubsampling)));
            if(jpegc->dec_output_buffer == NULL){
                ret = AICAM_ERROR_NO_MEMORY;
                break;
            }
            ret = AICAM_OK; 
            break;
        case JPEGC_CMD_GET_DEC_INFO:
            if(arg != sizeof(jpegc_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &jpegc->dec_info, sizeof(jpegc_params_t));
            ret = AICAM_OK;
            break;
        
        case JPEGC_CMD_INPUT_ENC_BUFFER:
            if(jpegc->mode != JPEG_MODE_IDLE || ubuf == NULL || (uint32_t)ubuf % 32 != 0 || jpegc->enc_output_buffer == NULL){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            jpegc->enc_input_buffer = ubuf;
            jpegc->mode = JPEG_MODE_ENC;
            HAL_JPEG_Abort(&hjpeg);
            if(JPEG_Encode_DMA(&hjpeg, jpegc) != 0){
                jpegc->mode = JPEG_MODE_IDLE;
                ret = AICAM_ERROR;
                break;
            }
            if (jpegc->evt_id) {
                osEventFlagsSet(jpegc->evt_id, JPEGC_EVT_KICK);
            }
            ret = AICAM_OK;
            break;

        case JPEGC_CMD_OUTPUT_ENC_BUFFER:
            if(jpegc->mode == JPEG_MODE_ENC_COMPLETE){
                *((unsigned char **)ubuf) = jpegc->enc_output_buffer;
                ret = jpegc->enc_output_buffer_size;
                jpegc->mode = JPEG_MODE_IDLE;
                break;
            }
            
            if(jpegc->mode != JPEG_MODE_ENC){
                ret = AICAM_ERROR;
                break;
            }

            osMutexRelease(jpegc->mtx_id);
            if (osSemaphoreAcquire(jpegc->sem_enc, 10000) == osOK){
                osMutexAcquire(jpegc->mtx_id, osWaitForever);
                if(jpegc->mode == JPEG_MODE_ENC_COMPLETE){
                    *((unsigned char **)ubuf) = jpegc->enc_output_buffer;
                    ret = jpegc->enc_output_buffer_size;
                }else{
                    ret = AICAM_ERROR_TIMEOUT;
                }
            }else{
                osMutexAcquire(jpegc->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            jpegc->mode = JPEG_MODE_IDLE;
            break;

        case JPEGC_CMD_INPUT_DEC_BUFFER:
            if(jpegc->mode != JPEG_MODE_IDLE || ubuf == NULL || jpegc->dec_output_buffer == NULL){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            jpegc->dec_input_buffer = ubuf;
            jpegc->mode = JPEG_MODE_DEC;
            jpegc->dec_input_buffer_size = arg;
            HAL_JPEG_Abort(&hjpeg);
            if(JPEG_Decode_DMA(&hjpeg, jpegc) != 0){
                jpegc->mode = JPEG_MODE_IDLE;
                ret = AICAM_ERROR;
                break;
            }
            if (jpegc->evt_id) {
                osEventFlagsSet(jpegc->evt_id, JPEGC_EVT_KICK);
            }
            ret = AICAM_OK;
            break;

        case JPEGC_CMD_OUTPUT_DEC_BUFFER:
            if(jpegc->mode == JPEG_MODE_DEC_COMPLETE){
                *((unsigned char **)ubuf) = jpegc->dec_output_buffer;
                ret = jpegc->dec_output_buffer_size;
                jpegc->mode = JPEG_MODE_IDLE;
                break;
            }
            osMutexRelease(jpegc->mtx_id);
            if (osSemaphoreAcquire(jpegc->sem_dec, 15000) == osOK){
                osMutexAcquire(jpegc->mtx_id, osWaitForever);
                if(jpegc->mode == JPEG_MODE_DEC_COMPLETE){
                    *((unsigned char **)ubuf) = jpegc->dec_output_buffer;
                    ret = jpegc->dec_output_buffer_size;
                }else if(jpegc->mode == JPEG_MODE_ERROR){
                    LOG_DRV_DEBUG("jpegc decode data error\r\n");
                    LOG_DRV_DEBUG("parse param width:%d, height:%d, Quality:%d, Subsampling:%d\r\n",jpegc->dec_info.ImageWidth, jpegc->dec_info.ImageHeight, jpegc->dec_info.ImageQuality, jpegc->dec_info.ChromaSubsampling);
                    ret = AICAM_ERROR_INVALID_DATA;
                }else{
                    ret = AICAM_ERROR_TIMEOUT;
                }
            }else{
                osMutexAcquire(jpegc->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            jpegc->mode = JPEG_MODE_IDLE;
            break;

        case JPEGC_CMD_RETURN_ENC_BUFFER:
            if(jpegc->mode == JPEG_MODE_ENC){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(jpegc->enc_output_buffer != NULL && jpegc->enc_output_buffer == ubuf){
                hal_mem_free(jpegc->enc_output_buffer);
                jpegc->enc_output_buffer = NULL;
                ret = AICAM_OK;
            }else{
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case JPEGC_CMD_RETURN_DEC_BUFFER:
            if(jpegc->mode == JPEG_MODE_DEC){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(jpegc->dec_output_buffer != NULL && jpegc->dec_output_buffer == ubuf){
                hal_mem_free(jpegc->dec_output_buffer);
                jpegc->dec_output_buffer = NULL;
                ret = AICAM_OK;
            }else{
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case JPEGC_CMD_UNSHARE_ENC_BUFFER:
            /* Detach encoder-owned output buffer so caller manages its lifetime.
             * Valid after JPEGC_CMD_OUTPUT_ENC_BUFFER returns a pointer. */
            if (jpegc->mode == JPEG_MODE_ENC) {
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if (jpegc->enc_output_buffer != NULL && jpegc->enc_output_buffer == ubuf) {
                jpegc->enc_output_buffer = NULL;
                ret = AICAM_OK;
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case JPEGC_CMD_FREE_ENC_BUFFER:
            if (ubuf != NULL){
                hal_mem_free(ubuf);
            }
            ret = AICAM_OK;
            break;
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    osMutexRelease(jpegc->mtx_id);
    return ret;
}

static int jpegc_init(void *priv)
{
    LOG_DRV_DEBUG("jpegc_init \r\n");
    jpegc_t *jpegc = (jpegc_t *)priv;
    jpegc->mtx_id = osMutexNew(NULL);
    jpegc->evt_id = osEventFlagsNew(NULL);
    jpegc->sem_id = osSemaphoreNew(1, 0, NULL);
    jpegc->sem_enc = osSemaphoreNew(1, 0, NULL);
    jpegc->sem_dec = osSemaphoreNew(1, 0, NULL);
    MX_JPEG_Init();
    JPEG_InitColorTables();

    jpegc->enc_params.ColorSpace = ENC_DEFAULT_COLOR_SPACE;
    jpegc->enc_params.ChromaSubsampling = ENC_DEFAULT_CHROMA_SAMPLING;
    jpegc->enc_params.ImageHeight = DEFAULT_IMAGE_HEIGHT;
    jpegc->enc_params.ImageWidth = DEFAULT_IMAGE_WIDTH;
    jpegc->enc_params.ImageQuality = ENC_DEFAULT_IMAGE_QUALITY;
    jpegc->enc_input_buffer = NULL;
    jpegc->enc_output_buffer = NULL;
    jpegc->enc_output_buffer_size = 0;
    jpegc->enc_output_buffer_capacity = 0;

    jpegc->dec_params.ColorSpace = JPEG_YCBCR_COLORSPACE;
    jpegc->dec_params.ChromaSubsampling = JPEG_444_SUBSAMPLING;
    jpegc->dec_params.ImageHeight = DEFAULT_IMAGE_HEIGHT;
    jpegc->dec_params.ImageWidth = DEFAULT_IMAGE_WIDTH;
    jpegc->dec_input_buffer_size = 0;
    jpegc->dec_input_buffer = NULL;
    jpegc->dec_output_buffer = NULL;
    memset(&jpegc->dec_info, 0, sizeof(jpegc_params_t));

    jpegc->mode = JPEG_MODE_IDLE;
    jpegc->jpegc_processId = osThreadNew(jpegcProcess, jpegc, &jpegcTask_attributes);
    return 0;
}

static int jpegc_deinit(void *priv)
{
    jpegc_t *jpegc = (jpegc_t *)priv;

    jpegc->is_init = false;
    if (jpegc->evt_id) {
        osEventFlagsSet(jpegc->evt_id, JPEGC_EVT_KICK);
    }
    osSemaphoreRelease(jpegc->sem_id);
    osDelay(100);
    if (jpegc->jpegc_processId != NULL && osThreadGetId() != jpegc->jpegc_processId) {
        osThreadTerminate(jpegc->jpegc_processId);
        jpegc->jpegc_processId = NULL;
    }

    if (jpegc->sem_id != NULL) {
        osSemaphoreDelete(jpegc->sem_id);
        jpegc->sem_id = NULL;
    }

    if (jpegc->evt_id != NULL) {
        osEventFlagsDelete(jpegc->evt_id);
        jpegc->evt_id = NULL;
    }

    if (jpegc->mtx_id != NULL) {
        osMutexDelete(jpegc->mtx_id);
        jpegc->mtx_id = NULL;
    }

    if (jpegc->enc_output_buffer) {
        hal_mem_free(jpegc->enc_output_buffer);
        jpegc->enc_output_buffer = NULL;
    }

    if (jpegc->dec_output_buffer) {
        hal_mem_free(jpegc->dec_output_buffer);
        jpegc->dec_output_buffer = NULL;
    }
    return 0;
}

int jpegc_register(void)
{
    static dev_ops_t jpegc_ops = {
        .init = jpegc_init, 
        .deinit = jpegc_deinit, 
        .start = jpegc_start,
        .stop = jpegc_stop,
        .ioctl = jpegc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_jpegc.dev = dev;
    strcpy(dev->name, JPEG_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &jpegc_ops;
    dev->priv_data = &g_jpegc;

    device_register(g_jpegc.dev);
    return AICAM_OK;
}

int jpegc_unregister(void)
{
    device_unregister(g_jpegc.dev);
    if (g_jpegc.dev) {
        hal_mem_free(g_jpegc.dev);
        g_jpegc.dev = NULL;
    }
    return AICAM_OK;
}