 /**
 ******************************************************************************
 * @file    draw.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#include "draw.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "common_utils.h"
#include "stm32n6xx_hal.h"
#include "mem.h"
#include "debug.h"

draw_t g_draw = {0};
static DMA2D_HandleTypeDef *dma2d_current;

const osThreadAttr_t drawTask_attributes = {
    .name = "drawTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 128 * 10
};

static void DRAW_HwLock(void *dma2d_handle)
{
    osMutexAcquire(g_draw.mtx_id, osWaitForever);
    dma2d_current = (DMA2D_HandleTypeDef *)dma2d_handle;
}

static void DRAW_HwUnlock()
{
    osMutexRelease(g_draw.mtx_id);
}

static void DRAW_Wfe()
{
    osSemaphoreAcquire(g_draw.sem_id, osWaitForever);
}

static void DRAW_Signal()
{
    osSemaphoreRelease(g_draw.sem_id);
}

static void draw_dma2d_cb(DMA2D_HandleTypeDef *hdma2d)
{
    DRAW_Signal();
}

static void draw_dma2d_error_cb(DMA2D_HandleTypeDef *hdma2d)
{
    printf("draw_dma2d_error_cb\r\n");
    assert(0);
}

static int draw_copy_hw(uint8_t *p_dst, int dst_width, int dst_height, uint8_t *p_src, int src_width,
                              int src_height, int x_offset, int y_offset, uint32_t src_colormode)
{
    DMA2D_HandleTypeDef hdma2d = { 0 };
    uint32_t dst_addr;
    int ret;

    hdma2d.Instance            = DMA2D;
    hdma2d.Init.Mode           = DMA2D_M2M_BLEND;
    hdma2d.Init.ColorMode      = g_draw.colormode_param.out_colormode;
    hdma2d.Init.OutputOffset   = dst_width - src_width;
    hdma2d.Init.AlphaInverted  = DMA2D_REGULAR_ALPHA;
    hdma2d.Init.RedBlueSwap    = DMA2D_RB_REGULAR;
    hdma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    hdma2d.Init.BytesSwap      = DMA2D_BYTES_REGULAR;

    DRAW_HwLock(&hdma2d);
    ret = HAL_DMA2D_Init(&hdma2d);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_Init error\r\n");
        DRAW_HwUnlock();
        return ret;
    }

    /* background layer come from dest buffer */
    hdma2d.LayerCfg[0].InputAlpha     = 0xFF;
    hdma2d.LayerCfg[0].InputColorMode = g_draw.colormode_param.in_colormode;
    hdma2d.LayerCfg[0].InputOffset    = dst_width - src_width;
    hdma2d.LayerCfg[0].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[0].RedBlueSwap    = DMA2D_RB_REGULAR;
    /* foreground layer is the src layer */
    hdma2d.LayerCfg[1].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[1].InputAlpha     = 0xFF;
    hdma2d.LayerCfg[1].InputColorMode = src_colormode;
    hdma2d.LayerCfg[1].InputOffset    = 0;
    hdma2d.LayerCfg[1].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_REGULAR;
    hdma2d.LayerCfg[0].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
        DRAW_HwUnlock();
        return ret;
    }
    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 0);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
        DRAW_HwUnlock();
        return ret;
    }

    hdma2d.XferCpltCallback = draw_dma2d_cb;
    hdma2d.XferErrorCallback = draw_dma2d_error_cb;

    uint8_t bytes_per_pixel;
    switch (g_draw.colormode_param.out_colormode) {
        case DMA2D_OUTPUT_ARGB8888:
            bytes_per_pixel = 4;
            break;
        case DMA2D_OUTPUT_RGB888:
            bytes_per_pixel = 3;
            break;
        case DMA2D_OUTPUT_RGB565:
        case DMA2D_OUTPUT_ARGB1555:
        case DMA2D_OUTPUT_ARGB4444:
            bytes_per_pixel = 2;
            break;
        default:
            LOG_DRV_ERROR("Unsupported color mode\r\n");
            HAL_DMA2D_DeInit(&hdma2d);
            DRAW_HwUnlock();
            return HAL_ERROR;
    }

    dst_addr = (uint32_t) (p_dst + (dst_width * y_offset + x_offset) * bytes_per_pixel);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    ret = HAL_DMA2D_BlendingStart_IT(&hdma2d, (uint32_t) p_src, dst_addr, dst_addr, src_width, src_height);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_BlendingStart_IT error\r\n");
    }

    if(ret == HAL_OK)
        DRAW_Wfe();

    HAL_NVIC_DisableIRQ(DMA2D_IRQn);

    HAL_DMA2D_DeInit(&hdma2d);

    DRAW_HwUnlock();
    return ret;
}

