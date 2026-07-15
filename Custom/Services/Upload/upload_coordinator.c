/**
 * @file upload_coordinator.c
 * @brief Capture persistence, upload dispatch, retry, cleanup.
 *
 * On-disk layout (root is /captures/ on the chosen FS). Records are partitioned
 * by date+hour so no single directory ever holds more than ~60 files (1/min):
 *   /captures/meta/<YYYY-MM-DD>/<HH>/cap_<ts>_<seq>.json   record metadata
 *   /captures/data/<YYYY-MM-DD>/<HH>/cap_<ts>_<seq>_p.jpg  primary image
 *   /captures/data/<YYYY-MM-DD>/<HH>/cap_<ts>_<seq>_i.jpg  inference image (opt)
 *   /captures/data/<YYYY-MM-DD>/<HH>/cap_<ts>_<seq>_a.json AI result (opt)
 *   /captures/index/<YYYY-MM-DD>.idx                        manifest (append-only)
 *
 * Record STATE (pending/sent/failed/local) lives ONLY in the manifest, NOT in
 * the directory path - the metadata .json never moves between state directories.
 * State transitions append a new 32-byte entry to the day's manifest; the
 * current state of a record is its latest non-tombstone entry. This makes
 * count/list O(days) file reads instead of O(records) opendir, and eliminates
 * expensive LittleFS renames on every upload. The .json retains a `state`
 * field only so the index can be rebuilt from disk if the manifest is lost.
 */

#include "upload_coordinator.h"

#include "cJSON.h"
#include "cmsis_os2.h"
#include "tx_api.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "common_utils.h"
#include "generic_file.h"
#include "sd_file.h"
#include "storage.h"
#include "drtc.h"
#include "device_service.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "service_init.h"
#include "wake_scheduler.h"
#include "nn.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

/* Verbose [UPLOAD] diagnostic logs. Default OFF - set to 1 to trace init,
 * space checks, enqueue/dispatch, network waits, and flush passes. Key
 * operational info still goes through LOG_SVC_* (the log system) regardless. */
#define UPLOAD_DEBUG 0
#if UPLOAD_DEBUG
#define UPLOAD_LOG(...) printf("[UPLOAD] " __VA_ARGS__)
#else
#define UPLOAD_LOG(...) ((void)0)
#endif

/* ==================== Configuration ==================== */

#define UPLOAD_TASK_STACK       (4096 * 4)
#define UPLOAD_TASK_PRIORITY    osPriorityBelowNormal
#define UPLOAD_QUEUE_DEPTH      8

/* Flush time budget. Instead of capping the record count (which fights the
 * user's configured batch size and doesn't adapt), the flush runs serially
 * (publish+ack+move-to-SENT per record) until a DEADLINE that scales with the
 * pending count. Each fully-acked record is moved to SENT → no duplicate next
 * wake. If the deadline hits, remaining records stay PENDING (un-published) →
 * also no duplicate. The sleep path (wait_upload_before_sleep) asks
 * upload_coordinator_get_flush_budget_ms() for the same scaled timeout so it
 * waits long enough for the flush to finish, but no longer than necessary.
 *
 *   budget(N) = min(FLUSH_MAX_BUDGET_MS, N*FLUSH_PER_RECORD_MS + FLUSH_BASE_MS)
 *
 * Tune PER_RECORD to your typical publish+ack latency. MAX is the hard cap on
 * a single wake's upload time (low-power budget). */
#define FLUSH_PER_RECORD_MS    3000u   /* publish + ack estimate per record    */
#define FLUSH_BASE_MS          3000u   /* channel wait + overhead              */
#define FLUSH_MAX_BUDGET_MS    60000u  /* hard cap on one flush pass           */
#define FLUSH_ACK_TIMEOUT_MS   3000u   /* per-ack wait in the sliding window   */
#define FLUSH_WINDOW_MAX       16u     /* cap on in-flight parallel publishes  */
#define CLEANUP_MAX_MS         5000u   /* bound cleanup_for_space (full-FS removes are slow) */

#define CAPTURES_ROOT           "/captures"
#define CAPTURES_DIR_DATA       CAPTURES_ROOT "/data"
#define CAPTURES_DIR_META       CAPTURES_ROOT "/meta"    /* metadata .json (never moved) */
#define CAPTURES_DIR_INDEX      CAPTURES_ROOT "/index"   /* daily manifest .idx files  */

/* Manifest entry: fixed 32 bytes, append-only. LittleFS file content is
 * crash-safe (block-level COW), so a power loss mid-append at most drops the
 * trailing partial entry - readers validate filesize % 32 == 0 and skip the
 * tail. State transitions append a fresh entry; deletes append a tombstone. */
#pragma pack(push, 1)
typedef struct {
    char     id[24];      /* "cap_<ts>_<seq>" - ts is 10 digits until 2286, seq 5
                           * digits → 20 chars + NUL = 21. [20] was too small and
                           * manifest_append truncated the last seq digit, so the
                           * .idx entry id never matched the actual file name
                           * (e.g. stored cap_<ts>_0000, file cap_<ts>_00001) →
                           * every record orphaned, nothing uploaded. [24] fits. */
    uint8_t  state;       /* record_state_t, or IDX_STATE_DELETED */
    uint8_t  _pad[3];
    uint32_t timestamp;   /* unix seconds */
    uint32_t size;        /* primary image bytes */
} cap_idx_entry_t;        /* 24 + 1 + 3 + 4 + 4 = 36 bytes */
#pragma pack(pop)
#define IDX_ENTRY_SIZE      (sizeof(cap_idx_entry_t))
#define IDX_STATE_DELETED   0xFFu
/* Sanity: catch any toolchain packing surprise at compile time. */
_Static_assert(IDX_ENTRY_SIZE == 36, "cap_idx_entry_t must be 36 bytes");

/* Cleanup watermark (bytes) - keep this much head-room when wrapping. */
#define CLEANUP_HEADROOM_BYTES  (2 * 1024u * 1024u)

/* Persisted "storage full" flag (NVS_USER). Set when a capture write failed
 * for lack of space; cleared once a write succeeds. Persists across wakes so
 * the FIRST capture of a low-power wake runs proactive cleanup BEFORE writing
 * (avoiding the write-fail→cleanup→retry cycle and the lfs_file_write-on-full
 * hang every wake). Written only on state change (infrequent → negligible wear). */
#define NVS_KEY_UPLOAD_STORAGE_FULL  "up_stor_full"

/* Max wait for SD media to finish opening before a capture-store attempt.
 * sdProcess opens the media asynchronously; on a cold boot the first capture
 * may arrive before fx_media_open completes, so we wait rather than fail. */
#define SD_STORE_READY_TIMEOUT_MS  3000u

/* Minimum free bytes required before calling ensure_dirs at init time.
 * Creating 6 directories on littlefs requires metadata-pair allocations
 * (~8 KB per dir + parent commits). We add margin so lfs GC never spins. */
#define ENSURE_DIRS_MIN_FREE_BYTES (2 * 1024u * 1024u)

/* Event message types delivered to the worker queue. */
typedef enum {
    EV_KICK         = 1,
    EV_DRAIN        = 2,
    EV_RELOAD       = 3,
    EV_SHUTDOWN     = 4,
} upload_event_kind_t;

/* Queue carries a single ULONG per slot (matches webhook_service's pattern -
 * avoids -fshort-enums shrinking an enum-typed struct and breaking the
 * mq_size >= count*msg_size check in the CMSIS-RTOS2 wrapper). */
typedef uint32_t upload_event_t;

/* ==================== State ==================== */

typedef struct {
    aicam_bool_t       initialized;
    aicam_bool_t       running;
    service_state_t    state;
    osMutexId_t        mutex;            /* protects do_flush_pass re-entrancy */
    osMessageQueueId_t queue;
    osThreadId_t       task_handle;
    volatile aicam_bool_t flush_active;  /* true while do_flush_pass is running - lets the
                                          * sleep path wait for an in-progress flush to finish
                                          * before cutting power (avoids abandoning an upload) */

    capture_upload_config_t cfg;            /* cached copy, reloaded via EV_RELOAD */
    FS_Type_t          active_fs;           /* AUTO resolves here */
    aicam_bool_t       storage_full;        /* set when STOP policy rejects a write */
} upload_state_t;

static upload_state_t g_up;

/* RAM count cache for get_status - rebuilt from the manifest when dirty (set
 * on every create/state-change/delete). Avoids a full manifest sweep on every
 * web status poll. Lost on wake (cold boot) → first query rebuilds. */
static uint32_t g_count_cache[4];
static volatile bool g_count_cache_dirty = true;

/* Per-FS "captures tree already created this wake" cache for ensure_dirs().
 * Reset to FS_MAX by reload_config so a post-format re-init recreates the
 * tree on the wiped volume (see ensure_dirs). */
static FS_Type_t s_ensured_fs = FS_MAX;

/* Persisted storage_full helpers (NVS_USER). */
static void storage_full_persist(aicam_bool_t v) {
    uint8_t b = v ? 1u : 0u;
    (void)storage_nvs_write(NVS_USER, NVS_KEY_UPLOAD_STORAGE_FULL, &b, sizeof(b));
}
static aicam_bool_t storage_full_load(void) {
    uint8_t b = 0;
    if (storage_nvs_read(NVS_USER, NVS_KEY_UPLOAD_STORAGE_FULL, &b, sizeof(b)) <= 0)
        return AICAM_FALSE;
    return b ? AICAM_TRUE : AICAM_FALSE;
}
/* Set g_up.storage_full and persist ONLY on change (avoids per-capture NVS wear). */
static void storage_full_set(aicam_bool_t v) {
    if (g_up.storage_full == v) return;
    g_up.storage_full = v;
    storage_full_persist(v);
}

static uint8_t upload_task_stack[UPLOAD_TASK_STACK] __attribute__((aligned(32))) IN_PSRAM;

static TX_QUEUE upload_queue_cb __attribute__((aligned(8)));
static uint8_t  upload_queue_mem[UPLOAD_QUEUE_DEPTH * sizeof(ULONG)] __attribute__((aligned(8)));
static const osMessageQueueAttr_t upload_queue_attr = {
    .name = "upload_q",
    .cb_mem = &upload_queue_cb,
    .cb_size = sizeof(upload_queue_cb),
    .mq_mem = upload_queue_mem,
    .mq_size = sizeof(upload_queue_mem),
};

/* ==================== Forward Decls ==================== */

static void        upload_task(void *arg);
static aicam_result_t ensure_dirs(FS_Type_t fs);
static void        ensure_dirs_with_space_check(FS_Type_t fs);
static FS_Type_t   resolve_storage_target(capture_storage_t cs);
static const char *trigger_str(aicam_capture_trigger_t t);
static const char *wakeup_src_str(wakeup_source_type_t w);
static void        path_for_meta(char *out, size_t n, const char *id);
static void        path_for_data(char *out, size_t n, const char *id, char suffix);
static aicam_result_t ensure_record_dirs(FS_Type_t fs, const char *id);
static aicam_result_t manifest_append(FS_Type_t fs, const char *id,
                                      uint8_t state, uint32_t ts, uint32_t size);
static aicam_result_t persist_record(FS_Type_t fs, const char *id,
                                      const uint8_t *jpeg, uint32_t jpeg_size,
                                      const uint8_t *inf, uint32_t inf_size,
                                      const char *ai_json,
                                      const mqtt_image_metadata_t *meta,
                                      aicam_capture_trigger_t trigger,
                                      wakeup_source_type_t wakeup_src,
                                      record_state_t state);
/* move_record() now changes state in place (rewrite .json state + manifest
 * append) - `from` is retained only for signature stability at call sites. */
static aicam_result_t move_record(FS_Type_t fs, const char *id,
                                   record_state_t from, record_state_t to);
static aicam_result_t parse_meta_file(FS_Type_t fs, const char *path, cJSON **out_json);
static aicam_result_t upload_one_record(FS_Type_t fs, const char *id,
                                        const mqtt_ai_result_t *ai_result);
static aicam_result_t load_ai_result_from_disk(FS_Type_t fs, const char *id,
                                               mqtt_ai_result_t *out);
static aicam_result_t cleanup_for_space(FS_Type_t fs, uint64_t need_bytes);
static aicam_result_t purge_old_sent(FS_Type_t fs);
static uint32_t      count_state(FS_Type_t fs, record_state_t state);
static void          manifest_counts_all(FS_Type_t fs, uint32_t counts[4]);
static aicam_result_t get_storage_free_bytes(FS_Type_t fs, uint64_t *out_free, uint64_t *out_total);
static aicam_result_t wait_for_upload_channel(uint32_t timeout_ms);

/* ==================== Helpers ==================== */

static uint64_t now_unix(void)
{
    return rtc_get_timeStamp();
}

static uint32_t next_seq(void)
{
    /* RAM-only counter - resets to 0 on each cold boot (wake). Different wakes
     * have different timestamps (minutes apart), so cross-boot id collision is
     * impossible. Within a single wake, the counter increments for same-second
     * captures (e.g. PIR + button simultaneously). No NVS write - eliminates
     * per-capture flash wear that the previous NVS-persisted counter caused. */
    static uint32_t s = 0;
    return ++s;
}

static const char *trigger_str(aicam_capture_trigger_t t)
{
    switch (t) {
    case AICAM_CAPTURE_TRIGGER_RTC:      return "rtc";
    case AICAM_CAPTURE_TRIGGER_PIR:      return "pir";
    case AICAM_CAPTURE_TRIGGER_WEB:      return "web";
    case AICAM_CAPTURE_TRIGGER_REMOTE:   return "remote";
    case AICAM_CAPTURE_TRIGGER_GPIO:     return "gpio";
    case AICAM_CAPTURE_TRIGGER_BUTTON:   return "button";
    case AICAM_CAPTURE_TRIGGER_SCHEDULE: return "schedule";
    default:                             return "unknown";
    }
}

static const char *wakeup_src_str(wakeup_source_type_t w)
{
    switch (w) {
    case WAKEUP_SOURCE_IO:                return "io";
    case WAKEUP_SOURCE_RTC:               return "rtc";
    case WAKEUP_SOURCE_PIR:               return "pir";
    case WAKEUP_SOURCE_BUTTON:            return "button";
    case WAKEUP_SOURCE_BUTTON_LONG:       return "button_long";
    case WAKEUP_SOURCE_BUTTON_SUPER_LONG: return "button_xlong";
    case WAKEUP_SOURCE_REMOTE:            return "remote";
    case WAKEUP_SOURCE_WUFI:              return "wufi";
    default:                              return "other";
    }
}

/* ==================== Date/hour-partitioned paths ==================== */

/* Parse the unix-timestamp field from a record id ("cap_<ts>_<seq>"). The ts
 * is zero-padded for lexicographic sort. Returns 0 on parse failure. */
static unsigned long ts_from_id(const char *id)
{
    if (!id || strncmp(id, "cap_", 4) != 0) return 0;
    unsigned long ts = 0;
    if (sscanf(id + 4, "%lu_", &ts) != 1) return 0;
    return ts;
}

/* Format a record id's timestamp as a UTC date directory name "YYYY-MM-DD".
 * On parse failure returns "unknown" so the record still lands somewhere
 * deterministic and listable. */
static void date_dir_from_id(const char *id, char *out, size_t n)
{
    if (!out || n == 0) return;
    unsigned long ts = ts_from_id(id);
    if (ts == 0) { snprintf(out, n, "unknown"); return; }
    time_t t = (time_t)ts;
    struct tm *tmv = gmtime(&t);
    if (!tmv) { snprintf(out, n, "unknown"); return; }
    strftime(out, n, "%Y-%m-%d", tmv);
}

/* Format a record id's timestamp as a UTC hour directory "HH" (zero-padded so
 * lexicographic readdir order == chronological). "unknown" on parse failure. */
static void hour_dir_from_id(const char *id, char *out, size_t n)
{
    if (!out || n == 0) return;
    unsigned long ts = ts_from_id(id);
    if (ts == 0) { snprintf(out, n, "unknown"); return; }
    time_t t = (time_t)ts;
    struct tm *tmv = gmtime(&t);
    if (!tmv) { snprintf(out, n, "unknown"); return; }
    strftime(out, n, "%H", tmv);
}

/* Format an arbitrary unix timestamp as "YYYY-MM-DD" (UTC). */
static void date_str_from_ts(uint64_t ts, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (ts == 0) { snprintf(out, n, "0000-00-00"); return; }
    time_t t = (time_t)ts;
    struct tm *tmv = gmtime(&t);
    if (!tmv) { snprintf(out, n, "0000-00-00"); return; }
    strftime(out, n, "%Y-%m-%d", tmv);
}

/* stat-first mkdir: lfs_mkdir is NOT cheaply idempotent (every call runs
 * lfs_fs_forceconsistency()), so only mkdir when the dir is actually missing. */
