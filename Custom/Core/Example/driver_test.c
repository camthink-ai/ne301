#include <stdbool.h>
#include "driver_test.h"
#include "debug.h"
#include "storage.h"
#include "generic_file.h"
#include "lfs.h"
#include "camera.h"
#include "enc.h"
#include "draw.h"
#include "dev_manager.h"
#include "cli_cmd.h"
#include "framework.h"
#include "wifi.h"
#include "common_utils.h"
#include "drtc.h"
#include "jpegc.h"
#include "mem.h"
#include "pixel_format_map.h"
#include "nn.h"
#include "ai_draw.h"
#include "mem_map.h"
#include "rs485_driver.h"
#include "storage.h"
#include "../FSBL/Core/Inc/xspim.h"
#include "crc.h"

static int create_file(const char* filename, const void* data, size_t data_size);

static const char *sysclk_profile_name(uint32_t p)
{
    switch (p) {
    case FSBL_APP_SYSCLK_PROFILE_HSE_200MHZ: return "HSE_200MHZ";
    case FSBL_APP_SYSCLK_PROFILE_HSE_400MHZ: return "HSE_400MHZ";
    case FSBL_APP_SYSCLK_PROFILE_HSI_800MHZ: return "HSI_800MHZ";
    case FSBL_APP_SYSCLK_PROFILE_HSE_800MHZ: return "HSE_800MHZ";
    default: return "unknown";
    }
}

static int sysclk_test_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("sysclk: usage\r\n");
        LOG_SIMPLE("  sysclk get              -- read saved sys_clk_config from flash\r\n");
        LOG_SIMPLE("  sysclk set <prof>       -- write profile for next boot (200|400|800|hsi800)\r\n");
        LOG_SIMPLE("  sysclk delaytest [ms]   -- measure osDelay vs HAL tick + DWT (default 1000)\r\n");
        return -1;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "delaytest") == 0) {
        uint32_t ms = 1000U;
        if (argc >= 3)
            ms = (uint32_t)atoi(argv[2]);
        if (ms == 0U || ms > 60000U) {
            LOG_SIMPLE("sysclk delaytest: ms must be 1..60000\r\n");
            return -1;
        }

        SystemCoreClockUpdate();
        uint32_t cpu_hz = SystemCoreClock;

        uint32_t t0 = HAL_GetTick();
        osDelay(ms);
        uint32_t t1 = HAL_GetTick();
        uint32_t dt = t1 - t0;
        LOG_SIMPLE("sysclk delaytest: osDelay(%lu) HAL_GetTick delta=%lu ms (expect ~%lu)\r\n",
                   (unsigned long)ms, (unsigned long)dt, (unsigned long)ms);

        /* DWT window capped: CYCCNT is 32-bit, high CPU MHz can wrap in long delays */
        uint32_t dwt_ms = (ms > 50U) ? 50U : ms;
#if defined(CoreDebug_DEMCR_TRCENA_Msk)
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#endif
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        uint32_t tick_d0 = HAL_GetTick();
        uint32_t c0 = DWT->CYCCNT;
        osDelay(dwt_ms);
        uint32_t c1 = DWT->CYCCNT;
        uint32_t tick_d1 = HAL_GetTick();
        uint32_t cy = c1 - c0;
        uint32_t tick_step = tick_d1 - tick_d0;
        uint64_t expect_nom = (uint64_t)cpu_hz * (uint64_t)dwt_ms / 1000ULL;
        uint64_t expect_tick = (uint64_t)cpu_hz * (uint64_t)tick_step / 1000ULL;
        int64_t err_nom = (int64_t)cy - (int64_t)expect_nom;
        int64_t err_tick = (int64_t)cy - (int64_t)expect_tick;
        /* If SysCoreClk matches CYCCNT rate, cycles / Hz ~= wall time that CYCCNT "saw" */
        uint64_t implied_us = (cy * 1000000ULL + ((uint64_t)cpu_hz / 2ULL)) / (uint64_t)cpu_hz;
        uint32_t implied_whole_ms = (uint32_t)(implied_us / 1000ULL);
        uint32_t implied_frac_us = (uint32_t)(implied_us % 1000ULL);
        LOG_SIMPLE("sysclk delaytest: DWT osDelay(%lu ms req): HAL tick step=%lu ms, cycles=%lu\r\n",
                   (unsigned long)dwt_ms, (unsigned long)tick_step, (unsigned long)cy);
        LOG_SIMPLE("  SysClk %lu MHz: expect_cycles@tick_step=%lu err=%ld; implied~%lu.%03lu ms from cycles/Hz\r\n",
                   (unsigned long)(cpu_hz / 1000000U), (unsigned long)expect_tick, (long)err_tick,
                   (unsigned long)implied_whole_ms, (unsigned long)implied_frac_us);
        if (tick_step != dwt_ms) {
            LOG_SIMPLE("  note: requested %lu ms but tick stepped %lu ms (tick quantization); nominal err=%ld cycles\r\n",
                       (unsigned long)dwt_ms, (unsigned long)tick_step, (long)err_nom);
        }
        return 0;
    }

    if (strcmp(sub, "get") == 0 || strcmp(sub, "read") == 0) {
        sys_clk_config_t cfg = {0};
        int ret = fsbl_app_read_sys_clk_config(&cfg);
        if (ret == 0) {
            LOG_SIMPLE("sysclk: saved profile=%lu (%s) crc32=0x%08lx\r\n",
                       (unsigned long)cfg.sys_clk_profile, sysclk_profile_name(cfg.sys_clk_profile),
                       (unsigned long)cfg.crc32);
            return 0;
        }
        if (ret == -4)
            LOG_SIMPLE("sysclk: no valid saved config (crc mismatch or empty)\r\n");
        else
            LOG_SIMPLE("sysclk: read failed (%d)\r\n", ret);
        return ret;
    }

    if (strcmp(sub, "set") == 0 || strcmp(sub, "write") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("sysclk set: need profile 200|400|800|hsi800\r\n");
            return -1;
        }
        uint32_t prof;
        if (strcmp(argv[2], "200") == 0)
            prof = FSBL_APP_SYSCLK_PROFILE_HSE_200MHZ;
        else if (strcmp(argv[2], "400") == 0)
            prof = FSBL_APP_SYSCLK_PROFILE_HSE_400MHZ;
        else if (strcmp(argv[2], "800") == 0)
            prof = FSBL_APP_SYSCLK_PROFILE_HSE_800MHZ;
        else if (strcmp(argv[2], "hsi800") == 0)
            prof = FSBL_APP_SYSCLK_PROFILE_HSI_800MHZ;
        else {
            LOG_SIMPLE("sysclk set: unknown profile '%s'\r\n", argv[2]);
            return -1;
        }

        sys_clk_config_t cfg = {0};
        cfg.sys_clk_profile = prof;
        int ret = fsbl_app_write_sys_clk_config(&cfg);
        if (ret != 0) {
            LOG_SIMPLE("sysclk: write failed (%d)\r\n", ret);
            return ret;
        }
        LOG_SIMPLE("sysclk: wrote profile %lu (%s), reboot for FSBL to apply\r\n",
                   (unsigned long)prof, sysclk_profile_name(prof));
        return 0;
    }

    LOG_SIMPLE("sysclk: unknown '%s'\r\n", sub);
    return -1;
}

debug_cmd_reg_t sysclk_cmd_table[] = {
    {"sysclk", "Flash sys_clk_config + delay timing test", sysclk_test_cmd},
};

static void sysclk_cmd_register(void)
{
    debug_cmdline_register(sysclk_cmd_table, sizeof(sysclk_cmd_table) / sizeof(sysclk_cmd_table[0]));
}

mpe_draw_conf_t mpe_draw_conf = {0};
od_draw_conf_t od_draw_conf = {0};
pp_od_out_t od;
pp_mpe_out_t mpe;
osMutexId_t mtx_ai;

typedef struct {
    uint32_t frame_count;
    uint32_t draw_time;
    uint32_t draw_count;
    uint32_t enc_time;
    uint32_t total_time;
    uint32_t last_time;
} video_time_t;

static int captrue_flag;
static int video_flag;
static int aipipe_flag;
static int ai_result_flag;
static video_time_t video_time;
__attribute__((unused)) static osThreadId_t VideoTest_processId;
__attribute__((unused)) static osThreadId_t CaptureTest_processId;
__attribute__((unused)) static osThreadId_t AITest_processId;
static uint8_t capture_tread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
static uint8_t ai_tread_stack[1024 * 6] ALIGN_32 IN_PSRAM;
const osThreadAttr_t VideoTestTask_attributes = {
    .name = "VideoTestTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_size = 4 * 1024,
};

const osThreadAttr_t CaptureTestTask_attributes = {
    .name = "CaptureTestTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_mem = capture_tread_stack,
    .stack_size = sizeof(capture_tread_stack),
};

const osThreadAttr_t AiTestTask_attributes = {
    .name = "AiTestTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_mem = ai_tread_stack,
    .stack_size = sizeof(ai_tread_stack),
};


