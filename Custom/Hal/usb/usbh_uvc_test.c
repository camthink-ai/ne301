/**
 * @file    usbh_uvc_test.c
 * @brief   Host UVC streaming test: ISO IN plumbing, MJPEG frame assembly
 *          and validation stats on top of the vendored usbh_video class.
 *
 * Streaming architecture (constraints: dwc2 buffer-DMA reports no
 * per-packet boundaries, and the periodic channel must be re-armed within
 * the same (micro)frame to keep the isochronous stream gapless):
 *
 *   USB IRQ (urb complete)                     worker thread "uvc_rx"
 *   - update counters                    slot  - walk slot in MPS strides
 *   - hand slot to worker (index ring)        - strip UVC payload headers
 *   - pick a free slot                        - FID/EOF -> MJPEG frames
 *   - usbh_submit_urb() again, inline         - SOI resync + validation
 *     (never blocks, never prints)            - capture handoff
 *
 * Packet-boundary reconstruction depends on the transport:
 *  - bulk: the stream is read in UVC_SLOT_BYTES chunks; a payload may span
 *    many chunks. Boundaries are exact: a chunk completes short iff a
 *    payload ended inside it, and a payload ending exactly at the chunk
 *    size is revealed by its mandatory ZLP (zero-length completion) —
 *    which is why the chunk size stays a multiple of 512. Payloads larger
 *    than a slot are normal here (measured camera: 102656 B ≈ one MJPEG
 *    frame per payload).
 *  - iso: a slot is a contiguous run of whole USB packets, walked in MPS
 *    strides; a frame's last packet may be short and shift the grid, so
 *    after EOF the next frame's header is found by scanning for the SOI
 *    anchor (FF D8 FF preceded by a plausible payload header), which also
 *    recovers the true length of the EOF packet. ZLPs are byte-invisible.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_test.h"
#include "generic_file.h"
#include "web_server.h"

#ifndef IN_PSRAM
#define IN_PSRAM __attribute__((section(".psram_bss")))
#endif
#ifndef ALIGN_32
#define ALIGN_32 __attribute__((aligned(32)))
#endif

/* One service interval per ISO URB. dwc2 buffer-DMA terminates ISO IN on
 * the first short packet (same semantics as bulk), so multi-interval URBs
 * collapse to the first interval whenever the camera under-fills a
 * microframe — per-interval URBs are the honest granularity (~8000/s at
 * HS, re-armed from the completion IRQ like the bulk path). */
#define UVC_ISO_INTERVALS 1
/* Fixed DMA block in AXI_SRAM_UNCACHED (the pool is tight — 64KB is what
 * fits). Split per mode: bulk 4 x 16KB chunks (multiple of 512, required
 * by the ZLP framing trick; 4 slots = ~33ms worker-stall tolerance — a
 * 2-slot ring dropped slots under load and produced torn, undetected
 * mid-JPEG corruption), ISO n x isoin_mps sub-slots capped at 16. */
#define UVC_SLOT_MEM_BYTES   (64 * 1024)
#define UVC_BULK_CHUNK_BYTES (16 * 1024)
#define UVC_FRAME_BYTES   (768 * 1024)
#define UVC_CAPTURE_BYTES (512 * 1024)

#define UVC_HDR_FID 0x01
#define UVC_HDR_EOF 0x02

/* Big buffers: section attributes are not allowed on struct members, so
 * they live at file scope and are referenced directly. Slots are DMA
 * targets (uncached AXI SRAM); frame/capture buffers are CPU-only (PSRAM). */
static uint8_t g_uvc_slot_mem[UVC_SLOT_MEM_BYTES] ALIGN_32 __attribute__((section(".uncached_bss")));
static uint8_t g_uvc_capture_buf[UVC_CAPTURE_BYTES] ALIGN_32 IN_PSRAM;

/* AVI idx1 entries: 4096 frames covers ~136s @ 30fps */
#define UVC_REC_IDX_MAX 4096
static uint32_t g_uvc_rec_idx_off[UVC_REC_IDX_MAX] IN_PSRAM;
static uint32_t g_uvc_rec_idx_len[UVC_REC_IDX_MAX] IN_PSRAM;

/* Latest-frame store for the web preview (/uvc.jpg, /uvc.mjpg): double
 * buffer, lock-free. The worker publishes intact frames into slot[seq&1]
 * then bumps seq; readers snapshot (seq-1)&1 and stay valid until two more
 * publishes. */
#define UVC_PREVIEW_BYTES (512 * 1024)
/* triple buffer: a reader holding slot (seq-1)%3 keeps it safe for three
 * publish periods (100ms @ 30fps) — slow web sends under priority bursts
 * used to tear on a 2-slot rotation */
#define UVC_PREV_SLOTS 3
static uint32_t s_prev_pub_sig;         /* last published frame signature */
static uint8_t g_uvc_prev_buf[UVC_PREV_SLOTS][UVC_PREVIEW_BYTES] ALIGN_32 IN_PSRAM;
static volatile uint32_t g_uvc_prev_len[UVC_PREV_SLOTS];
static volatile uint32_t g_uvc_prev_seq;
static uint16_t g_uvc_prev_w, g_uvc_prev_h;
static volatile int g_uvc_prev_users; /* web preview watchers (refcount) */

/* ISO record handoff (IRQ -> worker): the IRQ hands buffer POINTERS, the
 * worker snapshots them into its ping-pong buffers (thread context) and
 * owns the SD file. 2 staging entries; a full queue drops the frame. */
#define UVC_REC_STAGE_CNT   2
static const uint8_t *volatile g_rec_stage_ptr[UVC_REC_STAGE_CNT];
static volatile uint32_t g_rec_stage_len[UVC_REC_STAGE_CNT];
static volatile uint32_t g_rec_stage_seq[UVC_REC_STAGE_CNT]; /* 0 = free */
static volatile uint32_t g_rec_stage_ticket;
static uint8_t g_uvc_rec_snap[UVC_REC_STAGE_CNT][UVC_PREVIEW_BYTES] ALIGN_32 IN_PSRAM;


/* Streaming state shared between the USB IRQ and the worker/CLI threads.
 * prod_tail/cons_head are the SPSC ring indices; only the IRQ advances
 * prod_tail and only the worker advances cons_head (a dropped slot is
 * marked with slot_len = 0 instead of moving cons_head from IRQ context). */
static struct {
    struct usbh_video *video;

    struct usbh_urb urb;
    volatile uint32_t slot_len[16];
    volatile uint32_t prod_tail;
    volatile uint32_t cons_head;
    uint32_t urb_slot_bytes;   /* ISO: intervals*isoin_mps; bulk: one payload */
    uint32_t slot_count;       /* ring depth: 16x3072 (iso) or 4x12288 (bulk) */
    uint32_t slot_stride;      /* bytes per ring slot */
    uint16_t pkt_stride;       /* ISO only: endpoint MPS (bytes per packet) */
    int is_bulk;               /* bulk transport camera */
    uint32_t bulk_payload_max; /* dwMaxPayloadTransferSize after commit */
    int bulk_at_header;        /* bulk framing: next chunk starts a payload */
    volatile int rearm_pending;/* NAK deferral: worker re-arms the bulk pipe */
    volatile int streaming;

    osThreadId_t worker;
    volatile int worker_run;
    osSemaphoreId_t data_sem;

    uint32_t frame_len;
    uint8_t *frame_cur;   /* assembly target: preview slot (iso) or frame_buf (bulk) */
    int frame_bad;
    int frame_skip;       /* writer late: drop this frame instead of tearing */
    int have_fid;
    int cur_fid;

    volatile int capture_pend;
    volatile uint32_t capture_len;
    volatile int capture_bad;   /* captured frame failed SOI/EOI check */
    osSemaphoreId_t capture_sem;

    volatile int trace_cnt;      /* per-chunk state machine trace (debug) */
    volatile int trace_fin;      /* frame-finish trace lines remaining */

    /* MJPEG recording (usb host uvc record <s> [name]) — AVI/MJPG on SD */
    void *rec_fd;
    volatile int rec_active;     /* 1 while the worker should write frames */
    volatile int rec_error;
    uint32_t rec_end_tick;
    uint32_t rec_frames, rec_skipped, rec_bytes, rec_maxframe;
    uint32_t rec_idx_cnt;
    uint16_t rec_w, rec_h;
    uint32_t rec_fps;
    uint32_t rec_movi_fourcc_pos;

    /* raw pre-parse chunk snapshot (usb host uvc raw) */
    uint8_t raw_snap[3][64];
    uint32_t raw_snap_len[3];
    volatile int raw_snap_cnt;
    volatile int raw_pend;
    osSemaphoreId_t raw_sem;

    /* ISO boundary-header scan (usb host uvc isodbg): counts header-like
     * patterns at 1024B packet boundaries inside long slots */
    volatile int iso_scan_left;
    volatile uint32_t iso_scan_bound, iso_scan_hdr;
    volatile int iso_scan_done;
    osSemaphoreId_t iso_scan_sem;

