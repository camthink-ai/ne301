#include "cli_cmd.h"
#include "debug.h"
#include "generic_file.h"
#include "lfs.h"
#include "storage.h"
#include "sd_file.h"
#include "json_config_mgr.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "drtc.h"
#include "misc.h"
#include "ai_service.h"
#include "communication_service.h"
#include "ai_draw_service.h"
#include "usb_cherry.h"
#include "usbh_uvc_test.h"
#include "usbh_uvc_service.h"
#include "usbh_uvc_jpeg.h"
#include "nn.h"
#include "image_utils.h"
#include "buffer_mgr.h"
#include "dev_manager.h"
#include "jpegc.h"
#include "gpu_resize.h"
extern void jpegc_encode_set_raw_yuv(int on);
#include "upgrade_manager.h"
#include "mqtt_service.h"
#include "service_init.h"
#include "ota_header.h"
#include "u0_module.h"
#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"
#include "storage.h"
#include "video_pipeline.h"
#include "websocket_stream_server.h"
#include "mongoose.h"
#include "version.h"
#include "factory_test.h"
#include "rtmp_service.h"
#include "system_service.h"


static int cat_cmd(int argc, char* argv[]) 
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: cat <filename>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "r");
    if (!fd) {
        LOG_SIMPLE("cat: cannot open %s\r\n", local_filename);
        return -1;
    }
    char buf[1024];
    int n;
    while ((n = file_fread(fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    LOG_SIMPLE("\r\n");
    file_fclose(fd);
    return 0;
}

static int ls_cmd(int argc, char* argv[]) 
{
    char local_path[MAX_FILENAME_LEN];
    if (argc > 1) {
        strncpy(local_path, argv[1], MAX_FILENAME_LEN - 1);
        local_path[MAX_FILENAME_LEN - 1] = '\0';
    } else {
        strncpy(local_path, "/", MAX_FILENAME_LEN - 1);
        local_path[MAX_FILENAME_LEN - 1] = '\0';
    }
    void *dd = file_opendir(local_path);
    if (!dd) {
        LOG_SIMPLE("ls: cannot open directory %s\r\n", local_path);
        return -1;
    }
    // Union large enough for both lfs_info (264B) and sd_info (~282B)
    union { struct lfs_info lfs; char _pad[300]; } entry;
    int ret;
    LOG_SIMPLE("\r\n");
    while ((ret = file_readdir(dd, (char*)&entry)) == 1) {
        if (entry.lfs.type == LFS_TYPE_DIR) {
            LOG_SIMPLE("%-20s <DIR>\r\n", entry.lfs.name);
        } else {
            LOG_SIMPLE("%-20s %10lu bytes\r\n", entry.lfs.name, (unsigned long)entry.lfs.size);
        }
    }
    if (ret < 0) {
        LOG_SIMPLE("ls: readdir error\r\n");
    }
    file_closedir(dd);
    return 0;
}

static int cp_cmd(int argc, char* argv[]) {
    if (argc < 3) {
        LOG_SIMPLE("Usage: cp <src> <dst>\r\n");
        return -1;
    }
    char local_src[MAX_FILENAME_LEN];
    char local_dst[MAX_FILENAME_LEN];
    strncpy(local_src, argv[1], MAX_FILENAME_LEN - 1);
    local_src[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(local_dst, argv[2], MAX_FILENAME_LEN - 1);
    local_dst[MAX_FILENAME_LEN - 1] = '\0';

    void *fd_src = file_fopen(local_src, "r");
    if (!fd_src) {
        LOG_SIMPLE("cp: cannot open %s\r\n", local_src);
        return -1;
    }
    void *fd_dst = file_fopen(local_dst, "w");
    if (!fd_dst) {
        LOG_SIMPLE("cp: cannot create %s\r\n", local_dst);
        file_fclose(fd_src);
        return -1;
    }
    char buf[1024];
    int n;
    while ((n = file_fread(fd_src, buf, sizeof(buf))) > 0) {
        if (file_fwrite(fd_dst, buf, n) != n) {
            LOG_SIMPLE("cp: write error\r\n");
            file_fclose(fd_src);
            file_fclose(fd_dst);
            return -1;
        }
    }
    file_fclose(fd_src);
    file_fclose(fd_dst);
    return 0;
}

static int mv_cmd(int argc, char* argv[]) {
    if (argc < 3) {
        LOG_SIMPLE("Usage: mv <src> <dst>\r\n");
        return -1;
    }
    char local_src[MAX_FILENAME_LEN];
    char local_dst[MAX_FILENAME_LEN];
    strncpy(local_src, argv[1], MAX_FILENAME_LEN - 1);
    local_src[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(local_dst, argv[2], MAX_FILENAME_LEN - 1);
    local_dst[MAX_FILENAME_LEN - 1] = '\0';

    if (file_rename(local_src, local_dst) != 0) {
        LOG_SIMPLE("mv: cannot move %s to %s\r\n", local_src, local_dst);
        return -1;
    }
    return 0;
}

static int rm_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        LOG_SIMPLE("Usage: rm <file>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    if (file_remove(local_filename) != 0) {
        LOG_SIMPLE("rm: cannot remove %s\r\n", local_filename);
        return -1;
    }
    return 0;
}

static int touch_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        LOG_SIMPLE("Usage: touch <file>\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "a");
    if (!fd) {
        LOG_SIMPLE("touch: cannot touch %s\r\n", local_filename);
        return -1;
    }
    file_fclose(fd);
    return 0;
}

static int write_cmd(int argc, char* argv[]) 
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: write <filename> <content>\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    // Concatenate all subsequent parameters as content (supports multiple spaces)
    char content[1024] = {0};
    int offset = 0;
    for (int i = 2; i < argc; i++) {
        int n = snprintf(content + offset, sizeof(content) - offset, "%s%s", (i == 2) ? "" : " ", argv[i]);
        if (n < 0 || n >= (int)(sizeof(content) - offset)) {
            LOG_SIMPLE("write: content too long\r\n");
            return -1;
        }
        offset += n;
    }

    void *fd = file_fopen(local_filename, "w");
    if (!fd) {
        LOG_SIMPLE("write: cannot open %s\r\n", local_filename);
        return -1;
    }
    if (file_fwrite(fd, content, strlen(content)) != (int)strlen(content)) {
        LOG_SIMPLE("write: write error\r\n");
        file_fclose(fd);
        return -1;
    }
    file_fclose(fd);
    return 0;
}

static int seektest_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: seektest <filename> <offset> [write_str]\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[1], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    int offset = atoi(argv[2]);
    if (offset < 0) {
        LOG_SIMPLE("seektest: offset must be >= 0\r\n");
        return -1;
    }

    // If write parameter exists, open mode should allow writing
    void *fd = file_fopen(local_filename, (argc > 3) ? "r+" : "r");
    if (!fd) {
        LOG_SIMPLE("seektest: cannot open %s\r\n", local_filename);
        return -1;
    }

    if (file_fseek(fd, offset, SEEK_SET) < 0) {
        LOG_SIMPLE("seektest: seek failed\r\n");
        file_fclose(fd);
        return -1;
    }

    if (argc > 3) {
        // Write operation
        const char *str = argv[3];
        int wn = file_fwrite(fd, str, strlen(str));
        if (wn > 0) {
            LOG_SIMPLE("seektest: wrote '%s' at offset %d, bytes=%d\r\n", str, offset, wn);
        } else {
            LOG_SIMPLE("seektest: write failed at offset %d\r\n", offset);
        }
        // Reposition to offset to read content after write
        file_fseek(fd, offset, SEEK_SET);
    }

    // Read operation
    char buf[128] = {0};
    int n = file_fread(fd, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0'; // Ensure string termination
        LOG_SIMPLE("seektest: content from offset %d:\r\n%s\r\n", offset, buf);
    } else {
        LOG_SIMPLE("seektest: nothing read from offset %d\r\n", offset);
    }
    file_fclose(fd);
    return 0;
}

static int format_cmd(int argc, char* argv[]) 
{
    LOG_SIMPLE("The file system is being formatted...\r\n");
    storage_format();
    LOG_SIMPLE("The file system formatting is complete.\r\n");
    return 0;
}

static int sdfile_cmd(int argc, char* argv[]) 
{
    sd_file_ops_switch();
    return 0;
}

static int flashfile_cmd(int argc, char* argv[]) 
{
    storage_file_ops_switch();
    return 0;
}


static int captest_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    LOG_SIMPLE("capture test: triggering capture+upload (AI on)...\r\n");
    aicam_result_t r = system_service_capture_and_upload_mqtt(AICAM_TRUE, 0, AICAM_TRUE,
                                                              AICAM_CAPTURE_TRIGGER_BUTTON);
    LOG_SIMPLE("capture test result: %d\r\n", (int)r);
    return 0;
}

/* Sleep-wake test support: during a wake event (PIR/RTC interval capture)
 * the device returns to standby right after the capture. Sending this
 * command inside that window clears the u0 wake events and reboots into
 * the FULL boot (network up, OTA possible) - the remote-test substitute
 * for the physical long-press config key. */
static int configmode_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    LOG_SIMPLE("entering config mode: clearing u0 wake events and rebooting...\r\n");
    debug_flush_logs();
    osDelay(100);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    return 0;
}

/* AI input-pipeline debug: render the current UVC frame into the NPU model
 * input two ways and print 8x8 grids of per-tile mean RGB (hex RRGGBB per
 * tile) for PC-side comparison against a reference squash of the same frame.
 *   A: live AI path (uvc jpegc decode -> convert -> resize)
 *   B: proven decode path (ai_jpeg_decode -> convert -> same resize)
 * A==B!=PC pins image_resize; A!=B pins the uvc decode/convert stage. */