static int draw_fill_hw(uint8_t *p_dst, int dst_width, int dst_height, int src_width, int src_height,
                              int x_offset, int y_offset, uint32_t color, uint32_t color_mode)
{
    DMA2D_HandleTypeDef hdma2d = { 0 };
    uint32_t dst_addr;
    int ret;

    hdma2d.Instance          = DMA2D;
    hdma2d.Init.Mode         = DMA2D_R2M;
    hdma2d.Init.ColorMode    = color_mode;
    hdma2d.Init.OutputOffset = dst_width - src_width;

    DRAW_HwLock(&hdma2d);
    ret = HAL_DMA2D_Init(&hdma2d);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_Init error\r\n");
        DRAW_HwUnlock();
        return ret;
    }

    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
    }

    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    hdma2d.XferCpltCallback = draw_dma2d_cb;
    hdma2d.XferErrorCallback = draw_dma2d_error_cb;

    uint8_t bytes_per_pixel;
    switch (color_mode) {
        case DMA2D_OUTPUT_ARGB8888:
            bytes_per_pixel = 4;
            break;
        case DMA2D_OUTPUT_RGB888:
            bytes_per_pixel = 3;
            break;
        case DMA2D_OUTPUT_RGB565:
        case DMA2D_OUTPUT_ARGB1555:
        case DMA2D_OUTPUT_ARGB4444:
            bytes_per_pixel = 2;
            break;
        default:
            LOG_DRV_ERROR("Unsupported color mode\r\n");
            HAL_DMA2D_DeInit(&hdma2d);
            DRAW_HwUnlock();
            return HAL_ERROR;
    }

    dst_addr = (uint32_t)(p_dst + (dst_width * y_offset + x_offset) * bytes_per_pixel);
    ret = HAL_DMA2D_Start_IT(&hdma2d, color, dst_addr, src_width, src_height);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_Start_IT error\r\n");
    }
    if(ret == HAL_OK)
        DRAW_Wfe();

    HAL_NVIC_DisableIRQ(DMA2D_IRQn);

    HAL_DMA2D_DeInit(&hdma2d);
    DRAW_HwUnlock();
    return ret;
}