    /* hdrerr evidence ring (last 8 failing slots) */
    volatile uint32_t hdrdbg_len[8];
    volatile uint32_t hdrdbg_pos[8];
    volatile uint8_t hdrdbg_cnt;
    uint8_t hdrdbg_data[8][16];

    /* stats (IRQ side: 32-bit increments only) */
    volatile uint32_t st_urb_ok, st_urb_err, st_urb_zero, st_drop_slot;
    volatile uint32_t st_dup;       /* duplicate frames suppressed at publish */
    volatile uint32_t st_bytes, st_frames, st_frames_bad, st_hdr_err, st_resync;
    volatile int st_last_err;
    volatile uint32_t last_urb_tick; /* tick of the last completion (IRQ) */
    volatile uint32_t st_urb_total; /* total completions (IRQ) */
    volatile uint32_t st_miss;      /* ISO: microframes lost to late re-arms */
    volatile uint32_t st_cps;       /* completions in the last 1s window */
    uint32_t miss_win_base, miss_win_tick;
    volatile uint32_t st_wdt;       /* silent-wedge recoveries (watchdog) */
    volatile uint32_t st_reconnects;/* full session re-inits (port reset) */
    uint32_t wdt_streak;            /* recoveries without any completion */
    volatile int recovering;        /* watchdog kill in progress: the SHUTDOWN
                                     * completion that follows must not stop
                                     * the stream */
    /* stats (worker side) */
    uint32_t fps, fps_window_frames;
    uint64_t fps_window_tick;
    uint32_t last_good_len;
    uint32_t bytes_seen;           /* st_bytes snapshot for dead-pipe detect */
    uint32_t bytes_seen_tick;      /* when the snapshot last changed */
    uint32_t dead_pipes;           /* NAK-idle session rebuilds */
} g_uvct;

/* ==================== MJPEG recording (AVI/MJPG on SD) ==================== */

static void uvc_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* one '00dc' chunk per frame; called from the worker on intact frames */
static void uvc_rec_write_frame(const uint8_t *frame, uint32_t len)
{
    uint8_t hdr[8];

    if (g_uvct.rec_idx_cnt >= UVC_REC_IDX_MAX) {
        g_uvct.rec_skipped++;
        return;
    }

    long pos = disk_file_ftell(FS_SD, g_uvct.rec_fd);
    if (pos < 0) {
        goto err;
    }

    /* glued frames (several JPEGs concatenated by dropped EOF markers) pass
     * the SOI/EOI check but break decoders — reject any frame with an
     * embedded SOI past the start */
    for (uint32_t i = 4; (i + 2) < len; i++) {
        if (frame[i] == 0xFF && frame[i + 1] == 0xD8 &&
            frame[i + 2] == 0xFF) {
            g_uvct.rec_skipped++;
            return;
        }
    }

    hdr[0] = '0';
    hdr[1] = '0';
    hdr[2] = 'd';
    hdr[3] = 'c';
    uvc_put_le32(hdr + 4, len);

    if (disk_file_fwrite(FS_SD, g_uvct.rec_fd, hdr, 8) != 8 ||
        disk_file_fwrite(FS_SD, g_uvct.rec_fd, frame, len) != (int)len) {
        goto err;
    }
    if (len & 1) { /* RIFF chunks are word aligned */
        uint8_t pad = 0;
        if (disk_file_fwrite(FS_SD, g_uvct.rec_fd, &pad, 1) != 1) {
            goto err;
        }
    }

    /* idx1 offsets are relative to the position of the 'movi' fourcc */
    g_uvc_rec_idx_off[g_uvct.rec_idx_cnt] = (uint32_t)pos - g_uvct.rec_movi_fourcc_pos;
    g_uvc_rec_idx_len[g_uvct.rec_idx_cnt] = len;
    g_uvct.rec_idx_cnt++;
    g_uvct.rec_frames++;
    g_uvct.rec_bytes += len + 8 + (len & 1);
    if (len > g_uvct.rec_maxframe) {
        g_uvct.rec_maxframe = len;
    }
    return;
err:
    g_uvct.rec_error = 1;
    g_uvct.rec_active = 0;
}

/* IRQ context: hand the finished frame's buffer POINTER to the writer.
 * Zero copy — the worker snapshots the data into its own buffers well
 * inside the two-frame window before the IRQ reuses the assembly slot. */
static void uvc_rec_stage_frame(void)
{
    for (uint32_t i = 0; i < UVC_REC_STAGE_CNT; i++) {
        uint32_t slot = (g_rec_stage_ticket + i) & (UVC_REC_STAGE_CNT - 1);
        if (g_rec_stage_seq[slot] == 0) {
            g_rec_stage_ptr[slot] = g_uvct.frame_cur;
            g_rec_stage_len[slot] = g_uvct.frame_len;
            g_rec_stage_ticket++;
            g_rec_stage_seq[slot] = g_rec_stage_ticket;
            (void)osSemaphoreRelease(g_uvct.data_sem);
            return;
        }
    }
    g_uvct.rec_skipped++; /* writer behind: drop this frame */
}

/* worker context: write staged frames oldest-first */
static void uvc_rec_drain_stage(void)
{
    if (!g_uvct.rec_active) {
        return;
    }
    for (uint32_t round = 0; round < UVC_REC_STAGE_CNT; round++) {
        int pick = -1;
        for (uint32_t i = 0; i < UVC_REC_STAGE_CNT; i++) {
            if (g_rec_stage_seq[i] != 0 &&
                (pick < 0 || g_rec_stage_seq[i] < g_rec_stage_seq[pick])) {
                pick = (int)i;
            }
        }
        if (pick < 0) {
            break;
        }
        uint32_t n = g_rec_stage_len[pick];
        if (n > UVC_PREVIEW_BYTES) {
            n = UVC_PREVIEW_BYTES;
        }
        memcpy(g_uvc_rec_snap[pick], g_rec_stage_ptr[pick], n); /* 2-frame window */
        g_rec_stage_seq[pick] = 0; /* release the assembly slot first */
        uvc_rec_write_frame(g_uvc_rec_snap[pick], n);
    }
}

/* 224-byte AVI header: sizes are patched in uvc_rec_finalize() */
static int uvc_rec_write_header(void)
{
    static uint8_t hdr[224];
    uint32_t w = g_uvct.rec_w;
    uint32_t h = g_uvct.rec_h;

    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "RIFF", 4);
    uvc_put_le32(hdr + 4, 0);
    memcpy(hdr + 8, "AVI ", 4);
    memcpy(hdr + 12, "LIST", 4);
    uvc_put_le32(hdr + 16, 192);
    memcpy(hdr + 20, "hdrl", 4);
    memcpy(hdr + 24, "avih", 4);
    uvc_put_le32(hdr + 28, 56);
    uvc_put_le32(hdr + 32, g_uvct.rec_fps ? 1000000u / g_uvct.rec_fps : 33333u);
    uvc_put_le32(hdr + 36, 4000000);
    uvc_put_le32(hdr + 44, 0x10); /* AVIF_HASINDEX */
    uvc_put_le32(hdr + 48, 0);    /* dwTotalFrames (patch) */
    uvc_put_le32(hdr + 56, 1);    /* dwStreams */
    uvc_put_le32(hdr + 60, 0);    /* dwSuggestedBufferSize (patch) */
    uvc_put_le32(hdr + 64, w);
    uvc_put_le32(hdr + 68, h);
    memcpy(hdr + 88, "LIST", 4);
    uvc_put_le32(hdr + 92, 116);
    memcpy(hdr + 96, "strl", 4);
    memcpy(hdr + 100, "strh", 4);
    uvc_put_le32(hdr + 104, 56);
    memcpy(hdr + 108, "vids", 4);
    memcpy(hdr + 112, "MJPG", 4);
    uvc_put_le32(hdr + 128, 1);   /* dwScale */
    uvc_put_le32(hdr + 132, g_uvct.rec_fps ? g_uvct.rec_fps : 30); /* dwRate */
    uvc_put_le32(hdr + 140, 0);   /* dwLength (patch) */
    uvc_put_le32(hdr + 144, 0);   /* dwSuggestedBufferSize (patch) */
    uvc_put_le32(hdr + 148, 0xFFFFFFFF);
    hdr[160] = (uint8_t)w;        /* rcFrame right */
    hdr[161] = (uint8_t)(w >> 8);
    hdr[162] = (uint8_t)h;        /* rcFrame bottom */
    hdr[163] = (uint8_t)(h >> 8);
    memcpy(hdr + 164, "strf", 4);
    uvc_put_le32(hdr + 168, 40);
    uvc_put_le32(hdr + 172, 40);
    uvc_put_le32(hdr + 176, w);
    uvc_put_le32(hdr + 180, h);
    hdr[184] = 1;                 /* biPlanes */
    hdr[186] = 24;                /* biBitCount */
    memcpy(hdr + 188, "MJPG", 4);
    uvc_put_le32(hdr + 192, w * h * 3);
    memcpy(hdr + 212, "LIST", 4);
    uvc_put_le32(hdr + 216, 0);   /* movi list size (patch) */
    memcpy(hdr + 220, "movi", 4);

    return disk_file_fwrite(FS_SD, g_uvct.rec_fd, hdr, sizeof(hdr)) == (int)sizeof(hdr) ? 0 : -1;
}