static void aidump_grid(const uint8_t *rgb, uint32_t w, uint32_t h, const char *tag)
{
    LOG_SIMPLE("%s ", tag);
    for (uint32_t ty = 0; ty < 8; ty++) {
        for (uint32_t tx = 0; tx < 8; tx++) {
            uint32_t x0 = tx * w / 8, x1 = (tx + 1) * w / 8;
            uint32_t y0 = ty * h / 8, y1 = (ty + 1) * h / 8;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            for (uint32_t y = y0; y < y1; y += 3) {
                const uint8_t *p = rgb + ((size_t)y * w + x0) * 3u;
                for (uint32_t x = x0; x < x1; x += 3, p += 9) {
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            }
            if (n == 0) n = 1;
            LOG_SIMPLE("%02X%02X%02X ", (unsigned)(r / n), (unsigned)(g / n), (unsigned)(b / n));
        }
    }
    LOG_SIMPLE("\r\n");
}

static int aidump_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    const uint8_t *p = NULL;
    uint32_t len = 0;
    uint16_t w = 0, h = 0;
    uint32_t seq = usbh_uvc_service_latest_frame(&p, &len, &w, &h);
    if (seq == 0 || len == 0 || !p) {
        LOG_SIMPLE("aidump: no uvc frame (streaming?)\r\n");
        return -1;
    }
    if (len > 512u * 1024u) {
        len = 512u * 1024u;
    }
    uint8_t *frame = buffer_malloc_aligned(len, 32);
    nn_model_info_t mi = {0};
    uint8_t *model_buf = NULL;
    if (!frame) {
        LOG_SIMPLE("aidump: frame alloc failed\r\n");
        return -1;
    }
    memcpy(frame, p, len);

    if (nn_get_model_info(&mi) != AICAM_OK || mi.input_width == 0 || mi.input_height == 0) {
        LOG_SIMPLE("aidump: model info invalid\r\n");
        buffer_free(frame);
        return -1;
    }
    model_buf = buffer_malloc_aligned((size_t)mi.input_width * mi.input_height * 3u, 32);
    if (!model_buf) {
        LOG_SIMPLE("aidump: model buf alloc failed\r\n");
        buffer_free(frame);
        return -1;
    }

    uvc_jpeg_info_t info = {0};
    uvc_jpeg_parse_header(frame, len, &info);
    LOG_SIMPLE("aidump: frame %luB hdr %ux%u css=%d | model %ux%u\r\n",
               (unsigned long)len, (unsigned)info.width, (unsigned)info.height,
               (int)info.chroma_subsampling,
               (unsigned)mi.input_width, (unsigned)mi.input_height);

    aicam_result_t r = ai_uvc_frame_to_model_input(frame, len, model_buf,
                                                   mi.input_width, mi.input_height);
    if (r == AICAM_OK) {
        aidump_grid(model_buf, mi.input_width, mi.input_height, "A:");
    } else {
        LOG_SIMPLE("aidump: A (live path) failed: %d\r\n", (int)r);
    }

    /* NOTE: the former "B" cross-check (ai_jpeg_decode on the same frame)
     * was removed: its SET_DEC_PARAM/RETURN_DEC_BUFFER cycle tears down the
     * raster the live path shares with jpegc and left the decoder in a
     * sticky INVALID_DATA (-3) loop. Use gputest for GPU/software A/B. */

    buffer_free(model_buf);
    buffer_free(frame);
    return 0;
}

/* GPU2D resize A/B: decode+convert one live UVC frame once, then time the
 * NemaGFX bilinear blit against the software bilinear over N iterations.
 * A synthetic COLORED identity blit runs first: packing/format/channel
 * corruption hides on grayscale camera scenes (tile means barely move),
 * so correctness needs content with independent R/G/B structure. */
static int gputest_synth_identity(void)
{
    const uint32_t S = 256;
    uint8_t *src = buffer_malloc_aligned(S * S * 3u, 32);
    uint8_t *out = buffer_malloc_aligned(S * S * 3u, 32);
    if (!src || !out) {
        if (src) buffer_free(src);
        if (out) buffer_free(out);
        LOG_SIMPLE("gputest synth: alloc failed\r\n");
        return -1;
    }
    for (uint32_t y = 0; y < S; y++) {
        for (uint32_t x = 0; x < S; x++) {
            uint8_t *px = src + (y * S + x) * 3u;
            px[0] = (uint8_t)(x * 255u / (S - 1u));        /* R: horizontal */
            px[1] = (uint8_t)(y * 255u / (S - 1u));        /* G: vertical */
            px[2] = (uint8_t)((x + y) * 255u / (2u * S - 2u)); /* B: diag */
        }
    }

    static const struct { const char *name; int src_rgba; int (*fn)(const uint8_t *, uint32_t, uint32_t,
                                                      uint8_t *, uint32_t, uint32_t); } modes[] = {
        { "rgba8888+packing", 0, gpu_resize_rgb888 },
        { "rgb24-direct",     0, gpu_resize_rgb888_direct },
        { "rgba-src-direct",  1, gpu_resize_rgba8888_to_rgb888 },
        { "ref-combo-packed", 1, gpu_resize_rgba8888_packed },
    };
    uint8_t *src4 = buffer_malloc_aligned(S * S * 4u, 32);
    if (!src4) {
        buffer_free(src); buffer_free(out);
        LOG_SIMPLE("gputest synth: alloc4 failed\r\n");
        return -1;
    }
    for (uint32_t i = 0; i < S * S; i++) {
        src4[i * 4u + 0u] = src[i * 3u + 0u];
        src4[i * 4u + 1u] = src[i * 3u + 1u];
        src4[i * 4u + 2u] = src[i * 3u + 2u];
        src4[i * 4u + 3u] = 0xFFu;
    }
    int good = -1;
    for (int m = 0; m < 4; m++) {
        memset(out, 0xA5, S * S * 3u);
        /* push the sentinel pattern to memory: after the blit, remaining
         * 0xA5 bytes mean the GPU never wrote that region */
        SCB_CleanDCache_by_Addr((void *)out, (int32_t)(S * S * 3u));
        const uint8_t *s = modes[m].src_rgba ? src4 : src;
        int ret = modes[m].fn(s, S, S, out, S, S);
        if (ret != 0) {
            LOG_SIMPLE("gputest synth[%s]: blit failed (%d)\r\n", modes[m].name, ret);
            continue;
        }
        uint32_t unwritten = 0;
        for (uint32_t i = 0; i < S * S * 3u; i++) {
            if (out[i] == 0xA5u) unwritten++;
        }
        LOG_SIMPLE("gputest synth[%s]: sentinel-left=%lu/%lu\r\n", modes[m].name,
                   (unsigned long)unwritten, (unsigned long)(S * S * 3u));
        uint32_t acc = 0, mx = 0, bad = 0;
        for (uint32_t i = 0; i < S * S * 3u; i++) {
            uint32_t d = out[i] > src[i] ? out[i] - src[i] : src[i] - out[i];
            acc += d;
            if (d > mx) mx = d;
            if (d > 2) bad++;
        }
        LOG_SIMPLE("gputest synth[%s]: diff mean=%lu.%02lu max=%lu bytes>2=%lu/%lu\r\n",
                   modes[m].name,
                   (unsigned long)(acc / (S * S * 3u)),
                   (unsigned long)((acc % (S * S * 3u)) * 100u / (S * S * 3u)),
                   (unsigned long)mx, (unsigned long)bad,
                   (unsigned long)(S * S * 3u));
        /* first pixels: expected R,G,B = (0,y,y/2), got bytes expose any
         * channel permutation / byte shift directly */
        LOG_SIMPLE("  src[0..11]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n"
                   "  out[0..11]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7],src[8],src[9],src[10],src[11],
                   out[0],out[1],out[2],out[3],out[4],out[5],out[6],out[7],out[8],out[9],out[10],out[11]);
        if (mx <= 2u && good < 0) {
            good = m;
        }
    }
    buffer_free(src);
    buffer_free(src4);
    buffer_free(out);
    return good;
}

/* Deliberately decode a TRUNCATED MJPEG frame (no EOI) - suspected root
 * cause of the silent jpegc stall (core consumes all input, never
 * completes, no error callback). Then decode the full frame to prove the
 * watchdog recovery path restored the codec. */
static int jpegfuzz_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    const uint8_t *p = NULL;
    uint32_t len = 0;
    uint16_t w = 0, h = 0;

    if (usbh_uvc_service_latest_frame(&p, &len, &w, &h) == 0 || len < 2048) {
        LOG_SIMPLE("jpegfuzz: no uvc frame\r\n");
        return -1;
    }
    uint8_t *frame = buffer_malloc_aligned(len, 32);
    if (!frame) {
        return -1;
    }
    memcpy(frame, p, len);

    uint8_t *ycbcr = NULL;
    uint32_t ylen = 0;
    uvc_jpeg_info_t info = {0};

    if (uvc_jpeg_raster_trylock(5000) != 0) {
        LOG_SIMPLE("jpegfuzz: raster lock timeout\r\n");
        buffer_free(frame);
        return -1;
    }

    uint32_t t0 = osKernelGetTickCount();
    int r1 = uvc_jpeg_decode_ycbcr(frame, len / 2u, &ycbcr, &ylen, &info);
    uint32_t t1 = osKernelGetTickCount();
    LOG_SIMPLE("jpegfuzz: truncated %lu/%luB -> %d (%lu ticks)\r\n",
               (unsigned long)(len / 2u), (unsigned long)len, r1,
               (unsigned long)(t1 - t0));

    int r2 = uvc_jpeg_decode_ycbcr(frame, len, &ycbcr, &ylen, &info);
    uint32_t t2 = osKernelGetTickCount();
    LOG_SIMPLE("jpegfuzz: full -> %d (%lu ticks) %s\r\n",
               r2, (unsigned long)(t2 - t1),
               (r2 == 0) ? "RECOVERED" : "STILL BROKEN");

    uvc_jpeg_raster_unlock();
    buffer_free(frame);
    return (r2 == 0) ? 0 : -1;
}