#if VIDEO_DRAW_TEST
DRAW_Font_t font_12 = {0};
DRAW_Font_t font_16 = {0};
#endif
static void morning_wake_cb(void *arg)
{
    LOG_APP_DEBUG("morning_wake_cb \r\n");
}

static void interval_wake_cb(void *arg)
{
    LOG_APP_DEBUG("interval_wake_cb \r\n");
}

static void office_wake_cb(void *arg)
{
    LOG_APP_DEBUG("office_wake_cb \r\n");
}

static void sched_enter_cb(void *arg)
{
    LOG_APP_DEBUG("sched_enter_cb \r\n");
}

static void sched_exit_cb(void *arg)
{
    LOG_APP_DEBUG("sched_exit_cb \r\n");
}

__attribute__((unused)) static void rtc_test(void)
{
    rtc_wakeup_t rtc_wakeup;
    rtc_schedule_t rtc_schedule;

    schedule_period_t periods[] = {
        {.start_sec = 9*3600, .end_sec = 11*3600, .repeat = REPEAT_DAILY},
        {.start_sec = 10*3600, .end_sec = 12*3600, .repeat = REPEAT_WEEKLY, .weekdays = 0x3F} // Weekdays
    };

    strcpy(rtc_wakeup.name, "interval Wake");
    rtc_wakeup.type = WAKEUP_TYPE_INTERVAL;
    rtc_wakeup.trigger_sec = 10;
    rtc_wakeup.day_offset = 0;
    rtc_wakeup.repeat = REPEAT_INTERVAL;
    rtc_wakeup.weekdays = 0;
    rtc_wakeup.callback = interval_wake_cb;
    rtc_wakeup.arg = NULL;
    rtc_register_wakeup_ex(&rtc_wakeup);

    strcpy(rtc_wakeup.name, "Morning Wake");
    rtc_wakeup.type = WAKEUP_TYPE_ABSOLUTE;
    rtc_wakeup.trigger_sec = 8*3600;
    rtc_wakeup.day_offset = 0;
    rtc_wakeup.repeat = REPEAT_DAILY;
    rtc_wakeup.weekdays = 0;
    rtc_wakeup.callback = morning_wake_cb;
    rtc_wakeup.arg = NULL;
    rtc_register_wakeup_ex(&rtc_wakeup);

    strcpy(rtc_wakeup.name, "Office Wake");
    rtc_wakeup.type = WAKEUP_TYPE_ABSOLUTE;
    rtc_wakeup.trigger_sec = 9*3600;
    rtc_wakeup.day_offset = 0;
    rtc_wakeup.repeat = REPEAT_WEEKLY;
    rtc_wakeup.weekdays = 0x3F;
    rtc_wakeup.callback = office_wake_cb;
    rtc_wakeup.arg = NULL;
    rtc_register_wakeup_ex(&rtc_wakeup);

    strcpy(rtc_schedule.name, "sched test");
    rtc_schedule.periods = periods;
    rtc_schedule.period_count = sizeof(periods) / sizeof(periods[0]);
    rtc_schedule.enter_cb = sched_enter_cb;
    rtc_schedule.exit_cb = sched_exit_cb;
    rtc_schedule.arg = NULL;

    rtc_register_schedule_ex(&rtc_schedule);
}

static void capture_stop(void)
{
    int ret;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_t *jpeg = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);

    if(camera_dev == NULL || jpeg == NULL){
        LOG_APP_WARN("device not found\r\n");
        return;
    }
    ret = device_stop(camera_dev);
    if(ret != AICAM_OK){
        LOG_APP_WARN("camera stop failed :%d \r\n",ret);
    }
    ret = device_stop(jpeg);
    if(ret != AICAM_OK){
        LOG_APP_WARN("jpeg stop failed :%d \r\n",ret);
    }
}

static void capture_start(void)
{
    int ret;
    sensor_params_t sensor_param = {0};
    pipe_params_t pipe_param = {0};
    jpegc_params_t jpeg_param = {0};
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_t *jpeg = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_ioctl(camera_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
    LOG_SIMPLE("sensor name:%s, sensor width:%d, height:%d, fps:%d\r\n", sensor_param.name, sensor_param.width, sensor_param.height, sensor_param.fps);
    
    device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    /*--if need to change pipe param--*/
    // pipe_param.width = 1280;
    // pipe_param.height = 720;
    // pipe_param.fps = 30;
    // pipe_param.bpp = 2;
    // pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    // device_ioctl(camera_dev, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    LOG_APP_INFO(" pipe width:%d, height:%d, fps:%d ,format:%d, bpp:%d\r\n", pipe_param.width, pipe_param.height, pipe_param.fps, pipe_param.format, pipe_param.bpp);

    device_ioctl(jpeg, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    jpeg_param.ImageWidth = pipe_param.width;
    jpeg_param.ImageHeight = pipe_param.height;
    jpeg_param.ChromaSubsampling = JPEG_420_SUBSAMPLING;
    jpeg_param.ImageQuality = 90;

    device_ioctl(jpeg, JPEGC_CMD_SET_ENC_PARAM, (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    LOG_APP_INFO(" jpeg width:%d, height:%d, quality:%d, ChromaSubsampling:%d\r\n", jpeg_param.ImageWidth, jpeg_param.ImageHeight, jpeg_param.ImageQuality, jpeg_param.ChromaSubsampling);

    ret = device_start(camera_dev);
    if(ret != 0){
        LOG_APP_WARN("camera start failed :%d\r\n",ret);
        return;
    }

    ret = device_start(jpeg);
    if(ret != 0){
        LOG_APP_WARN("jpeg start failed :%d\r\n",ret);
        return;
    }
}

static uint8_t* capture_process(int *out_len)
{
    uint8_t *fb = NULL;
    uint8_t *outfb = NULL;
    int fb_len = 0;
    int ret;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_t *jpeg = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(camera_dev == NULL || jpeg == NULL){
        return NULL;
    }
    LOG_APP_DEBUG("capture_process\r\n");
    fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER,(uint8_t *)&fb, 0);
    if(fb_len > 0){
        ret = device_ioctl(jpeg, JPEGC_CMD_INPUT_ENC_BUFFER, fb, fb_len);
        if(ret != 0){
            LOG_APP_WARN("jpeg encode failed :%d\r\n",ret);
            return NULL;
        }
        *out_len = device_ioctl(jpeg, JPEGC_CMD_OUTPUT_ENC_BUFFER, (uint8_t *)&outfb, 0);
        device_ioctl(camera_dev, CAM_CMD_RETURN_PIPE1_BUFFER,fb, 0);
        if(*out_len < 0){
            LOG_APP_WARN("jpeg encode get buffer failed :%d\r\n",out_len);
        }
        return outfb;
    }
    return NULL;
}

uint8_t* video_start(int *out_len)
{
    uint8_t *fb = NULL;
    uint8_t *outfb = NULL;
    int ret;
    int fb_len = 0;
    int enc_len = 0;
    uint32_t start_time = osKernelGetTickCount();
    uint32_t tmp_time;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(camera_dev == NULL){
        return NULL;
    }

    device_t *enc = device_find_pattern(ENC_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(enc == NULL){
        return NULL;
    }

#if VIDEO_DRAW_TEST
    draw_printf_param_t print_param = {0};
    draw_rect_param_t rect_param = {0};
    int rect_x = 0,rect_y = 0;

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return NULL;
    }

    pipe_params_t pipe_param = {0};
    device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
#endif

    fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER,(uint8_t *)&fb, 0);
    if(fb_len > 0){
#if VIDEO_DRAW_TEST
        rect_x = rect_x + ((video_time.frame_count/30) * 2) % pipe_param.width;
        rect_y = rect_y + ((video_time.frame_count/30) * 2) % pipe_param.height;

        rect_param.p_dst = fb;
        rect_param.dst_width = pipe_param.width;
        rect_param.dst_height = pipe_param.height;
        rect_param.x_pos = rect_x;
        rect_param.y_pos = rect_y;
        rect_param.width = 200;
        rect_param.height = 200;
        rect_param.line_width = 2;
        rect_param.color = COLOR_YELLOW;

        if (rect_x + rect_param.width > pipe_param.width) {
            rect_x = pipe_param.width - rect_param.width;
            if (rect_x < 20) rect_x = 20;
        }
        if (rect_y + rect_param.height > pipe_param.height) {
            rect_y = pipe_param.height - rect_param.height;
            if (rect_y < 20) rect_y = 20;
        }

        device_ioctl(draw, DRAW_CMD_RECT, (uint8_t *)&rect_param, sizeof(draw_rect_param_t));

        // Text content
        snprintf(print_param.str, sizeof(print_param.str), "%5.1f%%", (float)(video_time.frame_count % 10000) / 100);
        int text_len = strlen(print_param.str);
        int text_width = text_len * font_12.width;
        int font_height = font_12.height;

        // Text box coordinates (above top-left corner)
        int text_x = rect_x;
        int text_y = rect_y - font_height;

        // Boundary check to prevent text from exceeding canvas top and right
        if (text_x + text_width > pipe_param.width) {
            text_x = pipe_param.width - text_width;
            if (text_x < 0) text_x = 0;
        }
        if (text_y < 0) {
            text_y = 0; // If text exceeds canvas top, align to top
        }

        print_param.p_font = &font_12;
        print_param.p_dst = fb;
        print_param.dst_width = pipe_param.width;
        print_param.dst_height = pipe_param.height;
        print_param.x_pos = text_x;
        print_param.y_pos = text_y;
        device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

        int box_x = rect_param.x_pos;
        int box_y = rect_param.y_pos;
        int box_w = rect_param.width;
        int box_h = rect_param.height;

        // Human figure parameters
        int center_x = box_x + box_w / 2;
        int center_y = box_y + box_h / 2;

        // Head (circle)
        draw_dot_param_t dot_param;
        dot_param.p_dst = fb;
        dot_param.dst_width = pipe_param.width;
        dot_param.dst_height = pipe_param.height;
        dot_param.x_pos = center_x;
        dot_param.y_pos = box_y + 40;      // 40 pixels from box top
        dot_param.dot_width = 24;          // Head diameter 24 pixels
        dot_param.color = COLOR_BLUE;
        device_ioctl(draw, DRAW_CMD_DOT, (uint8_t *)&dot_param, sizeof(draw_dot_param_t));

        // Body (vertical line)
        draw_line_param_t line_param;
        line_param.p_dst = fb;
        line_param.dst_width = pipe_param.width;
        line_param.dst_height = pipe_param.height;
        line_param.x1 = center_x;
        line_param.y1 = box_y + 52;         // Bottom of head
        line_param.x2 = center_x;
        line_param.y2 = box_y + 130;        // Bottom of body
        line_param.line_width = 6;
        line_param.color = COLOR_BLUE;
        device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));

        // Left arm (diagonal line)
        line_param.x1 = center_x;
        line_param.y1 = box_y + 70;
        line_param.x2 = center_x - 40;
        line_param.y2 = box_y + 90;
        line_param.line_width = 6;
        line_param.color = COLOR_GREEN;
        device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));

        // Right arm (diagonal line)
        line_param.x1 = center_x;
        line_param.y1 = box_y + 70;
        line_param.x2 = center_x + 40;
        line_param.y2 = box_y + 90;
        line_param.line_width = 6;
        line_param.color = COLOR_GREEN;
        device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));

        // Left leg (diagonal line)
        line_param.x1 = center_x;
        line_param.y1 = box_y + 130;
        line_param.x2 = center_x - 30;
        line_param.y2 = box_y + 180;
        line_param.line_width = 7;
        line_param.color = COLOR_RED;
        device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));

        // Right leg (diagonal line)
        line_param.x1 = center_x;
        line_param.y1 = box_y + 130;
        line_param.x2 = center_x + 30;
        line_param.y2 = box_y + 180;
        line_param.line_width = 7;
        line_param.color = COLOR_RED;
        device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));