static int uvc_rec_patch32(uint32_t file_off, uint32_t val)
{
    uint8_t b[4];

    if (disk_file_fseek(FS_SD, g_uvct.rec_fd, (long)file_off, SEEK_SET) != 0) {
        return -1;
    }
    uvc_put_le32(b, val);
    return disk_file_fwrite(FS_SD, g_uvct.rec_fd, b, 4) == 4 ? 0 : -1;
}

static void uvc_rec_finalize(uint32_t elapsed_ms)
{
    uint8_t e[16];
    uint32_t file_size = 224 + g_uvct.rec_bytes + 8 + 16u * g_uvct.rec_idx_cnt;
    /* real frame rate: when the SD writer drops frames (stream faster than
     * the card), patching the nominal fps would speed up playback */
    uint32_t real_fps = (elapsed_ms > 0 && g_uvct.rec_frames > 0)
                            ? (g_uvct.rec_frames * 1000u + elapsed_ms - 1) / elapsed_ms
                            : (g_uvct.rec_fps ? g_uvct.rec_fps : 30);

    (void)uvc_rec_patch32(4, file_size - 8);
    (void)uvc_rec_patch32(32, real_fps ? 1000000u / real_fps : 33333u);
    (void)uvc_rec_patch32(48, g_uvct.rec_frames);
    (void)uvc_rec_patch32(60, g_uvct.rec_maxframe + 8);
    (void)uvc_rec_patch32(132, real_fps ? real_fps : 30);
    (void)uvc_rec_patch32(140, g_uvct.rec_frames);
    (void)uvc_rec_patch32(144, g_uvct.rec_maxframe + 8);
    (void)uvc_rec_patch32(216, 4 + g_uvct.rec_bytes);

    if (disk_file_fseek(FS_SD, g_uvct.rec_fd, 0, SEEK_END) == 0) {
        memcpy(e, "idx1", 4);
        uvc_put_le32(e + 4, 16u * g_uvct.rec_idx_cnt);
        if (disk_file_fwrite(FS_SD, g_uvct.rec_fd, e, 8) == 8) {
            for (uint32_t i = 0; i < g_uvct.rec_idx_cnt; i++) {
                memcpy(e, "00dc", 4);
                uvc_put_le32(e + 4, 0x10); /* AVIIF_KEYFRAME */
                uvc_put_le32(e + 8, g_uvc_rec_idx_off[i]);
                uvc_put_le32(e + 12, g_uvc_rec_idx_len[i]);
                if (disk_file_fwrite(FS_SD, g_uvct.rec_fd, e, 16) != 16) {
                    break;
                }
            }
        }
    }

    (void)disk_file_fflush(FS_SD, g_uvct.rec_fd);
    (void)disk_file_fclose(FS_SD, g_uvct.rec_fd);
    g_uvct.rec_fd = NULL;
}

int usbh_uvc_test_record(uint32_t seconds, const char *filename)
{
    if (!g_uvct.streaming) {
        printf("[UVC] not streaming\r\n");
        return -1;
    }
    if (g_uvct.rec_active) {
        printf("[UVC] already recording\r\n");
        return -1;
    }

    g_uvct.rec_fd = disk_file_fopen(FS_SD, filename, "w");
    if (g_uvct.rec_fd == NULL) {
        printf("[UVC] cannot open %s on SD\r\n", filename);
        return -1;
    }

    g_uvct.rec_frames = 0;
    g_uvct.rec_skipped = 0;
    g_uvct.rec_bytes = 0;
    g_uvct.rec_maxframe = 0;
    g_uvct.rec_idx_cnt = 0;
    g_uvct.rec_error = 0;
    g_uvct.rec_movi_fourcc_pos = 220;
    g_rec_stage_ticket = 0;
    g_rec_stage_seq[0] = 0;
    g_rec_stage_seq[1] = 0;

    if (uvc_rec_write_header() != 0) {
        printf("[UVC] header write failed\r\n");
        (void)disk_file_fclose(FS_SD, g_uvct.rec_fd);
        g_uvct.rec_fd = NULL;
        return -1;
    }

    uint32_t rec_start_tick = osKernelGetTickCount();
    g_uvct.rec_end_tick = rec_start_tick + seconds * 1000u;
    g_uvct.rec_active = 1;
    printf("[UVC] recording %us -> SD:%s (%ux%u @ %ufps)\r\n",
           (unsigned)seconds, filename, g_uvct.rec_w, g_uvct.rec_h,
           (unsigned)(g_uvct.rec_fps ? g_uvct.rec_fps : 30));

    while ((osKernelGetTickCount() < g_uvct.rec_end_tick) && !g_uvct.rec_error) {
        osDelay(500);
    }
    g_uvct.rec_active = 0;
    osDelay(100); /* let an in-flight frame write drain */

    uint32_t elapsed_ms = osKernelGetTickCount() - rec_start_tick;
    uvc_rec_finalize(elapsed_ms);

    uint32_t mb_x100 = (uint32_t)((g_uvct.rec_bytes / 1048576ULL) * 100ULL +
                                  ((g_uvct.rec_bytes % 1048576ULL) * 100ULL) / 1048576ULL);
    uint32_t eff_fps = (elapsed_ms > 0 && g_uvct.rec_frames > 0)
                           ? g_uvct.rec_frames * 1000u / elapsed_ms
                           : 0;
    if (g_uvct.rec_error) {
        printf("[UVC] record ABORTED on write error\r\n");
    }
    printf("[UVC] record done: %u frames (%u skipped, idx cap %d), %u.%02u MB, max frame %u B, effective %ufps\r\n",
           (unsigned)g_uvct.rec_frames, (unsigned)g_uvct.rec_skipped, UVC_REC_IDX_MAX,
           (unsigned)(mb_x100 / 100), (unsigned)(mb_x100 % 100),
           (unsigned)g_uvct.rec_maxframe, (unsigned)eff_fps);
    return g_uvct.rec_error ? -1 : 0;
}

/* ==================== frame assembly (worker side) ==================== */

static void uvc_frame_append(const uint8_t *src, uint32_t len)
{
    /* subtraction form: immune to the len-underflow wrap that once turned a
     * bad (chunk - hlen) into a multi-GB memcpy and a HardFault */
    if (g_uvct.frame_len == 0) {
        /* pick the preview assembly slot; if the record writer is still
         * snapshotting it (later than 2 frame periods), drop this frame */
        uint32_t slot = g_uvc_prev_seq % UVC_PREV_SLOTS;
        if (g_rec_stage_seq[slot] != 0) {
            g_uvct.frame_bad = 1;
            g_uvct.frame_skip = 1;
        } else {
            g_uvct.frame_cur = g_uvc_prev_buf[slot];
        }
    }
    if (g_uvct.frame_skip) {
        return;
    }
    if (len <= UVC_PREVIEW_BYTES - g_uvct.frame_len) { /* actual slot size */
        memcpy(g_uvct.frame_cur + g_uvct.frame_len, src, len);
        g_uvct.frame_len += len;
    } else {
        g_uvct.frame_bad = 1;
    }
}