/* Isolate the capture-encode stall: encode a SYNTHETIC RGB565 1920x1080
 * gradient with each chroma subsampling, no camera/capture involved.
 * usage: enctest [css]   (css: 0=444 1=420 2=422; default tests 422+444) */
static int enctest_cmd(int argc, char* argv[])
{
    const uint32_t W = 1920, H = 1080;
    uint32_t css = (argc > 1) ? (uint32_t)atoi(argv[1]) : 0xFF;

    uint16_t *rgb565 = buffer_malloc_aligned(W * H * 2u, 32);
    uint8_t *jpg = NULL;
    uint32_t jpg_len = 0;
    if (!rgb565) {
        LOG_SIMPLE("enctest: alloc failed\r\n");
        return -1;
    }
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            rgb565[y * W + x] = (uint16_t)(((x * 31u) / W) | (((y * 63u) / H) << 5) |
                                           ((((x + y) * 31u) / (W + H)) << 11));
        }
    }

    static const uint32_t try_css[] = { JPEG_422_SUBSAMPLING, JPEG_444_SUBSAMPLING };
    for (uint32_t t = 0; t < 2u; t++) {
        uint32_t use = try_css[t];
        if (css != 0xFF && css != use) {
            continue;
        }

        /* capture-flow sequence: decode one live UVC frame FIRST (the
         * overlay step always runs before the re-encode), convert to
         * RGB565, DRAW a fake detection (the stall only happens when AI
         * results are drawn), then encode. The plain cold encode passes;
         * this mirrors the real generate-inference-image path. */
        uint16_t *draw_fb = NULL;
        {
            const uint8_t *fp = NULL;
            uint32_t flen = 0;
            uint16_t fw = 0, fh = 0;
            (void)usbh_uvc_service_latest_frame(&fp, &flen, &fw, &fh);
            if (fp != NULL && flen > 0 && flen <= 512u * 1024u) {
                uint8_t *fbuf = buffer_malloc_aligned(flen, 32);
                if (fbuf != NULL) {
                    uint8_t *yc = NULL;
                    uint32_t yclen = 0;
                    uvc_jpeg_info_t fi = {0};
                    memcpy(fbuf, fp, flen);
                    if (uvc_jpeg_raster_trylock(5000) == 0) {
                        int dr = uvc_jpeg_decode_ycbcr(fbuf, flen, &yc, &yclen, &fi);
                        uvc_jpeg_raster_unlock();
                        if (dr == 0) {
                            uint8_t *rgb = NULL;
                            uint32_t rlen = 0;
                            if (ai_color_convert(yc, fi.width, fi.height,
                                                 DMA2D_INPUT_YCBCR, 0,
                                                 fi.chroma_subsampling, &rgb, &rlen,
                                                 DMA2D_OUTPUT_RGB565) == AICAM_OK && rgb) {
                                draw_fb = (uint16_t *)rgb;
                                /* fake person box, normalized top-left */
                                static od_detect_t det = {
                                    .x = 0.30f, .y = 0.20f, .width = 0.25f,
                                    .height = 0.55f, .conf = 0.9f,
                                    .class_name = "person",
                                };
                                static nn_result_t res;
                                res.type = PP_TYPE_OD;
                                res.is_valid = 1;
                                res.od.nb_detect = 1;
                                res.od.detects = &det;
                                if (!ai_draw_is_initialized()) {
                                    ai_draw_config_t dc2;
                                    ai_draw_get_default_config(&dc2);
                                    dc2.image_width = fi.width;
                                    dc2.image_height = fi.height;
                                    (void)ai_draw_service_init(&dc2);
                                }
                                if (ai_draw_is_initialized()) {
                                    aicam_result_t drw = ai_draw_results(
                                        (uint8_t *)draw_fb, fi.width, fi.height, &res);
                                    LOG_SIMPLE("enctest: pre-decode+draw -> %d\r\n", drw);
                                } else {
                                    LOG_SIMPLE("enctest: draw init failed\r\n");
                                }
                            }
                        } else {
                            LOG_SIMPLE("enctest: pre-decode -> %d\r\n", dr);
                        }
                    }
                    buffer_free(fbuf);
                }
            }
        }

        jpg = NULL;
        const uint8_t *enc_src = (const uint8_t *)(draw_fb ? draw_fb : rgb565);
        ai_jpeg_encode_config_t ec = {
            .width = W, .height = H, .chroma_subsampling = use, .quality = 80,
        };
        uint32_t t0 = osKernelGetTickCount();
        aicam_result_t r = ai_jpeg_encode(enc_src, W * H * 2u, &ec, &jpg, &jpg_len);
        uint32_t dt = osKernelGetTickCount() - t0;
        LOG_SIMPLE("enctest: css=%u src=%s -> %d (%luB, %lu ticks)%s\r\n",
                   (unsigned)use, draw_fb ? "drawn" : "synth",
                   (int)r, (unsigned long)jpg_len, (unsigned long)dt,
                   (r == AICAM_OK) ? "" : "  <<<< STALL/FAIL");
        if (draw_fb) {
            buffer_free(draw_fb);
            draw_fb = NULL;
        }
        if (jpg) {
            ai_jpeg_free_buffer(jpg);
            jpg = NULL;
        }
    }
    buffer_free(rgb565);
    return 0;
}


/* EXPERIMENT: verify the JPEG encoder accepts the decode raster (planar
 * YCbCr) directly, skipping the RGB->YCbCr software conversion.
 * decode -> RAW encode -> decode back -> compare Y-plane 8x8 tile means. */
static int yuvtest_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    const uint8_t *p = NULL;
    uint32_t len = 0;
    uint16_t w = 0, h = 0;
    if (usbh_uvc_service_latest_frame(&p, &len, &w, &h) == 0 || len == 0) {
        LOG_SIMPLE("yuvtest: no uvc frame\r\n");
        return -1;
    }
    if (len > 512u * 1024u) len = 512u * 1024u;
    uint8_t *frame = buffer_malloc_aligned(len, 32);
    if (!frame) return -1;
    memcpy(frame, p, len);

    if (uvc_jpeg_raster_trylock(5000) != 0) {
        LOG_SIMPLE("yuvtest: raster lock timeout\r\n");
        buffer_free(frame);
        return -1;
    }

    uint8_t *yc = NULL;
    uint32_t ylen = 0;
    uvc_jpeg_info_t info = {0};
    if (uvc_jpeg_decode_ycbcr(frame, len, &yc, &ylen, &info) != 0 || !yc) {
        LOG_SIMPLE("yuvtest: decode failed\r\n");
        uvc_jpeg_raster_unlock();
        buffer_free(frame);
        return -1;
    }
    LOG_SIMPLE("yuvtest: decoded %ux%u css=%u raster=%luB\r\n",
               (unsigned)info.width, (unsigned)info.height,
               (unsigned)info.chroma_subsampling, (unsigned long)ylen);

    /* snapshot Y-plane tile means of the source raster */
    uint32_t W = info.width, H = info.height;
    uint32_t src_grid[64];
    for (uint32_t ty = 0; ty < 8; ty++)
        for (uint32_t tx = 0; tx < 8; tx++) {
            uint32_t acc = 0, n = 0;
            for (uint32_t y = ty * H / 8; y < (ty + 1) * H / 8; y += 16)
                for (uint32_t x = tx * W / 8; x < (tx + 1) * W / 8; x += 16) {
                    acc += yc[y * W + x]; n++;
                }
            src_grid[ty * 8 + tx] = n ? acc / n : 0;
        }

    /* RAW encode from the planar raster */
    jpegc_encode_set_raw_yuv(1);
    uint8_t *jpg = NULL;
    uint32_t jpg_len = 0;
    ai_jpeg_encode_config_t ec = {
        .width = W, .height = H,
        .chroma_subsampling = info.chroma_subsampling, .quality = 80,
    };
    uint32_t t0 = osKernelGetTickCount();
    aicam_result_t r = ai_jpeg_encode(yc, ylen, &ec, &jpg, &jpg_len);
    uint32_t dt = osKernelGetTickCount() - t0;
    jpegc_encode_set_raw_yuv(0);
    if (r != AICAM_OK || !jpg) {
        LOG_SIMPLE("yuvtest: RAW encode FAILED (%d) after %lu ticks\r\n", (int)r, (unsigned long)dt);
        uvc_jpeg_raster_unlock();
        buffer_free(frame);
        if (jpg) ai_jpeg_free_buffer(jpg);
        return -1;
    }
    LOG_SIMPLE("yuvtest: RAW encode OK %luB in %lu ticks\r\n",
               (unsigned long)jpg_len, (unsigned long)dt);

    /* decode the produced JPEG back and compare Y-plane tiles */
    uint8_t *yc2 = NULL;
    uint32_t ylen2 = 0;
    uvc_jpeg_info_t info2 = {0};
    int dr = uvc_jpeg_decode_ycbcr(jpg, jpg_len, &yc2, &ylen2, &info2);
    uvc_jpeg_raster_unlock();
    if (dr != 0 || !yc2) {
        LOG_SIMPLE("yuvtest: roundtrip decode FAILED - layout likely wrong\r\n");
        ai_jpeg_free_buffer(jpg);
        buffer_free(frame);
        return -1;
    }
    uint32_t acc = 0, mx = 0;
    for (uint32_t ty = 0; ty < 8; ty++)
        for (uint32_t tx = 0; tx < 8; tx++) {
            uint32_t a = 0, n = 0;
            for (uint32_t y = ty * H / 8; y < (ty + 1) * H / 8; y += 16)
                for (uint32_t x = tx * W / 8; x < (tx + 1) * W / 8; x += 16) {
                    a += yc2[y * W + x]; n++;
                }
            uint32_t m = n ? a / n : 0;
            uint32_t d = m > src_grid[ty*8+tx] ? m - src_grid[ty*8+tx] : src_grid[ty*8+tx] - m;
            acc += d;
            if (d > mx) mx = d;
        }
    LOG_SIMPLE("yuvtest: roundtrip Y-tile diff mean=%lu max=%lu -> %s\r\n",
               (unsigned long)(acc / 64), (unsigned long)mx,
               (mx <= 12u) ? "PLANAR YCbCb ACCEPTED (pass-through viable)"
                           : "LAYOUT MISMATCH (garbage) - needs MCU reorder");
    /* chroma plane order check: compare roundtrip Cb against BOTH source
     * planes - a match against Cr means the encoder swaps Cb/Cr (the AI
     * image would render red/blue swapped) */
    if (info.chroma_subsampling == JPEG_422_SUBSAMPLING) {
        uint32_t cw = W / 2u;
        const uint8_t *cb_src = yc + (size_t)W * H;
        const uint8_t *cr_src = cb_src + (size_t)cw * H;
        const uint8_t *cb_rt = yc2 + (size_t)W * H;
        uint32_t d_same = 0, d_swap = 0, n = 0;
        for (uint32_t y = 0; y < H; y += 32)
            for (uint32_t x = 0; x < cw; x += 32) {
                uint32_t i = (size_t)y * cw + x;
                uint32_t a = cb_rt[i] > cb_src[i] ? cb_rt[i] - cb_src[i] : cb_src[i] - cb_rt[i];
                uint32_t b = cb_rt[i] > cr_src[i] ? cb_rt[i] - cr_src[i] : cr_src[i] - cb_rt[i];
                d_same += a; d_swap += b; n++;
            }
        LOG_SIMPLE("yuvtest: chroma Cb-vs-Cb mean=%lu, Cb-vs-Cr(swap) mean=%lu -> %s\r\n",
                   (unsigned long)(d_same / n), (unsigned long)(d_swap / n),
                   (d_swap / n + 8u < d_same / n) ? "CB/CR SWAPPED BY ENCODER"
                                                  : "chroma order OK");
    }
    ai_jpeg_free_buffer(jpg);
    buffer_free(frame);
    return (mx <= 12u) ? 0 : -1;
}