#endif
        osMutexAcquire(mtx_ai, osWaitForever);
        if(ai_result_flag > 0){
            if(od.nb_detect > 0){
                tmp_time = osKernelGetTickCount();
                od_draw_conf.p_dst = fb;
                for(int i = 0; i < od.nb_detect; i++){
                    od_draw_result(&od_draw_conf, &od.detects[i]);
                }
                video_time.draw_count++;
                video_time.draw_time += osKernelGetTickCount() - tmp_time;
            }else if(mpe.nb_detect > 0){
                tmp_time = osKernelGetTickCount();
                mpe_draw_conf.p_dst = fb;
                for(int i = 0; i < mpe.nb_detect; i++){
                    mpe_draw_result(&mpe_draw_conf, &mpe.detects[i]);
                }
                video_time.draw_count++;
                video_time.draw_time += osKernelGetTickCount() - tmp_time;
            }
            ai_result_flag--;
        }
        osMutexRelease(mtx_ai);
        tmp_time = osKernelGetTickCount();
        ret = device_ioctl(enc, ENC_CMD_INPUT_BUFFER, fb, fb_len);
        if(ret != AICAM_OK){
            LOG_APP_WARN("enc input buffer failed :%d fb_len:%d\r\n",ret, fb_len);
        }else{
            enc_len = device_ioctl(enc, ENC_CMD_OUTPUT_BUFFER, (uint8_t *)&outfb, 0);
            if(video_time.frame_count % 300 == 0){
                LOG_APP_WARN("fb cnt:%d add:0x%x :enc_len:%d \r\n", video_time.frame_count,(int)fb, enc_len);
            }
        }
        video_time.enc_time += osKernelGetTickCount() - tmp_time;
        video_time.frame_count++;
        video_time.total_time += osKernelGetTickCount() - start_time;
        video_time.last_time = osKernelGetTickCount() - start_time;
        device_ioctl(camera_dev, CAM_CMD_RETURN_PIPE1_BUFFER,fb, 0);

    }else{
        // LOG_APP_WARN("video_start fb_len:%d \r\n", fb_len);
    }
    return NULL;
}


#define WRITE_CHUNK_SIZE 4096
uint8_t write_buf[WRITE_CHUNK_SIZE] ALIGN_32 UNCACHED;
static int create_file(const char* filename, const void* data, size_t data_size)
{
    if (!filename || !data || data_size == 0) {
        LOG_APP_DEBUG("create_file: invalid parameter\r\n");
        return -1;
    }
    LOG_APP_DEBUG("create_file name :%s data_size:%d \r\n", filename, data_size);
    void *fd = file_fopen(filename, "w");
    if (!fd) {
        LOG_APP_DEBUG("create_file: cannot open %s\r\n", filename);
        return -1;
    }

    size_t total_written = 0;
    const char *p = (const char*)data;
    size_t last_reported = 0;

    while (total_written < data_size) {
        size_t chunk_size = WRITE_CHUNK_SIZE;
        if (data_size - total_written < WRITE_CHUNK_SIZE) {
            chunk_size = data_size - total_written;
        }
        memcpy(write_buf, p + total_written, chunk_size);
        int written = file_fwrite(fd, write_buf, chunk_size);
        if (written != (int)chunk_size) {
            LOG_APP_DEBUG("create_file: write error \r\n");
            file_fclose(fd);
            return -1;
        }
        total_written += chunk_size;

        if (total_written - last_reported >= (WRITE_CHUNK_SIZE * 32) || total_written == data_size) {
            LOG_APP_DEBUG("create_file: written %u/%u bytes\r\n", (unsigned int)total_written, (unsigned int)data_size);
            last_reported = total_written;
        }
        osDelay(1);
    }
    file_fclose(fd);
    return 0;
}
#if !VIDEO_SEND_UVC
__attribute__((unused)) static int write_video_file_30s(const char* filename)
{
    void *fd = file_fopen(filename, "w");
    if (!fd) {
        LOG_APP_DEBUG("write_video_file_30s: cannot open %s\r\n", filename);
        return -1;
    }

    uint32_t start_tick = osKernelGetTickCount();
    uint32_t duration_ms = 30000; 
    uint8_t *fb = NULL;
    int fb_len = 0;

    while ((osKernelGetTickCount() - start_tick) < duration_ms && video_flag > 0) {
        fb = video_start(&fb_len); 
        if (fb && fb_len > 0) {
            int written = file_fwrite(fd, fb, fb_len);
            if (written != fb_len) {
                LOG_APP_DEBUG("write_video_file_30s: write error\r\n");
                file_fclose(fd);
                return -1;
            }
        }
        osDelay(10); 
    }

    file_fclose(fd);
    LOG_APP_DEBUG("write_video_file_30s: finish 30s\r\n");
    return 0;
}
#endif