static aicam_result_t ensure_dir_exists(FS_Type_t fs, const char *path)
{
    struct stat st = {0};
    if (disk_file_stat(fs, path, &st) == 0) return AICAM_OK;  /* exists */
    if (disk_file_mkdir(fs, path) != 0) {
        LOG_SVC_ERROR("ensure_dir_exists: mkdir failed path=%s", path);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/* Ensure the meta/ and data/ <date>/<hour> subdirs exist for a record (derived
 * from id). Replaces the old per-state ensure_date_dir - state no longer
 * affects the path. */
static aicam_result_t ensure_record_dirs(FS_Type_t fs, const char *id)
{
    char date[16], hour[8];
    date_dir_from_id(id, date, sizeof(date));
    hour_dir_from_id(id, hour, sizeof(hour));

    char path[96];
    /* meta/<date>/<hour> */
    snprintf(path, sizeof(path), "%s/%s", CAPTURES_DIR_META, date);
    if (ensure_dir_exists(fs, path) != AICAM_OK) return AICAM_ERROR;
    snprintf(path, sizeof(path), "%s/%s/%s", CAPTURES_DIR_META, date, hour);
    if (ensure_dir_exists(fs, path) != AICAM_OK) return AICAM_ERROR;

    /* data/<date>/<hour> */
    snprintf(path, sizeof(path), "%s/%s", CAPTURES_DIR_DATA, date);
    if (ensure_dir_exists(fs, path) != AICAM_OK) return AICAM_ERROR;
    snprintf(path, sizeof(path), "%s/%s/%s", CAPTURES_DIR_DATA, date, hour);
    if (ensure_dir_exists(fs, path) != AICAM_OK) return AICAM_ERROR;

    return AICAM_OK;
}

/* Metadata .json path - fixed location regardless of state. */
static void path_for_meta(char *out, size_t n, const char *id)
{
    char date[16], hour[8];
    date_dir_from_id(id, date, sizeof(date));
    hour_dir_from_id(id, hour, sizeof(hour));
    snprintf(out, n, "%s/%s/%s/%s.json", CAPTURES_DIR_META, date, hour, id);
}

/* Data file path (primary/inference/ai) under data/<date>/<hour>/. */
static void path_for_data(char *out, size_t n, const char *id, char suffix)
{
    char date[16], hour[8];
    date_dir_from_id(id, date, sizeof(date));
    hour_dir_from_id(id, hour, sizeof(hour));
    snprintf(out, n, "%s/%s/%s/%s_%c.%s", CAPTURES_DIR_DATA, date, hour, id, suffix,
             suffix == 'a' ? "json" : "jpg");
}

/* ==================== Manifest (append-only index) ==================== */

/* Path to a day's manifest file: /captures/index/<YYYY-MM-DD>.idx */
static void manifest_path_for_id(char *out, size_t n, const char *id)
{
    char date[16];
    date_dir_from_id(id, date, sizeof(date));
    snprintf(out, n, "%s/%s.idx", CAPTURES_DIR_INDEX, date);
}

/* Append one entry to the record's day manifest. Idempotent at the FS level
 * (open "a", write 32B, close). The caller passes the post-transition state. */
static aicam_result_t manifest_append(FS_Type_t fs, const char *id,
                                      uint8_t state, uint32_t ts, uint32_t size)
{
    char mpath[96];
    manifest_path_for_id(mpath, sizeof(mpath), id);
    cap_idx_entry_t e;
    memset(&e, 0, sizeof(e));
    size_t idlen = strlen(id);
    if (idlen >= sizeof(e.id)) idlen = sizeof(e.id) - 1;
    memcpy(e.id, id, idlen);
    e.state = state;
    e.timestamp = ts;
    e.size = size;

    void *fd = disk_file_fopen(fs, mpath, "a");
    if (!fd) {
        LOG_SVC_ERROR("manifest_append: fopen failed path=%s", mpath);
        return AICAM_ERROR;
    }
    int wn = disk_file_fwrite(fs, fd, &e, IDX_ENTRY_SIZE);
    disk_file_fclose(fs, fd);
    return (wn == (int)IDX_ENTRY_SIZE) ? AICAM_OK : AICAM_ERROR;
}

/* ==================== Storage selection ==================== */

static FS_Type_t resolve_storage_target(capture_storage_t cs)
{
    switch (cs) {
    case CAPTURE_STORE_SD:
        return FS_SD;
    case CAPTURE_STORE_FLASH:
        return FS_FLASH;
    case CAPTURE_STORE_NONE:
        return FS_MAX; /* signal "no persistence" */
    case CAPTURE_STORE_AUTO:
    default:
        if (device_service_storage_is_sd_connected()) return FS_SD;
        return FS_FLASH;
    }
}

/* Non-blocking FS readiness check for ensure_dirs. Returns true only when the
 * backing FS is actually mounted/open - so the init-time call (SD media may
 * still be opening asynchronously in sdProcess) does NOT cache a failed mkdir
 * run and block the real mkdir from happening later in persist_record. */
static bool fs_ready_for_dirs(FS_Type_t fs)
{
    if (fs == FS_SD) {
        return sd_is_media_open() ? true : false;
    }
    if (fs == FS_FLASH) {
        /* Just read the RAM mounted flag - do NOT call storage_get_disk_info
         * (that runs lfs_fs_size, a full 8192-block traverse, just to read a
         * boolean). The caller (ensure_dirs_with_space_check / enqueue_capture)
         * already did a real space probe, so the volume is known accessible. */
        return storage_is_lfs_mounted();
    }
    return false;
}

static aicam_result_t ensure_dirs(FS_Type_t fs)
{
    if (fs == FS_MAX) return AICAM_OK;
    /* If the backing FS isn't ready yet (e.g. SD media still opening at init
     * time), bail without touching s_ensured so persist_record retries the
     * real mkdir once the FS is up. Caching a failure here is what previously
     * caused /captures/data to never get created and every write to fail. */
    if (!fs_ready_for_dirs(fs)) return AICAM_OK;

    /* mkdir is expensive on SD (166ms+ per call). Once we've created the
     * captures tree on a given filesystem this wake, skip - the dirs persist
     * across sleeps anyway. Track per-filesystem so switching storage (e.g.
     * SD→FLASH via web) still creates dirs on the new target.
     * Invalidated (set to FS_MAX) by upload_coordinator_reload_config() so a
     * post-format re-init actually recreates the tree on the wiped volume. */
    if (s_ensured_fs == fs) return AICAM_OK;

    /* NOTE: the caller (ensure_dirs_with_space_check or enqueue_capture) has
     * already verified free space against the ENSURE_DIRS_MIN_FREE_BYTES /
     * CLEANUP_HEADROOM threshold. Do NOT re-probe here - a second
     * storage_get_disk_info would run another full lfs_fs_size traverse
     * (~120 ms holding the storage mutex) and contend with the log task. */

    uint64_t te = rtc_get_uptime_ms();
    const char *dirs[] = {
        CAPTURES_ROOT,
        CAPTURES_DIR_DATA,
        CAPTURES_DIR_META,
        CAPTURES_DIR_INDEX,
    };
    aicam_result_t ret = AICAM_OK;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        /* stat first: lfs_mkdir is NOT cheaply idempotent - every call runs
         * lfs_fs_forceconsistency() (a full deorphan traverse) before checking
         * existence, so calling it on already-existing dirs is expensive and
         * can hang if the FS has a borderline state (deorphan loops with
         * LFS_ASSERT disabled). Only mkdir when the dir is actually missing. */
        struct stat st = {0};
        if (disk_file_stat(fs, dirs[i], &st) == 0) continue;  /* exists */
        if (disk_file_mkdir(fs, dirs[i]) != 0) {
            /* mkdir failed (e.g. NOSPC on a full FS, or parent missing). Do NOT
             * mark s_ensured_fs - otherwise persist_record would skip ensure_dirs
             * on retry and the date-subdir mkdir would fail forever (NOENT), making
             * cleanup_for_space + retry futile (a ~30s hang on a full FS). Leaving
             * s_ensured_fs unset lets the retry re-create the tree once cleanup
             * has freed space. */
            LOG_SVC_ERROR("ensure_dirs: mkdir failed path=%s", dirs[i]);
            ret = AICAM_ERROR;
        }
    }
    if (ret == AICAM_OK) s_ensured_fs = fs;   /* only mark ensured if ALL created */
    uint64_t dt = rtc_get_uptime_ms() - te;
    if (dt > 5) {
        UPLOAD_LOG("ensure_dirs time=%lu ms\r\n", (unsigned long)dt);
    }
    return ret;
}

/* Call ensure_dirs. The proactive lfs_fs_size-based space check was removed -
 * it ran a full-FS traverse (O(file count), 3-4 s with thousands of records)
 * on every boot, tripping the watchdog. Instead, space is checked reactively
 * in enqueue_capture on write failure (cleanup_for_space is only called then).
 * Shared by init, reload_config (sync), and EV_RELOAD. */
static void ensure_dirs_with_space_check(FS_Type_t fs)
{
    ensure_dirs(fs);
}

static aicam_result_t get_storage_free_bytes(FS_Type_t fs, uint64_t *out_free, uint64_t *out_total)
{
    if (!out_free || !out_total) return AICAM_ERROR_INVALID_PARAM;
    *out_free = 0;
    *out_total = 0;

    if (fs == FS_SD) {
        sd_disk_info_t info;
        if (sd_get_disk_info(&info) != 0) return AICAM_ERROR;
        *out_free = (uint64_t)info.free_KBytes * 1024u;
        *out_total = (uint64_t)info.total_KBytes * 1024u;
        return AICAM_OK;
    }
    if (fs == FS_FLASH) {
        storage_disk_info_t info;
        if (storage_get_disk_info(&info) != 0) return AICAM_ERROR;
        *out_free = (uint64_t)info.free_KBytes * 1024u;
        *out_total = (uint64_t)info.total_KBytes * 1024u;
        return AICAM_OK;
    }
    return AICAM_ERROR_INVALID_PARAM;
}

/* ==================== File-write helpers ==================== */

static aicam_result_t write_file(FS_Type_t fs, const char *path,
                                  const uint8_t *buf, uint32_t size)
{
    void *fd = disk_file_fopen(fs, path, "w");
    if (!fd) {
        LOG_SVC_ERROR("write_file: fopen failed path=%s", path);
        return AICAM_ERROR;
    }
    uint32_t off = 0;
    aicam_result_t r = AICAM_OK;
    while (off < size) {
        uint32_t chunk = (size - off > 4096) ? 4096 : (size - off);
        int wn = disk_file_fwrite(fs, fd, buf + off, chunk);
        if (wn != (int)chunk) {
            LOG_SVC_ERROR("write_file: fwrite short path=%s off=%lu chunk=%u wn=%d",
                          path, (unsigned long)off, (unsigned)chunk, wn);
            r = AICAM_ERROR;
            break;
        }
        off += chunk;
    }
    disk_file_fclose(fs, fd);
    return r;
}

static aicam_result_t read_text_file(FS_Type_t fs, const char *path, char **out, uint32_t *out_size)
{
    if (!out || !out_size) return AICAM_ERROR_INVALID_PARAM;
    *out = NULL;
    *out_size = 0;
    struct stat st = {0};
    if (disk_file_stat(fs, path, &st) != 0 || st.st_size <= 0) return AICAM_ERROR;
    if (st.st_size > 1024 * 1024) return AICAM_ERROR;  /* sanity */
    char *buf = (char *)buffer_calloc(1, (size_t)st.st_size + 1);
    if (!buf) return AICAM_ERROR_NO_MEMORY;
    void *fd = disk_file_fopen(fs, path, "r");
    if (!fd) { buffer_free(buf); return AICAM_ERROR; }
    int rn = disk_file_fread(fs, fd, buf, (size_t)st.st_size);
    disk_file_fclose(fs, fd);
    if (rn <= 0) { buffer_free(buf); return AICAM_ERROR; }
    buf[rn] = '\0';
    *out = buf;
    *out_size = (uint32_t)rn;
    return AICAM_OK;
}

/* ==================== Metadata JSON ==================== */

static cJSON *build_record_meta_json(const char *id,
                                     const mqtt_image_metadata_t *meta,
                                     aicam_capture_trigger_t trigger,
                                     wakeup_source_type_t wakeup_src,
                                     const char *primary_name,
                                     const char *inference_name,
                                     const char *ai_name,
                                     upload_proto_t protocol,
                                     record_state_t state)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddNumberToObject(r, "version", 1);
    cJSON_AddStringToObject(r, "id", id);
    cJSON_AddNumberToObject(r, "state", (double)state);
    cJSON_AddStringToObject(r, "image_id", meta->image_id);
    cJSON_AddNumberToObject(r, "timestamp", (double)meta->timestamp);
    cJSON_AddNumberToObject(r, "width", meta->width);
    cJSON_AddNumberToObject(r, "height", meta->height);
    cJSON_AddNumberToObject(r, "size", meta->size);
    cJSON_AddNumberToObject(r, "quality", meta->quality);
    cJSON_AddNumberToObject(r, "format", meta->format);
    cJSON_AddStringToObject(r, "trigger", trigger_str(trigger));
    cJSON_AddStringToObject(r, "wakeup_source", wakeup_src_str(wakeup_src));
    cJSON_AddNumberToObject(r, "trigger_type_id", (double)trigger);
    cJSON_AddStringToObject(r, "primary", primary_name ? primary_name : "");
    cJSON_AddStringToObject(r, "inference_image", inference_name ? inference_name : "");
    cJSON_AddStringToObject(r, "ai_result", ai_name ? ai_name : "");
    cJSON_AddStringToObject(r, "upload_protocol",
                            protocol == UPLOAD_PROTO_WEBHOOK ? "webhook" : "mqtt");
    cJSON_AddNumberToObject(r, "retry_count", 0);
    cJSON_AddStringToObject(r, "last_error", "");

    /* battery snapshot (best-effort).
     * Only need battery %, device name, serial - do NOT call
     * device_service_get_info() which internally calls update_storage_info()
     * → storage_get_disk_info() → lfs_fs_size() (230 ms on 8189-block flash).
     * These fields are updated at boot and change rarely. */
    {
        device_info_config_t dev_buf;
        if (device_service_get_cached_info(&dev_buf) == AICAM_OK) {
            cJSON *bat = cJSON_AddObjectToObject(r, "battery");
            if (bat) {
                cJSON_AddNumberToObject(bat, "percent", dev_buf.battery_percent);
            }
            cJSON_AddStringToObject(r, "device_name", dev_buf.device_name);
            cJSON_AddStringToObject(r, "serial_number", dev_buf.serial_number);
        }
    }
    return r;
}

static aicam_result_t parse_meta_file(FS_Type_t fs, const char *path, cJSON **out_json)
{
    if (!out_json) return AICAM_ERROR_INVALID_PARAM;
    *out_json = NULL;
    char *text = NULL;
    uint32_t size = 0;
    aicam_result_t r = read_text_file(fs, path, &text, &size);
    if (r != AICAM_OK) return r;
    cJSON *json = cJSON_Parse(text);
    buffer_free(text);
    if (!json) return AICAM_ERROR;
    *out_json = json;
    return AICAM_OK;
}

static aicam_result_t write_meta_json(FS_Type_t fs, const char *path, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return AICAM_ERROR;
    aicam_result_t r = write_file(fs, path, (const uint8_t *)str, (uint32_t)strlen(str));
    cJSON_free(str);
    return r;
}

/* ==================== Persist a record ==================== */