static int gputest_cmd(int argc, char* argv[])
{
    (void)argc; (void)argv;
    const uint8_t *p = NULL;
    uint32_t len = 0;
    uint16_t w = 0, h = 0;

    LOG_SIMPLE("gputest: gpu available=%d\r\n", gpu_resize_is_available());
    (void)gputest_synth_identity();

    if (usbh_uvc_service_latest_frame(&p, &len, &w, &h) == 0 || len == 0) {
        LOG_SIMPLE("gputest: no uvc frame\r\n");
        return -1;
    }
    if (len > 512u * 1024u) {
        len = 512u * 1024u;
    }
    uint8_t *frame = buffer_malloc_aligned(len, 32);
    nn_model_info_t mi = {0};
    uint8_t *rgb = NULL;
    uint8_t *dst = NULL;
    uint32_t rgb_len = 0;
    if (!frame) {
        return -1;
    }
    memcpy(frame, p, len);
    if (nn_get_model_info(&mi) != AICAM_OK || mi.input_width == 0) {
        LOG_SIMPLE("gputest: model info invalid\r\n");
        buffer_free(frame);
        return -1;
    }

    /* decode + convert once into a stable RGB888 source */
    uint8_t *ycbcr = NULL;
    uvc_jpeg_info_t info = {0};
    if (uvc_jpeg_raster_trylock(5000) != 0) {
        LOG_SIMPLE("gputest: raster lock timeout\r\n");
        buffer_free(frame);
        return -1;
    }
    int dec = uvc_jpeg_decode_ycbcr(frame, len, &ycbcr, &rgb_len, &info);
    if (dec != 0) {
        LOG_SIMPLE("gputest: decode failed (%d)\r\n", dec);
    } else {
        aicam_result_t cc = ai_color_convert(ycbcr, info.width, info.height,
                                             DMA2D_INPUT_YCBCR, 1,
                                             info.chroma_subsampling, &rgb,
                                             &rgb_len, DMA2D_OUTPUT_ARGB8888);
        if (cc != AICAM_OK) {
            LOG_SIMPLE("gputest: convert failed (%d)\r\n", (int)cc);
        }
    }
    uvc_jpeg_raster_unlock();
    if (dec != 0 || rgb == NULL) {
        buffer_free(frame);
        return -1;
    }
    dst = buffer_malloc_aligned((size_t)mi.input_width * mi.input_height * 3u, 32);
    if (!dst) {
        buffer_free(rgb); buffer_free(frame);
        return -1;
    }

    const int N = 20;
    uint32_t t0, t_gpu = 0, t_sw = 0;
    int gpu_ok = 0;

    for (int i = 0; i < N; i++) {
        t0 = osKernelGetTickCount();
        if (gpu_resize_rgba8888_packed(rgb, info.width, info.height, dst,
                                       mi.input_width, mi.input_height) == 0) {
            gpu_ok++;
        }
        t_gpu += osKernelGetTickCount() - t0;
    }
    for (int i = 0; i < N; i++) {
        t0 = osKernelGetTickCount();
        (void)image_resize(rgb, info.width, info.height, DMA2D_INPUT_ARGB8888,
                           dst, mi.input_width, mi.input_height, DMA2D_INPUT_RGB888);
        t_sw += osKernelGetTickCount() - t0;
    }

    LOG_SIMPLE("gputest: %ux%u -> %ux%u x%d: gpu=%lums (ok %d/%d) sw=%lums\r\n",
               (unsigned)info.width, (unsigned)info.height,
               (unsigned)mi.input_width, (unsigned)mi.input_height,
               N, (unsigned long)t_gpu, gpu_ok, N, (unsigned long)t_sw);

    /* correctness A/B on the SAME source: GPU render vs software render,
     * compared as 8x8 tile-mean grids (immune to scene motion). */
    if (gpu_ok > 0) {
        uint8_t *sw = buffer_malloc_aligned((size_t)mi.input_width * mi.input_height * 3u, 32);
        if (sw &&
            gpu_resize_rgba8888_packed(rgb, info.width, info.height, dst,
                                       mi.input_width, mi.input_height) == 0 &&
            image_resize(rgb, info.width, info.height, DMA2D_INPUT_ARGB8888,
                         sw, mi.input_width, mi.input_height,
                         DMA2D_INPUT_RGB888) == AICAM_OK) {
            uint32_t acc = 0, mx = 0;
            size_t bytes = (size_t)mi.input_width * mi.input_height * 3u;
            for (size_t i = 0; i < bytes; i++) {
                uint32_t d = dst[i] > sw[i] ? dst[i] - sw[i] : sw[i] - dst[i];
                acc += d;
                if (d > mx) {
                    mx = d;
                }
            }
            LOG_SIMPLE("gputest: gpu-vs-sw pixels: mean=%lu.%02lu max=%lu\r\n",
                       (unsigned long)(acc / bytes),
                       (unsigned long)((acc % bytes) * 100u / bytes),
                       (unsigned long)mx);
        }
        if (sw) {
            buffer_free(sw);
        }
    }

    buffer_free(dst);
    buffer_free(rgb);
    buffer_free(frame);
    return 0;
}


