/**
 * @file upload_coordinator.h
 * @brief Capture-record persistence, mode dispatch, retry, and cleanup.
 *
 * Owns the on-disk /captures/ tree (pending/sent/failed/local/data subdirs) on
 * whichever filesystem the user has chosen (auto/flash/sd/none). After a
 * capture completes the system_service hands the JPEG buffer here; the
 * coordinator persists it, attaches metadata, and dispatches according to
 * capture_upload_config_t.mode:
 *
 *   INSTANT     - Upload synchronously inside enqueue_capture(); on failure,
 *                 the record stays in pending/ for retry. With storage=NONE,
 *                 nothing is persisted and failure is final.
 *   BATCH       - Persist only; flush is triggered when pending_count reaches
 *                 batch_count, or on the next wake_scheduler tick.
 *   SCHEDULED   - Persist only; flush is triggered by wake_scheduler at the
 *                 configured minutes-of-day.
 *   LOCAL_ONLY  - Persist into local/ and never upload.
 */

#ifndef UPLOAD_COORDINATOR_H
#define UPLOAD_COORDINATOR_H

#include "aicam_types.h"
#include "json_config_mgr.h"
#include "service_interfaces.h"
#include "system_service.h"
#include "communication_service.h"
#include "mqtt_service.h"
#include "drtc.h"
#include "u0_module.h"

#include <stdint.h>

/* Hard cap on total records when storing on flash (LittleFS). Exceeding this
 * triggers count-based cleanup (WRAP) or rejection (STOP). SD has no limit.
 * Exposed in the header so the web API can surface it in the upload-config
 * response for the UI. */
#define FLASH_MAX_RECORDS       512u

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Service Lifecycle ==================== */

aicam_result_t upload_coordinator_init(void *config);
aicam_result_t upload_coordinator_start(void);
aicam_result_t upload_coordinator_stop(void);
aicam_result_t upload_coordinator_deinit(void);
service_state_t upload_coordinator_get_state(void);

/* ==================== Capture Enqueue ==================== */

/**
 * @brief Hand a freshly captured JPEG (+ optional inference image + AI result)
 *        to the coordinator. The coordinator copies the metadata and persists
 *        the bytes; on return, caller retains ownership of jpeg_buffer and
 *        inference_jpeg and must free them as usual. ai_result_json is also
 *        copied internally.
 *
 *        For mode==INSTANT, this call blocks until the upload completes (or
 *        fails into pending/), so caller can rely on AICAM_OK = "sent or
 *        queued for retry". For other modes it returns immediately after
 *        persisting.
 */
aicam_result_t upload_coordinator_enqueue_capture(
    const uint8_t *jpeg_buffer,
    uint32_t jpeg_size,
    const uint8_t *inference_jpeg,        /* may be NULL */
    uint32_t inference_jpeg_size,
    const char *ai_result_json,           /* may be NULL; copied (for persistence) */
    const mqtt_ai_result_t *ai_result,    /* may be NULL; for MQTT upload */
    const mqtt_image_metadata_t *meta_in, /* timestamp/trigger/size/etc */
    aicam_capture_trigger_t trigger,
    wakeup_source_type_t wakeup_src);

/* ==================== Queue Operations ==================== */

/**
 * @brief Get the configured wake-capture upload netif.
 * @return communication_type_t; COMM_TYPE_NONE = default (use system comm-pref,
 *         init all netifs). Only meaningful when mode != LOCAL_ONLY and
 *         storage != NONE (i.e. the capture actually needs to upload).
 */
communication_type_t upload_coordinator_get_required_comm_type(void);

/**
 * @brief Decide whether this wake cycle needs the network stack up.
 *
 * Called by the boot/wake flow BEFORE service_start() to skip bringing up
 * communication/mqtt/webhook when this wake won't actually upload anything -
 * saves the multi-second network bring-up time and battery.
 *
 *   LOCAL_ONLY  → never
 *   INSTANT     → always (snap-and-upload)
 *   BATCH       → only when this capture would reach batch_count, or there
 *                 are failed records worth retrying
 *   SCHEDULED   → only when an upload schedule node is due now, or there is
 *                 pending/failed backlog
 */
aicam_bool_t upload_coordinator_needs_network(void);

/** Wake the worker to try draining pending uploads now (non-blocking). */
aicam_result_t upload_coordinator_kick(void);

/** Block until the worker finishes its current pass or timeout expires. */
aicam_result_t upload_coordinator_drain(uint32_t timeout_ms);

/** True while a flush pass (do_flush_pass) is running - the sleep path polls
 *  this to avoid cutting power mid-upload. */
aicam_bool_t upload_coordinator_is_flushing(void);

/** Time (ms) the current pending backlog needs for a flush pass, scaling with
 *  the pending count and capped at FLUSH_MAX_BUDGET_MS. The sleep path uses this
 *  as an adaptive wait timeout so a small backlog → short wake, large backlog →
 *  longer wake, but never over the cap. */
uint32_t upload_coordinator_get_flush_budget_ms(void);

/** Re-read capture_upload_config_t from NVS, propagate to runtime state. */
aicam_result_t upload_coordinator_reload_config(void);

/* ==================== Status / Records ==================== */

typedef struct {
    capture_mode_t  mode;
    capture_storage_t storage;
    aicam_bool_t    initialized;
    aicam_bool_t    running;
    uint32_t        pending_count;
    uint32_t        sent_count;
    uint32_t        failed_count;
    uint32_t        local_count;
    uint64_t        next_scheduled_at;     /* 0 = N/A */
    uint64_t        bytes_used_kb;
    uint64_t        bytes_available_kb;
    aicam_bool_t    storage_full;
    FS_Type_t       actual_fs;             /* resolved FS (FS_SD/FS_FLASH) for record file access */
} upload_coordinator_status_t;

aicam_result_t upload_coordinator_get_status(upload_coordinator_status_t *out);

/** Record state for record listing. */
typedef enum {
    RECORD_STATE_PENDING = 0,
    RECORD_STATE_SENT    = 1,
    RECORD_STATE_FAILED  = 2,
    RECORD_STATE_LOCAL   = 3,
} record_state_t;

typedef struct {
    char     id[64];                /* "cap_<ts>_<seq>" */
    record_state_t state;
    uint64_t timestamp;             /* capture wall-clock */
    uint32_t size;                  /* primary image bytes */
    uint8_t  retry_count;
    char     trigger[16];           /* "pir" "button" ... */
    char     last_error[64];        /* may be empty */
    aicam_bool_t has_inference;
} record_info_t;

/** Iterate records; returns count written (caller-provided buffer).
 *  from_ts/to_ts: inclusive time range (0 / UINT64_MAX = unbounded).
 *  sort_desc: true = newest first. */
int upload_coordinator_list_records(record_state_t filter,
                                    uint32_t offset, uint32_t limit,
                                    uint64_t from_ts, uint64_t to_ts,
                                    aicam_bool_t sort_desc,
                                    record_info_t *out, int max);

/** Total count of records in the given state within [from_ts, to_ts]
 *  (0 / UINT64_MAX = unbounded). */
uint32_t upload_coordinator_count_records(record_state_t filter,
                                          uint64_t from_ts, uint64_t to_ts);

/** Move a failed record back to pending and bump retry budget. */
aicam_result_t upload_coordinator_retry_record(const char *id);

/** Retry all failed records. Returns count of records reset. */
int upload_coordinator_retry_all_failed(void);

/** Delete a record group (metadata + data files). */
aicam_result_t upload_coordinator_delete_record(const char *id);

#ifdef __cplusplus
}
#endif

#endif /* UPLOAD_COORDINATOR_H */