static aicam_result_t persist_record(FS_Type_t fs, const char *id,
                                      const uint8_t *jpeg, uint32_t jpeg_size,
                                      const uint8_t *inf, uint32_t inf_size,
                                      const char *ai_json,
                                      const mqtt_image_metadata_t *meta,
                                      aicam_capture_trigger_t trigger,
                                      wakeup_source_type_t wakeup_src,
                                      record_state_t state)
{
    if (fs == FS_MAX) return AICAM_ERROR_INVALID_PARAM;
    if (ensure_dirs(fs) != AICAM_OK) return AICAM_ERROR;

    /* Ensure the per-date/per-hour subdirectories exist for this record's
     * capture date+hour (derived from id). State no longer affects the path.
     * If mkdir fails (e.g. FS is mid-recovery, forceconsistency error), bail
     * early - otherwise lfs_file_open fails on a non-existent parent dir and
     * we'd log "fopen failed" with no clear cause. */
    if (ensure_record_dirs(fs, id) != AICAM_OK) {
        return AICAM_ERROR;
    }

    char pri_path[128], inf_path[128], ai_path[128], meta_path[128];
    path_for_data(pri_path, sizeof(pri_path), id, 'p');
    path_for_data(inf_path, sizeof(inf_path), id, 'i');
    path_for_data(ai_path,  sizeof(ai_path),  id, 'a');
    path_for_meta(meta_path, sizeof(meta_path), id);

    aicam_result_t r = write_file(fs, pri_path, jpeg, jpeg_size);
    if (r != AICAM_OK) {
        LOG_SVC_ERROR("upload: write primary image failed: %s", pri_path);
        return r;
    }

    if (inf && inf_size > 0) {
        if (write_file(fs, inf_path, inf, inf_size) != AICAM_OK) {
            LOG_SVC_WARN("upload: write inference image failed: %s", inf_path);
            inf = NULL;
        }
    }
    if (ai_json && ai_json[0]) {
        if (write_file(fs, ai_path, (const uint8_t *)ai_json, (uint32_t)strlen(ai_json)) != AICAM_OK) {
            LOG_SVC_WARN("upload: write ai_result failed: %s", ai_path);
            ai_json = NULL;
        }
    }

    char pri_name[64], inf_name[64], ai_name[64];
    snprintf(pri_name, sizeof(pri_name), "%s_p.jpg", id);
    snprintf(inf_name, sizeof(inf_name), inf ? "%s_i.jpg" : "", id);
    snprintf(ai_name,  sizeof(ai_name),  ai_json ? "%s_a.json" : "", id);

    cJSON *j = build_record_meta_json(id, meta, trigger, wakeup_src,
                                       pri_name, inf ? inf_name : "",
                                       ai_json ? ai_name : "",
                                       g_up.cfg.upload_protocol, state);
    if (!j) return AICAM_ERROR_NO_MEMORY;
    aicam_result_t rr = write_meta_json(fs, meta_path, j);
    cJSON_Delete(j);
    if (rr != AICAM_OK) return rr;

    /* Append the create entry to the day's manifest so count/list can find
     * this record without opendir-ing thousands of files. */
    uint32_t ts = (uint32_t)(meta->timestamp ? meta->timestamp : now_unix());
    (void)manifest_append(fs, id, (uint8_t)state, ts, jpeg_size);
    g_count_cache_dirty = true;
    return AICAM_OK;
}

static aicam_result_t move_record(FS_Type_t fs, const char *id,
                                   record_state_t from, record_state_t to)
{
    /* State change is now in-place: the metadata .json never moves between
     * directories. We rewrite its `state` field and append a manifest entry
     * with the new state. `from` is unused (kept for call-site stability). */
    (void)from;
    char meta_path[128];
    path_for_meta(meta_path, sizeof(meta_path), id);

    cJSON *meta = NULL;
    if (parse_meta_file(fs, meta_path, &meta) != AICAM_OK) {
        return AICAM_ERROR;
    }
    cJSON_ReplaceItemInObject(meta, "state", cJSON_CreateNumber((double)to));
    uint32_t ts = 0;
    cJSON *t = cJSON_GetObjectItem(meta, "timestamp");
    if (t) ts = (uint32_t)t->valuedouble;
    uint32_t size = 0;
    t = cJSON_GetObjectItem(meta, "size");
    if (t) size = (uint32_t)t->valueint;

    /* Rewrite the .json via a temp file + atomic rename, so a power cut
     * (sleep) mid-rewrite leaves the original .json intact instead of a
     * truncated/empty file. The manifest entry is appended only after the
     * rename succeeds, so .json and .idx stay consistent. */
    char tmp_path[136];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);
    aicam_result_t r = write_meta_json(fs, tmp_path, meta);
    cJSON_Delete(meta);
    if (r != AICAM_OK) {
        (void)disk_file_remove(fs, tmp_path);
        return AICAM_ERROR;
    }
    if (disk_file_rename(fs, tmp_path, meta_path) != 0) {
        (void)disk_file_remove(fs, tmp_path);
        return AICAM_ERROR;
    }

    (void)manifest_append(fs, id, (uint8_t)to, ts, size);
    g_count_cache_dirty = true;
    if (to == RECORD_STATE_SENT) LOG_SVC_INFO("upload ok: %s", id);
    return AICAM_OK;
}

/* stat → if present, remove. Returns 0 on success (removed or not present),
 * -1 on remove failure (file exists but remove failed → FS likely corrupt).
 * Updates *freed with the content size of successfully removed files. */
static int stat_and_remove(FS_Type_t fs, const char *path, uint64_t *freed)
{
    struct stat st = {0};
    if (disk_file_stat(fs, path, &st) != 0) return 0;   /* not present - ok */
    uint64_t sz = (uint64_t)st.st_size;
    if (disk_file_remove(fs, path) == 0) { *freed += sz; return 0; }
    LOG_SVC_ERROR("delete_record_files: remove failed path=%s - aborting (FS corrupt?)", path);
    return -1;
}

static aicam_result_t delete_record_files(FS_Type_t fs, const char *id, record_state_t state,
                                const char *reason, uint64_t *out_freed)
{
    /* Tombstone FIRST, then remove the files. If power is cut between the
     * tombstone and the removes (e.g. sleep mid-cleanup), the record stays
     * correctly excluded from counts/lists and the orphaned files are just
     * wasted space - recoverable on next cleanup. The reverse order (remove
     * then tombstone) leaves a stale PENDING entry pointing at a gone .json,
     * which the flush pass retries forever.
     *
     * If the tombstone append FAILS, do NOT remove the files - the index would
     * still list the record as PENDING pointing at gone files (stale). Leave it
     * for the next cleanup to retry.
     *
     * If a remove FAILS (file present but remove returns non-zero), the FS is
     * likely corrupt - abort the remaining removes and return an error so the
     * caller stops the whole cleanup (no point deleting more on a broken FS).
     *
     * out_freed (optional): content bytes actually removed (lower bound on
     * blocks freed; littlefs per-file overhead is extra). */
    (void)state;
    LOG_SVC_INFO("upload: delete_record_files id=%s reason=%s", id, reason ? reason : "?");
    if (manifest_append(fs, id, IDX_STATE_DELETED, 0, 0) != AICAM_OK) {
        LOG_SVC_ERROR("delete_record_files: tombstone failed id=%s - leaving files", id);
        return AICAM_ERROR;
    }
    g_count_cache_dirty = true;

    uint64_t freed = 0;
    char path[128];

    path_for_meta(path, sizeof(path), id);
    if (stat_and_remove(fs, path, &freed) != 0) { if (out_freed) *out_freed = freed; return AICAM_ERROR; }
    path_for_data(path, sizeof(path), id, 'p');
    if (stat_and_remove(fs, path, &freed) != 0) { if (out_freed) *out_freed = freed; return AICAM_ERROR; }
    path_for_data(path, sizeof(path), id, 'i');
    if (stat_and_remove(fs, path, &freed) != 0) { if (out_freed) *out_freed = freed; return AICAM_ERROR; }
    path_for_data(path, sizeof(path), id, 'a');
    if (stat_and_remove(fs, path, &freed) != 0) { if (out_freed) *out_freed = freed; return AICAM_ERROR; }

    if (out_freed) *out_freed = freed;
    return AICAM_OK;
}

/* ==================== Directory iteration ==================== */

/* Compatible readdir entry - must be a superset of both lfs_info and sd_info
 * layouts. type(1) + pad(3) + size(4) + name(256) + mtime(4) + short_name(14).
 */
typedef struct {
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t size;
    char     name[256];
    uint32_t mtime;
    char     short_name[14];
} dir_entry_t;

/* Iterate records currently in a given state. Returns count of records
 * visited (passing offset/limit filter). */
typedef aicam_result_t (*record_visitor_t)(FS_Type_t fs, const char *id, void *user);

#define MAX_DATE_FILES 128
#define IDX_MAX_ENTRIES 65536   /* sanity cap: 2 MB manifest */

/* FNV-1a hash over a record id (NUL-padded, ≤20 bytes). */
static uint32_t id_hash(const char *id)
{
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < (int)sizeof(((cap_idx_entry_t *)0)->id) && id[i]; i++) {
        h ^= (uint8_t)id[i];
        h *= 0x01000193u;
    }
    return h;
}