static int draw_blend_colorrect_hw(uint8_t *p_dst, int dst_width, int dst_height,
                                   int x_pos, int y_pos, int width, int height,
                                   uint32_t color, uint8_t alpha, uint32_t color_mode)
{
    int ret = HAL_OK;
    uint8_t bytes_per_pixel;
    uint32_t dst_addr;
    uint8_t *p_fg = NULL;

    // Calculate bytes per pixel
    switch (color_mode) {
        case DMA2D_OUTPUT_ARGB8888: 
            bytes_per_pixel = 4; 
            break;
        case DMA2D_OUTPUT_RGB888:   
            bytes_per_pixel = 3;
            break;
        case DMA2D_OUTPUT_RGB565:
        case DMA2D_OUTPUT_ARGB1555:
        case DMA2D_OUTPUT_ARGB4444: 
            bytes_per_pixel = 2; 
            break;
        default:
            LOG_DRV_ERROR("Unsupported color mode\r\n");
            return HAL_ERROR;
    }

    // Construct solid color block buffer
    size_t fg_buf_size = width * height * bytes_per_pixel;
    p_fg = hal_mem_alloc_aligned(fg_buf_size, 32, MEM_LARGE);
    if (!p_fg) {
        LOG_DRV_ERROR("No memory for blend colorrect\r\n");
        return HAL_ERROR;
    }

    size_t pixel_count = width * height;
    switch (color_mode) {
        case DMA2D_OUTPUT_ARGB8888: {
            uint32_t *p = (uint32_t*)p_fg;
            for (size_t i = 0; i < pixel_count; i++)
                p[i] = color; // ARGB8888 direct write
            break;
        }
        case DMA2D_OUTPUT_RGB888: {
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            for (size_t i = 0; i < pixel_count; i++) {
                p_fg[i*3 + 0] = r;
                p_fg[i*3 + 1] = g;
                p_fg[i*3 + 2] = b;
            }
            break;
        }
        case DMA2D_OUTPUT_RGB565: {
            uint16_t rgb565 = ARGB8888_TO_RGB565(color);
            uint16_t *p = (uint16_t*)p_fg;
            for (size_t i = 0; i < pixel_count; i++)
                p[i] = rgb565;
            break;
        }
        case DMA2D_OUTPUT_ARGB1555: {
            uint16_t argb1555 = ARGB8888_TO_ARGB1555(color);
            uint16_t *p = (uint16_t*)p_fg;
            for (size_t i = 0; i < pixel_count; i++)
                p[i] = argb1555;
            break;
        }
        case DMA2D_OUTPUT_ARGB4444: {
            uint16_t argb4444 = ARGB8888_TO_ARGB4444(color);
            uint16_t *p = (uint16_t*)p_fg;
            for (size_t i = 0; i < pixel_count; i++)
                p[i] = argb4444;
            break;
        }
        default:
            LOG_DRV_ERROR("Unsupported color mode\r\n");
            return HAL_ERROR;
    }

    DMA2D_HandleTypeDef hdma2d = { 0 };
    hdma2d.Instance            = DMA2D;
    hdma2d.Init.Mode           = DMA2D_M2M_BLEND;
    hdma2d.Init.ColorMode      = color_mode;
    hdma2d.Init.OutputOffset   = dst_width - width;

    // Configure background layer (original image)
    hdma2d.LayerCfg[0].InputAlpha     = 0xFF;
    hdma2d.LayerCfg[0].InputColorMode = color_mode;
    hdma2d.LayerCfg[0].InputOffset    = dst_width - width;
    hdma2d.LayerCfg[0].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[0].RedBlueSwap    = DMA2D_RB_REGULAR;

    // Configure foreground layer (solid color block)
    hdma2d.LayerCfg[1].AlphaMode      = DMA2D_COMBINE_ALPHA;
    hdma2d.LayerCfg[1].InputAlpha     = alpha;
    hdma2d.LayerCfg[1].InputColorMode = color_mode;
    hdma2d.LayerCfg[1].InputOffset    = 0;
    hdma2d.LayerCfg[1].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_REGULAR;

    DRAW_HwLock(&hdma2d);
    ret = HAL_DMA2D_Init(&hdma2d);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_Init error\r\n");
        hal_mem_free(p_fg);
        DRAW_HwUnlock();
        return ret;
    }
    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
        HAL_DMA2D_DeInit(&hdma2d);
        hal_mem_free(p_fg);
        DRAW_HwUnlock();
        return ret;
    }
    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 0);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
        HAL_DMA2D_DeInit(&hdma2d);
        hal_mem_free(p_fg);
        DRAW_HwUnlock();
        return ret;
    }

    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    hdma2d.XferCpltCallback = draw_dma2d_cb;
    hdma2d.XferErrorCallback = draw_dma2d_error_cb;

    dst_addr = (uint32_t)(p_dst + (dst_width * y_pos + x_pos) * bytes_per_pixel);
    ret = HAL_DMA2D_BlendingStart_IT(&hdma2d, (uint32_t)p_fg, dst_addr, dst_addr, width, height);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_BlendingStart_IT error\r\n");
    }
    if(ret == HAL_OK)
        DRAW_Wfe();

    HAL_NVIC_DisableIRQ(DMA2D_IRQn);
    HAL_DMA2D_DeInit(&hdma2d);
    DRAW_HwUnlock();

    hal_mem_free(p_fg);
    return ret;
}