static int mem_cmd(int argc, char* argv[])
{
    if (argc < 4) {
        LOG_SIMPLE("Usage: mem r <address> <length>\r\n");
        LOG_SIMPLE("       mem w <address> <value>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "r") == 0) {
        unsigned int addr = strtoul(argv[2], NULL, 0);
        int len = atoi(argv[3]);
        unsigned char *p = (unsigned char*)addr;
        LOG_SIMPLE("Read memory at 0x%08X:\r\n", addr);
        for (int i = 0; i < len; i++) {
            LOG_SIMPLE("%02X ", p[i]);
            if ((i+1)%16 == 0) LOG_SIMPLE("\r\n");
        }
        LOG_SIMPLE("\n");
    } else if (strcmp(argv[1], "w") == 0) {
        unsigned int addr = strtoul(argv[2], NULL, 0);
        unsigned int value = strtoul(argv[3], NULL, 0);
        unsigned int *p = (unsigned int*)addr;
        *p = value;
        LOG_SIMPLE("Write 0x%08X to 0x%08X\r\n", value, addr);
    } else {
        LOG_SIMPLE("Unknown mem subcommand: %s\r\n", argv[1]);
        return -1;
    }
    return 0;
}

static int fget_cmd(int argc, char* argv[])
{
    if (argc == 1) {
        // No parameters, dump all data
        LOG_SIMPLE("Dump NVS_FACTORY:\r\n");
        storage_nvs_dump(NVS_FACTORY);
        LOG_SIMPLE("Dump NVS_USER:\r\n");
        storage_nvs_dump(NVS_USER);
        return 0;
    }

    if (argc == 2) {
        // With key parameter, read key
        char *key = argv[1];
        char value[128] = {0};
        int ret_factory = storage_nvs_read(NVS_FACTORY, key, value, sizeof(value)-1);
        if (ret_factory > 0) {
            LOG_SIMPLE("[FACTORY] Key: %s, Value: %s\r\n", key, value);
        } else {
            LOG_SIMPLE("[FACTORY] Key: %s not found\r\n", key);
        }
        int ret_user = storage_nvs_read(NVS_USER, key, value, sizeof(value)-1);
        if (ret_user > 0) {
            LOG_SIMPLE("[USER]    Key: %s, Value: %s\r\n", key, value);
        } else {
            LOG_SIMPLE("[USER]    Key: %s not found\r\n", key);
        }
        return 0;
    }

    LOG_SIMPLE("Usage: fget [key]\r\n");
    return -1;
}

static int fset_cmd(int argc, char* argv[])
{
    if (argc == 2) {
        char *key = argv[1];
        storage_nvs_delete(NVS_USER, key);
        return 0;
    }

    if (argc == 3) {
        char *key = argv[1];
        char *value = argv[2];
        storage_nvs_write(NVS_USER, key, value, strlen(value) + 1);
        return 0;
    }

    LOG_SIMPLE("Usage: fset <key> [value]\r\n");
    return -1;
}

static int standby_cmd(int argc, char* argv[])
{
#if ENABLE_U0_MODULE
    uint32_t wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
    uint32_t sleep_second = 0;
    if (argc > 1) sleep_second = (uint32_t)(atoi(argv[1]));
    u0_module_enter_sleep_mode(wakeup_flags, 0, sleep_second);
#else
    if (argc > 1)
    {
        char* endptr;
        uint64_t wake_time = strtoull(argv[1], &endptr, 10);
        if (*endptr != '\0')
        {
            LOG_SIMPLE("Invalid standby time: %s\n", argv[1]);
            return -1;
        }
        usr_set_rtc_alarm(wake_time);
    }
    pwr_enter_standby_mode();
#endif

    return 0;
}

/* ==================== Configuration Management Commands ==================== */

/**
 * @brief Show current configuration
 */
static int config_show_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("=== Current Configuration ===\r\n");
    
    //print current json config
    aicam_global_config_t *config = NULL;
    aicam_result_t result;
    config = (aicam_global_config_t*)buffer_calloc(1, sizeof(aicam_global_config_t));
    if (!config) {
        LOG_SIMPLE("Failed to allocate memory for config\r\n");
        return -1;
    }
    result = json_config_load_from_file(NULL, config);
    // show in json format
    char* json_buffer = (char*)buffer_calloc(1, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (!json_buffer) {
        LOG_SIMPLE("Failed to allocate memory for json buffer\r\n");
        return -1;
    }
    result = json_config_serialize_to_string(config, json_buffer, JSON_CONFIG_MAX_BUFFER_SIZE);
    if (result != AICAM_OK) {
        LOG_SIMPLE("Failed to serialize config to string\r\n");
        buffer_free(json_buffer);
        return -1;
    }
    printf("%s\r\n", json_buffer);
    buffer_free(json_buffer);
    buffer_free(config);
    return 0;
}

/**
 * @brief Set configuration value
 */
static int config_set_cmd(int argc, char* argv[])
{
    //TODO: to implement
    return 0;
}

/* ==================== Utility Commands ==================== */

/**
 * @brief CherryUSB bring-up: usb host <init|deinit|status> | usb device <init|deinit|status>
 */
static int usb_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        goto usage;
    }
    if (strcmp(argv[1], "host") == 0) {
        if (strcmp(argv[2], "init") == 0) {
            int ret = usb_cherry_host_init(NULL);
            LOG_SIMPLE("usb host init: %s (%d)\r\n", ret == 0 ? "ok" : "failed", ret);
            return ret;
        }
        if (strcmp(argv[2], "deinit") == 0) {
            int ret = usb_cherry_host_deinit();
            LOG_SIMPLE("usb host deinit: %s (%d)\r\n", ret == 0 ? "ok" : "failed", ret);
            return ret;
        }
        if (strcmp(argv[2], "status") == 0) {
            LOG_SIMPLE("usb host: %s\r\n", usb_cherry_host_is_inited() ? "running" : "stopped");
            return 0;
        }
        if (strcmp(argv[2], "uvc") == 0) {
            const char *op = (argc >= 4) ? argv[3] : "";
            if (strcmp(op, "info") == 0) {
                return usbh_uvc_test_info();
            }
            if (strcmp(op, "open") == 0) {
                if (argc < 6) {
                    LOG_SIMPLE("Usage: usb host uvc open <w> <h> [alt]\r\n");
                    return -1;
                }
                uint8_t alt = (argc >= 7) ? (uint8_t)atoi(argv[6]) : 0xff;
                return usbh_uvc_test_open((uint16_t)atoi(argv[4]),
                                          (uint16_t)atoi(argv[5]), alt);
            }
            if (strcmp(op, "close") == 0) {
                return usbh_uvc_test_close();
            }
            if (strcmp(op, "start") == 0) {
                return usbh_uvc_test_start();
            }
            if (strcmp(op, "stop") == 0) {
                return usbh_uvc_test_stop();
            }
            if (strcmp(op, "stat") == 0) {
                return usbh_uvc_test_stat();
            }
            if (strcmp(op, "capture") == 0) {
                uint32_t tmo = (argc >= 5) ? (uint32_t)atoi(argv[4]) : 5000;
                return usbh_uvc_test_capture(tmo);
            }
            if (strcmp(op, "dump") == 0) {
                uint32_t off = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;
                uint32_t len = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 64;
                return usbh_uvc_test_dump(off, len);
            }
            if (strcmp(op, "raw") == 0) {
                return usbh_uvc_test_raw();
            }
            if (strcmp(op, "hdrdbg") == 0) {
                return usbh_uvc_test_hdrdbg();
            }
            if (strcmp(op, "isodbg") == 0) {
                return usbh_uvc_test_isodbg();
            }
            if (strcmp(op, "fps") == 0) {
                int v = (argc >= 5) ? atoi(argv[4]) : -1; /* -1 = query only */
                return usbh_uvc_test_webfps(v);
            }
            if (strcmp(op, "trace") == 0) {
                uint32_t cnt = (argc >= 5) ? (uint32_t)atoi(argv[4]) : 16;
                return usbh_uvc_test_trace(cnt);
            }
            if (strcmp(op, "record") == 0) {
                if (argc < 5) {
                    LOG_SIMPLE("Usage: usb host uvc record <seconds> [filename]\r\n");
                    return -1;
                }
                const char *fname = (argc >= 6) ? argv[5] : "uvc_rec.avi";
                return usbh_uvc_test_record((uint32_t)atoi(argv[4]), fname);
            }
            LOG_SIMPLE("Usage: usb host uvc <info|open w h [alt]|close|start|stop|stat|capture [ms]|dump [off [len]]|raw|trace [n]|record s [file]>\r\n");
            return -1;
        }
        goto usage;
    }
    if (strcmp(argv[1], "device") == 0) {
        if (strcmp(argv[2], "init") == 0) {
            int ret = usb_cherry_device_init();
            LOG_SIMPLE("usb device init: %s (%d)\r\n", ret == 0 ? "ok" : "failed", ret);
            return ret;
        }
        if (strcmp(argv[2], "deinit") == 0) {
            int ret = usb_cherry_device_deinit();
            LOG_SIMPLE("usb device deinit: %s (%d)\r\n", ret == 0 ? "ok" : "failed", ret);
            return ret;
        }
        if (strcmp(argv[2], "status") == 0) {
            char diag[128];
            usb_cherry_device_diag(diag, sizeof(diag));
            LOG_SIMPLE("usb device: %s | %s\r\n",
                       usb_cherry_device_is_inited() ? "running" : "stopped", diag);
            return 0;
        }
        goto usage;
    }
usage:
    LOG_SIMPLE("Usage: usb host <init|deinit|status|uvc ...>\r\n");
    LOG_SIMPLE("       usb host uvc <info|open w h [alt]|close|start|stop|stat|capture [ms]|dump [off [len]]>\r\n");
    LOG_SIMPLE("       usb device <init|deinit|status>\r\n");
    return -1;
}

/**
 * @brief Show system version
 */
static int version_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("=== AICAM System Version ===\r\n");
    LOG_SIMPLE("Firmware Version: %s\r\n", FW_VERSION_STRING);
    LOG_SIMPLE("Build Date: %s %s\r\n", FW_BUILD_DATE, FW_BUILD_TIME);
    LOG_SIMPLE("Git Hash: %s\r\n", FW_GIT_COMMIT);
    LOG_SIMPLE("Git Branch: %s\r\n", FW_GIT_BRANCH);
    LOG_SIMPLE("Core System: JSON Config + Event Bus\r\n");
    return 0;
}

/**
 * @brief Echo command for testing
 */
static int echo_cmd(int argc, char* argv[])
{
    LOG_SIMPLE("Echo: ");
    for (int i = 1; i < argc; i++) {
        LOG_SIMPLE("%s ", argv[i]);
    }
    LOG_SIMPLE("\r\n");
    return 0;
}