static void uvc_frame_finish(void)
{
    if (g_uvct.frame_len == 0) {
        return;
    }

    if (g_uvct.trace_fin > 0) {
        g_uvct.trace_fin--;
        printf("[UVC t] FINISH flen=%u bad=%d\r\n",
               (unsigned)g_uvct.frame_len, g_uvct.frame_bad);
    }

    /* some devices zero-pad the last payload after EOI; trimming from the
     * tail stops exactly on the EOI's 0xd9 and never touches JPEG data */
    while (g_uvct.frame_len > 0 && g_uvct.frame_cur[g_uvct.frame_len - 1] == 0x00) {
        g_uvct.frame_len--;
    }

    g_uvct.st_frames++;

    {
        uint64_t now = (uint64_t)osKernelGetTickCount();
        if ((now - g_uvct.fps_window_tick) >= 1000) {
            g_uvct.fps = g_uvct.fps_window_frames;
            g_uvct.fps_window_frames = 0;
            g_uvct.fps_window_tick = now;
        }
    }

    int ok = (g_uvct.frame_len >= 4) &&
             (g_uvct.frame_cur[0] == 0xFF) && (g_uvct.frame_cur[1] == 0xD8) &&
             (g_uvct.frame_cur[g_uvct.frame_len - 2] == 0xFF) && (g_uvct.frame_cur[g_uvct.frame_len - 1] == 0xD9);

    if (g_uvct.frame_bad || !ok) {
        g_uvct.st_frames_bad++;
    } else {
        g_uvct.last_good_len = g_uvct.frame_len;

        /* duplicate-frame suppression at the publish boundary: some
         * cameras resend the previous encoded frame when nothing changed
         * (observed 2x per frame at 320x240, doubling preview bandwidth).
         * Content signature = length + sparse byte samples; a genuine
         * re-encode always differs in bytes, identical bytes = identical
         * picture for preview purposes. Duplicates are neither published
         * nor counted in fps - the badge stays honest. */
        uint32_t sig = g_uvct.frame_len;
        if (g_uvct.frame_len >= 32) {
            uint32_t stride = g_uvct.frame_len / 8u;
            for (uint32_t i = 0; i < g_uvct.frame_len; i += stride) {
                sig = (sig * 33u) + g_uvct.frame_cur[i];
            }
        }
        int dup = (sig == s_prev_pub_sig);
        s_prev_pub_sig = sig;

        if (dup) {
            g_uvct.st_dup++;
        } else {
            g_uvct.fps_window_frames++; /* unique-frame rate for the badge */
        }
        if (!dup && g_uvct.frame_len <= UVC_PREVIEW_BYTES) {
            /* publish to the web preview store. ISO assembles DIRECTLY in
             * the preview double buffer, so publishing is a zero-copy seq
             * bump (a memcpy here used to stall the channel re-arm ~300us
             * and lose microframes). Bulk still copies in worker context. */
            g_uvc_prev_len[g_uvc_prev_seq % UVC_PREV_SLOTS] = g_uvct.frame_len;
            g_uvc_prev_seq++;
        }
    }

    if (g_uvct.rec_active) {
        if (ok && !g_uvct.frame_bad) {
            uvc_rec_stage_frame(); /* IRQ ctx: hand the buffer to the writer */
        } else {
            g_uvct.rec_skipped++;
        }
    }

    /* capture ANY finished frame (good or bad) for protocol diagnosis */
    if (g_uvct.capture_pend) {
        uint32_t n = g_uvct.frame_len;
        if (n > UVC_CAPTURE_BYTES) {
            n = UVC_CAPTURE_BYTES;
        }
        memcpy(g_uvc_capture_buf, g_uvct.frame_cur, n);
        g_uvct.capture_len = n;
        g_uvct.capture_bad = (g_uvct.frame_bad || !ok);
        g_uvct.capture_pend = 0;
        (void)osSemaphoreRelease(g_uvct.capture_sem);
    }

    g_uvct.frame_len = 0;
    g_uvct.frame_bad = 0;
    g_uvct.frame_skip = 0;
    g_uvct.have_fid = 0;
}


/*
 * Bulk transport: payloads may be far larger than one URB (measured camera:
 * 102656 B data payload + 4876 B tail payload + a 12-byte header-only EOF
 * payload per frame). The stream is therefore read in UVC_SLOT_BYTES chunks
 * and framing is exact: a bulk device terminates every payload with a short
 * packet; the chunk read completes short iff a payload ended inside it. A
 * payload ending EXACTLY at the chunk size is revealed by its mandatory ZLP
 * (zero-length completion) on the next URB — which is why the chunk size
 * must stay a multiple of 512. State: bulk_at_header says whether the next
 * chunk starts with a payload header or is pure payload continuation.
 *
 * Frame delimiting: the EOF header bit is the ONLY finish trigger. FID is
 * deliberately NOT used to cut frames: the measured camera toggles FID per
 * PAYLOAD instead of per frame (spec violation), which would shred frames
 * mid-stream. On a header desync the chunk is swallowed as data and the
 * next short packet re-establishes a true payload boundary, so sync
 * provably reconverges (at most one tainted frame).
 */
static void uvc_process_bulk(const uint8_t *buf, uint32_t len)
{
    int short_xfer = (len < g_uvct.urb_slot_bytes);

    if (g_uvct.bulk_at_header) {
        if (len < 2) {
            return; /* ZLP while expecting a header: keep waiting */
        }

        uint32_t hlen = buf[0];
        uint8_t info = buf[1];

        if (hlen < 2 || hlen > 12 || hlen > len) {
            /* desynced at_header guess: swallow as data, taint the frame,
             * resync at the next short packet (payload ends are always
             * short-marked on bulk; real UVC headers are 2..12 bytes) */
            g_uvct.st_hdr_err++;
            g_uvct.frame_bad = 1;
            g_uvct.bulk_at_header = short_xfer;
            if (len) {
                uvc_frame_append(buf, len);
            }
            return;
        }

        if (len > hlen) {
            uvc_frame_append(buf + hlen, len - hlen);
        }

        /* short chunk = payload ended here -> the next chunk starts with a
         * new payload header; full chunk = payload continues (no header) */
        g_uvct.bulk_at_header = short_xfer;

        if (info & UVC_HDR_EOF) {
            uvc_frame_finish();
        }
    } else {
        if (len) {
            uvc_frame_append(buf, len);
        }
        if (short_xfer || len == 0) {
            /* payload ended (short packet), or its trailing ZLP arrived */
            g_uvct.bulk_at_header = 1;
        }
    }
}

/* ISO walker — runs in the completion IRQ. Measured camera format (this
 * device does NOT repeat the payload header in continuation packets, so a
 * 1024-stride grid parse desyncs on payloads > 1012 B):
 *   - one payload per microframe, header at the slot start, payload data
 *     (possibly spanning several raw 1024B continuation packets) runs to
 *     the slot end;
 *   - after a whole 1024B packet the camera pads the microframe with a
 *     short ALL-ZERO packet (no header) — trimmed back to the packet edge;
 *   - FID toggles per frame, EOF marks the last payload. */
static void uvc_process_iso(const uint8_t *buf, uint32_t len)
{
    if (g_uvct.iso_scan_left > 0) {
        g_uvct.iso_scan_left--;
        for (uint32_t off = 1024; (off + 1) < len; off += 1024) {
            g_uvct.iso_scan_bound++;
            uint32_t hl = buf[off];
            uint8_t inf = buf[off + 1];
            if (hl >= 2 && hl <= 12 && (inf & 0xE0) == 0) {
                g_uvct.iso_scan_hdr++;
            }
        }
        if (g_uvct.iso_scan_left == 0 && !g_uvct.iso_scan_done) {
            g_uvct.iso_scan_done = 1;
            if (g_uvct.iso_scan_sem) {
                (void)osSemaphoreRelease(g_uvct.iso_scan_sem);
            }
        }
    }

    if (len < 2) {
        return;
    }

    uint32_t hlen = buf[0];

    if (hlen < 2 || hlen > 12 || hlen > len) {
        /* not a header: desync or garbage — drop the slot, taint the frame */
        {
            uint8_t k = g_uvct.hdrdbg_cnt & 7;
            g_uvct.hdrdbg_len[k] = len;
            g_uvct.hdrdbg_pos[k] = 0;
            memcpy(g_uvct.hdrdbg_data[k], buf, (len >= 16) ? 16 : len);
            g_uvct.hdrdbg_cnt++;
        }
        g_uvct.st_hdr_err++;
        g_uvct.frame_bad = 1;
        return;
    }

    uint8_t info = buf[1];
    int fid = info & UVC_HDR_FID;
    if (!g_uvct.have_fid) {
        g_uvct.have_fid = 1;
        g_uvct.cur_fid = fid;
    } else if (fid != g_uvct.cur_fid) {
        uvc_frame_finish();
        g_uvct.cur_fid = fid;
    }

    /* Payload runs to the slot end. The camera terminates under-filled
     * microframes with an ALL-ZERO short packet ("no data" marker, 12-76B
     * seen); those zeros are NOT payload data. A trailing zero run of >=12
     * bytes is padding (real JPEG entropy hitting 12 consecutive zero bytes
     * is vanishingly unlikely) — trim it wherever the payload ended, not
     * only at 1024B packet boundaries. */
    uint32_t pay_end = len;
    {
        uint32_t q = len;
        while (q > hlen && buf[q - 1] == 0x00) {
            q--;
        }
        if ((len - q) >= 12) {
            pay_end = q;
        }
    }

    if (pay_end > hlen) {
        uvc_frame_append(buf + hlen, pay_end - hlen);
    }

    if (info & UVC_HDR_EOF) {
        uvc_frame_finish();
    }
}


/* bulk ring drain (worker thread); ISO parses directly in the IRQ */
static void uvc_process_slot(const uint8_t *buf, uint32_t len)
{
    uvc_process_bulk(buf, len);
}

static uint8_t *uvc_slot_ptr(uint32_t idx)
{
    return g_uvc_slot_mem + (size_t)idx * g_uvct.slot_stride;
}

static void uvc_submit_slot(uint8_t *buf);