static int draw_color_convert(draw_color_convert_param_t *param)
{

    DMA2D_HandleTypeDef hdma2d = { 0 };
    int ret;
    hdma2d.Instance = DMA2D;
    hdma2d.Init.Mode = DMA2D_M2M_PFC;
    hdma2d.Init.ColorMode = param->out_colormode;
    hdma2d.Init.OutputOffset = 0;
    hdma2d.LayerCfg[1].InputOffset = 0;
    hdma2d.LayerCfg[1].InputColorMode = param->in_colormode;
    hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[1].InputAlpha = 0xFF;

    if(param->rb_swap){
        hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_SWAP;
    }

    if(param->in_colormode == DMA2D_INPUT_YCBCR){
        hdma2d.LayerCfg[1].ChromaSubSampling  = param->ChromaSubSampling;
    }

    DRAW_HwLock(&hdma2d);
    ret = HAL_DMA2D_Init(&hdma2d);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_Init error\r\n");
        DRAW_HwUnlock();
        return ret;
    }

    ret = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_ConfigLayer error\r\n");
    }

    hdma2d.XferCpltCallback = draw_dma2d_cb;
    hdma2d.XferErrorCallback = draw_dma2d_error_cb;

    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    ret = HAL_DMA2D_Start_IT(&hdma2d,(uint32_t)param->p_src, (uint32_t)param->p_dst, param->src_width, param->src_height);
    if(ret != HAL_OK){
        LOG_DRV_ERROR("HAL_DMA2D_BlendingStart_IT error\r\n");
    }

    if(ret == HAL_OK)
        DRAW_Wfe();

    HAL_NVIC_DisableIRQ(DMA2D_IRQn);

    HAL_DMA2D_DeInit(&hdma2d);

    DRAW_HwUnlock();
    return ret;
}

static int draw_hline_hw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos, int len, int line_width,
                               uint32_t color, uint32_t color_mode)
{
    return draw_fill_hw(p_dst, dst_width, dst_height, len, line_width, x_pos, y_pos, color, color_mode);
}

static int draw_vline_hw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos, int len, int line_width,
                               uint32_t color, uint32_t color_mode)
{
    return draw_fill_hw(p_dst, dst_width, dst_height, line_width, len, x_pos, y_pos, color, color_mode);
}

static int draw_rect_hw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos, int width, int height, int line_width,
                       uint32_t color, uint32_t color_mode)
{
    int ret;
    ret = draw_hline_hw(p_dst, dst_width, dst_height, x_pos, y_pos, width, line_width, color, color_mode);
    if(ret != HAL_OK){
        return ret;
    }

    ret = draw_hline_hw(p_dst, dst_width, dst_height, x_pos, y_pos + height - 1, width, line_width, color, color_mode);
    if(ret != HAL_OK){
        return ret;
    }

    ret = draw_vline_hw(p_dst, dst_width, dst_height, x_pos, y_pos, height, line_width , color, color_mode);
    if(ret != HAL_OK){
        return ret;
    }

    ret = draw_vline_hw(p_dst, dst_width, dst_height, x_pos + width - 1, y_pos, height, line_width, color, color_mode);

    return ret;
}

static void draw_font_cvt(sFONT *p_font_in, uint32_t *dout, uint8_t *din)
{
    uint32_t height, width;
    uint32_t offset;
    uint8_t  *pchar;
    uint32_t line;
    int i, j;

    height = p_font_in->Height;
    width  = p_font_in->Width;

    offset =  8 *((width + 7)/8) -  width;
    for( i = 0; i < height; i++) {
        pchar = (din + (width + 7)/8 * i);

        switch(((width + 7)/8)) {
            case 1:
                line =  pchar[0];
                break;
            case 2:
                line =  (pchar[0]<< 8) | pchar[1];
                break;
            case 3:
            default:
                line =  (pchar[0]<< 16) | (pchar[1]<< 8) | pchar[2];
            break;
        }

        for (j = 0; j < width; j++) {
            if (line & (1 << (width- j + offset- 1)))
                *dout++ = 0xFFFFFFFFUL;
            else
                *dout++ = 0x40000000UL;
        }
    }
}