static int battery_cmd(int argc, char* argv[]) 
{
    uint8_t rate = 0;
    int ret;
    if (argc > 2) {
        LOG_SIMPLE("Usage: battery\n");
        return -1;
    }
    device_t *misc = device_find_pattern(BATTERY_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    ret = device_ioctl(misc, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&rate, 0);
    if(!ret){
        LOG_SIMPLE("battery rate: %d \r\n", rate);
    }else{
        LOG_SIMPLE("get battery rate failed \r\n");
    }
    
    return 0;
}

static int light_cmd(int argc, char* argv[]) 
{
    uint8_t rate = 0;
    int ret;
    if (argc > 2) {
        LOG_SIMPLE("Usage: light\r\n");
        return -1;
    }

    device_t *misc = device_find_pattern(LIGHT_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    ret = device_ioctl(misc, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&rate, 0);
    if(!ret){
        LOG_SIMPLE("light rate: %d \r\n", rate);
    }else{
        LOG_SIMPLE("get light rate failed \r\n");
    }
    
    return 0;
}

static int led_cmd(int argc, char* argv[]) 
{
    blink_params_t blink_params;
    int led_index = 0;
    if (argc < 3) {
        LOG_SIMPLE("Usage: led <index> <on/off/blink> [blink_times interval_ms]\r\n");
        return -1;
    }

    sscanf(argv[1], "%d", &led_index);
    if(led_index < 0 || led_index > 1){
        LOG_SIMPLE("Invalid led index: %d\r\n", led_index);
        return -1;
    }
    device_t *misc;

    if(led_index == 0){
        misc = device_find_pattern(IND_DEVICE_NAME, DEV_TYPE_MISC);
    }else if(led_index == 1){
        misc = device_find_pattern(IND_EXT_DEVICE_NAME, DEV_TYPE_MISC);
    }

    if(misc == NULL){
        return -1;
    }

    if (strcmp(argv[2], "on") == 0){
        device_ioctl(misc, MISC_CMD_LED_ON, 0, 0);
    }else if(strcmp(argv[2], "off") == 0){
        device_ioctl(misc, MISC_CMD_LED_OFF, 0, 0);
    }else if(strcmp(argv[2], "blink") == 0){
        if (argc < 4)
            return -1; 
        sscanf(argv[3], "%d", &blink_params.blink_times);
        sscanf(argv[4], "%d", &blink_params.interval_ms);
        device_ioctl(misc, MISC_CMD_LED_SET_BLINK, (uint8_t *)&blink_params, 0);
    }
    return 0;
}

static int flash_cmd(int argc, char* argv[]) 
{
    int duty;
    blink_params_t blink_params;
    if (argc < 2) {
        LOG_SIMPLE("Usage: flash <on/off/duty/blink>\r\n");
        return -1;
    }

    device_t *misc = device_find_pattern(FLASH_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    if (strcmp(argv[1], "on") == 0){
        device_ioctl(misc, MISC_CMD_PWM_ON, 0, 0);
    }else if(strcmp(argv[1], "off") == 0){
        device_ioctl(misc, MISC_CMD_PWM_OFF, 0, 0);
    }else if(strcmp(argv[1], "duty") == 0){
        if (argc < 3)
            return -1; 
        sscanf(argv[2], "%d", &duty);
        uint8_t data = (uint8_t)duty;
        device_ioctl(misc, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&data, 0);
        device_ioctl(misc, MISC_CMD_PWM_ON, 0, 0);
    }else if(strcmp(argv[1], "blink") == 0){
        if (argc < 4)
            return -1;
        sscanf(argv[2], "%d", &blink_params.blink_times);
        sscanf(argv[3], "%d", &blink_params.interval_ms);
        device_ioctl(misc, MISC_CMD_PWM_SET_BLINK, (uint8_t *)&blink_params, 0);
    }
    return 0;
}

static void button_short_press(void)
{
    LOG_SIMPLE("button short press ....\r\n");
}

static int button_cmd(int argc, char* argv[]) 
{
    if (argc > 2) {
        return -1;
    }

    device_t *misc = device_find_pattern(KEY_DEVICE_NAME, DEV_TYPE_MISC);
    if(misc == NULL){
        return -1;
    }

    device_ioctl(misc, MISC_CMD_BUTTON_SET_SP_CB, (uint8_t *)button_short_press, 0);
    return 0;
}

static int sdformat_cmd(int argc, char* argv[]) 
{
    sd_format();
    return 0;
}

static int sdinfo_cmd(int argc, char* argv[])
{
    sd_disk_info_t info;
    if (sd_get_disk_info(&info) == 0) {
        LOG_SIMPLE("sd_get_disk_info: mode %d, fs_type:%s, total: %ld Kbytes, free: %ld Kbytes\r\n", info.mode, info.fs_type, info.total_KBytes, info.free_KBytes);
    }
    return 0;
}

static int sdspeed_cmd(int argc, char* argv[])
{
    sd_speed_info_t s;
    if (sd_get_speed_info(&s) != 0) {
        LOG_SIMPLE("SD card not ready (init failed or not inserted)\r\n");
        return -1;
    }
    uint32_t mhz_int  = s.bus_clk_hz / 1000000UL;
    uint32_t mhz_frac = (s.bus_clk_hz % 1000000UL) / 10000UL;
    LOG_SIMPLE("=== SD Speed Info ===\r\n");
    LOG_SIMPLE("Card type    : %s\r\n", s.card_type);
    LOG_SIMPLE("Card cap     : %s (capability, not active mode)\r\n", s.card_speed);
    LOG_SIMPLE("Bus width    : %d bit\r\n", s.bus_width);
    LOG_SIMPLE("Bus clock    : %lu Hz (%lu.%02lu MHz)\r\n", s.bus_clk_hz, mhz_int, mhz_frac);
    LOG_SIMPLE("Src clock    : %lu Hz\r\n", s.src_clk_hz);
    LOG_SIMPLE("CLKDIV       : %lu\r\n", s.clkdiv);
    LOG_SIMPLE("HS switched  : %s\r\n", s.hs_switched ? "yes (card in High Speed)" : "no (card in Default Speed)");
    return 0;
}

static int sdswitch_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: sdswitch <high|default|auto|overclock>\r\n");
        LOG_SIMPLE("  high      50MHz + CMD6 HS (spec-compliant, boot default)\r\n");
        LOG_SIMPLE("  default   slow bus to 25MHz\r\n");
        LOG_SIMPLE("  auto      best supported (collapses to high)\r\n");
        LOG_SIMPLE("  overclock 100MHz (CLKDIV=0, OUT OF SPEC - test only)\r\n");
        return -1;
    }
    sd_speed_mode_e m;
    if      (strcmp(argv[1], "high")      == 0) m = SD_SPEED_HIGH;
    else if (strcmp(argv[1], "default")   == 0) m = SD_SPEED_DEFAULT;
    else if (strcmp(argv[1], "auto")      == 0) m = SD_SPEED_AUTO;
    else if (strcmp(argv[1], "overclock") == 0) m = SD_SPEED_OVERCLOCK;
    else { LOG_SIMPLE("Usage: sdswitch <high|default|auto|overclock>\r\n"); return -1; }

    if (sd_set_speed_mode(m) != 0) {
        LOG_SIMPLE("sd_set_speed_mode(%s) failed\r\n", argv[1]);
        return -1;
    }
    sd_speed_info_t s;
    if (sd_get_speed_info(&s) == 0) {
        LOG_SIMPLE("switched -> bus clock %lu Hz, HS switched: %s\r\n",
                   s.bus_clk_hz, s.hs_switched ? "yes" : "no");
    }
    return 0;
}

static int sdrwtest_cmd(int argc, char* argv[])
{
    uint32_t total_kb = 2048;
    uint32_t chunk_kb = 32;
    if (argc >= 2) total_kb = (uint32_t)atoi(argv[1]);
    if (argc >= 3) chunk_kb = (uint32_t)atoi(argv[2]);
    if (total_kb == 0) total_kb = 2048;
    if (chunk_kb == 0) chunk_kb = 32;

    LOG_SIMPLE("SD rwtest: total=%luKB chunk=%luKB (blocks SD I/O for duration)...\r\n",
               (unsigned long)total_kb, (unsigned long)chunk_kb);
    uint32_t w_kbps = 0, r_kbps = 0;
    int r = sd_speed_test(total_kb, chunk_kb, &w_kbps, &r_kbps);
    if (r != 0) {
        LOG_SIMPLE("sd_speed_test failed: %d\r\n", r);
        return -1;
    }
    LOG_SIMPLE("Write: %lu KiB/s (%lu.%02lu MB/s)\r\n",
               (unsigned long)w_kbps,
               (unsigned long)(w_kbps / 1024UL),
               (unsigned long)((w_kbps % 1024UL) * 100UL / 1024UL));
    LOG_SIMPLE("Read : %lu KiB/s (%lu.%02lu MB/s)\r\n",
               (unsigned long)r_kbps,
               (unsigned long)(r_kbps / 1024UL),
               (unsigned long)((r_kbps % 1024UL) * 100UL / 1024UL));
    return 0;
}

static int camera_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE(" camera bri <val> | camera bri");
        LOG_SIMPLE(" camera con <val> | camera con");
        LOG_SIMPLE(" camera mir <val> | camera mir");
        LOG_SIMPLE(" camera aec <val> | camera aec");
        LOG_SIMPLE(" camera skip <val> | camera skip");
        return -1;
    }
    int val = 0, ret = 0, set_flag = 0;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(camera_dev == NULL){
        LOG_SIMPLE("camera device not found\r\n");
        return -1;
    }
    sensor_params_t sensor_param;
    ret = device_ioctl(camera_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
    if(ret != AICAM_OK){
        LOG_SIMPLE("get sensor param failed\r\n");
        return -1;
    }
    if (strcmp(argv[1], "bri") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            if (val < 0) val = 0;
            if (val > 100) val = 100;
            sensor_param.brightness = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("brightness: %d\r\n", sensor_param.brightness);
        }
    } else if (strcmp(argv[1], "con") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.contrast = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("con: %d\r\n", sensor_param.contrast);
        }
    } else if (strcmp(argv[1], "mir") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.mirror_flip = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("mir: %d\r\n", sensor_param.mirror_flip);
        }
    } else if (strcmp(argv[1], "aec") == 0) {
        if (argc >= 3) {
            val = atoi(argv[2]);
            sensor_param.aec = val;
            set_flag = 1;
        } else {
            LOG_SIMPLE("aec: %d\r\n", sensor_param.aec);
        }
    } else if (strcmp(argv[1], "skip") == 0) {
        // Handle startup skip frames - use ioctl directly, persist via image_config
        int skip_frames = 0;
        if (argc >= 3) {
            val = atoi(argv[2]);
            if (val < 1) val = 1;
            if (val > 300) val = 300;
            ret = device_ioctl(camera_dev, CAM_CMD_SET_STARTUP_SKIP_FRAMES, NULL, val);
            if (ret == AICAM_OK) {
                // Also save to config for persistence
                image_config_t image_config;
                if (json_config_get_device_service_image_config(&image_config) == AICAM_OK) {
                    image_config.startup_skip_frames = val;
                    json_config_set_device_service_image_config(&image_config);
                }
                LOG_SIMPLE("startup_skip_frames set to: %d\r\n", val);
            } else {
                LOG_SIMPLE("set startup_skip_frames failed: %d\r\n", ret);
                return -1;
            }
        } else {
            ret = device_ioctl(camera_dev, CAM_CMD_GET_STARTUP_SKIP_FRAMES, (uint8_t *)&skip_frames, 0);
            if (ret == AICAM_OK) {
                LOG_SIMPLE("startup_skip_frames: %d\r\n", skip_frames);
            } else {
                LOG_SIMPLE("get startup_skip_frames failed\r\n");
            }
        }
        return ret;
    } else {
        LOG_SIMPLE("Unknown camera command\r\n");
        return -1;
    }
    if(set_flag){
        ret = device_ioctl(camera_dev, CAM_CMD_SET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
        if(ret != AICAM_OK){
            LOG_SIMPLE("set sensor param failed\r\n");
            return -1;
        }
    }
    if (ret != 0) LOG_SIMPLE("Camera command failed, ret=%d\r\n", ret);
    return ret;
}