static void uvc_worker(void *arg)
{
    (void)arg;

    while (g_uvct.worker_run) {
        (void)osSemaphoreAcquire(g_uvct.data_sem, 100);

        /* deferred re-arm (bulk NAK / idle): paced from thread context, the
         * bulk pipe is flow controlled so lateness costs no data */
        if (g_uvct.rearm_pending && g_uvct.streaming) {
            g_uvct.rearm_pending = 0;
            osDelay(1);
            uvc_submit_slot(g_uvct.urb.transfer_buffer);
            if (!g_uvct.streaming) {
                continue;
            }
        }

        /* silent-wedge watchdog: a parked URB with NO completion for >1.5s
         * means the channel died without raising an interrupt (observed
         * once after `usb device init` — root cause pending). Kill and
         * resubmit: converts the wedge into one recoverable glitch. */
        if (g_uvct.streaming && g_uvct.urb.hcpriv != NULL &&
            (osKernelGetTickCount() - g_uvct.last_urb_tick) > 1500) {
            g_uvct.st_wdt++;
            if (g_uvct.st_wdt <= 3) { /* register dump on the first events */
                volatile uint32_t *r = (volatile uint32_t *)USB2_OTG_HS_BASE;
                printf("[UVC] wdt wedge: GINTSTS=%08x GINTMSK=%08x GAHBCFG=%08x HAINT=%08x HAINTMSK=%08x HPRT=%08x\r\n",
                       (unsigned)r[0x014 / 4], (unsigned)r[0x018 / 4], (unsigned)r[0x008 / 4],
                       (unsigned)r[0x414 / 4], (unsigned)r[0x418 / 4], (unsigned)r[0x440 / 4]);
                for (int ch = 0; ch < 16; ch++) {
                    volatile uint32_t *hc = (volatile uint32_t *)(USB2_OTG_HS_BASE + 0x500 + ch * 0x20);
                    if ((hc[0] & 0xC0000000U) || hc[2]) { /* CHENA/CHDIS or pending int */
                        printf("[UVC]  ch%d: HCCHAR=%08x HCINT=%08x HCINTMSK=%08x HCTSIZ=%08x\r\n",
                               ch, (unsigned)hc[0], (unsigned)hc[2], (unsigned)hc[3], (unsigned)hc[4]);
                    }
                }
            }
            printf("[UVC] wdt: silent wedge (%ums), recovering\r\n",
                   (unsigned)(osKernelGetTickCount() - g_uvct.last_urb_tick));
            g_uvct.recovering = 1;
            usbh_kill_urb(&g_uvct.urb);
            g_uvct.recovering = 0;
            osDelay(2);
            uvc_submit_slot(g_uvct.urb.transfer_buffer);

            /* repeated useless recoveries = the device went away behind our
             * back (port reset re-enumerated it, e.g. USB1 cold init
             * glitching the USB2 port). Rebuild the whole session. */
            if (++g_uvct.wdt_streak >= 4 && g_uvct.video != NULL) {
                printf("[UVC] wdt: %u dead recoveries, re-initializing session\r\n",
                       (unsigned)g_uvct.wdt_streak);
                g_uvct.st_reconnects++;
                uint16_t w = g_uvct.rec_w, h = g_uvct.rec_h;
                g_uvct.streaming = 0; /* inline stop: keep THIS worker alive */
                if (g_uvct.urb.hcpriv != NULL) {
                    g_uvct.recovering = 1;
                    usbh_kill_urb(&g_uvct.urb);
                    g_uvct.recovering = 0;
                }
                (void)usbh_video_close(g_uvct.video);
                osDelay(100); /* let the hub settle after the port reset */
                if (usbh_uvc_test_open(w, h, 0xff) == 0) {
                    (void)usbh_uvc_test_start(); /* reuses this worker */
                }
                g_uvct.wdt_streak = 0;
            }
        }

        /* dead-pipe watchdog (bulk): NAK completions keep last_urb_tick
         * fresh, so the silent-wedge watchdog above never fires when the
         * camera simply never delivers (zero commit / wedged firmware):
         * the session looks "streaming" while zero payload bytes arrive.
         * A bulk camera at 30fps always sends; 3s of no bytes = rebuild. */
        if (g_uvct.streaming && g_uvct.is_bulk) {
            if (g_uvct.st_bytes != g_uvct.bytes_seen) {
                g_uvct.bytes_seen = g_uvct.st_bytes;
                g_uvct.bytes_seen_tick = osKernelGetTickCount();
            } else if (g_uvct.bytes_seen_tick != 0 &&
                       (osKernelGetTickCount() - g_uvct.bytes_seen_tick) > 3000 &&
                       g_uvct.video != NULL) {
                printf("[UVC] dead pipe: 0 payload bytes for %ums, rebuilding session\r\n",
                       (unsigned)(osKernelGetTickCount() - g_uvct.bytes_seen_tick));
                g_uvct.dead_pipes++;
                g_uvct.st_reconnects++;
                uint16_t w = g_uvct.rec_w, h = g_uvct.rec_h;
                g_uvct.streaming = 0; /* inline stop: keep THIS worker alive */
                if (g_uvct.urb.hcpriv != NULL) {
                    g_uvct.recovering = 1;
                    usbh_kill_urb(&g_uvct.urb);
                    g_uvct.recovering = 0;
                }
                (void)usbh_video_close(g_uvct.video);
                osDelay(100); /* let the hub settle after the port reset */
                if (usbh_uvc_test_open(w, h, 0xff) == 0) {
                    (void)usbh_uvc_test_start(); /* reuses this worker */
                }
                g_uvct.bytes_seen_tick = osKernelGetTickCount();
                g_uvct.wdt_streak = 0;
            }
        }

        uvc_rec_drain_stage();
    }
}

/* ==================== IRQ side: URB completion ==================== */

static void uvc_submit_slot(uint8_t *buf)
{
    g_uvct.urb.transfer_buffer = buf;
    g_uvct.urb.transfer_buffer_length = g_uvct.urb_slot_bytes;
    g_uvct.urb.num_of_iso_packets = g_uvct.is_bulk ? 0 : UVC_ISO_INTERVALS;
    g_uvct.urb.actual_length = 0;
    g_uvct.urb.errorcode = 0;

    int ret = usbh_submit_urb(&g_uvct.urb);
    if (ret < 0) {
        /* could not re-arm (device gone / channel exhausted): stop */
        g_uvct.streaming = 0;
        g_uvct.st_last_err = -ret;
        g_uvct.st_urb_err++;
    }
}

static void uvc_urb_complete(void *arg, int nbytes)
{
    (void)arg;

    /* completion-rate watchdog (ISO): the channel completes EVERY service
     * interval; a 1s window below ~8000 completions means microframes were
     * lost to late re-arms (IRQ blocked >125us) -> mid-frame data loss */
    {
        uint32_t now = osKernelGetTickCount();
        g_uvct.st_urb_total++;
        if (!g_uvct.is_bulk) {
            if ((now - g_uvct.miss_win_tick) >= 1000) {
                g_uvct.st_cps = g_uvct.st_urb_total - g_uvct.miss_win_base;
                if (g_uvct.miss_win_tick != 0 && g_uvct.st_cps < 7900u) {
                    g_uvct.st_miss += 8000u - g_uvct.st_cps;
                }
                g_uvct.miss_win_base = g_uvct.st_urb_total;
                g_uvct.miss_win_tick = now;
            }
        }
    }

    if (!g_uvct.streaming) {
        return; /* stop path: let the URB die */
    }

    if (nbytes < 0) {
        g_uvct.st_urb_err++;
        g_uvct.st_last_err = -nbytes;
        g_uvct.last_urb_tick = osKernelGetTickCount();
        if (nbytes == -USB_ERR_SHUTDOWN && g_uvct.recovering) {
            return; /* watchdog kill: the worker resubmits right after —
                     * NOT a real completion: keep the wdt streak */
        }
        if (nbytes == -USB_ERR_NOTCONN || nbytes == -USB_ERR_SHUTDOWN) {
            g_uvct.streaming = 0;
            return;
        }
        if (nbytes == -USB_ERR_NAK) {
            /* bulk idle: dwc2 buffer-dma error-completes NAKed IN polls; do
             * not spin INs from the IRQ — let the worker re-arm after 1ms */
            g_uvct.rearm_pending = 1;
            (void)osSemaphoreRelease(g_uvct.data_sem);
            return;
        }
        /* transient (FRMOR/XACT and friends): retry with the same slot */
        uvc_submit_slot(g_uvct.urb.transfer_buffer);
        return;
    }

    g_uvct.wdt_streak = 0; /* a real completion arrived: session is alive */

    if (nbytes == 0) {
        g_uvct.st_urb_zero++;
        g_uvct.last_urb_tick = osKernelGetTickCount();
    } else {
        g_uvct.st_urb_ok++;
        g_uvct.st_bytes += (uint32_t)nbytes;
        g_uvct.last_urb_tick = osKernelGetTickCount();
    }

    /* parse directly in the IRQ (both transports): the stream cadence
     * must not depend on thread scheduling — the web task at Realtime
     * starved a worker-based bulk ring under load (torn frames). */
    {
        uint8_t *buf = g_uvct.urb.transfer_buffer;

        if (g_uvct.raw_pend && g_uvct.raw_snap_cnt < 3) {
            uint32_t k = g_uvct.raw_snap_cnt;
            g_uvct.raw_snap_len[k] = (uint32_t)nbytes;
            memcpy(g_uvct.raw_snap[k], buf, ((uint32_t)nbytes < 64) ? (uint32_t)nbytes : 64);
            g_uvct.raw_snap_cnt++;
            if (g_uvct.raw_snap_cnt == 3 && g_uvct.raw_sem) {
                (void)osSemaphoreRelease(g_uvct.raw_sem);
            }
        }

        if (nbytes >= 2) {
            if (g_uvct.is_bulk) {
                uvc_process_bulk(buf, (uint32_t)nbytes);
            } else {
                uvc_process_iso(buf, (uint32_t)nbytes);
            }
        }
        uvc_submit_slot(buf); /* same buffer: parsing finished synchronously */
        return;
    }

    uint32_t idx = g_uvct.prod_tail % g_uvct.slot_count;
    g_uvct.slot_len[idx] = (uint32_t)nbytes;
    g_uvct.prod_tail++;

    /* every completion produces a slot, zero-length ones included (bulk ZLP
     * = payload boundary marker). If the worker is a full ring behind, the
     * produce overwrites the oldest pending slot: framing is tainted and
     * counted; the stream keeps running. */
    if ((g_uvct.prod_tail - g_uvct.cons_head) > g_uvct.slot_count) {
        g_uvct.st_drop_slot++;
    }

    (void)osSemaphoreRelease(g_uvct.data_sem);

    /* re-arm inside this IRQ so the next (micro)frame is still serviced */
    uvc_submit_slot(uvc_slot_ptr(g_uvct.prod_tail % g_uvct.slot_count));
}