/* Comparator: sort cap_idx_entry_t by timestamp ascending. */
static int idx_entry_cmp_ts(const void *a, const void *b)
{
    uint32_t ta = ((const cap_idx_entry_t *)a)->timestamp;
    uint32_t tb = ((const cap_idx_entry_t *)b)->timestamp;
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

/* Load one day's manifest, dedup to latest-state-per-id (last append wins),
 * drop tombstones, sort by timestamp ascending. Returns a buffer_calloc'd
 * array (caller frees via buffer_free) of live entries; *out_n = count.
 * NULL on failure/empty. Reading one small sequential file per day is what
 * makes count/list O(days) instead of O(records) opendir. */
static cap_idx_entry_t *manifest_load_day(FS_Type_t fs, const char *date, int *out_n)
{
    if (out_n) *out_n = 0;
    char mpath[96];
    snprintf(mpath, sizeof(mpath), "%s/%s.idx", CAPTURES_DIR_INDEX, date);
    struct stat st = {0};
    if (disk_file_stat(fs, mpath, &st) != 0 || st.st_size < (off_t)IDX_ENTRY_SIZE) {
        return NULL;
    }
    /* Size must be a clean multiple of IDX_ENTRY_SIZE. If not, the .idx has
     * old-format entries (e.g. 32-byte from a prior build) mixed with 36-byte,
     * which makes every read after the first misaligned → garbled ids. Delete
     * the corrupt .idx so rebuild_index_if_needed recreates it from .json. */
    if ((st.st_size % IDX_ENTRY_SIZE) != 0) {
        LOG_SVC_ERROR("manifest_load_day: .idx size %lu not multiple of %d - corrupt, deleting %s",
                      (unsigned long)st.st_size, (int)IDX_ENTRY_SIZE, mpath);
        (void)disk_file_remove(fs, mpath);
        return NULL;
    }
    int n_raw = (int)(st.st_size / IDX_ENTRY_SIZE);
    if (n_raw == 0) return NULL;
    if (n_raw > IDX_MAX_ENTRIES) n_raw = IDX_MAX_ENTRIES;

    cap_idx_entry_t *ent = (cap_idx_entry_t *)buffer_calloc((size_t)n_raw, IDX_ENTRY_SIZE);
    if (!ent) return NULL;
    void *fd = disk_file_fopen(fs, mpath, "r");
    if (!fd) { buffer_free(ent); return NULL; }
    int rd = disk_file_fread(fs, fd, ent, (size_t)n_raw * IDX_ENTRY_SIZE);
    disk_file_fclose(fs, fd);
    if (rd < (int)IDX_ENTRY_SIZE) { buffer_free(ent); return NULL; }
    int n_read = rd / (int)IDX_ENTRY_SIZE;

    /* Sanity: the first entry's id must start with "cap_". If not, the .idx is
     * corrupt or an old format (e.g. 33-byte entries from a prior build mixed
     * with 36-byte entries) - reading at 36-byte offsets produces garbled ids.
     * Delete the corrupt .idx so rebuild_index_if_needed recreates it from the
     * .json files (which always hold correct ids). */
    if (n_read > 0 && strncmp(ent[0].id, "cap_", 4) != 0) {
        LOG_SVC_ERROR("manifest_load_day: corrupt .idx (first id garbled) - deleting %s", mpath);
        buffer_free(ent);
        (void)disk_file_remove(fs, mpath);
        return NULL;
    }

    /* Dedup keeping the latest entry per id. Open-addressing hash id→index;
     * since entries are append-ordered, setting slot=i on each visit leaves
     * the last occurrence index in the slot. */
    #define IDX_HASH_SIZE 4096
    int32_t *hslot = (int32_t *)buffer_calloc(IDX_HASH_SIZE, sizeof(int32_t));
    if (!hslot) { buffer_free(ent); return NULL; }
    for (int i = 0; i < IDX_HASH_SIZE; i++) hslot[i] = -1;
    for (int i = 0; i < n_read; i++) {
        uint32_t h = id_hash(ent[i].id) & (IDX_HASH_SIZE - 1);
        for (;;) {
            int s = hslot[h];
            if (s < 0) { hslot[h] = i; break; }                /* new id */
            if (strncmp(ent[s].id, ent[i].id, sizeof(ent[i].id)) == 0) {
                hslot[h] = i; break;                            /* update latest */
            }
            h = (h + 1) & (IDX_HASH_SIZE - 1);
        }
    }
    /* Collect the latest entry per id into a SEPARATE array. In-place
     * compaction of `ent` is unsafe: a later hash slot may reference a low
     * index that an earlier compaction step already overwrote, resurrecting a
     * stale (e.g. pre-tombstone) entry for that id. Copying out avoids that. */
    int n_uniq = 0;
    for (int s = 0; s < IDX_HASH_SIZE; s++) if (hslot[s] >= 0) n_uniq++;
    cap_idx_entry_t *uniq = (cap_idx_entry_t *)buffer_calloc(
        (size_t)(n_uniq ? n_uniq : 1), IDX_ENTRY_SIZE);
    if (!uniq) { buffer_free(hslot); buffer_free(ent); return NULL; }
    int u = 0;
    for (int s = 0; s < IDX_HASH_SIZE; s++) {
        int i = hslot[s];
        if (i < 0) continue;
        uniq[u++] = ent[i];
    }
    buffer_free(hslot);
    buffer_free(ent);   /* raw array no longer needed */
    #undef IDX_HASH_SIZE

    /* Drop tombstones (DELETED) - they are not current records. */
    int n_live = 0;
    for (int i = 0; i < n_uniq; i++) {
        if (uniq[i].state == IDX_STATE_DELETED) continue;
        if (i != n_live) uniq[n_live] = uniq[i];
        n_live++;
    }
    qsort(uniq, (size_t)n_live, IDX_ENTRY_SIZE, idx_entry_cmp_ts);
    if (out_n) *out_n = n_live;
    return uniq;
}

/* Enumerate date manifest files in /captures/index/, filtered to
 * [from_date, to_date] (lexicographic = chronological for YYYY-MM-DD).
 * Fills caller-allocated `dates` ([][12]); returns count. sort_desc reverses. */
static int enumerate_date_files(FS_Type_t fs, char (*dates)[12], int max,
                                const char *from_date, const char *to_date,
                                aicam_bool_t sort_desc)
{
    int n = 0;
    void *dd = disk_file_opendir(fs, CAPTURES_DIR_INDEX);
    if (dd) {
        dir_entry_t e;
        while (n < max && disk_file_readdir(fs, dd, (char *)&e) > 0) {
            const char *name = e.name;
            if (name[0] == '.') continue;
            size_t nlen = strlen(name);
            if (nlen != 14) continue;               /* "YYYY-MM-DD.idx" */
            if (strcmp(name + 10, ".idx") != 0) continue;
            if (name[4] != '-' || name[7] != '-') continue;
            int ok = 1;
            for (int i = 0; i < 10; i++) {
                if (i == 4 || i == 7) continue;
                if (name[i] < '0' || name[i] > '9') { ok = 0; break; }
            }
            if (!ok) continue;
            char dn[12];
            memcpy(dn, name, 10); dn[10] = 0;
            if (from_date && strcmp(dn, from_date) < 0) continue;
            if (to_date && strcmp(dn, to_date) > 0) continue;
            memcpy(dates[n], dn, 11);
            n++;
        }
        disk_file_closedir(fs, dd);
    }
    for (int i = 1; i < n; i++) {                    /* insertion sort asc */
        char tmp[12]; memcpy(tmp, dates[i], 12);
        int j = i - 1;
        while (j >= 0 && strcmp(dates[j], tmp) > 0) {
            memcpy(dates[j+1], dates[j], 12); j--;
        }
        memcpy(dates[j+1], tmp, 12);
    }
    if (sort_desc) {
        for (int i = 0; i < n/2; i++) {
            char tmp[12]; memcpy(tmp, dates[i], 12);
            memcpy(dates[i], dates[n-1-i], 12);
            memcpy(dates[n-1-i], tmp, 12);
        }
    }
    return n;
}

/* Iterate records currently in `filter_state`, reading manifests (NOT opendir).
 *   offset/limit - pagination (visit mode); limit==0 = unlimited
 *   from_ts/to_ts - inclusive time range; 0 / UINT64_MAX = unbounded
 *   sort_desc - newest first
 *   fn/user - visitor; NULL = count mode (return matching total, no alloc)
 * The manifest snapshot is in RAM, so the visitor may freely delete/move
 * records (it touches meta/data files, not the index file we already closed).
 * Returns: count mode → matching total; visit mode → records passed to fn. */
static int iterate_records(FS_Type_t fs, record_state_t filter_state,
                            uint32_t offset, uint32_t limit,
                            uint64_t from_ts, uint64_t to_ts,
                            aicam_bool_t sort_desc,
                            record_visitor_t fn, void *user)
{
    char from_date[12], to_date[12];
    date_str_from_ts(from_ts > 0 ? from_ts : 0, from_date, sizeof(from_date));
    if (to_ts == UINT64_MAX) snprintf(to_date, sizeof(to_date), "9999-99-99");
    else date_str_from_ts(to_ts, to_date, sizeof(to_date));

    char (*dates)[12] = (char (*)[12])buffer_calloc(MAX_DATE_FILES, 12);
    if (!dates) return 0;
    int ndates = enumerate_date_files(fs, dates, MAX_DATE_FILES,
                                      from_date, to_date, sort_desc);

    uint32_t collected = 0, emitted = 0;
    int total = 0;
    aicam_bool_t stop = AICAM_FALSE;

    for (int di = 0; di < ndates && !stop; di++) {
        int nent = 0;
        cap_idx_entry_t *ent = manifest_load_day(fs, dates[di], &nent);
        if (!ent) continue;

        for (int idx = 0; idx < nent && !stop; idx++) {
            int k = sort_desc ? (nent - 1 - idx) : idx;
            if (ent[k].state != (uint8_t)filter_state) continue;
            uint32_t rts = ent[k].timestamp;
            if (rts != 0) {
                if (from_ts > 0 && rts < from_ts) continue;
                if (to_ts != UINT64_MAX && rts > to_ts) continue;
            }
            if (fn == NULL) { total++; continue; }
            if (collected < offset) { collected++; continue; }
            if (limit > 0 && emitted >= limit) { stop = AICAM_TRUE; break; }
            /* id field is NUL-padded (≤24B); copy to a 64B buf for safety. */
            char idbuf[64];
            size_t idl = 0;
            while (idl < sizeof(ent[k].id) && ent[k].id[idl]) idl++;
            if (idl >= sizeof(idbuf)) idl = sizeof(idbuf) - 1;
            memcpy(idbuf, ent[k].id, idl); idbuf[idl] = 0;
            emitted++;
            if (fn(fs, idbuf, user) != AICAM_OK) { stop = AICAM_TRUE; break; }
        }
        buffer_free(ent);
    }
    buffer_free(dates);
    return fn ? (int)emitted : total;
}

static uint32_t count_state(FS_Type_t fs, record_state_t state)
{
    if (fs == FS_MAX) return 0;
    uint64_t tc = rtc_get_uptime_ms();
    uint32_t n = (uint32_t)iterate_records(fs, state, 0, 0, 0, UINT64_MAX,
                                           AICAM_FALSE, NULL, NULL);
    uint64_t dt = rtc_get_uptime_ms() - tc;
    if (dt > 5) {
        UPLOAD_LOG("count_state(%d)=%u time=%lu ms\r\n",
                   (int)state, (unsigned)n, (unsigned long)dt);
    }
    return n;
}

/* Fill counts for all 4 states in a single manifest sweep (get_status). */
static void manifest_counts_all(FS_Type_t fs, uint32_t counts[4])
{
    counts[0] = counts[1] = counts[2] = counts[3] = 0;
    if (fs == FS_MAX) return;
    char (*dates)[12] = (char (*)[12])buffer_calloc(MAX_DATE_FILES, 12);
    if (!dates) return;
    int ndates = enumerate_date_files(fs, dates, MAX_DATE_FILES,
                                      "0000-00-00", "9999-99-99", AICAM_FALSE);
    for (int di = 0; di < ndates; di++) {
        int nent = 0;
        cap_idx_entry_t *ent = manifest_load_day(fs, dates[di], &nent);
        if (!ent) continue;
        for (int i = 0; i < nent; i++) {
            uint8_t s = ent[i].state;
            if (s < 4) counts[s]++;
        }
        buffer_free(ent);
    }
    buffer_free(dates);
}

/* Repair: rebuild /captures/index/<date>.idx from the metadata .json files
 * under /captures/meta/. Called at init when the manifest appears lost/empty
 * while meta/ has records (e.g. volume migrated or index corrupted). Expensive
 * - reads every .json - but rare. Duplicate-safe: manifest_load_day dedups by
 * id, so an existing partial .idx would not double-count. We still truncate
 * each day's .idx first for cleanliness. */
static aicam_result_t rebuild_index(FS_Type_t fs)
{
    char dates[MAX_DATE_FILES][12];
    int ndates = 0;
    void *dd = disk_file_opendir(fs, CAPTURES_DIR_META);
    if (!dd) return AICAM_ERROR;
    dir_entry_t e;
    while (ndates < MAX_DATE_FILES && disk_file_readdir(fs, dd, (char *)&e) > 0) {
        const char *name = e.name;
        if (name[0] == '.') continue;
        size_t nlen = strlen(name);
        if (nlen != 10) continue;
        if (name[4] != '-' || name[7] != '-') continue;
        int ok = 1;
        for (int i = 0; i < 10; i++) {
            if (i == 4 || i == 7) continue;
            if (name[i] < '0' || name[i] > '9') { ok = 0; break; }
        }
        if (!ok) continue;
        memcpy(dates[ndates], name, 11);
        ndates++;
    }
    disk_file_closedir(fs, dd);

    for (int d = 0; d < ndates; d++) {
        char ipath[96];
        snprintf(ipath, sizeof(ipath), "%s/%s.idx", CAPTURES_DIR_INDEX, dates[d]);
        (void)disk_file_remove(fs, ipath);   /* truncate existing */

        char mpath[160];
        snprintf(mpath, sizeof(mpath), "%s/%s", CAPTURES_DIR_META, dates[d]);
        void *hd = disk_file_opendir(fs, mpath);
        if (!hd) continue;
        dir_entry_t he;
        while (disk_file_readdir(fs, hd, (char *)&he) > 0) {
            const char *hname = he.name;
            if (hname[0] == '.') continue;
            char hpath[448];   /* mpath(≤159) + '/' + name(≤255) + NUL */
            snprintf(hpath, sizeof(hpath), "%s/%s", mpath, hname);
            void *fd_dir = disk_file_opendir(fs, hpath);
            if (!fd_dir) continue;
            dir_entry_t fe;
            while (disk_file_readdir(fs, fd_dir, (char *)&fe) > 0) {
                const char *fname = fe.name;
                if (fname[0] == '.') continue;
                size_t fl = strlen(fname);
                if (fl < 6 || strcmp(fname + fl - 5, ".json") != 0) continue;
                if (strncmp(fname, "cap_", 4) != 0) continue;
                char idbuf[64];
                size_t copy = fl - 5; if (copy >= 63) copy = 63;
                memcpy(idbuf, fname, copy); idbuf[copy] = 0;

                char jpath[704];   /* hpath(≤447) + '/' + name(≤255) + NUL */
                snprintf(jpath, sizeof(jpath), "%s/%s", hpath, fname);
                cJSON *meta = NULL;
                if (parse_meta_file(fs, jpath, &meta) != AICAM_OK) continue;
                uint8_t state = RECORD_STATE_PENDING;
                cJSON *t = cJSON_GetObjectItem(meta, "state");
                if (t) state = (uint8_t)t->valueint;
                uint32_t ts = 0;
                t = cJSON_GetObjectItem(meta, "timestamp");
                if (t) ts = (uint32_t)t->valuedouble;
                uint32_t size = 0;
                t = cJSON_GetObjectItem(meta, "size");
                if (t) size = (uint32_t)t->valueint;
                cJSON_Delete(meta);
                (void)manifest_append(fs, idbuf, state, ts, size);
            }
            disk_file_closedir(fs, fd_dir);
        }
        disk_file_closedir(fs, hd);
    }
    g_count_cache_dirty = true;
    return AICAM_OK;
}

/* One-time migration: the id field used to be 20 bytes, which truncated the
 * last seq digit of "cap_<ts>_<seq>" ids - every .idx entry pointed at a
 * non-existent file. Now it's 24 bytes (36-byte entries). Old 32-byte .idx
 * files are unreadable correctly (and 3168 is divisible by both 32 and 36, so
 * a size-modulo check can't detect them). Use a sentinel: if absent, delete
 * all *.idx so rebuild_index recreates them from the .json files (which always
 * held the correct full ids). Runs once. */
static void migrate_idx_format_if_needed(FS_Type_t fs)
{
    if (fs == FS_MAX) return;
    char spath[64];
    snprintf(spath, sizeof(spath), "%s/.v36", CAPTURES_DIR_INDEX);
    struct stat st = {0};
    if (disk_file_stat(fs, spath, &st) == 0) return;  /* already migrated */

    LOG_SVC_INFO("upload: migrating index format (old 32B → 36B entries)");
    void *dd = disk_file_opendir(fs, CAPTURES_DIR_INDEX);
    if (dd) {
        dir_entry_t e;
        while (disk_file_readdir(fs, dd, (char *)&e) > 0) {
            const char *name = e.name;
            if (name[0] == '.') continue;
            size_t nlen = strlen(name);
            if (nlen > 4 && strcmp(name + nlen - 4, ".idx") == 0) {
                char path[288];
                snprintf(path, sizeof(path), "%s/%s", CAPTURES_DIR_INDEX, name);
                (void)disk_file_remove(fs, path);
            }
        }
        disk_file_closedir(fs, dd);
    }
    /* Create the sentinel so this never runs again. */
    void *fd = disk_file_fopen(fs, spath, "w");
    if (fd) { (void)disk_file_fwrite(fs, fd, (const uint8_t *)"v36", 3); disk_file_fclose(fs, fd); }
}

/* At init: if /captures/index has no .idx files but /captures/meta has records,
 * the manifest is missing - rebuild it once. Cheap when both are empty (fresh
 * volume): two opendirs that return immediately. */
static void rebuild_index_if_needed(FS_Type_t fs)
{
    if (fs == FS_MAX) return;
    migrate_idx_format_if_needed(fs);
    int idx_count = 0;
    void *dd = disk_file_opendir(fs, CAPTURES_DIR_INDEX);
    if (dd) {
        dir_entry_t e;
        while (disk_file_readdir(fs, dd, (char *)&e) > 0) {
            const char *name = e.name;
            if (name[0] == '.') continue;
            size_t nlen = strlen(name);
            if (nlen == 14 && strcmp(name + 10, ".idx") == 0) {
                idx_count++;
                if (idx_count > 0) break;  /* one is enough to skip rebuild */
            }
        }
        disk_file_closedir(fs, dd);
    }
    if (idx_count > 0) return;  /* manifest present - trust it */

    int meta_count = 0;
    dd = disk_file_opendir(fs, CAPTURES_DIR_META);
    if (dd) {
        dir_entry_t e;
        while (disk_file_readdir(fs, dd, (char *)&e) > 0) {
            if (e.name[0] == '.') continue;
            meta_count++;
            if (meta_count > 0) break;
        }
        disk_file_closedir(fs, dd);
    }
    if (meta_count == 0) return;  /* fresh volume - nothing to rebuild */

    LOG_SVC_INFO("upload: manifest missing, rebuilding index from meta/");
    rebuild_index(fs);
}

/* ==================== Cleanup / wrap-around ==================== */

typedef struct {
    FS_Type_t fs;
    record_state_t state;
    uint64_t target_freed;
    uint64_t freed_so_far;
    aicam_bool_t enough;
    aicam_bool_t delete_failed;  /* a remove failed mid-way → FS likely corrupt */
    uint64_t deadline_ms;   /* rtc uptime ms; stop deleting past this */
} cleanup_ctx_t;

static aicam_result_t cleanup_visitor(FS_Type_t fs, const char *id, void *user)
{
    cleanup_ctx_t *ctx = (cleanup_ctx_t *)user;
    if (ctx->enough) return AICAM_ERROR; /* stop iteration */
    /* On a full littlefs each remove can trigger compaction (~seconds), so
     * deleting many records to free space can hang the wake. Bound it. */
    if (ctx->deadline_ms && rtc_get_uptime_ms() >= ctx->deadline_ms) {
        return AICAM_ERROR; /* stop iteration - out of budget */
    }

    /* delete_record_files reports the CONTENT bytes actually freed (only counts
     * successful removes). That's a lower bound on blocks freed (littlefs per-
     * file overhead is extra), so accumulating it to target_freed is a safe
     * stop condition - no need to re-probe free space with lfs_fs_size.
     * If it returns error, a remove failed (FS likely corrupt) - stop the whole
     * cleanup; continuing would just fail on every other file too. */
    uint64_t freed = 0;
    if (delete_record_files(fs, id, ctx->state, "cleanup_for_space", &freed) != AICAM_OK) {
        ctx->delete_failed = AICAM_TRUE;
        return AICAM_ERROR;  /* stop iteration */
    }

    ctx->freed_so_far += freed;
    if (ctx->freed_so_far >= ctx->target_freed) ctx->enough = AICAM_TRUE;
    return AICAM_OK;
}

static aicam_result_t cleanup_for_space(FS_Type_t fs, uint64_t need_bytes)
{
    /* Priority order: sent -> local -> failed -> pending */
    const record_state_t order[] = {
        RECORD_STATE_SENT, RECORD_STATE_LOCAL, RECORD_STATE_FAILED, RECORD_STATE_PENDING
    };

    cleanup_ctx_t ctx = { .fs = fs, .target_freed = need_bytes,
                          .deadline_ms = rtc_get_uptime_ms() + CLEANUP_MAX_MS };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        ctx.state = order[i];
        /* Oldest date first (sort_desc=false) so wrap policy evicts the
         * oldest records first. limit=0 = unlimited (chunked flush inside
         * iterate keeps memory bounded); visitor stops via return code. */
        iterate_records(fs, order[i], 0, 0,
                        0, UINT64_MAX, AICAM_FALSE,
                        cleanup_visitor, &ctx);
        if (ctx.enough) break;
        if (ctx.delete_failed) break;  /* FS corrupt - stop the whole cleanup */
        /* No lfs_fs_size re-check here. freed_so_far counts only bytes from
         * files actually removed (a lower bound on blocks freed - littlefs
         * per-file overhead is extra), so reaching target_freed by accumulation
         * guarantees the space is free. This avoids a full-FS traverse
         * (lfs_fs_size, ~120ms+ and fails on a corrupt FS) on every state pass,
         * and avoids the lfs_fs_size-failure case entirely. */
    }
    if (ctx.delete_failed) {
        LOG_SVC_ERROR("cleanup_for_space: delete failed - FS may be corrupt, aborting");
        return AICAM_ERROR;
    }
    return ctx.enough ? AICAM_OK : AICAM_ERROR;
}

/* Drop sent records older than keep_sent_hours. */
typedef struct {
    FS_Type_t fs;
    uint64_t  cutoff_ts;
} purge_ctx_t;

static aicam_result_t purge_visitor(FS_Type_t fs, const char *id, void *user)
{
    purge_ctx_t *ctx = (purge_ctx_t *)user;
    /* id format: cap_<ts>_<seq> - parse the timestamp portion */
    unsigned long ts = 0;
    if (sscanf(id, "cap_%lu_", &ts) != 1) return AICAM_OK;
    if (ts < ctx->cutoff_ts) {
        /* Stop purge on FS error (remove failed → likely corrupt); continuing
         * would just fail on every other record too. */
        if (delete_record_files(fs, id, RECORD_STATE_SENT, "purge_old_sent", NULL) != AICAM_OK) {
            return AICAM_ERROR;
        }
    }
    return AICAM_OK;
}

static aicam_result_t purge_old_sent(FS_Type_t fs)
{
    uint64_t now = now_unix();
    uint64_t cutoff_secs = (uint64_t)g_up.cfg.keep_sent_hours * 3600u;
    /* keep_sent_hours=0 → cutoff_ts = now → delete all sent.
     * Otherwise cutoff_ts = now - keep window; records older than that are
     * purged. Pass to_ts=cutoff_ts so iterate skips recent date dirs entirely
     * (no readdir of dirs that can't contain purgeable records). */
    uint64_t cutoff_ts = (now > cutoff_secs) ? (now - cutoff_secs) : now;
    purge_ctx_t ctx = { .fs = fs, .cutoff_ts = cutoff_ts };
    iterate_records(fs, RECORD_STATE_SENT, 0, 0,
                    0, cutoff_ts, AICAM_FALSE,
                    purge_visitor, &ctx);
    return AICAM_OK;
}

/* ==================== Upload one record ==================== */

/* Shared loader: parse metadata + load JPEG + reconstruct mqtt_image_metadata_t.
 * Used by both the MQTT pipeline path and the webhook serial path. */