static int draw_font_setup_with_memory(sFONT *p_font_in, DRAW_Font_t *p_font, void *data, int data_size)
{
    const int nb_char_in_font = '~' - ' ' + 1;
    int bytes_per_char;
    int char_size_in;
    int i;

    bytes_per_char = p_font_in->Width * p_font_in->Height * 4;
    if (data_size < bytes_per_char * nb_char_in_font)
        return -1;

    char_size_in = p_font_in->Height * ((p_font_in->Width + 7) / 8);
    p_font->width = p_font_in->Width;
    p_font->height = p_font_in->Height;
    p_font->data = data;

    for (i = 0; i < nb_char_in_font; i++)
        draw_font_cvt(p_font_in, (uint32_t *) &p_font->data[i * bytes_per_char],
                    (uint8_t *) &p_font_in->table[i * char_size_in]);

    return 0;
}

static int draw_draw_char_hw(DRAW_Font_t *p_font, uint8_t *p_dst, int dst_width, int dst_height, int x_pos,
                                   int y_pos, uint8_t *data)
{
    return draw_copy_hw(p_dst, dst_width, dst_height, data, p_font->width, p_font->height, x_pos, y_pos, DMA2D_INPUT_ARGB8888);
}

static int draw_display_char_hw(DRAW_Font_t *p_font, uint8_t *p_dst, int dst_width, int dst_height, int x_pos,
                                      int y_pos, char c)
{
    int char_size = p_font->height * p_font->width * 4;

    return draw_draw_char_hw(p_font, p_dst, dst_width, dst_height, x_pos, y_pos, &p_font->data[(c - ' ') * char_size]);
}

static int draw_puts_hw(DRAW_Font_t *p_font, uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos,
                              char *line)
{
    int ret;
    while (*line != '\0') {
        ret = draw_display_char_hw(p_font, p_dst, dst_width, dst_height, x_pos, y_pos, *line);
        if(ret != HAL_OK)
            return ret;
        x_pos += p_font->width;
        line++;
    }
    return HAL_OK;
}

static int DRAW_FontSetup(sFONT *p_font_in, DRAW_Font_t *p_font)
{
    const int nb_char_in_font = '~' - ' ' + 1;
    int bytes_per_char;

    bytes_per_char = p_font_in->Width * p_font_in->Height * 4;
    if(p_font->data == NULL){
        p_font->data = hal_mem_alloc_aligned(nb_char_in_font * bytes_per_char, 32, MEM_LARGE);
        if (!p_font->data)
            return -1;
    }

    return draw_font_setup_with_memory(p_font_in, p_font, p_font->data, nb_char_in_font * bytes_per_char);
}

static int DRAW_RectHw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos, int width, int height, int line_width,
                     uint32_t color, uint32_t color_mode)
{
    return draw_rect_hw(p_dst, dst_width, dst_height, x_pos, y_pos, width, height,line_width, color, color_mode);
}

static int DRAW_FillHw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos, int width, int height,
                     uint32_t color, uint32_t color_mode)
{
    return draw_fill_hw(p_dst, dst_width, dst_height, x_pos, y_pos, width, height, color, color_mode);
}

static int DRAW_PrintfHw(DRAW_Font_t *p_font, uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos,
                       char * str)
{
    return draw_puts_hw(p_font, p_dst, dst_width, dst_height, x_pos, y_pos, str);
}

static int DMA2D_CopyClip(uint8_t *p_dst, int dst_width, int dst_height,
                    uint8_t *p_src, int src_width, int src_height,
                    int src_x, int src_y,
                    int clip_width, int clip_height,
                    int dst_x, int dst_y, uint32_t src_colormode) 
{

    uint8_t *p_src_clip = p_src + (src_y * src_width + src_x) * 4;

    return draw_copy_hw(p_dst, dst_width, dst_height,
                      p_src_clip, clip_width, clip_height,
                      dst_x, dst_y, src_colormode);
}