/* ==================== class driver hooks ==================== */

void usbh_video_run(struct usbh_video *video_class)
{
    g_uvct.video = video_class;
    printf("[UVC] /dev/video%d registered, try: usb host uvc info\r\n",
           video_class->minor);
}

void usbh_video_stop(struct usbh_video *video_class)
{
    (void)video_class;
    if (g_uvct.streaming) {
        usbh_uvc_test_stop();
    }
    g_uvct.video = NULL;
    printf("[UVC] video device removed\r\n");
}

/* ==================== CLI operations ==================== */

int usbh_uvc_test_info(void)
{
    if (g_uvct.video == NULL) {
        printf("[UVC] no video device (plug camera + usb host init)\r\n");
        return -1;
    }
    usbh_video_list_info(g_uvct.video);
    return 0;
}

int usbh_uvc_test_open(uint16_t width, uint16_t height, uint8_t alt)
{
    if (g_uvct.video == NULL) {
        printf("[UVC] no video device\r\n");
        return -1;
    }
    if (g_uvct.streaming) {
        printf("[UVC] streaming, stop first\r\n");
        return -1;
    }

    struct usbh_video *v = g_uvct.video;
    if (alt == 0xff) {
        alt = (uint8_t)(v->num_of_intf_altsettings - 1); /* max bandwidth */
    }

    int ret = usbh_video_open(v, USBH_VIDEO_FORMAT_MJPEG, width, height, alt);
    if (ret < 0) {
        printf("[UVC] open %ux%u alt%u failed: %d\r\n", width, height, alt, ret);
        return ret;
    }

    if (v->is_bulk) {
        /* Wake-enumeration quirk: the first GET_CUR probe right after
         * enumeration can come back all-zero (observed ~200ms after the
         * port came up). Committing zeros arms nothing on the camera -
         * the pipe then NAKs forever while everything looks "streaming".
         * Re-read the probe until it is sane before committing. */
        for (int i = 0; i < 10 &&
                       (v->probe.dwMaxPayloadTransferSize == 0 ||
                        v->probe.dwFrameInterval == 0); i++) {
            osDelay(30);
            (void)usbh_videostreaming_get_cur_probe(v);
        }
        if (v->probe.dwMaxPayloadTransferSize == 0 ||
            v->probe.dwFrameInterval == 0) {
            /* refuse to commit zeros: fail so the caller retries the open
             * (a fresh usbh_video_open re-runs the whole negotiation) */
            printf("[UVC] open: probe stays zero after retries\r\n");
            return -1;
        }

        /* Bulk UVC: dwMaxPayloadTransferSize is nominally host-chosen. Ask
         * for the slot size; many devices (this one: 102656 B ≈ whole MJPEG
         * frames per payload) ignore it — read-back decides single-payload
         * vs chained-chunk streaming, both are supported. */
        const uint32_t cap = UVC_BULK_CHUNK_BYTES - 512;

        if (v->probe.dwMaxPayloadTransferSize > cap) {
            v->probe.dwMaxPayloadTransferSize = cap;
            ret = usbh_video_set(v, VIDEO_REQUEST_SET_CUR, v->data_intf, 0,
                                 VIDEO_VS_PROBE_CONTROL, (uint8_t *)&v->probe, 26);
            if (ret == 0) {
                ret = usbh_video_get(v, VIDEO_REQUEST_GET_CUR, v->data_intf, 0,
                                     VIDEO_VS_PROBE_CONTROL, (uint8_t *)&v->probe, 26);
            }
            if (ret == 0) {
                memcpy(&v->commit, &v->probe, sizeof(v->commit));
                ret = usbh_video_set(v, VIDEO_REQUEST_SET_CUR, v->data_intf, 0,
                                     VIDEO_VS_COMMIT_CONTROL, (uint8_t *)&v->commit, 26);
            }
            if (ret < 0) {
                printf("[UVC] dwMax renegotiation failed (%d), using device default\r\n", ret);
                /* fall back to the value the device last reported */
                (void)usbh_videostreaming_get_cur_probe(v);
            }
        }

        g_uvct.is_bulk = 1;
        g_uvct.bulk_payload_max = v->probe.dwMaxPayloadTransferSize;
        g_uvct.rec_w = width;
        g_uvct.rec_h = height;
        g_uvc_prev_w = width;
        g_uvc_prev_h = height;
        g_uvct.rec_fps = v->probe.dwFrameInterval ? 10000000u / v->probe.dwFrameInterval : 0;
        printf("[UVC] opened %ux%u BULK, ep %02x mps %u, payload max %u B (%s)\r\n",
               width, height, v->bulkin->bEndpointAddress,
               USB_GET_MAXPACKETSIZE(v->bulkin->wMaxPacketSize),
               (unsigned)g_uvct.bulk_payload_max,
               (g_uvct.bulk_payload_max > cap) ? "chained reads" : "one payload per urb");
        return 0;
    }

    g_uvct.is_bulk = 0;
    g_uvct.rec_w = width;
    g_uvct.rec_h = height;
    g_uvc_prev_w = width;
    g_uvc_prev_h = height;
    g_uvct.rec_fps = v->probe.dwFrameInterval ? 10000000u / v->probe.dwFrameInterval : 0;
    g_uvct.pkt_stride = USB_GET_MAXPACKETSIZE(v->isoin->wMaxPacketSize);
    printf("[UVC] opened %ux%u alt%u, iso ep %02x, mps %u, per-interval %u B, "
           "dwMaxPayloadTransferSize %u, service interval %u us\r\n",
           width, height, alt, v->isoin->bEndpointAddress, g_uvct.pkt_stride,
           v->isoin_mps, (unsigned)v->probe.dwMaxPayloadTransferSize,
           (v->hport->speed == USB_SPEED_HIGH) ? 125 : 1000);
    return 0;
}