__attribute__((unused)) static void CaptureTestProcess(void *argument)
{
    uint8_t *fb = NULL;
#if JPEG_DECODE
    int ret = 0;
    uint8_t *fraw = NULL;
    int fraw_len = 0;
#endif
    device_t *jpeg = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    int fb_len = 0;
    static int idx = 0;
    char filename[32];
    while(1){
        if(captrue_flag > 0){
            capture_start();
            LOG_APP_DEBUG("video capture start \r\n");
            while(captrue_flag > 0){
                sprintf(filename, "capture%d.jpg", idx);
                osDelay(2000);
                fb = capture_process(&fb_len);
                if(fb_len > 0){
                    create_file(filename, fb, fb_len);
#if !JPEG_DECODE
                    device_ioctl(jpeg, JPEGC_CMD_RETURN_ENC_BUFFER,fb, 0);
                }
#else
                }
                if(fb != NULL && fb_len > 0){
                    /*get picture param*/
                    jpegc_params_t jpeg_enc_param = {0};
                    ret = device_ioctl(jpeg, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)&jpeg_enc_param, sizeof(jpegc_params_t));
                    if(ret != 0){
                        LOG_APP_WARN("get pipe param failed :%d\r\n",ret);
                        break;
                    }

                    /*set decode param*/
                    jpegc_params_t jpeg_dec_param = {0};
                    jpeg_dec_param.ImageWidth = jpeg_enc_param.ImageWidth;
                    jpeg_dec_param.ImageHeight = jpeg_enc_param.ImageHeight;
                    jpeg_dec_param.ChromaSubsampling = jpeg_enc_param.ChromaSubsampling;
                    ret = device_ioctl(jpeg, JPEGC_CMD_SET_DEC_PARAM, (uint8_t *)&jpeg_dec_param, sizeof(jpegc_params_t));
                    if(ret != 0){
                        LOG_APP_WARN("set jpeg decode param failed :%d\r\n",ret);
                        break;
                    }

                    /*jpeg decode*/
                    ret = device_ioctl(jpeg, JPEGC_CMD_INPUT_DEC_BUFFER, fb, fb_len);
                    if(ret != 0){
                        LOG_APP_WARN("jpeg decode failed :%d\r\n",ret);
                        break;
                    }
                    
                    /*jpeg get decode result*/
                    fraw_len = device_ioctl(jpeg, JPEGC_CMD_OUTPUT_DEC_BUFFER, (uint8_t *)&fraw, 0);
                    if(fraw_len < 0){
                        LOG_APP_WARN("jpeg decode get buffer failed :%d\r\n",fraw_len);
                    }

                    /*return jpeg encode out buffer,If you are using other picture memory, you can also release it at this time*/
                    device_ioctl(jpeg, JPEGC_CMD_RETURN_ENC_BUFFER,fb, 0);

                    sprintf(filename, "decode%d.raw", idx);
                    if(fraw != NULL){
                        create_file(filename, fraw, fraw_len);
                    }else{
                        LOG_APP_DEBUG("jepgc decode failed \r\n");
                    }

                    /*color convert*/
                    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
                    if(draw != NULL){
                        draw_color_convert_param_t color_convert_param = {0};

                        color_convert_param.src_width = jpeg_dec_param.ImageWidth;
                        color_convert_param.src_height = jpeg_dec_param.ImageHeight;

                        /*Because the color space decoded by JPEG is fixed to JPEG_YCBCR_COLORSPACE, 
                        * the color space of DMA2D input is set to DMA2D_INPUT_YCBCR*/
                        color_convert_param.in_colormode = DMA2D_INPUT_YCBCR;
                        color_convert_param.out_colormode = DMA2D_OUTPUT_RGB888;
                        color_convert_param.p_src = fraw;
                        color_convert_param.p_dst = hal_mem_alloc_aligned(color_convert_param.src_width * color_convert_param.src_height * 3, 32, MEM_LARGE);
                        /*Because the image r and b decoded from jpeg are reversed, rb_swap needs to be set*/
                        color_convert_param.rb_swap = 1;
                        color_convert_param.ChromaSubSampling = CSS_jpeg_to_dma2d(jpeg_dec_param.ChromaSubsampling);
                        LOG_APP_DEBUG("color_convert_param.p_dst:0x%x size:%d\r\n",color_convert_param.p_dst, color_convert_param.src_width * color_convert_param.src_height * 3);
                        if(color_convert_param.p_dst != NULL){
                            ret = device_ioctl(draw, DRAW_CMD_COLOR_CONVERT, (uint8_t *)&color_convert_param, sizeof(draw_color_convert_param_t));
                            if(ret != 0){
                                LOG_APP_WARN("color convert failed :%d\r\n",ret);
                            }else{
                                sprintf(filename, "dma2d%d.raw", idx);
                                create_file(filename, color_convert_param.p_dst, color_convert_param.src_width * color_convert_param.src_height * 3);
                            }
                            /*The converted image raw can be released if not used.*/
                            hal_mem_free(color_convert_param.p_dst);
                            color_convert_param.p_dst = NULL;
                        }
                    }

                    /*return jpeg decode out buffer*/
                    device_ioctl(jpeg, JPEGC_CMD_RETURN_DEC_BUFFER,fraw, 0);
                }

#endif
                idx++;
                captrue_flag--;
            }
            if(video_flag == 0){
                capture_stop();
            }
            LOG_APP_DEBUG("video capture end \r\n");
        }else{
            osDelay(100);
        }
    }
}

__attribute__((unused)) static void VideoTestProcess(void *argument)
{
    int fb_len = 0;
    while(1){
        if(video_flag > 0){
            video_start(&fb_len);
        }else{
            osDelay(100);
        }
    }
}

__attribute__((unused)) static void AiTestProcess(void *argument)
{
    int fb_len = 0;
    uint8_t *fb = NULL;
    osDelay(2000);
    nn_result_t result;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(camera_dev == NULL){
        LOG_APP_WARN("camera device not found \r\n");
        return;
    }
    while(1){
        if(aipipe_flag > 0){
            fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE2_BUFFER,(uint8_t *)&fb, 0);
            if(fb_len > 0){
                if (nn_inference_frame(fb, fb_len, &result) == 0) {
                    if (result.type == PP_TYPE_OD && result.od.nb_detect > 0) {
                        // LOG_SIMPLE("result.od.nb_detect: %d\r\n", result.od.nb_detect);
                        osMutexAcquire(mtx_ai, osWaitForever);
                        od.nb_detect = result.od.nb_detect;
                        if(od.detects != NULL){
                            hal_mem_free(od.detects);
                            od.detects = NULL;
                        }
                        od.detects = hal_mem_alloc_aligned(sizeof(od_detect_t) * od.nb_detect, 32, MEM_LARGE);
                        if(od.detects == NULL || result.od.detects == NULL){
                            LOG_APP_WARN("alloc memory failed\r\n");
                            od.nb_detect = 0;
                            osMutexRelease(mtx_ai);
                            continue;
                        }
                        memcpy(od.detects, result.od.detects, sizeof(od_detect_t)* od.nb_detect);
                        ai_result_flag = 10;
                        osMutexRelease(mtx_ai);
                    } else if (result.type == PP_TYPE_MPE && result.mpe.nb_detect > 0) {
                        // LOG_SIMPLE("result.mpe.nb_detect: %d\r\n", result.mpe.nb_detect);
                        osMutexAcquire(mtx_ai, osWaitForever);
                        mpe.nb_detect = result.mpe.nb_detect;
                        if(mpe.detects != NULL){
                            hal_mem_free(mpe.detects);
                            mpe.detects = NULL;
                        }
                        mpe.detects = hal_mem_alloc_aligned(sizeof(mpe_detect_t) * mpe.nb_detect, 32, MEM_LARGE);
                        if(mpe.detects == NULL || result.mpe.detects == NULL){
                            LOG_APP_WARN("alloc memory failed\r\n");
                            mpe.nb_detect = 0;
                            osMutexRelease(mtx_ai);
                            continue;
                        }
                        memcpy(mpe.detects, result.mpe.detects, sizeof(mpe_detect_t)* mpe.nb_detect);
                        ai_result_flag = 10;
                        osMutexRelease(mtx_ai);
                    }
                }
                device_ioctl(camera_dev, CAM_CMD_RETURN_PIPE2_BUFFER,fb, 0);
            }
        }else{
            osDelay(100);
        }
    }
}

static int capture_cmd(int argc, char* argv[]) 
{
    if(argc > 1){
        int num = atoi(argv[1]);
        if(num > 0){
            captrue_flag = num;
        }else{
            captrue_flag = 1;
        }
    }else{
        captrue_flag = 1;
    }
    return 0;
}

static int video_cmd(int argc, char* argv[]) 
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: video <stop/start>\r\n");
        return -1;
    }

    int ret;
    nn_model_info_t model_info = {0};
    sensor_params_t sensor_param = {0};
    pipe_params_t pipe_param = {0};
    enc_param_t enc_param = {0};
    draw_colormode_param_t draw_param = {0};
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_t *enc = device_find_pattern(ENC_DEVICE_NAME, DEV_TYPE_VIDEO);
    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }
    if(camera_dev == NULL || enc == NULL){
        LOG_SIMPLE("device not found\r\n");
        return -1;
    }

    if (strcmp(argv[1], "start") == 0){
        if(video_flag > 0){
            LOG_SIMPLE("video has started\r\n");
            return -1;
        }
        device_ioctl(camera_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
        LOG_SIMPLE("sensor name:%s, sensor width:%d, height:%d, fps:%d\r\n", sensor_param.name, sensor_param.width, sensor_param.height, sensor_param.fps);

        /*The default control has been turned on CAMERA_CTRL_PIPE1_BIT*/
        // uint8_t camera_ctrl_pipe = CAMERA_CTRL_PIPE1_BIT;
        // ret = device_ioctl(camera_dev, CAM_CMD_SET_PIPE_CTRL, &camera_ctrl_pipe, 0);
        // if(ret != 0){
        //     LOG_SIMPLE("PIPE ctrl failed :%d\r\n",ret);
        //     return -1;
        // }

        device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        /*--if need to change pipe param--*/

        pipe_param.width = 720;
        pipe_param.height = 640;
        pipe_param.fps = 30;
        pipe_param.bpp = 2;
        pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
        device_ioctl(camera_dev, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));

        pipe_param.width = 1280;
        pipe_param.height = 720;
        pipe_param.fps = 30;
        pipe_param.bpp = 2;
        pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
        device_ioctl(camera_dev, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));

        LOG_SIMPLE(" pipe width:%d, height:%d, fps:%d ,format:%d, bpp:%d\r\n", pipe_param.width, pipe_param.height, pipe_param.fps, pipe_param.format, pipe_param.bpp);