static aicam_result_t record_load(FS_Type_t fs, const char *id,
                                   cJSON **out_meta,
                                   uint8_t **out_jpeg, uint32_t *out_jpeg_size,
                                   mqtt_image_metadata_t *out_m,
                                   upload_proto_t *out_proto)
{
    if (!out_meta || !out_jpeg || !out_jpeg_size || !out_m || !out_proto) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    *out_meta = NULL; *out_jpeg = NULL; *out_jpeg_size = 0;

    char meta_path[128];
    path_for_meta(meta_path, sizeof(meta_path), id);
    if (parse_meta_file(fs, meta_path, out_meta) != AICAM_OK) return AICAM_ERROR;

    char img_path[128];
    path_for_data(img_path, sizeof(img_path), id, 'p');
    struct stat st = {0};
    if (disk_file_stat(fs, img_path, &st) != 0 || st.st_size <= 0) {
        cJSON_Delete(*out_meta); *out_meta = NULL;
        return AICAM_ERROR;
    }
    uint8_t *jpeg = (uint8_t *)buffer_calloc(1, (size_t)st.st_size);
    if (!jpeg) { cJSON_Delete(*out_meta); *out_meta = NULL; return AICAM_ERROR_NO_MEMORY; }
    void *fd = disk_file_fopen(fs, img_path, "r");
    if (!fd) { buffer_free(jpeg); cJSON_Delete(*out_meta); *out_meta = NULL; return AICAM_ERROR; }
    int rn = disk_file_fread(fs, fd, jpeg, (size_t)st.st_size);
    disk_file_fclose(fs, fd);
    if (rn != (int)st.st_size) {
        buffer_free(jpeg); cJSON_Delete(*out_meta); *out_meta = NULL;
        return AICAM_ERROR;
    }

    *out_jpeg = jpeg;
    *out_jpeg_size = (uint32_t)st.st_size;

    memset(out_m, 0, sizeof(*out_m));
    cJSON *t;
    t = cJSON_GetObjectItem(*out_meta, "image_id");
    if (t && cJSON_IsString(t)) snprintf(out_m->image_id, sizeof(out_m->image_id), "%s", t->valuestring);
    t = cJSON_GetObjectItem(*out_meta, "timestamp"); if (t) out_m->timestamp = (uint64_t)t->valuedouble;
    t = cJSON_GetObjectItem(*out_meta, "width");     if (t) out_m->width    = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(*out_meta, "height");    if (t) out_m->height   = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(*out_meta, "size");      if (t) out_m->size     = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(*out_meta, "quality");   if (t) out_m->quality  = (uint8_t)t->valueint;
    t = cJSON_GetObjectItem(*out_meta, "format");    if (t) out_m->format   = (mqtt_image_format_t)t->valueint;
    t = cJSON_GetObjectItem(*out_meta, "trigger_type_id");
    if (t) out_m->trigger_type = (aicam_capture_trigger_t)t->valueint;

    *out_proto = UPLOAD_PROTO_MQTT;
    t = cJSON_GetObjectItem(*out_meta, "upload_protocol");
    if (t && cJSON_IsString(t) && strcmp(t->valuestring, "webhook") == 0) *out_proto = UPLOAD_PROTO_WEBHOOK;

    return AICAM_OK;
}

/* Failure path: bump retry_count, write back, maybe promote to failed/. */
static void record_mark_failed(FS_Type_t fs, const char *id, const char *err)
{
    char meta_path[128];
    path_for_meta(meta_path, sizeof(meta_path), id);
    cJSON *meta = NULL;
    if (parse_meta_file(fs, meta_path, &meta) != AICAM_OK) return;

    uint8_t retry = 0;
    cJSON *t = cJSON_GetObjectItem(meta, "retry_count");
    if (t) retry = (uint8_t)t->valueint;
    retry++;
    cJSON_ReplaceItemInObject(meta, "retry_count", cJSON_CreateNumber(retry));
    cJSON_ReplaceItemInObject(meta, "last_error", cJSON_CreateString(err ? err : "upload_failed"));

    char *str = cJSON_PrintUnformatted(meta);
    if (str) {
        /* Temp + atomic rename so a power cut mid-rewrite leaves the prior
         * .json intact instead of a truncated file (which would make the
         * record unloadable but still listed as PENDING). */
        char tmp_path[136];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);
        if (write_file(fs, tmp_path, (const uint8_t *)str, (uint32_t)strlen(str)) == AICAM_OK) {
            if (disk_file_rename(fs, tmp_path, meta_path) != 0) {
                (void)disk_file_remove(fs, tmp_path);
            }
        }
        cJSON_free(str);
    }
    cJSON_Delete(meta);

    if (g_up.cfg.retry_max_attempts > 0 && retry >= g_up.cfg.retry_max_attempts) {
        (void)move_record(fs, id, RECORD_STATE_PENDING, RECORD_STATE_FAILED);
    }
}

/* MQTT pipeline publish: load + publish (no ack wait). On success returns
 * AICAM_OK and *out_msg_id = the MQTT packet id. On failure bumps retry and
 * returns the error. Caller frees nothing - jpeg is freed internally. */
static aicam_result_t record_publish_mqtt(FS_Type_t fs, const char *id, int *out_msg_id)
{
    if (out_msg_id) *out_msg_id = -1;

    cJSON *meta = NULL;
    uint8_t *jpeg = NULL;
    uint32_t jpeg_size = 0;
    mqtt_image_metadata_t m = {0};
    upload_proto_t proto = UPLOAD_PROTO_MQTT;

    aicam_result_t r = record_load(fs, id, &meta, &jpeg, &jpeg_size, &m, &proto);
    if (r != AICAM_OK) return r;

    aicam_result_t result = AICAM_ERROR;
    int msg_id = -1;

    if (mqtt_service_is_running() && mqtt_service_is_connected()) {
        const uint32_t threshold = 1024u * 1024u;

        /* Load AI result from persisted file if present */
        mqtt_ai_result_t ai_from_disk;
        const mqtt_ai_result_t *ai_ptr = NULL;
        if (load_ai_result_from_disk(fs, id, &ai_from_disk) == AICAM_OK) {
            ai_ptr = &ai_from_disk;
        }

        int rc;
        if (m.size < threshold) {
            rc = mqtt_service_publish_image_with_ai(NULL, jpeg, jpeg_size, &m, ai_ptr);
        } else {
            rc = mqtt_service_publish_image_chunked(NULL, jpeg, jpeg_size, &m, ai_ptr, 100 * 1024);
        }
        if (rc >= 0) {
            msg_id = rc;
            result = AICAM_OK;
        } else {
            result = AICAM_ERROR;
        }
    } else {
        result = AICAM_ERROR_UNAVAILABLE;
    }

    buffer_free(jpeg);
    cJSON_Delete(meta);

    if (result != AICAM_OK) {
        record_mark_failed(fs, id,
            result == AICAM_ERROR_UNAVAILABLE ? "network_unavailable" : "publish_failed");
    } else if (out_msg_id) {
        *out_msg_id = msg_id;
    }
    
    return result;
}

/* Load persisted AI result from disk. Returns AICAM_OK and fills *out if the
 * ai_result JSON file exists and can be parsed into nn_result_t. Caller must
 * free *out with buffer_free. */
static aicam_result_t load_ai_result_from_disk(FS_Type_t fs, const char *id,
                                               mqtt_ai_result_t *out)
{
    if (!out) return AICAM_ERROR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));

    /* Read AI JSON file */
    char ai_path[128];
    path_for_data(ai_path, sizeof(ai_path), id, 'a');
    char *text = NULL;
    uint32_t size = 0;
    if (read_text_file(fs, ai_path, &text, &size) != AICAM_OK || !text) {
        return AICAM_ERROR;
    }
    cJSON *json = cJSON_Parse(text);
    buffer_free(text);
    if (!json) return AICAM_ERROR;

    /* Parse type and detection/pose count */
    nn_result_t nn = {0};
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (type_item) nn.type = (pp_type_t)type_item->valueint;

    cJSON *det_count = cJSON_GetObjectItem(json, "detection_count");
    cJSON *pose_count = cJSON_GetObjectItem(json, "pose_count");

    if (nn.type == PP_TYPE_OD && det_count && det_count->valueint > 0) {
        cJSON *detections = cJSON_GetObjectItem(json, "detections");
        if (detections && cJSON_IsArray(detections)) {
            int n = cJSON_GetArraySize(detections);
            if (n > 0 && (uint32_t)n <= det_count->valueint) {
                nn.od.detects = (od_detect_t *)buffer_calloc((size_t)n, sizeof(od_detect_t));
                if (nn.od.detects) {
                    for (int i = 0; i < n; i++) {
                        cJSON *d = cJSON_GetArrayItem(detections, i);
                        if (!d) continue;
                        cJSON *cx = cJSON_GetObjectItem(d, "x");
                        cJSON *cy = cJSON_GetObjectItem(d, "y");
                        cJSON *cw = cJSON_GetObjectItem(d, "width");
                        cJSON *ch = cJSON_GetObjectItem(d, "height");
                        cJSON *cc = cJSON_GetObjectItem(d, "conf");
                        cJSON *ccls = cJSON_GetObjectItem(d, "class_name");
                        if (cx) nn.od.detects[i].x = (float)cx->valuedouble;
                        if (cy) nn.od.detects[i].y = (float)cy->valuedouble;
                        if (cw) nn.od.detects[i].width = (float)cw->valuedouble;
                        if (ch) nn.od.detects[i].height = (float)ch->valuedouble;
                        if (cc) nn.od.detects[i].conf = (float)cc->valuedouble;
                        if (ccls && cJSON_IsString(ccls)) {
                            size_t clen = strlen(ccls->valuestring) + 1;
                            nn.od.detects[i].class_name = (char *)buffer_calloc(1, clen);
                            if (nn.od.detects[i].class_name)
                                memcpy(nn.od.detects[i].class_name, ccls->valuestring, clen);
                        }
                    }
                    nn.od.nb_detect = (uint8_t)n;
                    nn.is_valid = 1;
                }
            }
        }
    } else if (nn.type == PP_TYPE_MPE && pose_count && pose_count->valueint > 0) {
        cJSON *poses = cJSON_GetObjectItem(json, "poses");
        if (poses && cJSON_IsArray(poses)) {
            int n = cJSON_GetArraySize(poses);
            if (n > 0 && (uint32_t)n <= pose_count->valueint) {
                nn.mpe.detects = (mpe_detect_t *)buffer_calloc((size_t)n, sizeof(mpe_detect_t));
                if (nn.mpe.detects) {
                    for (int i = 0; i < n; i++) {
                        cJSON *p = cJSON_GetArrayItem(poses, i);
                        if (!p) continue;
                        cJSON *cx = cJSON_GetObjectItem(p, "x");
                        cJSON *cy = cJSON_GetObjectItem(p, "y");
                        cJSON *cw = cJSON_GetObjectItem(p, "width");
                        cJSON *ch = cJSON_GetObjectItem(p, "height");
                        cJSON *cc = cJSON_GetObjectItem(p, "conf");
                        if (cx) nn.mpe.detects[i].x = (float)cx->valuedouble;
                        if (cy) nn.mpe.detects[i].y = (float)cy->valuedouble;
                        if (cw) nn.mpe.detects[i].width = (float)cw->valuedouble;
                        if (ch) nn.mpe.detects[i].height = (float)ch->valuedouble;
                        if (cc) nn.mpe.detects[i].conf = (float)cc->valuedouble;
                    }
                    nn.mpe.nb_detect = (uint8_t)n;
                    nn.is_valid = 1;
                }
            }
        }
    }
    cJSON_Delete(json);

    if (!nn.is_valid) return AICAM_ERROR;

    /* Build mqtt_ai_result_t with default model info.
     * NOTE: mqtt_service_init_ai_result does a shallow memcpy of nn_result_t,
     * so the detects/class_name pointers are now owned by *out. Do NOT free
     * the arrays here — the caller stack-lifetime outlives the MQTT call. */
    mqtt_service_init_ai_result(out, &nn, NULL, NULL, 0);
    return AICAM_OK;
}

/* Webhook serial path: load + push + wait_pending + move/retry. */
static aicam_result_t upload_one_record_webhook(FS_Type_t fs, const char *id)
{
    cJSON *meta = NULL;
    uint8_t *jpeg = NULL;
    uint32_t jpeg_size = 0;
    mqtt_image_metadata_t m = {0};
    upload_proto_t proto = UPLOAD_PROTO_WEBHOOK;

    aicam_result_t r = record_load(fs, id, &meta, &jpeg, &jpeg_size, &m, &proto);
    if (r != AICAM_OK) return r;

    aicam_result_t result = AICAM_ERROR;
    if (webhook_service_is_enabled()) {
        /* Load AI result from persisted file if present */
        mqtt_ai_result_t ai_from_disk;
        const mqtt_ai_result_t *ai_ptr = NULL;
        if (load_ai_result_from_disk(fs, id, &ai_from_disk) == AICAM_OK) {
            ai_ptr = &ai_from_disk;
        }

        uint8_t *copy = (uint8_t *)buffer_calloc(1, jpeg_size);
        if (copy) {
            memcpy(copy, jpeg, jpeg_size);
            aicam_result_t pr = webhook_service_push_capture(copy, jpeg_size, &m, ai_ptr);
            if (pr == AICAM_OK) {
                result = (webhook_service_wait_pending(30000) == AICAM_OK) ? AICAM_OK : AICAM_ERROR;
            } else {
                buffer_free(copy);
                result = AICAM_ERROR;
            }
        } else {
            result = AICAM_ERROR_NO_MEMORY;
        }
    } else {
        result = AICAM_ERROR_UNAVAILABLE;
    }

    buffer_free(jpeg);
    cJSON_Delete(meta);

    if (result == AICAM_OK) {
        return move_record(fs, id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
    }
    record_mark_failed(fs, id,
        result == AICAM_ERROR_UNAVAILABLE ? "network_unavailable" : "upload_failed");
    return result;
}

static aicam_result_t upload_one_record(FS_Type_t fs, const char *id,
                                        const mqtt_ai_result_t *ai_result_in)
{
    char meta_path[128];
    path_for_meta(meta_path, sizeof(meta_path), id);

    cJSON *meta = NULL;
    if (parse_meta_file(fs, meta_path, &meta) != AICAM_OK) return AICAM_ERROR;

    /* Load primary image into RAM */
    char img_path[128];
    path_for_data(img_path, sizeof(img_path), id, 'p');
    struct stat st = {0};
    if (disk_file_stat(fs, img_path, &st) != 0 || st.st_size <= 0) {
        cJSON_Delete(meta);
        return AICAM_ERROR;
    }
    uint8_t *jpeg = (uint8_t *)buffer_calloc(1, (size_t)st.st_size);
    if (!jpeg) { cJSON_Delete(meta); return AICAM_ERROR_NO_MEMORY; }
    void *fd = disk_file_fopen(fs, img_path, "r");
    if (!fd) { buffer_free(jpeg); cJSON_Delete(meta); return AICAM_ERROR; }
    int rn = disk_file_fread(fs, fd, jpeg, (size_t)st.st_size);
    disk_file_fclose(fs, fd);
    if (rn != (int)st.st_size) {
        buffer_free(jpeg); cJSON_Delete(meta);
        return AICAM_ERROR;
    }

    /* Reconstruct mqtt_image_metadata_t from JSON */
    mqtt_image_metadata_t m = {0};
    cJSON *t;
    t = cJSON_GetObjectItem(meta, "image_id");
    if (t && cJSON_IsString(t)) snprintf(m.image_id, sizeof(m.image_id), "%s", t->valuestring);
    t = cJSON_GetObjectItem(meta, "timestamp"); if (t) m.timestamp = (uint64_t)t->valuedouble;
    t = cJSON_GetObjectItem(meta, "width");     if (t) m.width    = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "height");    if (t) m.height   = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "size");      if (t) m.size     = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "quality");   if (t) m.quality  = (uint8_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "format");    if (t) m.format   = (mqtt_image_format_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "trigger_type_id");
    if (t) m.trigger_type = (aicam_capture_trigger_t)t->valueint;

    /* Determine protocol from record (frozen at capture time) */
    upload_proto_t proto = UPLOAD_PROTO_MQTT;
    t = cJSON_GetObjectItem(meta, "upload_protocol");
    if (t && cJSON_IsString(t) && strcmp(t->valuestring, "webhook") == 0) proto = UPLOAD_PROTO_WEBHOOK;

    aicam_result_t result = AICAM_ERROR;

    if (proto == UPLOAD_PROTO_MQTT) {
        /* INSTANT mode may fire before the 4G/WiFi link + MQTT broker
         * connection are fully established. Wait (up to a budget) instead
         * of failing immediately - the record is already persisted to
         * pending/ and we'd rather get it out now than leave a retry
         * backlog. Matches the 40 s timeout historically used by
         * system_service for the same purpose.
         *
         * Wake fast-fail: also listen for SERVICE_READY_NETIF_ALL_FAILED so
         * that when all attempted netifs fail (e.g. selected 4G with no SIM),
         * we break out immediately instead of waiting the full budget. */
        if (!mqtt_service_is_running() || !mqtt_service_is_connected()) {
            uint32_t wait_ms = 30000;
            UPLOAD_LOG("upload_one_record: MQTT not ready, waiting up to %lu ms...\r\n",
                   (unsigned long)wait_ms);
            /* Wait for link-up OR all-failed (whichever first). */
            (void)service_wait_for_ready(
                SERVICE_READY_STA | SERVICE_READY_NETIF_ALL_FAILED,
                AICAM_FALSE, wait_ms);
            if (service_get_ready_flags() & SERVICE_READY_NETIF_ALL_FAILED) {
                UPLOAD_LOG("upload_one_record: all netifs failed, aborting upload\r\n");
                buffer_free(jpeg);
                cJSON_Delete(meta);
                return AICAM_ERROR_UNAVAILABLE;
            }
            if (mqtt_service_is_running()) {
                (void)mqtt_service_wait_for_event(MQTT_EVENT_CONNECTED, AICAM_FALSE, wait_ms);
            }
        }
        if (mqtt_service_is_running() && mqtt_service_is_connected()) {
            const uint32_t threshold = 1024u * 1024u;

            /* AI result: use caller-provided, or load from persisted file */
            mqtt_ai_result_t ai_from_disk;
            const mqtt_ai_result_t *ai_ptr = ai_result_in;
            if (!ai_ptr) {
                if (load_ai_result_from_disk(fs, id, &ai_from_disk) == AICAM_OK) {
                    ai_ptr = &ai_from_disk;
                }
            }

            int rc;
            if (m.size < threshold) {
                rc = mqtt_service_publish_image_with_ai(NULL, jpeg, m.size, &m, ai_ptr);
            } else {
                rc = mqtt_service_publish_image_chunked(NULL, jpeg, m.size, &m, ai_ptr, 100 * 1024);
            }
            if (rc >= 0) {
                /* publish_image_with_ai just queues the message - wait for the
                 * MQTT puback so we know the broker actually received it.
                 * Without this, the last record of a flush pass can be lost
                 * when we sleep and tear down the network immediately after
                 * publish. Dynamic timeout matches system_service Step 6. */
                uint32_t puback_timeout = 5000 + (m.size / 10240) * 2000;
                if (puback_timeout > 60000) puback_timeout = 60000;
                if (mqtt_service_wait_for_event(MQTT_EVENT_PUBLISHED, AICAM_TRUE, puback_timeout) == AICAM_OK) {
                    result = AICAM_OK;
                } else {
                    result = AICAM_ERROR;   /* puback timeout → leave in pending for retry */
                }
            } else {
                result = AICAM_ERROR;
            }
        } else {
            result = AICAM_ERROR_UNAVAILABLE;
        }
    } else {
        /* Webhook path - keep MVP simple: call existing async push and wait. */
        if (webhook_service_is_enabled()) {
            /* webhook task takes ownership of the buffer, allocate a fresh copy */
            uint8_t *copy = (uint8_t *)buffer_calloc(1, m.size);
            if (copy) {
                memcpy(copy, jpeg, m.size);
                aicam_result_t pr = webhook_service_push_capture(copy, m.size, &m, NULL);
                if (pr == AICAM_OK) {
                    /* Wait up to 30s for the push to complete */
                    aicam_result_t wr = webhook_service_wait_pending(30000);
                    result = (wr == AICAM_OK) ? AICAM_OK : AICAM_ERROR;
                } else {
                    buffer_free(copy);
                    result = AICAM_ERROR;
                }
            } else {
                result = AICAM_ERROR_NO_MEMORY;
            }
        } else {
            result = AICAM_ERROR_UNAVAILABLE;
        }
    }

    buffer_free(jpeg);

    if (result == AICAM_OK) {
        cJSON_Delete(meta);
        return move_record(fs, id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
    }

    /* Failure path: bump retry_count, optionally promote to failed/ */
    uint8_t retry = 0;
    t = cJSON_GetObjectItem(meta, "retry_count");
    if (t) retry = (uint8_t)t->valueint;
    retry++;
    cJSON_ReplaceItemInObject(meta, "retry_count", cJSON_CreateNumber(retry));
    cJSON_ReplaceItemInObject(meta, "last_error", cJSON_CreateString(
        result == AICAM_ERROR_UNAVAILABLE ? "network_unavailable" : "upload_failed"));

    /* Persist updated meta */
    char *str = cJSON_PrintUnformatted(meta);
    if (str) {
        /* Temp + atomic rename so a power cut mid-rewrite leaves the prior
         * .json intact instead of a truncated file (which would make the
         * record unloadable but still listed as PENDING). */
        char tmp_path[136];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);
        if (write_file(fs, tmp_path, (const uint8_t *)str, (uint32_t)strlen(str)) == AICAM_OK) {
            if (disk_file_rename(fs, tmp_path, meta_path) != 0) {
                (void)disk_file_remove(fs, tmp_path);
            }
        }
        cJSON_free(str);
    }
    cJSON_Delete(meta);

    /* 0 = unlimited retries - never promote to failed/ */
    if (g_up.cfg.retry_max_attempts > 0 && retry >= g_up.cfg.retry_max_attempts) {
        return move_record(fs, id, RECORD_STATE_PENDING, RECORD_STATE_FAILED);
    }
    return result;
}