int usbh_uvc_test_start(void)
{
    if (g_uvct.video == NULL || !g_uvct.video->is_opened) {
        printf("[UVC] device not opened (usb host uvc open w h [alt])\r\n");
        return -1;
    }
    if (g_uvct.streaming) {
        printf("[UVC] already streaming\r\n");
        return 0;
    }

    memset(&g_uvct.urb, 0, sizeof(g_uvct.urb));
    g_uvct.urb.hport = g_uvct.video->hport;
    g_uvct.urb.ep = g_uvct.is_bulk ? g_uvct.video->bulkin : g_uvct.video->isoin;
    g_uvct.urb.timeout = 0;
    g_uvct.urb.complete = uvc_urb_complete;
    g_uvct.urb.arg = NULL;

    if (g_uvct.is_bulk) {
        /* chained reads: chunk size must stay a multiple of 512 so a payload
         * ending exactly at the chunk boundary is revealed by its ZLP */
        g_uvct.urb_slot_bytes = UVC_BULK_CHUNK_BYTES;
        g_uvct.bulk_at_header = 1;
        g_uvct.slot_stride = UVC_BULK_CHUNK_BYTES;
        g_uvct.slot_count = UVC_SLOT_MEM_BYTES / UVC_BULK_CHUNK_BYTES;
    } else {
        g_uvct.urb_slot_bytes = (uint32_t)UVC_ISO_INTERVALS * g_uvct.video->isoin_mps;
        /* split the same uncached block into small sub-slots: at 8000
         * completions/s the ring depth IS the worker-stall tolerance */
        g_uvct.slot_stride = g_uvct.video->isoin_mps;
        uint32_t n = UVC_SLOT_MEM_BYTES / g_uvct.slot_stride;
        g_uvct.slot_count = (n > 16) ? 16 : n;
    }
    g_uvct.prod_tail = 0;
    g_uvct.cons_head = 0;
    g_uvct.rearm_pending = 0;
    g_uvct.frame_len = 0;
    g_uvct.frame_cur = g_uvc_prev_buf[g_uvc_prev_seq % UVC_PREV_SLOTS];
    g_uvct.frame_skip = 0;
    g_uvct.frame_bad = 0;
    g_uvct.have_fid = 0;
    g_uvct.st_urb_ok = g_uvct.st_urb_err = g_uvct.st_urb_zero = 0;
    g_uvct.st_drop_slot = g_uvct.st_bytes = g_uvct.st_frames = 0;
    g_uvct.bytes_seen = 0;
    g_uvct.bytes_seen_tick = osKernelGetTickCount(); /* dead-pipe window */
    g_uvct.st_frames_bad = g_uvct.st_hdr_err = g_uvct.st_resync = 0;
    g_uvct.st_last_err = 0;
    g_uvct.st_urb_total = 0;
    g_uvct.st_miss = 0;
    g_uvct.st_cps = 0;
    g_uvct.st_wdt = 0;
    g_uvct.wdt_streak = 0;
    g_uvct.recovering = 0;
    g_uvct.miss_win_base = 0;
    g_uvct.miss_win_tick = 0;
    g_uvct.last_urb_tick = osKernelGetTickCount(); /* wdt baseline */
    g_uvct.fps = g_uvct.fps_window_frames = 0;
    g_uvct.fps_window_tick = (uint64_t)osKernelGetTickCount();

    if (g_uvct.data_sem == NULL) {
        g_uvct.data_sem = osSemaphoreNew(g_uvct.slot_count, 0, NULL);
    }
    if (g_uvct.capture_sem == NULL) {
        g_uvct.capture_sem = osSemaphoreNew(1, 0, NULL);
    }
    if (g_uvct.data_sem == NULL || g_uvct.capture_sem == NULL) {
        printf("[UVC] osal alloc failed\r\n");
        return -1;
    }

    if (g_uvct.worker == NULL) { /* watchdog reconnect reuses this thread */
        static const osThreadAttr_t attr = {
            .name = "uvc_rx",
            /* Realtime (=8, same band as web/camera/wifi tasks). The ISO
             * path completes ~8000 URBs/s and the 1.25ms slot ring cannot
             * tolerate the priority-3..9 bursts that starved the worker at
             * AboveNormal (=16). Total worker demand is a few % CPU, so it
             * cannot starve the IWDG feeder (unlike a sustained Realtime
             * bulk sender). */
            .priority = (osPriority_t)osPriorityRealtime,
            .stack_size = 3072,
        };
        g_uvct.worker_run = 1;
        g_uvct.worker = osThreadNew(uvc_worker, NULL, &attr);
        if (g_uvct.worker == NULL) {
            printf("[UVC] worker create failed\r\n");
            g_uvct.worker_run = 0;
            return -1;
        }
    }

    g_uvct.streaming = 1;
    uvc_submit_slot(uvc_slot_ptr(0));
    if (!g_uvct.streaming) {
        printf("[UVC] first submit failed: err %d\r\n", g_uvct.st_last_err);
        g_uvct.worker_run = 0;
        return -1;
    }

    if (g_uvct.is_bulk) {
        printf("[UVC] streaming (bulk): %u B/urb payload reads, %d slots\r\n",
               (unsigned)g_uvct.urb_slot_bytes, (int)g_uvct.slot_count);
    } else {
        printf("[UVC] streaming (iso): %u B/urb x %d intervals, %d slots\r\n",
               (unsigned)g_uvct.urb_slot_bytes, UVC_ISO_INTERVALS, (int)g_uvct.slot_count);
    }
    return 0;
}

int usbh_uvc_test_stop(void)
{
    if (!g_uvct.streaming) {
        return 0;
    }

    g_uvct.streaming = 0; /* IRQ callback stops resubmitting */

    /* an in-flight URB may still be parked on the channel: force it out */
    if (g_uvct.urb.hcpriv != NULL) {
        usbh_kill_urb(&g_uvct.urb);
    }

    if (g_uvct.worker != NULL) {
        g_uvct.worker_run = 0;
        (void)osSemaphoreRelease(g_uvct.data_sem);
        osDelay(50);
        (void)osThreadTerminate(g_uvct.worker);
        g_uvct.worker = NULL;
    }

    printf("[UVC] stream stopped\r\n");
    return 0;
}

int usbh_uvc_test_close(void)
{
    (void)usbh_uvc_test_stop();
    if (g_uvct.video == NULL) {
        return -1;
    }
    if (g_uvct.video->is_opened) {
        int ret = usbh_video_close(g_uvct.video);
        if (ret < 0) {
            printf("[UVC] close failed: %d\r\n", ret);
            return ret;
        }
    }
    printf("[UVC] closed (interface alt 0)\r\n");
    return 0;
}

int usbh_uvc_test_stat(void)
{
    uint32_t mb_x100 = (uint32_t)((g_uvct.st_bytes / 1048576ULL) * 100ULL +
                                  ((g_uvct.st_bytes % 1048576ULL) * 100ULL) / 1048576ULL);
    uint32_t age = osKernelGetTickCount() - g_uvct.last_urb_tick;
    printf("[UVC] dev:%s stream:%s frames:%u bad:%u fps:%u last:%u B urb_age:%ums\r\n",
           g_uvct.video ? "present" : "none",
           g_uvct.streaming ? "on" : "off",
           (unsigned)g_uvct.st_frames, (unsigned)g_uvct.st_frames_bad,
           (unsigned)g_uvct.fps, (unsigned)g_uvct.last_good_len, (unsigned)age);
    printf("[UVC] data:%u.%02u MB urb ok:%u err:%u zero:%u dropslot:%u hdrerr:%u resync:%u lasterr:%d cps:%u miss:%u\r\n",
           (unsigned)(mb_x100 / 100), (unsigned)(mb_x100 % 100),
           (unsigned)g_uvct.st_urb_ok, (unsigned)g_uvct.st_urb_err, (unsigned)g_uvct.st_urb_zero,
           (unsigned)g_uvct.st_drop_slot, (unsigned)g_uvct.st_hdr_err,
           (unsigned)g_uvct.st_resync, g_uvct.st_last_err,
           (unsigned)g_uvct.st_cps, (unsigned)g_uvct.st_miss);
    printf("[UVC] wdt recoveries:%u\r\n", (unsigned)g_uvct.st_wdt);
    return 0;
}

int usbh_uvc_test_capture(uint32_t timeout_ms)
{
    if (!g_uvct.streaming) {
        printf("[UVC] not streaming\r\n");
        return -1;
    }
    if (g_uvct.capture_sem == NULL) {
        return -1;
    }
    (void)osSemaphoreAcquire(g_uvct.capture_sem, 0); /* drain stale token */
    g_uvct.capture_len = 0;
    g_uvct.capture_pend = 1;

    if (osSemaphoreAcquire(g_uvct.capture_sem, timeout_ms) != osOK) {
        g_uvct.capture_pend = 0;
        printf("[UVC] capture timeout (no intact frame in %u ms)\r\n", (unsigned)timeout_ms);
        return -1;
    }

    const uint8_t *h = g_uvc_capture_buf;
    uint32_t n = g_uvct.capture_len;
    printf("[UVC] captured %u B frame (%s) head:%02x %02x %02x %02x %02x %02x %02x %02x tail:%02x %02x %02x %02x\r\n",
           (unsigned)n, g_uvct.capture_bad ? "BAD" : "ok",
           h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7],
           h[(n > 4) ? n - 4 : 0], h[(n > 3) ? n - 3 : 0],
           h[(n > 2) ? n - 2 : 0], h[(n > 1) ? n - 1 : 0]);

    /* marker census: classifies the stream (JPEG markers vs H.264 NALs vs
     * unstructured entropy) and reveals framing rotation */
    {
        uint32_t c_soi = 0, c_eoi = 0, c_nal = 0, c_dqt = 0, c_dht = 0, c_sof = 0;
        uint32_t first_soi = 0, first_eoi = 0, first_nal = 0;
        for (uint32_t i = 0; (i + 1) < n; i++) {
            if (h[i] != 0xFF) {
                continue;
            }
            if (h[i + 1] == 0xD8) {
                if (c_soi++ == 0) first_soi = i;
            } else if (h[i + 1] == 0xD9) {
                if (c_eoi++ == 0) first_eoi = i;
            } else if (h[i + 1] == 0xDB) {
                c_dqt++;
            } else if (h[i + 1] == 0xC4) {
                c_dht++;
            } else if (h[i + 1] == 0xC0 || h[i + 1] == 0xC2) {
                c_sof++;
            }
        }
        for (uint32_t i = 0; (i + 3) < n; i++) {
            if (h[i] == 0 && h[i + 1] == 0 && h[i + 2] == 0 && h[i + 3] == 1) {
                if (c_nal++ == 0) first_nal = i;
            }
        }
        printf("[UVC] markers: SOI:%u@%u EOI:%u@%u DQT:%u DHT:%u SOF:%u NAL:%u@%u\r\n",
               (unsigned)c_soi, (unsigned)first_soi,
               (unsigned)c_eoi, (unsigned)first_eoi,
               (unsigned)c_dqt, (unsigned)c_dht, (unsigned)c_sof,
               (unsigned)c_nal, (unsigned)first_nal);
    }
    return 0;
}