static int upgrade_from_file_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: upgrade_from_file <firmware_type> <filename>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[2], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "rb");
    if (!fd) {
        LOG_SIMPLE("Cannot open %s\r\n", local_filename);
        return -1;
    }

    firmware_header_t header;
    struct stat st;
    if (file_stat(local_filename, &st) != 0) {
        LOG_SIMPLE("Cannot stat %s\r\n", local_filename);
        file_fclose(fd);
        return -1;
    }
    header.file_size = st.st_size;
    memcpy(header.version, local_filename, sizeof(header.version));
    upgrade_handle_t handle = {0};
    if (upgrade_begin(&handle, fw_type, &header) != 0) {
        LOG_SIMPLE("upgrade_begin failed\r\n");
        file_fclose(fd);
        return -1;
    }

    LOG_SIMPLE("Firmware size: %d upgrade address: 0x%x\r\n", header.file_size, handle.base_offset);
    char buf[1024];
    uint32_t remain = header.file_size;
    while (remain > 0) {
        size_t chunk = remain > sizeof(buf) ? sizeof(buf) : remain;
        int n = file_fread(fd, buf, chunk);
        if (n <= 0) break;
        if (upgrade_write_chunk(&handle, buf, n) != 0) {
            LOG_SIMPLE("upgrade_write_chunk failed\r\n");
            file_fclose(fd);
            return -1;
        }
        remain -= n;
    }
    file_fclose(fd);

    if (remain != 0) {
        LOG_SIMPLE("Firmware file size mismatch\r\n");
        return -1;
    }

    if (upgrade_finish(&handle) != 0) {
        LOG_SIMPLE("upgrade_finish failed\r\n");
        return -1;
    }
    LOG_SIMPLE("Upgrade from file success!\r\n");
    return 0;
}

static int dump_firmware_cmd(int argc, char* argv[])
{
    if (argc < 4) {
        LOG_SIMPLE("Usage: dump_firmware <firmware_type> <slot> <filename>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    int slot_idx = atoi(argv[2]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    if (slot_idx != SLOT_A && slot_idx != SLOT_B) {
        LOG_SIMPLE("Invalid slot (must be 0 or 1)\r\n");
        return -1;
    }
    char local_filename[MAX_FILENAME_LEN];
    strncpy(local_filename, argv[3], MAX_FILENAME_LEN - 1);
    local_filename[MAX_FILENAME_LEN - 1] = '\0';

    void *fd = file_fopen(local_filename, "wb");
    if (!fd) {
        LOG_SIMPLE("Cannot open %s for write\r\n", local_filename);
        return -1;
    }

    upgrade_handle_t handle = {0};
    firmware_header_t header = {0};
    handle.header = &header;
    if (upgrade_read_begin(&handle, fw_type, slot_idx) != 0) {
        LOG_SIMPLE("upgrade_read_begin failed\r\n");
        file_fclose(fd);
        return -1;
    }

    char buf[1024];
    uint32_t remain = handle.total_size;
    while (remain > 0) {
        size_t chunk = remain > sizeof(buf) ? sizeof(buf) : remain;
        if (upgrade_read_chunk(&handle, buf, chunk) != chunk) break;
        file_fwrite(fd, buf, chunk);
        remain -= chunk;
    }
    file_fclose(fd);

    if (remain != 0) {
        LOG_SIMPLE("Firmware dump failed (size mismatch)\r\n");
        return -1;
    }

    LOG_SIMPLE("Firmware dumped to %s ,size=%d\r\n", local_filename, handle.total_size);
    return 0;
}

static int switch_slot_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: switch_slot <firmware_type>\r\n");
        return -1;
    }
    int fw_type = atoi(argv[1]);
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
        LOG_SIMPLE("Invalid firmware type\r\n");
        return -1;
    }
    SystemState *sys_state = get_system_state();
    if (switch_active_slot(fw_type) == 0) {
        LOG_SIMPLE("Switch slot success! Now active slot=%d\r\n", sys_state->active_slot[fw_type]);
        return 0;
    } else {
        LOG_SIMPLE("Switch slot failed. No valid slot to switch.\r\n");
        return -1;
    }
}

static const char* slot_status_str[] = {
    "IDLE",
    "PENDING_VERIFICATION",
    "ACTIVE",
    "UNBOOTABLE"
};

static int show_slot_status_cmd(int argc, char* argv[])
{
    SystemState *sys_state = get_system_state();

    int fw_start = 0;
    int fw_end = FIRMWARE_TYPE_COUNT;

    if (argc == 2) {
        int fw_type = atoi(argv[1]);
        if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT) {
            LOG_SIMPLE("Invalid firmware type\r\n");
            return -1;
        }
        fw_start = fw_type;
        fw_end = fw_type + 1;
    }

    for (int fw = fw_start; fw < fw_end; fw++) {
        LOG_SIMPLE("------------------------------------------------------------\n");
        LOG_SIMPLE("Firmware %d | Active slot: %d\n", fw, sys_state->active_slot[fw]);
        LOG_SIMPLE("Slot | Status               | BootSuccess | TryCount | Version         | Size     | CRC32      \n");
        LOG_SIMPLE("-----+----------------------+-------------+----------+-----------------+----------+------------\n");
        for (int slot = 0; slot < SLOT_COUNT; slot++) {
            slot_info_t *info = &sys_state->slot[fw][slot];
            // Use OTA_VER_BUILD for 16-bit BUILD number support
            LOG_SIMPLE("%4d | %-20s | %11u | %8u | %d.%d.%d.%-5u | %8u | 0x%08X\n",
                slot,
                slot_status_str[info->status],
                info->boot_success,
                info->try_count,
                OTA_VER_MAJOR(info->version),
                OTA_VER_MINOR(info->version),
                OTA_VER_PATCH(info->version),
                OTA_VER_BUILD(info->version),
                info->firmware_size,
                info->crc32
            );
        }
    }
    return 0;
}

static int clean_slot_cmd(int argc, char* argv[])
{
    clean_system_state();
    return 0;
}

__attribute__((unused)) static void ota_header_print(const ota_header_t *header)
{
    if (!header) {
        return;
    }
    
    printf("=== OTA Header Information ===\r\n");
    printf("Magic: 0x%08lX\r\n", header->magic);
    printf("Header Version: 0x%04X\r\n", header->header_version);
    printf("Header Size: %d bytes\r\n", header->header_size);
    printf("Header CRC32: 0x%08lX\r\n", header->header_crc32);
    
    const char* fw_type_names[] = {
        "Unknown", "FSBL", "APP", "WEB", "AI_MODEL", "CONFIG", "PATCH", "FULL"
    };
    printf("Firmware Type: %s (%d)\r\n", 
           (header->fw_type < sizeof(fw_type_names)/sizeof(fw_type_names[0])) ? 
           fw_type_names[header->fw_type] : "Unknown", header->fw_type);
    
    printf("Encryption Type: %d\r\n", header->encrypt_type);
    printf("Compression Type: %d\r\n", header->compress_type);
    RTC_TIME_S tm_utc;
    timeStamp_to_time(header->timestamp, &tm_utc);
    printf("Timestamp: %04d-%02d-%02d %02d:%02d:%02d\r\n", tm_utc.year + 1970, tm_utc.month, tm_utc.date, tm_utc.hour, tm_utc.minute, tm_utc.second);
    printf("Sequence: %lu\r\n", header->sequence);
    printf("Total Package Size: %lu bytes\r\n", header->total_package_size);
    
    printf("\n=== Firmware Information ===\r\n");
    printf("Firmware Name: %s\r\n", header->fw_name);
    printf("Firmware Description: %s\r\n", header->fw_desc);
    
    // Extract full version (including suffix) using unified interface
    char version_str[64] = {0};
    if (ota_header_get_full_version(header, version_str, sizeof(version_str)) == 0) {
        printf("Firmware Version: %s\r\n", version_str);
    } else {
        // Fallback to numeric version only
        printf("Firmware Version: %d.%d.%d.%d\r\n", 
               header->fw_ver[0], header->fw_ver[1],
               header->fw_ver[2], header->fw_ver[3] + (header->fw_ver[4] << 8));
    }
    printf("Minimum Compatible Version: %d.%d.%d.%d\r\n", 
           header->min_ver[0], header->min_ver[1],
           header->min_ver[2], header->min_ver[3] + (header->min_ver[4] << 8));
    printf("Firmware Size: %lu bytes\r\n", header->fw_size);
    printf("Compressed Size: %lu bytes\r\n", header->fw_size_compressed);
    printf("Firmware CRC32: 0x%08lX\n", header->fw_crc32);
    
    printf("\n=== Target Information ===\r\n");
    printf("Part Table CRC: 0x%08lX\r\n", header->part_table_crc);
    printf("Target Size: %lu bytes\r\n", header->target_size);
    printf("Target Offset: 0x%08lX\r\n", header->target_offset);
    printf("Target Partition: %s\r\n", header->target_partition);
    printf("Hardware Version: 0x%08lX\r\n", header->hw_version);
    printf("Device Model: 0x%04lX%s\r\n", (unsigned long)header->device_model,
           header->device_model == 0 ? " (unstamped)" :
           (header->device_model == OTA_DEVICE_MODEL ? " (match)" : " (MISMATCH)"));
    printf("Chip ID: 0x%08lX\r\n", header->chip_id);
    
    printf("========================\r\n");
}