/* ==================== Worker pass ==================== */

typedef struct {
    FS_Type_t fs;
    int       attempts;
    int       successes;
    int       failures;
} flush_ctx_t;

/* One in-flight (published-but-unacked) record in the sliding window. */
typedef struct {
    char     id[64];
    int      msg_id;
    uint8_t  used;
} flush_inflight_t;

/* Visitor: collect record ids into an array. Used by do_flush_pass to snapshot
 * pending ids before publishing - iterate_records traverses date subdirs
 * (direct readdir of /captures/pending only returns date dirs, not .json). */
typedef struct { char (*ids)[64]; uint32_t n; uint32_t cap; } collect_ctx_t;
static aicam_result_t collect_visitor(FS_Type_t fs, const char *id, void *user)
{
    collect_ctx_t *ctx = (collect_ctx_t *)user;
    if (ctx->n < ctx->cap) {
        snprintf(ctx->ids[ctx->n], 64, "%s", id);
        ctx->n++;
    }
    return AICAM_OK;
}

static aicam_result_t flush_visitor(FS_Type_t fs, const char *id, void *user)
{
    flush_ctx_t *ctx = (flush_ctx_t *)user;
    ctx->attempts++;
    aicam_result_t r = upload_one_record(fs, id, NULL);  /* ai_result from disk */
    if (r == AICAM_OK) ctx->successes++;
    else               ctx->failures++;
    return AICAM_OK;
}

/* Check whether the configured upload channel is currently reachable.
 * We only sweep pending/ when this returns true - retrying with the network
 * down just burns IO and battery for no gain. */
static aicam_bool_t upload_channel_ready(void)
{
    if (g_up.cfg.upload_protocol == UPLOAD_PROTO_WEBHOOK) {
        return webhook_service_is_enabled() ? AICAM_TRUE : AICAM_FALSE;
    }
    /* MQTT: need both the service running and an active broker connection */
    return (mqtt_service_is_running() && mqtt_service_is_connected()) ? AICAM_TRUE : AICAM_FALSE;
}

/* Self-heal: if a record's metadata .json is gone (deleted externally, lost
 * after a partial write, or a stale index entry), the manifest's PENDING entry
 * is unreachable - the flush pass would retry it forever and fail at every
 * load. Tombstone it so counts/lists exclude it and the loop moves on. Returns
 * true if the record was healed (missing). */
static bool record_self_heal_if_missing(FS_Type_t fs, const char *id)
{
    char meta_path[128];
    path_for_meta(meta_path, sizeof(meta_path), id);
    struct stat st = {0};
    if (disk_file_stat(fs, meta_path, &st) == 0) return false;  /* meta exists */

    /* Diagnose the loss so we can tell apart "dir gone" vs "json selectively
     * lost" vs "fully deleted": check the parent dir and the primary image. */
    char pri_path[128];
    path_for_data(pri_path, sizeof(pri_path), id, 'p');
    bool pri_exists = (disk_file_stat(fs, pri_path, &st) == 0);
    /* parent dir /captures/meta/<date>/<hour> */
    char dir_path[96];
    {
        char date[16], hour[8];
        date_dir_from_id(id, date, sizeof(date));
        hour_dir_from_id(id, hour, sizeof(hour));
        snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", CAPTURES_DIR_META, date, hour);
    }
    bool dir_exists = (disk_file_stat(fs, dir_path, &st) == 0);
    LOG_SVC_WARN("upload: record %s meta missing (dir=%d jpg=%d) - tombstoning stale index entry",
                 id, (int)dir_exists, (int)pri_exists);
    (void)manifest_append(fs, id, IDX_STATE_DELETED, 0, 0);
    g_count_cache_dirty = true;
    return true;
}