#if VIDEO_DRAW_TEST
        draw_fontsetup_param_t font_param = {0};
        /* set 12 font */
        font_param.p_font_in = &Font12;
        font_param.p_font = &font_12;
        device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));

        /* set 16 font */
        font_param.p_font_in = &Font16;
        font_param.p_font = &font_16;
        device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));

        draw_param.in_colormode = fmt_dcmipp_to_dma2d(pipe_param.format);
        draw_param.out_colormode = DMA2D_OUTPUT_RGB565;
        device_ioctl(draw, DRAW_CMD_SET_COLOR_MODE, (uint8_t *)&draw_param, sizeof(draw_colormode_param_t));

        device_ioctl(enc, ENC_CMD_GET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));
        enc_param.width = pipe_param.width;
        enc_param.height = pipe_param.height;
        enc_param.fps = pipe_param.fps;
        enc_param.input_type = fmt_dma2d_to_enc(draw_param.out_colormode);
        enc_param.bpp = ENC_BYTES_PER_PIXEL(enc_param.input_type);
        device_ioctl(enc, ENC_CMD_SET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));

#else
        device_ioctl(enc, ENC_CMD_GET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));
        enc_param.width = pipe_param.width;
        enc_param.height = pipe_param.height;
        enc_param.fps = pipe_param.fps;
        enc_param.input_type = fmt_dcmipp_to_enc(pipe_param.format);
        enc_param.bpp = ENC_BYTES_PER_PIXEL(enc_param.input_type);
        device_ioctl(enc, ENC_CMD_SET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));
#endif
        ret = device_start(camera_dev);
        if(ret != 0){
            LOG_SIMPLE("camera start failed :%d\r\n",ret);
            return -1;
        }
        ret = device_start(enc);
        if(ret != 0){
            LOG_SIMPLE("encoder start failed :%d\r\n",ret);
            return -1;
        }
        memset(&video_time, 0, sizeof(video_time_t));
        video_flag = 1;
    }else if(strcmp(argv[1], "stop") == 0){
        if(video_flag == 0){
            LOG_SIMPLE("video not start\r\n");
            return -1;
        }
        video_flag = 0;
        osDelay(1000);
        ret = device_stop(camera_dev);
        if(ret != 0){
            LOG_SIMPLE("camera stop failed :%d\r\n",ret);
        }
        ret = device_stop(enc);
        if(ret != 0){
            LOG_SIMPLE("encoder stop failed :%d\r\n",ret);
        }
#if VIDEO_DRAW_TEST
        if(font_12.data){
            hal_mem_free(font_12.data);
            font_12.data = NULL;
        }

        if(font_16.data){
            hal_mem_free(font_16.data);
            font_16.data = NULL;
        }
#endif

    }else if(strcmp(argv[1], "aistart") == 0){
        if(video_flag > 0){
            LOG_SIMPLE("ai has started\r\n");
            return -1;
        }
        // uint32_t model_address = AI_1_BASE;

        // if(argc > 2){
        //     if(strcmp(argv[2], "1") == 0){
        //         model_address = AI_2_BASE;
        //     }
        // }
        // /*========= model load =========*/
        // ret = nn_load_model(model_address); //test model
        // if (ret != 0) {
        //     LOG_SIMPLE("nn load model failed :%d\r\n", ret);
        //     return -1;
        // }

        nn_get_model_info(&model_info);

        device_ioctl(camera_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
        LOG_SIMPLE("sensor name:%s, sensor width:%d, height:%d, fps:%d\r\n", sensor_param.name, sensor_param.width, sensor_param.height, sensor_param.fps);

        uint8_t camera_ctrl_pipe = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
        // uint8_t camera_ctrl_pipe = CAMERA_CTRL_PIPE2_BIT;
        ret = device_ioctl(camera_dev, CAM_CMD_SET_PIPE_CTRL, &camera_ctrl_pipe, 0);//set pipe ctrl
        if(ret != 0){
            LOG_SIMPLE("PIPE ctrl failed :%d\r\n",ret);
            return -1;
        }
        device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        /*--if need to change pipe param--*/

        pipe_param.width = 1280;
        pipe_param.height = 720;
        pipe_param.fps = 30;
        pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
        pipe_param.bpp = DCMIPP_BYTES_PER_PIXEL(pipe_param.format);
        ret = device_ioctl(camera_dev, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        if(ret != 0){
            LOG_SIMPLE("PIPE1 param failed :%d\r\n",ret);
            return -1;
        }
        device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));

        LOG_SIMPLE(" pipe1 width:%d, height:%d, fps:%d ,format:%d, bpp:%d\r\n", pipe_param.width, pipe_param.height, pipe_param.fps, pipe_param.format, pipe_param.bpp);
        /*========= dma2d init =========*/
        draw_param.in_colormode = fmt_dcmipp_to_dma2d(pipe_param.format);
        draw_param.out_colormode = DMA2D_OUTPUT_RGB565;
        ret = device_ioctl(draw, DRAW_CMD_SET_COLOR_MODE, (uint8_t *)&draw_param, sizeof(draw_colormode_param_t));
        if(ret != 0){
            LOG_SIMPLE("DMA2D set color mode failed :%d\r\n",ret);
            return -1;
        }
        LOG_SIMPLE(" draw in_colormode:%d, out_colormode:%d\r\n", draw_param.in_colormode, draw_param.out_colormode);

        mpe_draw_conf.image_width = pipe_param.width;
        mpe_draw_conf.image_height = pipe_param.height;
        ret = mpe_draw_init(&mpe_draw_conf);
        if(ret != 0){
            LOG_SIMPLE("mpe draw init failed :%d\r\n",ret);
            return -1;
        }

        od_draw_conf.image_width = pipe_param.width;
        od_draw_conf.image_height = pipe_param.height;
        ret = od_draw_init(&od_draw_conf);
        if(ret != 0){
            LOG_SIMPLE("od draw init failed :%d\r\n",ret);
            return -1;
        }
    
        /*========= venc init =========*/
        device_ioctl(enc, ENC_CMD_GET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));
        enc_param.width = pipe_param.width;
        enc_param.height = pipe_param.height;
        enc_param.fps = pipe_param.fps;
        enc_param.input_type = fmt_dma2d_to_enc(draw_param.out_colormode);
        enc_param.bpp = ENC_BYTES_PER_PIXEL(enc_param.input_type);
        ret = device_ioctl(enc, ENC_CMD_SET_PARAM, (uint8_t *)&enc_param, sizeof(enc_param_t));
        if(ret != 0){
            LOG_SIMPLE("venc set param failed :%d\r\n",ret);
            return -1;
        }
        LOG_SIMPLE(" enc width:%d, height:%d, fps:%d ,input_type:%d, bpp:%d\r\n", enc_param.width, enc_param.height, enc_param.fps, enc_param.input_type, enc_param.bpp);

        /*========= pipe2 init =========*/
        device_ioctl(camera_dev, CAM_CMD_GET_PIPE2_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        pipe_param.width = model_info.input_width;
        pipe_param.height = model_info.input_height;
        pipe_param.fps = 30;
        pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
        pipe_param.bpp = DCMIPP_BYTES_PER_PIXEL(pipe_param.format);
        ret = device_ioctl(camera_dev, CAM_CMD_SET_PIPE2_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        if(ret != 0){
            LOG_SIMPLE("PIPE2 param failed :%d\r\n",ret);
            return -1;
        }
        LOG_SIMPLE(" pipe2 width:%d, height:%d, fps:%d ,format:%d, bpp:%d\r\n", pipe_param.width, pipe_param.height, pipe_param.fps, pipe_param.format, pipe_param.bpp);

        ret = device_start(camera_dev);
        if(ret != 0){
            LOG_SIMPLE("camera start failed :%d\r\n",ret);
            return -1;
        }
        ret = device_start(enc);
        if(ret != 0){
            LOG_SIMPLE("encoder start failed :%d\r\n",ret);
            return -1;
        }
        memset(&video_time, 0, sizeof(video_time_t));
        ai_result_flag = 0;
        video_flag = 1;
        aipipe_flag = 1;
    }else if(strcmp(argv[1], "aistop") == 0){
        if(video_flag == 0){
            LOG_SIMPLE("ai not start\r\n");
            return -1;
        }
        video_flag = 0;
        aipipe_flag = 0;
        ai_result_flag = 0;
        osDelay(1000);
        ret = device_stop(camera_dev);
        if(ret != 0){
            LOG_SIMPLE("camera stop failed :%d\r\n",ret);
        }
        ret = device_stop(enc);
        if(ret != 0){
            LOG_SIMPLE("encoder stop failed :%d\r\n",ret);
        }
        // nn_unload_model();
        mpe_draw_deinit(&mpe_draw_conf);
        od_draw_deinit(&od_draw_conf);
        od.nb_detect = 0;
        mpe.nb_detect = 0;
        if(od.detects != NULL){
            hal_mem_free(od.detects);
            od.detects = NULL;
        }
        if(mpe.detects != NULL){
            hal_mem_free(mpe.detects);
            mpe.detects = NULL;
        }
    }else if(strcmp(argv[1], "time") == 0){
        LOG_SIMPLE("video time: total:%dms, avg:%dms, enc_time:%dms, enc_avg:%dms, last:%dms", 
            video_time.total_time, 
            (video_time.total_time > 0) ? (video_time.frame_count * 1000 / video_time.total_time) : 0,
            video_time.enc_time,
            (video_time.frame_count > 0) ? (video_time.enc_time / video_time.frame_count) : 0,
            video_time.last_time
        );
        LOG_SIMPLE("video time: draw_count:%d, draw_time:%dms, draw_avg:%dms\r\n", 
            video_time.draw_count,
            video_time.draw_time,
            (video_time.draw_count > 0) ? (video_time.draw_time / video_time.draw_count) : 0
        );
    }
    return 0;
}