// Helper to get version string from flash or system state
static void get_fw_version_str(FirmwareType fw_type, char *buf, size_t size)
{
    SystemState *sys_state = get_system_state();
    if (!sys_state) {
        snprintf(buf, size, "unknown");
        return;
    }
    
    int active_slot = sys_state->active_slot[fw_type];
    slot_info_t *slot_info = &sys_state->slot[fw_type][active_slot];
    
    // Try to read from flash header
    uint32_t partition = get_active_partition(fw_type);
    if (partition != 0) {
        ota_header_t header = {0};
        if (storage_flash_read(partition, &header, sizeof(ota_header_t)) == 0 && 
            ota_header_verify(&header) == 0) {
            if (ota_header_get_full_version(&header, buf, size) == 0) {
                return;
            }
        }
    }
    
    // Fallback to system state
    snprintf(buf, size, "%d.%d.%d.%u", 
             OTA_VER_MAJOR(slot_info->version), OTA_VER_MINOR(slot_info->version),
             OTA_VER_PATCH(slot_info->version), OTA_VER_BUILD(slot_info->version));
}

static int fw_version_cmd(int argc, char* argv[])
{
    char version_str[64];
    
    LOG_SIMPLE("=== Firmware Version Information ===\r\n\r\n");
    
    // FSBL
    get_fw_version_str(FIRMWARE_FSBL, version_str, sizeof(version_str));
    LOG_SIMPLE("FSBL:     %s\r\n", version_str);
    
    // APP
    get_fw_version_str(FIRMWARE_APP, version_str, sizeof(version_str));
    LOG_SIMPLE("APP:      %s\r\n", version_str);
    
    // WEB
    get_fw_version_str(FIRMWARE_WEB, version_str, sizeof(version_str));
    LOG_SIMPLE("WEB:      %s\r\n", version_str);
    
    // WAKECORE
#if ENABLE_U0_MODULE
    {
        ms_bridging_version_t wakecore_version = {0};
        if (u0_module_get_version(&wakecore_version) == 0) {
            LOG_SIMPLE("WAKECORE: %d.%d.%d.%d\r\n", wakecore_version.major, wakecore_version.minor, wakecore_version.patch, wakecore_version.build);
        } else {
            LOG_SIMPLE("WAKECORE: unknown\r\n");
        }
    }
#else
    LOG_SIMPLE("WAKECORE: N/A\r\n");
#endif
    
    // MODEL (check if AI_1 is active)
    FirmwareType model_type = json_config_get_ai_1_active() ? FIRMWARE_AI_2 : FIRMWARE_AI_1;
    get_fw_version_str(model_type, version_str, sizeof(version_str));
    LOG_SIMPLE("MODEL:    %s (%s)\r\n", version_str, 
               model_type == FIRMWARE_AI_2 ? "AI_2" : "AI_1");
    
    LOG_SIMPLE("\r\n====================================\r\n");
    
    return 0;
}

static int mg_log_level_cmd(int argc, char* argv[])
{
    if (argc == 1) {
        // No parameter, show current log level
        const char* level_names[] = {
            "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
        };
        int current_level = mg_log_level;
        if (current_level >= 0 && current_level < (int)(sizeof(level_names)/sizeof(level_names[0]))) {
            LOG_SIMPLE("Current mongoose log level: %s (%d)\r\n", level_names[current_level], current_level);
        } else {
            LOG_SIMPLE("Current mongoose log level: %d\r\n", current_level);
        }
        return 0;
    }
    
    if (argc == 2) {
        int level = -1;
        
        // Try to parse as number first
        char* endptr;
        level = (int)strtol(argv[1], &endptr, 10);
        if (*endptr == '\0' && level >= 0 && level <= MG_LL_VERBOSE) {
            mg_log_set(level);
            const char* level_names[] = {
                "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
            };
            LOG_SIMPLE("Mongoose log level set to: %s (%d)\r\n", level_names[level], level);
            return 0;
        }
        
        // Try to parse as string
        if (strcmp(argv[1], "none") == 0 || strcmp(argv[1], "NONE") == 0) {
            level = MG_LL_NONE;
        } else if (strcmp(argv[1], "error") == 0 || strcmp(argv[1], "ERROR") == 0) {
            level = MG_LL_ERROR;
        } else if (strcmp(argv[1], "info") == 0 || strcmp(argv[1], "INFO") == 0) {
            level = MG_LL_INFO;
        } else if (strcmp(argv[1], "debug") == 0 || strcmp(argv[1], "DEBUG") == 0) {
            level = MG_LL_DEBUG;
        } else if (strcmp(argv[1], "verbose") == 0 || strcmp(argv[1], "VERBOSE") == 0) {
            level = MG_LL_VERBOSE;
        }
        
        if (level >= 0) {
            mg_log_set(level);
            const char* level_names[] = {
                "NONE", "ERROR", "INFO", "DEBUG", "VERBOSE"
            };
            LOG_SIMPLE("Mongoose log level set to: %s (%d)\r\n", level_names[level], level);
            return 0;
        }
    }
    
    LOG_SIMPLE("Usage: mg_log_level [level]\r\n");
    LOG_SIMPLE("  level: 0=NONE, 1=ERROR, 2=INFO, 3=DEBUG, 4=VERBOSE\r\n");
    LOG_SIMPLE("        or: none, error, info, debug, verbose\r\n");
    LOG_SIMPLE("  If no level is specified, shows current log level\r\n");
    return -1;
}

debug_cmd_reg_t file_cmd_table[] = {
    {"cat",   "Display file contents",    cat_cmd},
    {"ls",    "List directory contents",  ls_cmd},
    {"cp",    "Copy file",                cp_cmd},
    {"mv",    "Move/rename file",         mv_cmd},
    {"rm",    "Remove file",              rm_cmd},
    {"touch", "Create empty file",        touch_cmd},
    {"write", "write file",               write_cmd},
    {"format", "File system formatting",  format_cmd},
    {"sdformat", "SD card formatting",    sdformat_cmd},
    {"sdinfo", "Show SD card info",      sdinfo_cmd},
    {"sdspeed", "Show SD bus speed/mode", sdspeed_cmd},
    {"sdswitch", "Switch SD speed mode. sdswitch <high|default|auto|overclock>", sdswitch_cmd},
    {"sdrwtest", "SD rw speed test. sdrwtest [total_kb] [chunk_kb]", sdrwtest_cmd},
    {"seektest", "Test file seek", seektest_cmd},
    {"sdfile", "Switch to sd filesystem", sdfile_cmd},
    {"flashfile", "Switch to flash filesystem", flashfile_cmd},
    {"mem", "Memory read/write. r addr len | w addr value", mem_cmd},
    {"captest", "Trigger a full capture+upload cycle (AI on)", captest_cmd},
    {"configmode", "Clear u0 wake events and reboot into full/config mode", configmode_cmd},
    {"aidump", "Dump AI model-input render grids for the current UVC frame", aidump_cmd},
    {"gputest", "Time GPU2D vs software resize on the current UVC frame", gputest_cmd},
    {"jpegfuzz", "Decode a truncated MJPEG frame to exercise the stall recovery", jpegfuzz_cmd},
    {"enctest", "Encode a synthetic RGB565 1080P frame per subsampling (422/444)", enctest_cmd},
    {"yuvtest", "EXPERIMENT: raw planar-YCbCr encode pass-through verification", yuvtest_cmd},
    {"fget", "NVS get. fget [key]", fget_cmd},
    {"fset", "NVS set/delete. fset <key> [value]", fset_cmd},
    {"standby", "standby mode", standby_cmd},
    {"config_show", "Show current configuration", config_show_cmd},
    {"config_set", "Set configuration value. config_set <key> <value>", config_set_cmd},
    {"version", "Show system version", version_cmd},
    {"echo", "Echo command for testing", echo_cmd},
    // {"mkdir", "Create directory",         mkdir_cmd},
    // {"rmdir", "Remove directory",         rmdir_cmd},
    {"led",      "System led control",      led_cmd},
    {"flash",    "Flash control",           flash_cmd},
    {"battery",  "Battery rate",      battery_cmd},
    {"light",    "Light rate",           light_cmd},
    {"button",   "Button short press cb test",        button_cmd},
    {"camera", "camera <bri|con|mir|aec> [val]", camera_cmd},
    {"upgrade_from_file", "upgrade firmware from file", upgrade_from_file_cmd },
    {"dump_firmware", "dump firmware to filesystem", dump_firmware_cmd },
    {"switch_slot", "switch slot", switch_slot_cmd },
    {"show_slot", "show slot", show_slot_status_cmd },
    {"clean_slot", "clean slot", clean_slot_cmd },
    {"fw_version", "Show all firmware versions (FSBL/APP/WEB/WAKECORE/MODEL)", fw_version_cmd },
    {"usb", "USB host/device control. usb host|device <init|deinit|status>", usb_cmd },
    {"mg_log_level", "Set/show mongoose log level. mg_log_level [0-4|none|error|info|debug|verbose]", mg_log_level_cmd },
};


void register_cmds(void)
{
    // register util commands
    for (int i = 0; i < (int)(sizeof(file_cmd_table) / sizeof(file_cmd_table[0])); i++) {
        debug_register_commands(&file_cmd_table[i], 1);
    }


    //web_example_register_commands();
    //ai_service_register_commands();
    comm_cmd_register();
    mqtt_cmd_register();
    service_debug_register_commands();
    video_pipeline_register_commands();
    websocket_stream_server_register_commands();
    factory_test_register_commands();
    rtmp_cmd_register();
    system_service_pir_debug_register_commands();

    
    // LOG_SIMPLE("[CLI] All commands registered (%d util commands + driver commands)\r\n", 
    //            (int)(sizeof(file_cmd_table) / sizeof(file_cmd_table[0])));
}