static void do_flush_pass(void)
{
    if (g_up.active_fs == FS_MAX) return;
    if (!upload_channel_ready()) {
        UPLOAD_LOG("flush skipped, channel not ready\r\n");
        return;
    }

    g_up.flush_active = AICAM_TRUE;
    FS_Type_t fs = g_up.active_fs;

    /* Phase 1: collect all pending record ids. No count cap - the flush is
     * bounded by a time deadline (below), not a record count, so it respects
     * the user's configured batch semantics and adapts to the backlog size. */
    uint32_t cap = count_state(fs, RECORD_STATE_PENDING);
    if (cap == 0) {
        purge_old_sent(fs);
        return;
    }
    char (*ids)[64] = (char (*)[64])buffer_calloc(cap, 64);
    if (!ids) {
        purge_old_sent(fs);
        return;
    }
    /* Collect pending ids via iterate_records - it reads the manifest (no
     * opendir of record directories), collecting current-state PENDING ids. */
    collect_ctx_t cctx = { .ids = ids, .n = 0, .cap = cap };
    iterate_records(fs, RECORD_STATE_PENDING, 0, cap,
                    0, UINT64_MAX, AICAM_FALSE,
                    collect_visitor, &cctx);
    uint32_t n = cctx.n;

    /* Deadline scales with the pending count so a small backlog finishes fast
     * (short wake) and a large backlog gets more time, up to a hard cap. It's a
     * ceiling, not a target - the flush finishes as soon as all acked. */
    uint32_t budget_ms = n * FLUSH_PER_RECORD_MS + FLUSH_BASE_MS;
    if (budget_ms > FLUSH_MAX_BUDGET_MS) budget_ms = FLUSH_MAX_BUDGET_MS;
    uint64_t deadline = rtc_get_uptime_ms() + budget_ms;

    flush_ctx_t ctx = { .fs = fs };

    if (g_up.cfg.upload_protocol == UPLOAD_PROTO_MQTT) {
        /* Sliding window: publish up to W records in flight (pipelined, broker
         * kept busy), and only block on an ack when the window is full. This
         * recovers the parallelism of the old batch path while bounding the
         * published-but-unacked set to W - so a sleep cut can only leave ≤W
         * records to re-publish (bounded duplicate risk), not the whole batch.
         * W = the user's configured batch_count (capped), so "批量上传数量"
         * controls the in-flight parallelism. */
        uint32_t window = g_up.cfg.batch_count;
        if (window < 2) window = 2;
        if (window > FLUSH_WINDOW_MAX) window = FLUSH_WINDOW_MAX;

        flush_inflight_t *inf = (flush_inflight_t *)buffer_calloc(window, sizeof(flush_inflight_t));
        if (!inf) {
            /* OOM fallback: serial */
            for (uint32_t i = 0; i < n; i++) {
                if (rtc_get_uptime_ms() >= deadline) break;
                ctx.attempts++;
                if (record_self_heal_if_missing(fs, ids[i])) { ctx.failures++; continue; }
                int mid = -1;
                if (record_publish_mqtt(fs, ids[i], &mid) == AICAM_OK) {
                    int am = 0;
                    if (mqtt_service_get_acked_msg_id(&am, FLUSH_ACK_TIMEOUT_MS) == AICAM_OK) {
                        (void)move_record(fs, ids[i], RECORD_STATE_PENDING, RECORD_STATE_SENT);
                        ctx.successes++;
                    } else { record_mark_failed(fs, ids[i], "ack_timeout"); ctx.failures++; }
                } else { ctx.failures++; }
            }
        } else {
            uint32_t inflight = 0, next = 0;
            while (next < n || inflight > 0) {
                bool dlup = (rtc_get_uptime_ms() >= deadline);
                /* Fill the window while there's room and budget remains. */
                while (!dlup && inflight < window && next < n) {
                    ctx.attempts++;
                    if (record_self_heal_if_missing(fs, ids[next])) { ctx.failures++; next++; dlup = (rtc_get_uptime_ms() >= deadline); continue; }
                    int mid = -1;
                    if (record_publish_mqtt(fs, ids[next], &mid) == AICAM_OK) {
                        for (uint32_t s = 0; s < window; s++) {
                            if (!inf[s].used) {
                                snprintf(inf[s].id, sizeof(inf[s].id), "%s", ids[next]);
                                inf[s].msg_id = mid; inf[s].used = 1; inflight++;
                                break;
                            }
                        }
                    } else { ctx.failures++; }
                    next++;
                    dlup = (rtc_get_uptime_ms() >= deadline);
                }
                if (inflight == 0) {  /* nothing in flight: done or deadline */
                    break;
                }
                /* Block on an ack (window full, no more to publish, or draining). */
                int am = 0;
                aicam_result_t ar = mqtt_service_get_acked_msg_id(&am, FLUSH_ACK_TIMEOUT_MS);
                if (ar == AICAM_OK) {
                    if (am == -1) {  /* QoS 0: all in-flight confirmed */
                        for (uint32_t s = 0; s < window; s++) {
                            if (inf[s].used) {
                                (void)move_record(fs, inf[s].id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
                                ctx.successes++; inf[s].used = 0; inflight--;
                            }
                        }
                    } else {
                        for (uint32_t s = 0; s < window; s++) {
                            if (inf[s].used && inf[s].msg_id == am) {
                                (void)move_record(fs, inf[s].id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
                                ctx.successes++; inf[s].used = 0; inflight--;
                                break;
                            }
                        }
                    }
                } else if (dlup) {
                    /* Budget exhausted - bounded drain (one more ack window) for
                     * acks that already arrived, then fail the rest so the retry
                     * budget applies. Duplicates bounded to ≤W records. */
                    uint64_t ddl = rtc_get_uptime_ms() + FLUSH_ACK_TIMEOUT_MS;
                    while (inflight > 0) {
                        uint64_t now = rtc_get_uptime_ms();
                        if (now >= ddl) break;
                        int am2 = 0;
                        if (mqtt_service_get_acked_msg_id(&am2, (uint32_t)(ddl - now)) != AICAM_OK) break;
                        if (am2 == -1) {
                            for (uint32_t s = 0; s < window; s++) {
                                if (inf[s].used) {
                                    (void)move_record(fs, inf[s].id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
                                    ctx.successes++; inf[s].used = 0; inflight--;
                                }
                            }
                        } else {
                            for (uint32_t s = 0; s < window; s++) {
                                if (inf[s].used && inf[s].msg_id == am2) {
                                    (void)move_record(fs, inf[s].id, RECORD_STATE_PENDING, RECORD_STATE_SENT);
                                    ctx.successes++; inf[s].used = 0; inflight--;
                                    break;
                                }
                            }
                        }
                    }
                    for (uint32_t s = 0; s < window; s++) {
                        if (inf[s].used) {
                            record_mark_failed(fs, inf[s].id, "ack_timeout");
                            ctx.failures++; inf[s].used = 0; inflight--;
                        }
                    }
                    break;
                }
                /* else: ack timeout but window not full, deadline not hit → loop,
                 * the fill loop will publish more (pipelining) or re-wait. */
            }
            buffer_free(inf);
        }
    } else {
        /* Webhook: serial (push + wait_pending + move per record) */
        for (uint32_t i = 0; i < n; i++) {
            if (rtc_get_uptime_ms() >= deadline) break;
            ctx.attempts++;
            if (record_self_heal_if_missing(fs, ids[i])) { ctx.failures++; continue; }
            if (upload_one_record_webhook(fs, ids[i]) == AICAM_OK) ctx.successes++;
            else ctx.failures++;
        }
    }

    buffer_free(ids);

    if (ctx.attempts > 0) {
        UPLOAD_LOG("flush pass attempts=%d ok=%d fail=%d\r\n",
               ctx.attempts, ctx.successes, ctx.failures);
        LOG_SVC_INFO("upload: flush pass attempts=%d ok=%d fail=%d",
                     ctx.attempts, ctx.successes, ctx.failures);
    }
    /* Housekeeping at the tail of every pass */
    purge_old_sent(fs);
}

static void upload_task(void *arg)
{
    (void)arg;
    UPLOAD_LOG("coordinator task started\r\n");
    LOG_SVC_INFO("upload coordinator task started");

    while (g_up.running) {
        upload_event_t ev = 0;
        osStatus_t st = osMessageQueueGet(g_up.queue, &ev, NULL, osWaitForever);
        if (st != osOK) continue;
        if (!g_up.running) break;

        switch ((upload_event_kind_t)ev) {
        case EV_KICK:
            /* Flush pass is mutex-guarded so it won't race with a synchronous
             * upload_coordinator_drain() call from the sleep path. Wait for
             * the channel first - on a BATCH-threshold or scheduled-flush wake,
             * the network was just brought up async and MQTT may still be
             * connecting. Without this wait, do_flush_pass sees channel-not-ready
             * and skips, so the upload never fires. If the service isn't running
             * (skip_network wake), wait_for_upload_channel returns immediately.
             *
             * Set flush_active BEFORE the wait so the sleep path
             * (wait_upload_before_sleep) knows we're busy and doesn't cut
             * power mid-wait. */
            if (osMutexAcquire(g_up.mutex, osWaitForever) == osOK) {
                g_up.flush_active = AICAM_TRUE;
                if (wait_for_upload_channel(30000) == AICAM_OK) {
                    do_flush_pass();
                }
                g_up.flush_active = AICAM_FALSE;
                osMutexRelease(g_up.mutex);
            }
            break;
        case EV_RELOAD:
            json_config_get_capture_upload_config(&g_up.cfg);
            g_up.active_fs = resolve_storage_target(g_up.cfg.storage);
            if (g_up.active_fs != FS_MAX) ensure_dirs_with_space_check(g_up.active_fs);
            UPLOAD_LOG("config reloaded (mode=%d storage=%d fs=%d)\r\n",
                   g_up.cfg.mode, g_up.cfg.storage, g_up.active_fs);
            LOG_SVC_INFO("upload: config reloaded (mode=%d storage=%d)",
                         g_up.cfg.mode, g_up.cfg.storage);
            break;
        case EV_SHUTDOWN:
            g_up.running = AICAM_FALSE;
            break;
        case EV_DRAIN:
            /* Handled synchronously by upload_coordinator_drain() - no queue path. */
            break;
        default: break;
        }
    }
    LOG_SVC_INFO("upload coordinator task exited");
}

/* ==================== Service Lifecycle ==================== */

aicam_result_t upload_coordinator_init(void *config)
{
    (void)config;
    uint64_t t0 = rtc_get_uptime_ms();
    memset(&g_up, 0, sizeof(g_up));
    g_up.state = SERVICE_STATE_UNINITIALIZED;
    /* Restore the persisted storage_full flag so the first capture of this wake
     * runs proactive cleanup if the volume was full when we slept. */
    g_up.storage_full = storage_full_load();

    g_up.mutex = osMutexNew(NULL);
    if (!g_up.mutex) {
        LOG_SVC_ERROR("upload_coordinator init: osMutexNew failed");
        return AICAM_ERROR;
    }

    g_up.queue = osMessageQueueNew(UPLOAD_QUEUE_DEPTH, sizeof(ULONG), &upload_queue_attr);
    if (!g_up.queue) {
        LOG_SVC_ERROR("upload_coordinator init: osMessageQueueNew failed");
        osMutexDelete(g_up.mutex); g_up.mutex = NULL;
        return AICAM_ERROR;
    }
    uint64_t t1 = rtc_get_uptime_ms();

    json_config_capture_upload_defaults(&g_up.cfg);
    json_config_get_capture_upload_config(&g_up.cfg);
    uint64_t t2 = rtc_get_uptime_ms();

    g_up.active_fs = resolve_storage_target(g_up.cfg.storage);
    uint64_t t3 = rtc_get_uptime_ms();

    if (g_up.active_fs != FS_MAX) {
        ensure_dirs_with_space_check(g_up.active_fs);
        rebuild_index_if_needed(g_up.active_fs);
    }
    uint64_t t4 = rtc_get_uptime_ms();

    g_up.initialized = AICAM_TRUE;
    g_up.state = SERVICE_STATE_INITIALIZED;
    UPLOAD_LOG("init ok (mode=%d storage=%d fs=%d) times: mutex/queue=%lu cfg=%lu resolve=%lu dirs=%lu total=%lu ms\r\n",
           g_up.cfg.mode, g_up.cfg.storage, g_up.active_fs,
           (unsigned long)(t1-t0), (unsigned long)(t2-t1), (unsigned long)(t3-t2),
           (unsigned long)(t4-t3), (unsigned long)(t4-t0));
    return AICAM_OK;
}

aicam_result_t upload_coordinator_start(void)
{
    UPLOAD_LOG("start() called, initialized=%d running=%d\r\n",
           (int)g_up.initialized, (int)g_up.running);
    if (!g_up.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    if (g_up.running) return AICAM_OK;

    g_up.running = AICAM_TRUE;
    osThreadAttr_t attr = {
        .name = "upload_co",
        .stack_size = UPLOAD_TASK_STACK,
        .stack_mem = upload_task_stack,
        .priority = UPLOAD_TASK_PRIORITY,
    };
    g_up.task_handle = osThreadNew(upload_task, NULL, &attr);
    UPLOAD_LOG("osThreadNew returned %p\r\n", (void*)g_up.task_handle);
    if (!g_up.task_handle) {
        g_up.running = AICAM_FALSE;
        UPLOAD_LOG("start FAILED - osThreadNew returned NULL\r\n");
        return AICAM_ERROR;
    }
    g_up.state = SERVICE_STATE_RUNNING;

    /* Initial sweep: any leftover pending from prior boot can flush now. */
    upload_coordinator_kick();
    UPLOAD_LOG("start ok, state=RUNNING\r\n");
    return AICAM_OK;
}

aicam_result_t upload_coordinator_stop(void)
{
    if (!g_up.running) return AICAM_OK;

    upload_event_t ev = EV_SHUTDOWN;
    osMessageQueuePut(g_up.queue, &ev, 0, 0);

    if (g_up.task_handle) {
        osThreadJoin(g_up.task_handle);
        osThreadTerminate(g_up.task_handle);
        g_up.task_handle = NULL;
    }
    g_up.running = AICAM_FALSE;
    g_up.state = SERVICE_STATE_INITIALIZED;
    return AICAM_OK;
}

aicam_result_t upload_coordinator_deinit(void)
{
    if (g_up.running) upload_coordinator_stop();
    if (g_up.queue) { osMessageQueueDelete(g_up.queue); g_up.queue = NULL; }
    if (g_up.mutex) { osMutexDelete(g_up.mutex); g_up.mutex = NULL; }
    g_up.initialized = AICAM_FALSE;
    g_up.state = SERVICE_STATE_UNINITIALIZED;
    return AICAM_OK;
}

service_state_t upload_coordinator_get_state(void) { return g_up.state; }

/* ==================== Public Operations ==================== */

communication_type_t upload_coordinator_get_required_comm_type(void)
{
    /* Only restrict the netif when the capture actually uploads. LOCAL_ONLY
     * never uploads; storage=NONE + INSTANT has no retry and uploads direct,
     * so it still needs a netif ( honour the setting there too). */
    if (g_up.cfg.mode == CAPTURE_MODE_LOCAL_ONLY) return COMM_TYPE_NONE;
    return (communication_type_t)g_up.cfg.upload_comm_type;
}

aicam_result_t upload_coordinator_kick(void)
{
    if (!g_up.running) return AICAM_ERROR_NOT_INITIALIZED;
    upload_event_t ev = EV_KICK;
    osStatus_t st = osMessageQueuePut(g_up.queue, &ev, 0, 0);
    return (st == osOK) ? AICAM_OK : AICAM_ERROR;
}

aicam_bool_t upload_coordinator_needs_network(void)
{
    uint64_t tn0 = rtc_get_uptime_ms();
    /* If coordinator isn't up yet, be conservative: bring up the network.
     * This only happens during the very first boot before init runs. */
    if (!g_up.initialized || g_up.active_fs == FS_MAX) {
        UPLOAD_LOG("needs_network=YES (not initialized)\r\n");
        return AICAM_TRUE;
    }

    /* Counting pending/failed is only needed for the BATCH threshold decision.
     * INSTANT always uploads, LOCAL_ONLY never, SCHEDULED only at the flush
     * node (wake_scheduler decides) - skip the directory traverses for those
     * modes to cut wake latency. */
    capture_mode_t mode = g_up.cfg.mode;
    uint32_t pending = 0, failed = 0;
    if (mode == CAPTURE_MODE_BATCH) {
        pending = count_state(g_up.active_fs, RECORD_STATE_PENDING);
        failed  = count_state(g_up.active_fs, RECORD_STATE_FAILED);
    }
    uint64_t tn1 = rtc_get_uptime_ms();
    aicam_bool_t decision = AICAM_TRUE;

    switch (mode) {
    case CAPTURE_MODE_LOCAL_ONLY:
        /* Never uploads - no network needed. */
        decision = AICAM_FALSE;
        break;

    case CAPTURE_MODE_INSTANT:
        /* Snap-and-upload: every capture goes out immediately. */
        decision = AICAM_TRUE;
        break;

    case CAPTURE_MODE_BATCH:
        /* Bring up network only when this capture would cross the threshold
         * (pending + 1 >= batch_count) or there's a failed backlog to retry.
         * Otherwise just enqueue and go back to sleep - saves the multi-second
         * network bring-up. */
        if (failed > 0) {
            decision = AICAM_TRUE;
        } else {
            decision = (pending + 1 >= g_up.cfg.batch_count) ? AICAM_TRUE : AICAM_FALSE;
        }
        break;

    case CAPTURE_MODE_SCHEDULED: {
        /* Network ONLY at the scheduled upload-flush node. pending/failed
         * backlog waits for the next flush node - bringing the network up on
         * every capture-only wake just because there's a backlog wastes multi-
         * seconds of bring-up time + battery. A pure capture wake (timer only,
         * no upload node due) just enqueues and sleeps. */
        uint64_t now = now_unix();
        wake_event_t evs[WAKE_DUTY_MAX];
        int n = wake_scheduler_due_events(
            now > WAKE_TOLERANCE_SEC ? now - WAKE_TOLERANCE_SEC : 0,
            now + WAKE_TOLERANCE_SEC,
            evs, WAKE_DUTY_MAX);
        decision = AICAM_FALSE;
        for (int i = 0; i < n; i++) {
            if (evs[i].duty == WAKE_DUTY_UPLOAD_FLUSH) { decision = AICAM_TRUE; break; }
        }
        break;
    }

    default:
        decision = AICAM_TRUE;
        break;
    }

    UPLOAD_LOG("needs_network=%d (mode=%d pending=%u failed=%u batch=%u) count_time=%lu ms\r\n",
           (int)decision, (int)g_up.cfg.mode,
           (unsigned)pending, (unsigned)failed, (unsigned)g_up.cfg.batch_count,
           (unsigned long)(tn1 - tn0));
    return decision;
}

/* Wait for the upload channel (MQTT broker connection or webhook link) to be
 * ready, with fast-fail on all-netifs-failed. Used by drain AND the upload
 * task's EV_KICK handler before flushing - on a wake, wifi is brought up async
 * in service_start, and by the time the flush runs MQTT may still be connecting.
 * Without this wait, do_flush_pass sees channel-not-ready and skips, so the
 * upload never fires. If the MQTT/webhook service isn't running at all (e.g.
 * skip_network_services wake), returns immediately without blocking. */
static aicam_result_t wait_for_upload_channel(uint32_t timeout_ms)
{
    if (g_up.cfg.upload_protocol == UPLOAD_PROTO_WEBHOOK) {
        if (!webhook_service_is_enabled()) return AICAM_ERROR_UNAVAILABLE;
        if (!(service_get_ready_flags() & SERVICE_READY_STA)) {
            (void)service_wait_for_ready(
                SERVICE_READY_STA | SERVICE_READY_NETIF_ALL_FAILED,
                AICAM_FALSE, timeout_ms);
            if (service_get_ready_flags() & SERVICE_READY_NETIF_ALL_FAILED)
                return AICAM_ERROR_UNAVAILABLE;
        }
        return AICAM_OK;
    }
    /* MQTT: if the service isn't running (skip_network_services), don't wait
     * - there's no network coming up to wait for. */
    if (!mqtt_service_is_running()) return AICAM_ERROR_UNAVAILABLE;
    if (mqtt_service_is_connected()) return AICAM_OK;
    UPLOAD_LOG("wait_channel: MQTT not ready, waiting up to %lu ms...\r\n",
           (unsigned long)timeout_ms);
    (void)service_wait_for_ready(
        SERVICE_READY_STA | SERVICE_READY_NETIF_ALL_FAILED,
        AICAM_FALSE, timeout_ms);
    if (service_get_ready_flags() & SERVICE_READY_NETIF_ALL_FAILED) {
        UPLOAD_LOG("wait_channel: all netifs failed, aborting\r\n");
        return AICAM_ERROR_UNAVAILABLE;
    }
    if (mqtt_service_is_running()) {
        (void)mqtt_service_wait_for_event(MQTT_EVENT_CONNECTED, AICAM_FALSE, timeout_ms);
    }
    return (mqtt_service_is_running() && mqtt_service_is_connected())
           ? AICAM_OK : AICAM_ERROR_TIMEOUT;
}

aicam_result_t upload_coordinator_drain(uint32_t timeout_ms)
{
    if (!g_up.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    /* Synchronous: run a flush pass on the caller's thread, guarded by the
     * same mutex the worker uses for EV_KICK. This avoids osEventFlags (which
     * is unreliable on this ThreadX port) and guarantees the caller that
     * pending/ has been swept before we return. */
    if (osMutexAcquire(g_up.mutex, timeout_ms) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    /* Set flush_active BEFORE the channel wait so the sleep path
     * (wait_upload_before_sleep) sees we're busy and doesn't cut power
     * mid-wait. Without this, the upload task could be waiting for MQTT
     * while the main task sleeps and abandons it. */
    g_up.flush_active = AICAM_TRUE;
    if (wait_for_upload_channel(timeout_ms) == AICAM_OK) {
        do_flush_pass();
    } else {
        UPLOAD_LOG("drain: channel not ready after wait, skipping flush\r\n");
    }
    g_up.flush_active = AICAM_FALSE;
    osMutexRelease(g_up.mutex);
    return AICAM_OK;
}

aicam_bool_t upload_coordinator_is_flushing(void)
{
    return g_up.flush_active;
}

/* Time the current pending backlog needs for a flush pass, so the sleep path
 * (system_service_wait_upload_before_sleep) can wait an adaptive amount: small
 * backlog → short wait (short wake), large backlog → longer wait, up to a cap.
 * Mirrors the deadline computed inside do_flush_pass. Uses the RAM count cache
 * when fresh to avoid a manifest sweep on the sleep path. */
uint32_t upload_coordinator_get_flush_budget_ms(void)
{
    if (!g_up.initialized || g_up.active_fs == FS_MAX) return 0;
    uint32_t pending = 0;
    if (!g_count_cache_dirty) {
        pending = g_count_cache[RECORD_STATE_PENDING];
    } else {
        /* Cache cold - compute from the manifest (one sweep), then it's cached. */
        manifest_counts_all(g_up.active_fs, g_count_cache);
        g_count_cache_dirty = false;
        pending = g_count_cache[RECORD_STATE_PENDING];
    }
    uint32_t budget = pending * FLUSH_PER_RECORD_MS + FLUSH_BASE_MS;
    if (budget > FLUSH_MAX_BUDGET_MS) budget = FLUSH_MAX_BUDGET_MS;
    return budget;
}

aicam_result_t upload_coordinator_reload_config(void)
{
    if (!g_up.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    /* A reload (storage switch, or post-format re-init) may change which
     * records are visible - invalidate the RAM count cache so get_status
     * rebuilds from the manifest on next query. Also invalidate the
     * "dirs already created" cache: after a format the captures tree was
     * wiped, so ensure_dirs() must recreate it on the next call (otherwise
     * it would skip and mkdir of /captures/meta/<date> would fail with
     * LFS_ERR_NOENT - missing parent). */
    g_count_cache_dirty = true;
    s_ensured_fs = FS_MAX;
    /* A format (or storage switch) clears the volume → not full anymore. Clear
     * the persisted flag too, so the next capture doesn't pointlessly run
     * proactive cleanup on the freshly-empty FS. */
    storage_full_set(AICAM_FALSE);
    if (!g_up.running) {
        /* Apply synchronously when worker isn't running */
        json_config_get_capture_upload_config(&g_up.cfg);
        g_up.active_fs = resolve_storage_target(g_up.cfg.storage);
        if (g_up.active_fs != FS_MAX) ensure_dirs_with_space_check(g_up.active_fs);
        wake_scheduler_invalidate();
        return AICAM_OK;
    }
    upload_event_t ev = EV_RELOAD;
    osMessageQueuePut(g_up.queue, &ev, 0, 0);
    wake_scheduler_invalidate();
    return AICAM_OK;
}

/* ==================== Enqueue ==================== */

/* Direct in-memory upload (no persistence). Used by INSTANT+NONE and as a
 * fallback when INSTANT+storage can't persist (storage full). On success
 * returns AICAM_OK; on failure the capture is lost (no retry possible without
 * persistence). Waits for network readiness with fast-fail on all-netifs-failed. */
static aicam_result_t direct_publish_capture(const uint8_t *jpeg_buffer,
                                             uint32_t jpeg_size,
                                             const mqtt_image_metadata_t *meta_in,
                                             const mqtt_ai_result_t *ai_result)
{
    aicam_result_t r = AICAM_ERROR;
    uint32_t wait_ms = 30000;

    if (g_up.cfg.upload_protocol == UPLOAD_PROTO_MQTT) {
        if (!mqtt_service_is_running() || !mqtt_service_is_connected()) {
            UPLOAD_LOG("direct publish: MQTT not ready, waiting up to %lu ms...\r\n",
                   (unsigned long)wait_ms);
            (void)service_wait_for_ready(
                SERVICE_READY_STA | SERVICE_READY_NETIF_ALL_FAILED,
                AICAM_FALSE, wait_ms);
            if (service_get_ready_flags() & SERVICE_READY_NETIF_ALL_FAILED) {
                UPLOAD_LOG("direct publish: all netifs failed, aborting\r\n");
                return AICAM_ERROR_UNAVAILABLE;
            }
            if (mqtt_service_is_running()) {
                (void)mqtt_service_wait_for_event(MQTT_EVENT_CONNECTED, AICAM_FALSE, wait_ms);
            }
        }
        if (mqtt_service_is_running() && mqtt_service_is_connected()) {
            const uint32_t threshold = 1024u * 1024u;
            int rc = (jpeg_size < threshold)
                ? mqtt_service_publish_image_with_ai(NULL, jpeg_buffer, jpeg_size, meta_in, ai_result)
                : mqtt_service_publish_image_chunked(NULL, jpeg_buffer, jpeg_size, meta_in, ai_result, 100 * 1024);
            if (rc >= 0) {
                uint32_t puback_timeout = 5000 + (jpeg_size / 10240) * 2000;
                if (puback_timeout > 60000) puback_timeout = 60000;
                r = (mqtt_service_wait_for_event(MQTT_EVENT_PUBLISHED, AICAM_TRUE, puback_timeout) == AICAM_OK)
                    ? AICAM_OK : AICAM_ERROR;
            }
        }
    } else if (g_up.cfg.upload_protocol == UPLOAD_PROTO_WEBHOOK) {
        UPLOAD_LOG("direct publish: network not ready, waiting up to %lu ms...\r\n",
               (unsigned long)wait_ms);
        (void)service_wait_for_ready(
            SERVICE_READY_STA | SERVICE_READY_NETIF_ALL_FAILED,
            AICAM_FALSE, wait_ms);
        if (service_get_ready_flags() & SERVICE_READY_NETIF_ALL_FAILED) {
            UPLOAD_LOG("direct publish: all netifs failed, aborting\r\n");
            return AICAM_ERROR_UNAVAILABLE;
        }
        if (webhook_service_is_enabled()) {
            uint8_t *copy = (uint8_t *)buffer_calloc(1, jpeg_size);
            if (copy) {
                memcpy(copy, jpeg_buffer, jpeg_size);
                aicam_result_t pr = webhook_service_push_capture(copy, jpeg_size, meta_in, ai_result);
                if (pr == AICAM_OK) {
                    r = webhook_service_wait_pending(30000);
                } else {
                    buffer_free(copy);
                }
            }
        }
    }
    if (r == AICAM_OK) LOG_SVC_INFO("upload ok (direct): %s", meta_in->image_id);
    return r;
}

aicam_result_t upload_coordinator_enqueue_capture(
    const uint8_t *jpeg_buffer, uint32_t jpeg_size,
    const uint8_t *inference_jpeg, uint32_t inference_jpeg_size,
    const char *ai_result_json,
    const mqtt_ai_result_t *ai_result,
    const mqtt_image_metadata_t *meta_in,
    aicam_capture_trigger_t trigger,
    wakeup_source_type_t wakeup_src)
{
    if (!jpeg_buffer || jpeg_size == 0 || !meta_in) return AICAM_ERROR_INVALID_PARAM;
    if (!g_up.initialized) return AICAM_ERROR_NOT_INITIALIZED;

    /* Build record id: cap_<timestamp>_<seq>, both zero-padded for correct
     * lexicographic ordering (cleanup deletes oldest by filename sort).
     * Avoid PRIu64 - on this toolchain it prints "lu" literally; %lu with
     * (unsigned long) is safe for timestamps < 2038. */
    uint64_t ts = meta_in->timestamp ? meta_in->timestamp : now_unix();
    uint32_t seq = next_seq();
    char id[64];
    snprintf(id, sizeof(id), "cap_%08lu_%05u", (unsigned long)ts, (unsigned)seq);

    capture_mode_t mode = g_up.cfg.mode;
    FS_Type_t fs = g_up.active_fs;

    /* INSTANT + storage=NONE → in-memory upload only, no persistence needed. */
    if (mode == CAPTURE_MODE_INSTANT && g_up.cfg.storage == CAPTURE_STORE_NONE) {
        return direct_publish_capture(jpeg_buffer, jpeg_size, meta_in, ai_result);
    }

    /* All persistent modes (LOCAL_ONLY / BATCH / SCHEDULED / INSTANT-with-storage)
     * need a real FS and a space budget check. We MUST check free space BEFORE
     * writing - otherwise littlefs on a full flash will trigger GC/compact
     * internally and hang for tens of seconds trying to make room. */
    if (fs == FS_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    /* SD media is opened asynchronously by sdProcess; on a cold boot the first
     * capture may arrive before fx_media_open completes. Wait (up to
     * SD_STORE_READY_TIMEOUT_MS) so store-only captures actually get persisted
     * instead of failing with "write primary image failed". For FS_FLASH the
     * littlefs is mounted synchronously during storage_init, so no wait needed. */
    if (fs == FS_SD) {
        if (sd_wait_ready_for_open(SD_STORE_READY_TIMEOUT_MS) != AICAM_OK) {
            LOG_SVC_ERROR("upload: SD media not ready after %u ms, cannot store capture",
                          SD_STORE_READY_TIMEOUT_MS);
            return AICAM_ERROR_TIMEOUT;
        }
    }

    /* Proactive cleanup: if a PRIOR write failed (storage_full flag set), free
     * space BEFORE writing this capture. This avoids the lfs_file_write-on-full
     * hang (littlefs GC/compact can block tens of seconds on a near-full FS)
     * and the write-fail→cleanup→retry cycle. cleanup_for_space does NOT call
     * lfs_fs_size (it accumulates deleted bytes), so this is cheap and bounded
     * by CLEANUP_MAX_MS. The flag is cleared once a write succeeds. */
    uint64_t need = (uint64_t)jpeg_size + (uint64_t)inference_jpeg_size +
                    (ai_result_json ? strlen(ai_result_json) : 0) + 2048u;
    if (g_up.storage_full && g_up.cfg.policy == STORAGE_POLICY_WRAP) {
        UPLOAD_LOG("storage_full flag set - proactive cleanup before write\r\n");
        (void)cleanup_for_space(fs, need + CLEANUP_HEADROOM_BYTES);
    }

    /* Try to persist. On failure (likely FS full), do WRAP cleanup + retry
     * (reactive fallback for the first failure that sets storage_full). No
     * proactive lfs_fs_size traverse on the normal hot path - it's a full-FS
     * traverse that grows with file count and hit seconds/watchdog with
     * thousands of files. */
    aicam_result_t pr = persist_record(fs, id, jpeg_buffer, jpeg_size,
                                        inference_jpeg, inference_jpeg_size,
                                        ai_result_json, meta_in, trigger, wakeup_src,
                                        (mode == CAPTURE_MODE_LOCAL_ONLY)
                                            ? RECORD_STATE_LOCAL : RECORD_STATE_PENDING);
    if (pr != AICAM_OK) {
        UPLOAD_LOG("persist failed (%d), trying WRAP cleanup\r\n", (int)pr);
        if (g_up.cfg.policy == STORAGE_POLICY_WRAP &&
            cleanup_for_space(fs, need + CLEANUP_HEADROOM_BYTES) == AICAM_OK) {
            pr = persist_record(fs, id, jpeg_buffer, jpeg_size,
                                inference_jpeg, inference_jpeg_size,
                                ai_result_json, meta_in, trigger, wakeup_src,
                                (mode == CAPTURE_MODE_LOCAL_ONLY)
                                    ? RECORD_STATE_LOCAL : RECORD_STATE_PENDING);
        }
        if (pr != AICAM_OK) {
            storage_full_set(AICAM_TRUE);
            /* INSTANT = 即拍即传: JPEG still in RAM - attempt direct upload. */
            if (mode == CAPTURE_MODE_INSTANT) {
                UPLOAD_LOG("storage full - INSTANT fallback to direct publish\r\n");
                return direct_publish_capture(jpeg_buffer, jpeg_size, meta_in, ai_result);
            }
            return AICAM_ERROR_NO_MEMORY;
        }
    }
    storage_full_set(AICAM_FALSE);

    /* LOCAL_ONLY → persist into local/, no upload. */
    if (mode == CAPTURE_MODE_LOCAL_ONLY) {
        return AICAM_OK;
    }

    UPLOAD_LOG("enqueue id=%s mode=%d storage=%d fs=%d\r\n",
           id, mode, g_up.cfg.storage, fs);

    /* Dispatch */
    switch (mode) {
    case CAPTURE_MODE_INSTANT:
        /* Sync attempt. Only on success do we kick a sweep of pending/ -
         * a successful upload means the channel is up, so earlier-failed
         * records are worth retrying now. On failure the channel is down
         * and a sweep would just waste IO; leave pending/ untouched. */
        {
            aicam_result_t r = upload_one_record(fs, id, ai_result);
            UPLOAD_LOG("INSTANT upload_one_record=%d\r\n", r);
            if (r == AICAM_OK) {
                upload_coordinator_kick();
            }
        }
        return AICAM_OK;

    case CAPTURE_MODE_BATCH: {
        uint32_t pending = count_state(fs, RECORD_STATE_PENDING);
        UPLOAD_LOG("BATCH pending=%u batch_count=%u\r\n",
               (unsigned)pending, (unsigned)g_up.cfg.batch_count);
        if (pending >= g_up.cfg.batch_count) {
            upload_coordinator_kick();
        }
        return AICAM_OK;
    }
    case CAPTURE_MODE_SCHEDULED:
        /* wake_scheduler handles flushing at configured minutes */
        return AICAM_OK;
    default:
        return AICAM_OK;
    }
}

/* ==================== Status / Records ==================== */

aicam_result_t upload_coordinator_get_status(upload_coordinator_status_t *out)
{
    if (!out) return AICAM_ERROR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));
    out->mode = g_up.cfg.mode;
    out->storage = g_up.cfg.storage;
    out->initialized = g_up.initialized;
    out->running = g_up.running;
    out->storage_full = g_up.storage_full;
    out->actual_fs = g_up.active_fs;

    FS_Type_t fs = g_up.active_fs;
    if (fs != FS_MAX) {
        /* Use the RAM count cache; rebuild from the manifest only when dirty
         * (after a create/state-change/delete, or on first query after wake).
         * Avoids a full manifest sweep on every web status poll. */
        if (g_count_cache_dirty) {
            manifest_counts_all(fs, g_count_cache);
            g_count_cache_dirty = false;
        }
        out->pending_count = g_count_cache[RECORD_STATE_PENDING];
        out->sent_count    = g_count_cache[RECORD_STATE_SENT];
        out->failed_count  = g_count_cache[RECORD_STATE_FAILED];
        out->local_count   = g_count_cache[RECORD_STATE_LOCAL];
        uint64_t free_b = 0, total_b = 0;
        if (get_storage_free_bytes(fs, &free_b, &total_b) == AICAM_OK) {
            out->bytes_used_kb = (total_b - free_b) / 1024u;
            out->bytes_available_kb = free_b / 1024u;
        }
    }
    wake_event_t ev;
    out->next_scheduled_at = wake_scheduler_next_event(now_unix(), 24 * 3600, &ev);
    return AICAM_OK;
}

typedef struct {
    record_info_t *out;
    int max;
    int written;
    int skipped;   /* parse-failed count for diagnostics */
    record_state_t state;
    FS_Type_t fs;
} list_ctx_t;

static aicam_result_t list_visitor(FS_Type_t fs, const char *id, void *user)
{
    list_ctx_t *ctx = (list_ctx_t *)user;
    if (ctx->written >= ctx->max) return AICAM_ERROR;

    char path[128];
    path_for_meta(path, sizeof(path), id);
    cJSON *meta = NULL;
    if (parse_meta_file(fs, path, &meta) != AICAM_OK) {
        /* Diagnose WHY parse failed: file absent / empty / corrupt content */
        struct stat st = {0};
        int stat_ret = disk_file_stat(fs, path, &st);
        long sz = (stat_ret == 0) ? (long)st.st_size : -1;
        UPLOAD_LOG("list: SKIP %s (stat=%d size=%ld path=%s)\r\n",
               id, stat_ret, sz, path);
        ctx->skipped++;
        return AICAM_OK;
    }
    UPLOAD_LOG("list: ok %s\r\n", id);

    record_info_t *r = &ctx->out[ctx->written];
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", id);
    r->state = ctx->state;

    cJSON *t;
    t = cJSON_GetObjectItem(meta, "timestamp");  if (t) r->timestamp = (uint64_t)t->valuedouble;
    t = cJSON_GetObjectItem(meta, "size");       if (t) r->size = (uint32_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "retry_count"); if (t) r->retry_count = (uint8_t)t->valueint;
    t = cJSON_GetObjectItem(meta, "trigger");
    if (t && cJSON_IsString(t)) snprintf(r->trigger, sizeof(r->trigger), "%s", t->valuestring);
    t = cJSON_GetObjectItem(meta, "last_error");
    if (t && cJSON_IsString(t)) snprintf(r->last_error, sizeof(r->last_error), "%s", t->valuestring);
    t = cJSON_GetObjectItem(meta, "inference_image");
    r->has_inference = (t && cJSON_IsString(t) && t->valuestring[0] != '\0');

    cJSON_Delete(meta);
    ctx->written++;
    return AICAM_OK;
}

int upload_coordinator_list_records(record_state_t filter,
                                     uint32_t offset, uint32_t limit,
                                     uint64_t from_ts, uint64_t to_ts,
                                     aicam_bool_t sort_desc,
                                     record_info_t *out, int max)
{
    if (!out || max <= 0 || g_up.active_fs == FS_MAX) return 0;
    list_ctx_t ctx = { .out = out, .max = max, .written = 0, .skipped = 0,
                       .state = filter, .fs = g_up.active_fs };
    iterate_records(g_up.active_fs, filter, offset, limit,
                    from_ts, to_ts, sort_desc, list_visitor, &ctx);
    UPLOAD_LOG("list_records state=%d ok=%d skipped=%d\r\n",
           (int)filter, ctx.written, ctx.skipped);
    return ctx.written;
}

uint32_t upload_coordinator_count_records(record_state_t filter,
                                          uint64_t from_ts, uint64_t to_ts)
{
    if (g_up.active_fs == FS_MAX) return 0;
    /* Count mode: no visitor, no alloc; iterate reads manifest files only and
     * skips date files outside the requested time range. */
    return (uint32_t)iterate_records(g_up.active_fs, filter,
                                     0, 0, from_ts, to_ts, AICAM_FALSE, NULL, NULL);
}

aicam_result_t upload_coordinator_retry_record(const char *id)
{
    if (!id) return AICAM_ERROR_INVALID_PARAM;
    if (g_up.active_fs == FS_MAX) return AICAM_ERROR_INVALID_PARAM;

    char from[128];
    path_for_meta(from, sizeof(from), id);
    cJSON *meta = NULL;
    if (parse_meta_file(g_up.active_fs, from, &meta) != AICAM_OK) return AICAM_ERROR;
    cJSON_ReplaceItemInObject(meta, "retry_count", cJSON_CreateNumber(0));
    cJSON_ReplaceItemInObject(meta, "last_error", cJSON_CreateString(""));
    write_meta_json(g_up.active_fs, from, meta);
    cJSON_Delete(meta);

    aicam_result_t r = move_record(g_up.active_fs, id, RECORD_STATE_FAILED, RECORD_STATE_PENDING);
    if (r == AICAM_OK) upload_coordinator_kick();
    return r;
}

typedef struct { int reset; FS_Type_t fs; } retry_all_ctx_t;
static aicam_result_t retry_all_visitor(FS_Type_t fs, const char *id, void *user)
{
    retry_all_ctx_t *ctx = (retry_all_ctx_t *)user;
    if (upload_coordinator_retry_record(id) == AICAM_OK) ctx->reset++;
    (void)fs;
    return AICAM_OK;
}

int upload_coordinator_retry_all_failed(void)
{
    if (g_up.active_fs == FS_MAX) return 0;
    retry_all_ctx_t ctx = { .reset = 0, .fs = g_up.active_fs };
    iterate_records(g_up.active_fs, RECORD_STATE_FAILED, 0, 0,
                    0, UINT64_MAX, AICAM_FALSE, retry_all_visitor, &ctx);
    return ctx.reset;
}

aicam_result_t upload_coordinator_delete_record(const char *id)
{
    if (!id) return AICAM_ERROR_INVALID_PARAM;
    if (g_up.active_fs == FS_MAX) return AICAM_ERROR_INVALID_PARAM;
    /* Paths are state-independent now (state lives in the manifest), so a single
     * call covers the record regardless of its current state. Returns error if a
     * remove failed (FS likely corrupt) so the web UI can surface it. */
    return delete_record_files(g_up.active_fs, id, RECORD_STATE_PENDING, "web_delete", NULL);
}