debug_cmd_reg_t video_cmd_table[] = {
    {"capture",   "Captures and saves the image.",    capture_cmd},
    {"video",   "video contorl.",    video_cmd},
};

__attribute__((unused)) static void video_cmd_register(void)
{
    debug_cmdline_register(video_cmd_table, sizeof(video_cmd_table) / sizeof(video_cmd_table[0]));
}

// static int usb2_phy_reg_cmd(int argc, char* argv[]) 
// {
//     int ret = 0;
//     char *endptr = NULL;
//     uint32_t reg_value = 0;
    
//     if (argc < 2) {
//         LOG_SIMPLE("Usage: usb2phy <cr1/cr2> [value | \"reset\"]\r\n");
//         return -1;
//     }
    
//     if (strcmp(argv[1], "cr1") == 0) {
//         if (argc >= 3) {
//             if (strcmp(argv[2], "reset") == 0) {
//                 ret = storage_nvs_delete(NVS_FACTORY, "usb2phy_cr1");
//                 if (ret != 0) {
//                     LOG_SIMPLE("Failed to reset USB2 PHY TRIM1CR: %d\r\n", ret);
//                     return -1;
//                 }
//                 LOG_SIMPLE("USB2 PHY TRIM1CR reset.\r\n");
//                 return 0;
//             } else {
//                 reg_value = strtoul(argv[2], &endptr, 0);
//                 if (endptr == argv[2]) {
//                     LOG_SIMPLE("Invalid value: %s\r\n", argv[2]);
//                     return -1;
//                 }
//                 ret = storage_nvs_write(NVS_FACTORY, "usb2phy_cr1", &reg_value, sizeof(reg_value));
//                 if (ret != sizeof(reg_value)) {
//                     LOG_SIMPLE("Failed to set USB2 PHY TRIM1CR: %d\r\n", ret);
//                     return -1;
//                 }
//                 LOG_SIMPLE("USB2 PHY TRIM1CR set to 0x%08X\r\n", reg_value);
//                 return 0;
//             }
//         } else {
//             ret = storage_nvs_read(NVS_FACTORY, "usb2phy_cr1", &reg_value, sizeof(reg_value));
//             if (ret == sizeof(reg_value)) {
//                 LOG_SIMPLE("Stored USB2 PHY TRIM1CR: 0x%08X\r\n", reg_value);
//             } else {
//                 LOG_SIMPLE("No stored USB2 PHY TRIM1CR found.\r\n");
//             }
//             return 0;
//         }
//     } else if (strcmp(argv[1], "cr2") == 0) {
//         if (argc >= 3) {
//             if (strcmp(argv[2], "reset") == 0) {
//                 ret = storage_nvs_delete(NVS_FACTORY, "usb2phy_cr2");
//                 if (ret != 0) {
//                     LOG_SIMPLE("Failed to reset USB2 PHY TRIM2CR: %d\r\n", ret);
//                     return -1;
//                 }
//                 LOG_SIMPLE("USB2 PHY TRIM2CR reset.\r\n");
//                 return 0;
//             } else {
//                 reg_value = strtoul(argv[2], &endptr, 0);
//                 if (endptr == argv[2]) {
//                     LOG_SIMPLE("Invalid value: %s\r\n", argv[2]);
//                     return -1;
//                 }
//                 ret = storage_nvs_write(NVS_FACTORY, "usb2phy_cr2", &reg_value, sizeof(reg_value));
//                 if (ret != sizeof(reg_value)) {
//                     LOG_SIMPLE("Failed to set USB2 PHY TRIM2CR: %d\r\n", ret);
//                     return -1;
//                 }
//                 LOG_SIMPLE("USB2 PHY TRIM2CR set to 0x%08X\r\n", reg_value);
//                 return 0;
//             }
//         } else {
//             ret = storage_nvs_read(NVS_FACTORY, "usb2phy_cr2", &reg_value, sizeof(reg_value));
//             if (ret == sizeof(reg_value)) {
//                 LOG_SIMPLE("Stored USB2 PHY TRIM2CR: 0x%08X\r\n", reg_value);
//             } else {
//                 LOG_SIMPLE("No stored USB2 PHY TRIM2CR found.\r\n");
//             }
//             return 0;
//         }
//     } else {
//         LOG_SIMPLE("Usage: usb2phy <cr1/cr2> [value]\r\n");
//         return -1;
//     }

//     return 0;
// }

// int usb2_speed_cmd(int argc, char* argv[]) 
// {
//     int ret = 0;

//     if (argc < 2) {
//         LOG_SIMPLE("Usage: usb2speed <high|full|low>\r\n");
//         return -1;
//     }
    
//     if (strcmp(argv[1], "full") == 0) {
//         ret = storage_nvs_write(NVS_FACTORY, "usb2_speed", "full", sizeof("full"));
//         if (ret != sizeof("full")) {
//             LOG_SIMPLE("Failed to set USB2 speed to full: %d\r\n", ret);
//             return -1;
//         }
//         LOG_SIMPLE("USB2 speed set to full speed.\r\n");
//     } else if (strcmp(argv[1], "low") == 0) {
//         ret = storage_nvs_write(NVS_FACTORY, "usb2_speed", "low", sizeof("low"));
//         if (ret != sizeof("low")) {
//             LOG_SIMPLE("Failed to set USB2 speed to low: %d\r\n", ret);
//             return -1;
//         }
//         LOG_SIMPLE("USB2 speed set to low speed.\r\n");
//     } else if (strcmp(argv[1], "high") == 0) {
//         ret = storage_nvs_write(NVS_FACTORY, "usb2_speed", "high", sizeof("high"));
//         if (ret != sizeof("high")) {
//             LOG_SIMPLE("Failed to set USB2 speed to high: %d\r\n", ret);
//             return -1;
//         }
//         LOG_SIMPLE("USB2 speed set to high speed.\r\n");
//     } else {
//         LOG_SIMPLE("Usage: usb2speed <high|full|low>\r\n");
//         return -1;
//     }
//     return 0;
// }

// debug_cmd_reg_t usb_cmd_table[] = {
//     {"usb2phy",   "USB2 PHY register read/write.", usb2_phy_reg_cmd},
//     {"usb2speed",   "Set USB2 speed mode.", usb2_speed_cmd},
// };

// static void usb_cmd_register(void)
// {
//     debug_cmdline_register(usb_cmd_table, sizeof(usb_cmd_table) / sizeof(usb_cmd_table[0]));
// }

#if POWER_MODULE_TEST