static inline int abs_sw(int x) {
    return x < 0 ? -x : x;
}

static int draw_line_hw(uint8_t *p_dst, int dst_width, int dst_height,
                        int x1, int y1, int x2, int y2, int line_width,
                        uint32_t color, uint32_t color_mode)
{
    int dx = abs_sw(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs_sw(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;

    line_width = line_width < 1 ? 1 : line_width;
    
    while (1) {
        int start_x = x1 - line_width/2;
        int start_y = y1 - line_width/2;
        
        start_x = start_x < 0 ? 0 : start_x;
        start_y = start_y < 0 ? 0 : start_y;
        
        int draw_width = line_width;
        int draw_height = line_width;
        
        if (start_x + draw_width > dst_width) {
            draw_width = dst_width - start_x;
        }
        if (start_y + draw_height > dst_height) {
            draw_height = dst_height - start_y;
        }

        if (draw_width > 0 && draw_height > 0) {
            draw_fill_hw(p_dst, dst_width, dst_height, 
                         draw_width, draw_height, 
                         start_x, start_y, 
                         color, color_mode);
        }
        
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    return HAL_OK;
}

static int draw_dot_hw(uint8_t *p_dst, int dst_width, int dst_height, int x_pos, int y_pos,
                       int dot_width, uint32_t color, uint32_t color_mode)
{
    dot_width = dot_width < 1 ? 1 : dot_width;
    int radius = dot_width / 2;
    int x0 = x_pos;
    int y0 = y_pos;
    int x, y;

    for (y = -radius; y <= radius; y++) {
        int current_y = y0 + y;
        if (current_y < 0 || current_y >= dst_height) continue;
        
        int start_x = -1;
        for (x = -radius; x <= radius; x++) {
            int current_x = x0 + x;
            if (current_x < 0) continue;
            if (current_x >= dst_width) break;
            
            if (x*x + y*y <= radius*radius) {
                if (start_x == -1) {
                    start_x = current_x;
                }
            } else {
                if (start_x != -1) {
                    int seg_width = current_x - start_x;
                    if (seg_width > 0) {
                        draw_fill_hw(p_dst, dst_width, dst_height, 
                                     seg_width, 1, 
                                     start_x, current_y, 
                                     color, color_mode);
                    }
                    start_x = -1;
                }
            }
        }
        
        if (start_x != -1) {
            int seg_width = (x0 + radius) - start_x + 1;
            if (seg_width > dst_width - start_x) {
                seg_width = dst_width - start_x;
            }
            if (seg_width > 0) {
                draw_fill_hw(p_dst, dst_width, dst_height, 
                             seg_width, 1, 
                             start_x, current_y, 
                             color, color_mode);
            }
        }
    }
    return HAL_OK;
}
void DMA2D_IRQHandler(void)
{
    HAL_DMA2D_IRQHandler(dma2d_current);
}

static void drawProcess(void *argument)
{
    draw_t *draw = (draw_t *)argument;
    while (draw->is_init) {
        osDelay(100);
    }
    osThreadExit();
}

static int draw_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    draw_t *draw = (draw_t *)priv;
    int ret = AICAM_OK;
    if(!draw->is_init)
        return AICAM_ERROR_NOT_FOUND;
    osMutexAcquire(draw->mtx_id, osWaitForever);
    switch (cmd) {
        case DRAW_CMD_SET_COLOR_MODE:{
            if(arg != sizeof(draw_colormode_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_colormode_param_t *p = (draw_colormode_param_t*)ubuf;
            if((p->in_colormode != DMA2D_INPUT_ARGB8888) &&
               (p->in_colormode != DMA2D_INPUT_RGB888) &&
               (p->in_colormode != DMA2D_INPUT_RGB565) &&
               (p->in_colormode != DMA2D_INPUT_ARGB1555) &&
               (p->in_colormode != DMA2D_INPUT_ARGB4444) &&
               (p->in_colormode != DMA2D_INPUT_YCBCR)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            if((p->out_colormode != DMA2D_OUTPUT_ARGB8888) &&
               (p->out_colormode != DMA2D_OUTPUT_RGB888) &&
               (p->out_colormode != DMA2D_OUTPUT_RGB565)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw->colormode_param.in_colormode = p->in_colormode;
            draw->colormode_param.out_colormode = p->out_colormode;
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_FONT_SETUP:{
            if(arg != sizeof(draw_fontsetup_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_fontsetup_param_t *p = (draw_fontsetup_param_t*)ubuf;
            ret = DRAW_FontSetup(p->p_font_in, p->p_font);
            if(ret != 0){
                ret = AICAM_ERROR_NO_MEMORY;
            }else{
                ret = AICAM_OK;
            }
            break;
        }

        case DRAW_CMD_FILL:{
            if(arg != sizeof(draw_fill_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_fill_param_t *p = (draw_fill_param_t*)ubuf;

            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->x_pos < 0 || p->y_pos < 0 || p->width <= 0 || p->height <= 0 ||
               (p->x_pos + p->width) > p->dst_width || (p->y_pos + p->height) > p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            DRAW_FillHw(p->p_dst, p->dst_width, p->dst_height, p->x_pos, p->y_pos, p->width, p->height, p->color, draw->colormode_param.out_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_RECT:{
            if(arg != sizeof(draw_rect_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_rect_param_t *p = (draw_rect_param_t*)ubuf;
            if(p->line_width < 1)
                p->line_width = 1;

            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->x_pos < 0 || p->y_pos < 0 || p->width <= 0 || p->height <= 0 ||
               (p->x_pos + p->width) > p->dst_width || (p->y_pos + p->height) > p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }

            DRAW_RectHw(p->p_dst, p->dst_width, p->dst_height, p->x_pos, p->y_pos, p->width, p->height, p->line_width, p->color, draw->colormode_param.out_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_COPY:{
            if(arg != sizeof(draw_copy_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_copy_param_t *p = (draw_copy_param_t*)ubuf;
            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->p_src == NULL || p->src_width <= 0 || p->src_height <= 0 ||
               p->x_offset < 0 || p->y_offset < 0 ||
               (p->x_offset + p->src_width) > p->dst_width || (p->y_offset + p->src_height) > p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_copy_hw(p->p_dst, p->dst_width, p->dst_height, p->p_src, p->src_width, p->src_height, p->x_offset, p->y_offset, draw->colormode_param.in_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_PRINTF:{
            if(arg != sizeof(draw_printf_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_printf_param_t *p = (draw_printf_param_t*)ubuf;
            if(p->p_font == NULL || p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->x_pos < 0 || p->y_pos < 0 ){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            DRAW_PrintfHw(p->p_font, p->p_dst, p->dst_width, p->dst_height, p->x_pos, p->y_pos, p->str);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_CLIP:{
            if(arg != sizeof(draw_clip_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_clip_param_t *p = (draw_clip_param_t*)ubuf;
            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->p_src == NULL || p->src_width <= 0 || p->src_height <= 0 ||
               p->src_x < 0 || p->src_y < 0 || p->clip_width <= 0 || p->clip_height <= 0 ||
               (p->src_x + p->clip_width) > p->src_width || (p->src_y + p->clip_height) > p->src_height ||
               p->dst_x < 0 || p->dst_y < 0 ||
               (p->dst_x + p->clip_width) > p->dst_width || (p->dst_y + p->clip_height) > p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            DMA2D_CopyClip(p->p_dst, p->dst_width, p->dst_height,
                        p->p_src, p->src_width, p->src_height,
                        p->src_x, p->src_y,
                        p->clip_width, p->clip_height,
                        p->dst_x, p->dst_y, draw->colormode_param.in_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_COLOR_CONVERT:{
            if(arg != sizeof(draw_color_convert_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_color_convert_param_t *p = (draw_color_convert_param_t*)ubuf;
            if(p->p_dst == NULL || p->p_src == NULL || p->src_width <= 0 || p->src_height <= 0){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_color_convert(p);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_DOT: {
            if(arg != sizeof(draw_dot_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_dot_param_t *p = (draw_dot_param_t*)ubuf;
            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->x_pos < 0 || p->y_pos < 0){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_dot_hw(p->p_dst, p->dst_width, p->dst_height, p->x_pos, p->y_pos, p->dot_width, p->color, draw->colormode_param.out_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_LINE: {
            if(arg != sizeof(draw_line_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_line_param_t *p = (draw_line_param_t*)ubuf;
            if(p->line_width < 1)
                p->line_width = 1;
            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
               p->x1 < 0 || p->y1 < 0 || p->x2 < 0 || p->y2 < 0 ||
               p->x1 >= p->dst_width || p->x2 >= p->dst_width ||
               p->y1 >= p->dst_height || p->y2 >= p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_line_hw(p->p_dst, p->dst_width, p->dst_height, p->x1, p->y1, p->x2, p->y2, p->line_width, p->color, draw->colormode_param.out_colormode);
            ret = AICAM_OK;
            break;
        }
        case DRAW_CMD_BLEND_COLOR_RECT: {
            if(arg != sizeof(draw_colorrect_param_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            draw_colorrect_param_t *p = (draw_colorrect_param_t*)ubuf;
            if(p->p_dst == NULL || p->dst_width <= 0 || p->dst_height <= 0 ||
            p->x_pos < 0 || p->y_pos < 0 || p->width <= 0 || p->height <= 0 ||
            (p->x_pos + p->width) > p->dst_width || (p->y_pos + p->height) > p->dst_height){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            ret = draw_blend_colorrect_hw(p->p_dst, p->dst_width, p->dst_height, p->x_pos, p->y_pos,
                                        p->width, p->height, p->color, p->alpha, draw->colormode_param.out_colormode);
            ret = (ret == HAL_OK) ? AICAM_OK : AICAM_ERROR_UNKNOWN;
            break;
        }
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    osMutexRelease(draw->mtx_id);
    return ret;
}

static int draw_init(void *priv)
{
    draw_t *draw = (draw_t *)priv;
    draw->mtx_id = osMutexNew(NULL);
    draw->sem_id = osSemaphoreNew(1, 0, NULL);

    draw->colormode_param.in_colormode = DRAW_DEFAULT_INPUT_COLORMODE;
    draw->colormode_param.out_colormode = DRAW_DEFAULT_OUTPUT_COLORMODE;

    draw->draw_processId = osThreadNew(drawProcess, draw, &drawTask_attributes);
    draw->is_init = true;
    return 0;
}

static int draw_deinit(void *priv)
{
    draw_t *draw = (draw_t *)priv;

    draw->is_init = false;
    osSemaphoreRelease(draw->sem_id);
    osDelay(20);
    if (draw->draw_processId != NULL) {
        osThreadTerminate(draw->draw_processId);
        draw->draw_processId = NULL;
    }

    if (draw->sem_id != NULL) {
        osSemaphoreDelete(draw->sem_id);
        draw->sem_id = NULL;
    }

    if (draw->mtx_id != NULL) {
        osMutexDelete(draw->mtx_id);
        draw->mtx_id = NULL;
    }

    return 0;
}

int draw_register(void)
{
    static dev_ops_t draw_ops = {
        .init = draw_init, 
        .deinit = draw_deinit, 
        .ioctl = draw_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_draw.dev = dev;
    strcpy(dev->name, DRAW_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &draw_ops;
    dev->priv_data = &g_draw;

    device_register(g_draw.dev);
    return AICAM_OK;
}

int draw_unregister(void)
{
    if (g_draw.dev) {
        device_unregister(g_draw.dev);
        hal_mem_free(g_draw.dev);
        g_draw.dev = NULL;
    }
    return AICAM_OK;
}