int usbh_uvc_test_trace(uint32_t count)
{
    if (!g_uvct.streaming) {
        printf("[UVC] not streaming\r\n");
        return -1;
    }
    g_uvct.trace_cnt = (int)count;
    g_uvct.trace_fin = (int)count;
    return 0;
}

int usbh_uvc_test_webfps(int fps)
{
    if (fps >= 0) {
        web_server_uvc_preview_set_fps((uint32_t)fps);
    }
    uvc_preview_stats_t st;
    web_server_uvc_preview_get_stats(&st);
    printf("[UVC] web: throttle=%ums(%sfps) sent:%u frames %u.%02uMB fps:%u bp_skip:%u clients:%u\r\n",
           (unsigned)st.interval_ms, st.interval_ms ? "capped" : "uncapped",
           (unsigned)st.sent_frames,
           (unsigned)(st.sent_bytes / 1048576u), (unsigned)((st.sent_bytes % 1048576u) * 100u / 1048576u),
           (unsigned)st.sent_fps, (unsigned)st.bp_skips, (unsigned)st.clients);
    return 0;
}

int usbh_uvc_test_isodbg(void)
{
    if (!g_uvct.streaming) {
        printf("[UVC] not streaming\r\n");
        return -1;
    }
    if (g_uvct.iso_scan_sem == NULL) {
        g_uvct.iso_scan_sem = osSemaphoreNew(1, 0, NULL);
        if (g_uvct.iso_scan_sem == NULL) {
            return -1;
        }
    }
    (void)osSemaphoreAcquire(g_uvct.iso_scan_sem, 0);
    g_uvct.iso_scan_bound = 0;
    g_uvct.iso_scan_hdr = 0;
    g_uvct.iso_scan_done = 0;
    g_uvct.iso_scan_left = 512;

    if (osSemaphoreAcquire(g_uvct.iso_scan_sem, 5000) != osOK) {
        printf("[UVC] isodbg: scan incomplete (%d left)\r\n", g_uvct.iso_scan_left);
    }
    printf("[UVC] isodbg: %u packet boundaries scanned, %u header-like (%u%%)\r\n",
           (unsigned)g_uvct.iso_scan_bound, (unsigned)g_uvct.iso_scan_hdr,
           g_uvct.iso_scan_bound ? (unsigned)(g_uvct.iso_scan_hdr * 100u / g_uvct.iso_scan_bound) : 0u);
    return 0;
}

int usbh_uvc_test_hdrdbg(void)
{
    uint32_t cnt = g_uvct.hdrdbg_cnt;
    printf("[UVC] hdrerr total %u, last 8 events:\r\n", (unsigned)g_uvct.st_hdr_err);
    for (int i = 0; i < 8; i++) {
        uint32_t k = (cnt + (uint32_t)i) & 7; /* oldest first */
        printf("[UVC] ev%u slotlen=%u pos=%u:", (unsigned)(cnt - 8 + (uint32_t)i),
               (unsigned)g_uvct.hdrdbg_len[k], (unsigned)g_uvct.hdrdbg_pos[k]);
        for (int j = 0; j < 16; j++) {
            printf(" %02x", g_uvct.hdrdbg_data[k][j]);
        }
        printf("\r\n");
    }
    return 0;
}

int usbh_uvc_test_raw(void)
{
    if (!g_uvct.streaming) {
        printf("[UVC] not streaming\r\n");
        return -1;
    }
    if (g_uvct.raw_sem == NULL) {
        g_uvct.raw_sem = osSemaphoreNew(1, 0, NULL);
        if (g_uvct.raw_sem == NULL) {
            return -1;
        }
    }
    (void)osSemaphoreAcquire(g_uvct.raw_sem, 0);
    g_uvct.raw_snap_cnt = 0;
    g_uvct.raw_pend = 1;

    if (osSemaphoreAcquire(g_uvct.raw_sem, 3000) != osOK) {
        g_uvct.raw_pend = 0;
        printf("[UVC] raw capture timeout\r\n");
        return -1;
    }

    for (int i = 0; i < 3; i++) {
        printf("[UVC] raw chunk %d len %u/%u:\r\n", i,
               (unsigned)g_uvct.raw_snap_len[i], (unsigned)g_uvct.urb_slot_bytes);
        for (uint32_t j = 0; j < 64; j += 16) {
            printf("  %04x  ", (unsigned)j);
            for (uint32_t k = 0; k < 16; k++) {
                printf("%02x ", g_uvct.raw_snap[i][j + k]);
            }
            printf("\r\n");
        }
    }
    return 0;
}

uint32_t usbh_uvc_preview_get(const uint8_t **buf, uint32_t *len, uint16_t *w, uint16_t *h)
{
    uint32_t seq = g_uvc_prev_seq;

    if (seq == 0) {
        return 0; /* nothing published yet */
    }

    uint32_t slot = (seq - 1) % UVC_PREV_SLOTS;
    *buf = g_uvc_prev_buf[slot];
    *len = g_uvc_prev_len[slot];
    *w = g_uvc_prev_w;
    *h = g_uvc_prev_h;
    return seq;
}

void usbh_uvc_preview_acquire(void)
{
    g_uvc_prev_users++;
}

void usbh_uvc_preview_release(void)
{
    if (g_uvc_prev_users > 0) {
        g_uvc_prev_users--;
    }
}

/* ==================== service layer introspection ==================== */

int usbh_uvc_test_dev_ready(void)
{
    return g_uvct.video != NULL;
}

int usbh_uvc_test_get_devinfo(struct usbh_uvc_devinfo *info)
{
    if (g_uvct.video == NULL || info == NULL) {
        return -1;
    }
    struct usbh_video *v = g_uvct.video;
    memset(info, 0, sizeof(*info));
    info->vid = v->hport->device_desc.idVendor;
    info->pid = v->hport->device_desc.idProduct;
    info->is_bulk = (v->bulkin != NULL);
    if (v->hport->iProduct != NULL) {
        snprintf(info->product, sizeof(info->product), "%s", v->hport->iProduct);
    }
    return 0;
}

int usbh_uvc_test_enumerate(struct usbh_uvc_stream_cfg *out, int max)
{
    if (g_uvct.video == NULL || out == NULL || max <= 0) {
        return -1;
    }
    struct usbh_video *v = g_uvct.video;
    int n = 0;
    for (int i = 0; i < v->num_of_formats; i++) {
        if (v->format[i].format_type != USBH_VIDEO_FORMAT_MJPEG) {
            continue; /* MJPEG-only product path */
        }
        for (int j = 0; j < v->format[i].num_of_frames; j++) {
            if (n >= max) {
                return n;
            }
            /* some cameras list a frame twice in the descriptors */
            int dup = 0;
            for (int k = 0; k < n; k++) {
                if (out[k].width == v->format[i].frame[j].wWidth &&
                    out[k].height == v->format[i].frame[j].wHeight) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            out[n].width = v->format[i].frame[j].wWidth;
            out[n].height = v->format[i].frame[j].wHeight;
            out[n].interval_100ns = v->format[i].frame[j].dwDefaultFrameInterval;
            n++;
        }
    }
    return n;
}

void usbh_uvc_test_get_state(struct usbh_uvc_state *st)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->dev_ready = (g_uvct.video != NULL);
    st->opened = st->dev_ready && g_uvct.video->is_opened;
    st->streaming = g_uvct.streaming;
    st->is_bulk = g_uvct.is_bulk;
    st->width = g_uvct.rec_w;
    st->height = g_uvct.rec_h;
    st->fps = g_uvct.rec_fps;
    st->fps_measured = g_uvct.fps; /* worker-side 1s window of intact frames */
    st->frames = g_uvct.st_frames;
    st->frames_bad = g_uvct.st_frames_bad;
    st->reconnects = g_uvct.st_reconnects;
}

int usbh_uvc_test_dump(uint32_t offset, uint32_t len)
{
    if (g_uvct.capture_len == 0) {
        printf("[UVC] nothing captured\r\n");
        return -1;
    }
    if (offset >= g_uvct.capture_len) {
        offset = 0;
    }
    if (len == 0 || offset + len > g_uvct.capture_len) {
        len = g_uvct.capture_len - offset;
    }

    const uint8_t *p = g_uvc_capture_buf + offset;
    for (uint32_t i = 0; i < len; i += 16) {
        printf("%08lx  ", (unsigned long)(offset + i));
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            printf("%02x ", (unsigned)p[i + j]);
        }
        printf("\r\n");
    }
    return 0;
}