extern void NPURam_enable();
extern void NPURam_disable();
extern void NPUCache_config();
extern void npu_cache_disable(void);
static int module_test_cmd(int argc, char* argv[]) 
{
    device_t *dev = NULL;
    char filename[MAX_FILENAME_LEN] = {0};
    uint8_t *fb = NULL;
    int fb_len = 0;
    if (argc < 3) {
        LOG_SIMPLE("Usage: module <camera/enc/ai> <on/off>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "camera") == 0) {
        dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (strcmp(argv[2], "on") == 0) {
            if (dev != NULL) {
                LOG_SIMPLE("Camera module is already powered on.\r\n");
                return -1;
            }
            camera_register();
            dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
            if (dev == NULL) {
                LOG_SIMPLE("Failed to register camera module.\r\n");
                return -1;
            }
            device_start(dev);
            LOG_SIMPLE("Camera module powered on.\r\n");
        } else if (strcmp(argv[2], "off") == 0) {
            if (dev == NULL) {
                LOG_SIMPLE("Camera module is already powered off.\r\n");
                return -1;
            }
            device_stop(dev);
            camera_unregister();
            LOG_SIMPLE("Camera module powered off.\r\n");
        } else if (strcmp(argv[2], "capture") == 0) {
            dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
            if (dev == NULL) {
                LOG_SIMPLE("Camera module is not powered on.\r\n");
                return -1;
            }
            fb_len = device_ioctl(dev, CAM_CMD_GET_PIPE1_BUFFER, (uint8_t *)&fb, 0);
            if (fb_len <= 0) {
                LOG_SIMPLE("Failed to get capture buffer, length: %d.\r\n", fb_len);
                return -1;
            }
            snprintf(filename, MAX_FILENAME_LEN, "capture%lu.rgb", osKernelGetTickCount());
            create_file(filename, fb, fb_len);
            device_ioctl(dev, CAM_CMD_RETURN_PIPE1_BUFFER, fb, 0);
            LOG_SIMPLE("Capture saved to %s.\r\n", filename);
        } else {
            LOG_SIMPLE("Usage: module <camera/enc/ai> <on/off>\r\n");
            return -1;
        }
    } else if (strcmp(argv[1], "enc") == 0) {
        dev = device_find_pattern(ENC_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (strcmp(argv[2], "on") == 0) {
            if (dev != NULL) {
                LOG_SIMPLE("Encoder module is already powered on.\r\n");
                return -1;
            }
            enc_register();
            dev = device_find_pattern(ENC_DEVICE_NAME, DEV_TYPE_VIDEO);
            if (dev == NULL) {
                LOG_SIMPLE("Failed to register encoder module.\r\n");
                return -1;
            }
            device_start(dev);
            LOG_SIMPLE("Encoder module powered on.\r\n");
        } else if (strcmp(argv[2], "off") == 0) {
            if (dev == NULL) {
                LOG_SIMPLE("Encoder module is already powered off.\r\n");
                return -1;
            }
            device_stop(dev);
            enc_unregister();
            LOG_SIMPLE("Encoder module powered off.\r\n");
        } else {
            LOG_SIMPLE("Usage: module <camera/enc/ai> <on/off>\r\n");
            return -1;
        }
    } else if (strcmp(argv[1], "ai") == 0) {
        dev = device_find_pattern("nn", DEV_TYPE_AI);
        if (strcmp(argv[2], "on") == 0) {
            if (dev != NULL) {
                LOG_SIMPLE("AI module is already powered on.\r\n");
                return -1;
            }
            NPURam_enable();
            NPUCache_config();
            nn_register();
            LOG_SIMPLE("AI module powered on.\r\n");
        } else if (strcmp(argv[2], "off") == 0) {
            if (dev == NULL) {
                LOG_SIMPLE("AI module is already powered off.\r\n");
                return -1;
            }
            nn_unregister();
            npu_cache_disable();
            NPURam_disable();
            LOG_SIMPLE("AI module powered off.\r\n");
        } else {
            LOG_SIMPLE("Usage: module <camera/enc/ai> <on/off>\r\n");
            return -1;
        }
    } else {
        LOG_SIMPLE("Usage: module <camera/enc/ai> <on/off>\r\n");
        return -1;
    }
    return 0;
}

debug_cmd_reg_t module_cmd_table[] = {
    {"module",   "Module power on/off control.", module_test_cmd},
};

static void module_cmd_register(void)
{
    debug_cmdline_register(module_cmd_table, sizeof(module_cmd_table) / sizeof(module_cmd_table[0]));
}

#endif

/* ==================== GPIO/IO TEST TOOL ==================== */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       is_output;
} io_test_pin_t;

#define IO_TEST_MAX_PINS   16
static io_test_pin_t g_io_test_pins[IO_TEST_MAX_PINS] = {0};

static GPIO_TypeDef *io_test_get_port(char port_char)
{
    switch (port_char) {
        case 'A': return GPIOA;
        case 'B': return GPIOB;
        case 'C': return GPIOC;
        case 'D': return GPIOD;
        case 'E': return GPIOE;
        case 'F': return GPIOF;
        case 'G': return GPIOG;
        case 'H': return GPIOH;
        default:  return NULL;
    }
}

static void io_test_enable_port_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
}

static int io_test_parse_pin(const char *str, GPIO_TypeDef **port, uint16_t *pin)
{
    if (!str || strlen(str) < 3 || (str[0] != 'P' && str[0] != 'p')) {
        return -1;
    }
    char port_char = (char)toupper((int)str[1]);
    int pin_num = atoi(&str[2]);
    if (pin_num < 0 || pin_num > 15) {
        return -1;
    }
    *port = io_test_get_port(port_char);
    if (*port == NULL) {
        return -1;
    }
    *pin = (uint16_t)(1U << pin_num);
    return 0;
}

static io_test_pin_t *io_test_get_pin(GPIO_TypeDef *port, uint16_t pin, int create_if_not_exist)
{
    int free_index = -1;
    for (int i = 0; i < IO_TEST_MAX_PINS; i++) {
        if (g_io_test_pins[i].port == port && g_io_test_pins[i].pin == pin) {
            return &g_io_test_pins[i];
        }
        if (!g_io_test_pins[i].port && free_index < 0) {
            free_index = i;
        }
    }
    if (!create_if_not_exist || free_index < 0) {
        return NULL;
    }
    g_io_test_pins[free_index].port = port;
    g_io_test_pins[free_index].pin = pin;
    g_io_test_pins[free_index].is_output = 0;
    return &g_io_test_pins[free_index];
}

static int io_test_cmd(int argc, char *argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage:\r\n");
        LOG_SIMPLE("  io config <PA1> <in|out>\r\n");
        LOG_SIMPLE("  io read   <PA1>\r\n");
        LOG_SIMPLE("  io write  <PA1> <0|1>\r\n");
        return -1;
    }

    const char *sub = argv[1];
    GPIO_TypeDef *port = NULL;
    uint16_t pin = 0;

    if (io_test_parse_pin(argv[2], &port, &pin) != 0) {
        LOG_SIMPLE("io: invalid pin '%s', use like PA1/PE12\r\n", argv[2]);
        return -1;
    }

    if (strcmp(sub, "config") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("io config: missing mode, use in/out\r\n");
            return -1;
        }
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        io_test_enable_port_clock(port);
        GPIO_InitStruct.Pin = pin;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

        io_test_pin_t *info = io_test_get_pin(port, pin, 1);
        if (!info) {
            LOG_SIMPLE("io config: no free slot\r\n");
            return -1;
        }

        if (strcmp(argv[3], "out") == 0) {
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            info->is_output = 1;
        } else if (strcmp(argv[3], "in") == 0) {
            GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
            info->is_output = 0;
        } else {
            LOG_SIMPLE("io config: invalid mode '%s', use in/out\r\n", argv[3]);
            return -1;
        }

        HAL_GPIO_Init(port, &GPIO_InitStruct);
        LOG_SIMPLE("io config: %s as %s\r\n", argv[2], info->is_output ? "output" : "input");
        return 0;
    } else if (strcmp(sub, "read") == 0) {
        GPIO_PinState state = HAL_GPIO_ReadPin(port, pin);
        LOG_SIMPLE("io read %s = %d\r\n", argv[2], (state == GPIO_PIN_SET) ? 1 : 0);
        return 0;
    } else if (strcmp(sub, "write") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("io write: missing value 0/1\r\n");
            return -1;
        }
        io_test_pin_t *info = io_test_get_pin(port, pin, 0);
        if (!info || !info->is_output) {
            LOG_SIMPLE("io write: %s is not configured as output\r\n", argv[2]);
            return -1;
        }
        int val = atoi(argv[3]);
        HAL_GPIO_WritePin(port, pin, (val ? GPIO_PIN_SET : GPIO_PIN_RESET));
        LOG_SIMPLE("io write %s = %d\r\n", argv[2], val ? 1 : 0);
        return 0;
    }

    LOG_SIMPLE("io: unknown subcmd '%s'\r\n", sub);
    return -1;
}

debug_cmd_reg_t io_cmd_table[] = {
    {"io", "GPIO test tool", io_test_cmd},
};

static void io_cmd_register(void)
{
    debug_cmdline_register(io_cmd_table, sizeof(io_cmd_table) / sizeof(io_cmd_table[0]));
}

/* ==================== RS485 TEST TOOL ==================== */

typedef enum {
    RS485_TEST_MODE_STRING = 0,
    RS485_TEST_MODE_HEX    = 1,
} rs485_test_mode_t;

static rs485_test_mode_t g_rs485_mode = RS485_TEST_MODE_STRING;
static int g_rs485_listen_enabled = 0;

static void rs485_test_rx_callback(uint8_t *data, uint16_t length)
{
    if (!g_rs485_listen_enabled || !data || length == 0) {
        return;
    }

    if (g_rs485_mode == RS485_TEST_MODE_STRING) {
        /* Ensure printable and zero-terminated copy */
        uint16_t len = (length < 256) ? length : 255;
        char buf[256] = {0};
        memcpy(buf, data, len);
        LOG_SIMPLE("[RS485][RX][STR] len=%d data=\"%s\"\r\n", length, buf);
    } else {
        LOG_SIMPLE("[RS485][RX][HEX] len=%d data=", length);
        for (uint16_t i = 0; i < length; i++) {
            LOG_SIMPLE("%02X ", data[i]);
        }
        LOG_SIMPLE("\r\n");
    }
}

static int rs485_test_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:\r\n");
        LOG_SIMPLE("  rs485 init <baud> [cfg] [str|hex]\r\n");
        LOG_SIMPLE("  rs485 write <data...>\r\n");
        LOG_SIMPLE("  rs485 read <len> [timeout_ms]\r\n");
        LOG_SIMPLE("  rs485 listen <on|off>\r\n");
        LOG_SIMPLE("  rs485 mode <str|hex>\r\n");
        LOG_SIMPLE("  rs485 destroy\r\n");
        return -1;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "init") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("rs485 init: missing baudrate\r\n");
            return -1;
        }
        uint32_t baud = (uint32_t)atoi(argv[2]);
        const char *cfg = (argc >= 4) ? argv[3] : "8N1";
        if (argc >= 5) {
            if (strcmp(argv[4], "hex") == 0) g_rs485_mode = RS485_TEST_MODE_HEX;
            else g_rs485_mode = RS485_TEST_MODE_STRING;
        }
        int ret = rs485_driver_init(baud, cfg);
        if (ret != 0) {
            LOG_SIMPLE("rs485 init failed: %d\r\n", ret);
            return ret;
        }
        /* default: not listening until user enables */
        g_rs485_listen_enabled = 0;
        rs485_driver_set_rx_callback(rs485_test_rx_callback);
        LOG_SIMPLE("rs485 init ok, baud=%lu cfg=%s mode=%s\r\n",
                   (unsigned long)baud, cfg,
                   (g_rs485_mode == RS485_TEST_MODE_HEX) ? "hex" : "str");
        return 0;
    } else if (strcmp(sub, "write") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("rs485 write: missing data\r\n");
            return -1;
        }
        uint8_t buf[RS485_TX_BUFFER_SIZE] = {0};
        int len = 0;

        if (g_rs485_mode == RS485_TEST_MODE_STRING) {
            /* concatenate arguments with space for readability */
            for (int i = 2; i < argc && len < (int)sizeof(buf) - 1; i++) {
                int l = (int)strlen(argv[i]);
                if (len + l + 1 >= (int)sizeof(buf)) break;
                memcpy(&buf[len], argv[i], l);
                len += l;
                if (i != argc - 1) buf[len++] = ' ';
            }
        } else {
            /* hex mode:
             *  - 支持按字节:  01 0x02 FF
             *  - 也支持连续串: 0102ff 或 0x0102ff（会按每两位拆成字节）
             */
            for (int i = 2; i < argc && len < (int)sizeof(buf); i++) {
                const char *p = argv[i];
                int plen = (int)strlen(p);

                /* 去掉可选 0x/0X 前缀 */
                if (plen >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                    p += 2;
                    plen -= 2;
                }

                if (plen == 0) {
                    LOG_SIMPLE("rs485 write: invalid hex '%s'\r\n", argv[i]);
                    return -1;
                }

                if (plen <= 2) {
                    /* 单字节写法，如 '0F' 或 'F' */
                    char *endp = NULL;
                    long v = strtol(p, &endp, 16);
                    if (endp == p || v < 0 || v > 0xFF) {
                        LOG_SIMPLE("rs485 write: invalid hex byte '%s'\r\n", argv[i]);
                        return -1;
                    }
                    buf[len++] = (uint8_t)v;
                } else {
                    /* 连续 hex 串，长度必须为偶数，每两位一个字节 */
                    if (plen % 2 != 0) {
                        LOG_SIMPLE("rs485 write: hex string length must be even '%s'\r\n", argv[i]);
                        return -1;
                    }
                    for (int pos = 0; pos < plen && len < (int)sizeof(buf); pos += 2) {
                        char tmp[3] = { p[pos], p[pos + 1], 0 };
                        char *endp = NULL;
                        long v = strtol(tmp, &endp, 16);
                        if (endp == tmp || v < 0 || v > 0xFF) {
                            LOG_SIMPLE("rs485 write: invalid hex byte in '%s'\r\n", argv[i]);
                            return -1;
                        }
                        buf[len++] = (uint8_t)v;
                    }
                }
            }
        }

        if (len <= 0) {
            LOG_SIMPLE("rs485 write: no data\r\n");
            return -1;
        }
        int ret = rs485_driver_send_data(buf, (uint16_t)len, 3000);
        if (ret != 0) {
            LOG_SIMPLE("rs485 write failed: %d\r\n", ret);
            return ret;
        }
        LOG_SIMPLE("rs485 write ok, len=%d\r\n", len);
        return 0;
    } else if (strcmp(sub, "read") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("rs485 read: missing len\r\n");
            return -1;
        }
        int want_len = atoi(argv[2]);
        if (want_len <= 0 || want_len > RS485_RX_BUFFER_SIZE) {
            LOG_SIMPLE("rs485 read: invalid len %d\r\n", want_len);
            return -1;
        }
        uint32_t timeout = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 3000;
        uint8_t buf[RS485_RX_BUFFER_SIZE] = {0};
        int ret = rs485_driver_receive_data(buf, (uint16_t)want_len, timeout);
        if (ret < 0) {
            LOG_SIMPLE("rs485 read failed: %d\r\n", ret);
            return ret;
        }
        int rlen = ret;
        if (g_rs485_mode == RS485_TEST_MODE_STRING) {
            int copy_len = (rlen < RS485_RX_BUFFER_SIZE - 1) ? rlen : (RS485_RX_BUFFER_SIZE - 1);
            buf[copy_len] = 0;
            LOG_SIMPLE("[RS485][READ][STR] len=%d data=\"%s\"\r\n", rlen, buf);
        } else {
            LOG_SIMPLE("[RS485][READ][HEX] len=%d data=", rlen);
            for (int i = 0; i < rlen; i++) {
                LOG_SIMPLE("%02X ", buf[i]);
            }
            LOG_SIMPLE("\r\n");
        }
        return 0;
    } else if (strcmp(sub, "listen") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("rs485 listen: use on/off\r\n");
            return -1;
        }
        if (strcmp(argv[2], "on") == 0) {
            g_rs485_listen_enabled = 1;
        } else if (strcmp(argv[2], "off") == 0) {
            g_rs485_listen_enabled = 0;
        } else {
            LOG_SIMPLE("rs485 listen: invalid arg '%s', use on/off\r\n", argv[2]);
            return -1;
        }
        LOG_SIMPLE("rs485 listen %s\r\n", g_rs485_listen_enabled ? "on" : "off");
        return 0;
    } else if (strcmp(sub, "mode") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("rs485 mode: use str/hex\r\n");
            return -1;
        }
        if (strcmp(argv[2], "hex") == 0) g_rs485_mode = RS485_TEST_MODE_HEX;
        else g_rs485_mode = RS485_TEST_MODE_STRING;
        LOG_SIMPLE("rs485 mode set to %s\r\n",
                   (g_rs485_mode == RS485_TEST_MODE_HEX) ? "hex" : "str");
        return 0;
    } else if (strcmp(sub, "destroy") == 0 || strcmp(sub, "deinit") == 0) {
        int ret = rs485_driver_deinit();
        if (ret != 0) {
            LOG_SIMPLE("rs485 destroy failed: %d\r\n", ret);
            return ret;
        }
        g_rs485_listen_enabled = 0;
        LOG_SIMPLE("rs485 destroy ok\r\n");
        return 0;
    }

    LOG_SIMPLE("rs485: unknown subcmd '%s'\r\n", sub);
    return -1;
}

debug_cmd_reg_t rs485_cmd_table[] = {
    {"rs485", "RS485 test tool", rs485_test_cmd},
};

static void rs485_cmd_register(void)
{
    debug_cmdline_register(rs485_cmd_table, sizeof(rs485_cmd_table) / sizeof(rs485_cmd_table[0]));
}

void driver_test_main(void)
{
#if POWER_MODULE_TEST
    driver_cmd_register_callback("module", module_cmd_register);
#endif
    driver_cmd_register_callback("io", io_cmd_register);
    driver_cmd_register_callback("rs485", rs485_cmd_register);
    driver_cmd_register_callback("sysclk", sysclk_cmd_register);

    // driver_cmd_register_callback("usb", usb_cmd_register);
    // driver_cmd_register_callback("driver_core", video_cmd_register);
    // rtc_test();
    // VideoTest_processId = osThreadNew(VideoTestProcess, NULL, &VideoTestTask_attributes);
    // CaptureTest_processId = osThreadNew(CaptureTestProcess, NULL, &CaptureTestTask_attributes);
    // AITest_processId = osThreadNew(AiTestProcess, NULL, &AiTestTask_attributes);
    // mtx_ai = osMutexNew(NULL);
}
