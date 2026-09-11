/**
 * @file api_file_module.c
 * @brief File Management API Module Implementation
 * @details Provides REST APIs for file browsing, upload, download, delete,
 *          rename, preview, and edit across both flash (LittleFS) and SD card.
 */

#include "api_file_module.h"
#include "web_server.h"
#include "generic_file.h"
#include "storage.h"
#include "device_service.h"
#include "debug.h"
#include "auth_mgr.h"
#include "buffer_mgr.h"
#include "mongoose.h"
#include "api_business_error.h"
#include "mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ==================== Constants ==================== */

#define MAX_PREVIEW_SIZE_TEXT  (512 * 1024)   // 512KB max text preview
#define MAX_PREVIEW_SIZE_IMAGE (2 * 1024 * 1024)  // 2MB max image preview
#define MAX_FILE_NAME_LEN      128
#define MAX_PATH_LEN           512
#define MAX_ENTRIES_PER_DIR    256

/**
 * @brief Flash readdir buffer - must match lfs_info exactly
 *        type(1) + pad(3) + size(4) + name(256) = 264 bytes
 */
typedef struct {
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t size;
    char     name[256];
} flash_entry_t;

/**
 * @brief SD readdir buffer - must match sd_info exactly
 *        type(1) + pad(3) + size(4) + name(256) + mtime(4) + short_name(14) ≈ 282 bytes
 */
typedef struct {
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t size;
    char     name[256];
    uint32_t mtime;
    char     short_name[14];
} sd_entry_t;

/* ==================== Helpers ==================== */

/**
 * @brief Parse FS type from string
 */
static FS_Type_t parse_fs_type(const char *fs_str)
{
    if (!fs_str) return FS_FLASH;
    if (strcmp(fs_str, "sd") == 0) return FS_SD;
    return FS_FLASH;
}

/* Shared pre-check for file handlers: verifies the backing FS is ready. On
 * failure, sends an error response (so the frontend can show a format prompt
 * for flash) and returns false. */
static bool file_handler_check_fs(http_handler_context_t *ctx, FS_Type_t fs_type)
{
    if (fs_type == FS_SD && !device_service_storage_is_sd_connected()) {
        (void)api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "SD card not available");
        return false;
    }
    if (fs_type == FS_FLASH && !storage_is_lfs_mounted()) {
        (void)api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                 "Flash FS not mounted, format required");
        return false;
    }
    return true;
}

/**
 * @brief Get query parameter from request
 */
static int get_query_param(http_handler_context_t *ctx, const char *name,
                           char *buf, size_t buf_size)
{
    return mg_http_get_var(&ctx->msg->query, name, buf, buf_size);
}

/**
 * @brief Check if file extension is text-editable
 */
static bool is_text_editable(const char *filename)
{
    const char *extensions[] = {
        ".txt", ".json", ".csv", ".log", ".xml", ".yml", ".yaml",
        ".cfg", ".ini", ".html", ".htm", ".css", ".js", ".md",
        ".sh", ".py", ".c", ".h", ".cpp", ".hpp", NULL
    };
    const char *dot = strrchr(filename, '.');
    if (!dot) return true;  // No extension, assume text
    for (int i = 0; extensions[i] != NULL; i++) {
        if (strcasecmp(dot, extensions[i]) == 0) return true;
    }
    return false;
}

/**
 * @brief Check if file extension is a previewable image
 */
static bool is_previewable_image(const char *filename)
{
    const char *extensions[] = {
        ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".svg", ".ico", NULL
    };
    const char *dot = strrchr(filename, '.');
    if (!dot) return false;
    for (int i = 0; extensions[i] != NULL; i++) {
        if (strcasecmp(dot, extensions[i]) == 0) return true;
    }
    return false;
}

/**
 * @brief Get MIME type from file extension
 */
static const char* get_mime_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";

    if (strcasecmp(dot, ".txt") == 0)  return "text/plain; charset=utf-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(dot, ".csv") == 0)  return "text/csv; charset=utf-8";
    if (strcasecmp(dot, ".log") == 0)  return "text/plain; charset=utf-8";
    if (strcasecmp(dot, ".xml") == 0)  return "application/xml; charset=utf-8";
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcasecmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcasecmp(dot, ".md") == 0)   return "text/markdown; charset=utf-8";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(dot, ".png") == 0)  return "image/png";
    if (strcasecmp(dot, ".bmp") == 0)  return "image/bmp";
    if (strcasecmp(dot, ".gif") == 0)  return "image/gif";
    if (strcasecmp(dot, ".svg") == 0)  return "image/svg+xml";
    if (strcasecmp(dot, ".ico") == 0)  return "image/x-icon";
    if (strcasecmp(dot, ".pdf") == 0)  return "application/pdf";
    if (strcasecmp(dot, ".zip") == 0)  return "application/zip";
    if (strcasecmp(dot, ".bin") == 0)  return "application/octet-stream";

    return "application/octet-stream";
}

/**
 * @brief Build a full path from dir and filename
 */
static void build_full_path(const char *dir, const char *filename,
                            char *out, size_t out_size)
{
    size_t dir_len = strlen(dir);
    snprintf(out, out_size, "%s%s%s",
             dir,
             (dir_len > 0 && dir[dir_len - 1] == '/') ? "" : "/",
             filename);
}

/* ============================================================================
 * Streaming File Upload
 * ============================================================================
 * Intercepted at MG_EV_HTTP_HDRS in web_server_event_handler().  We take over
 * the Mongoose connection (c->pfn = NULL) and stream body chunks directly to
 * the filesystem via c->recv, keeping memory usage constant.
 * ============================================================================ */

#define FILE_UPLOAD_STREAM_BUF_SIZE   10240
#define FILE_UPLOAD_FLASH_MAX_SIZE    (16 * 1024 * 1024)
#define FILE_UPLOAD_SD_MAX_SIZE       (16 * 1024 * 1024)
#define FILE_UPLOAD_PROGRESS_INTERVAL 102400u

typedef struct {
    uint32_t     magic;
    FS_Type_t    fs_type;
    void        *fd;
    char         full_path[MAX_PATH_LEN];   /* temp path actually written   */
    char         final_path[MAX_PATH_LEN];  /* path renamed to on success   */
    size_t       total_received;
    size_t       content_length;
    uint8_t      write_buf[FILE_UPLOAD_STREAM_BUF_SIZE];
    size_t       write_buf_pos;
    aicam_bool_t initialized;
    aicam_bool_t failed;
    size_t       last_progress_log;
} file_upload_ctx_t;

static void file_upload_send_response(struct mg_connection *c, int err_code,
                                       const char *msg)
{
    if (!c) return;
    char jb[256];
    if (err_code == 0 || err_code == API_ERROR_NONE) {
        snprintf(jb, sizeof(jb),
            "{\"success\":true,\"error_code\":\"NONE\",\"message\":\"%s\"}",
            msg ? msg : "OK");
        mg_http_reply(c, 200,
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
            "%s", jb);
    } else {
        snprintf(jb, sizeof(jb),
            "{\"success\":false,\"error_code\":\"%s\",\"message\":\"%s\"}",
            api_business_error_code_to_string(err_code), msg ? msg : "Error");
        mg_http_reply(c,
            (err_code == API_ERROR_UNAUTHORIZED) ? 401 :
            (err_code == API_ERROR_INVALID_REQUEST) ? 400 : 500,
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
            "%s", jb);
    }
    c->is_draining = 1;
}

static void file_upload_cleanup(file_upload_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->fd) {
        if (ctx->write_buf_pos > 0 && !ctx->failed)
            disk_file_fwrite(ctx->fs_type, ctx->fd, ctx->write_buf, ctx->write_buf_pos);
        disk_file_fclose(ctx->fs_type, ctx->fd);
        ctx->fd = NULL;
    }
    if (ctx->failed && ctx->full_path[0])
        disk_file_remove(ctx->fs_type, ctx->full_path);
    buffer_free(ctx);
}

void file_upload_stream_processor(struct mg_connection *c, int ev, void *ev_data)
{
    file_upload_ctx_t *ctx = (file_upload_ctx_t *)c->fn_data;

    /* Phase 1 (MG_EV_HTTP_HDRS) needs no ctx yet. Skip magic check there. */
    if (ev != MG_EV_HTTP_HDRS) {
        if (!ctx || ctx->magic != FILE_UPLOAD_CTX_MAGIC) return;
    }

    /* ---- Connection closed / error ---- */
    if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        if (ctx) {
            /* Close/error before the success path ran means a truncated body:
             * mark failed so cleanup drops the buffered partial instead of
             * flushing it, and removes the temp file. Any pre-existing file
             * at the target path was never touched. */
            ctx->failed = AICAM_TRUE;
            file_upload_cleanup(ctx);
            c->fn_data = NULL;
        }
        return;
    }

    /* ====== Phase 1 - MG_EV_HTTP_HDRS: initialise ====== */
    if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) return; /* pass through */

        /* Auth */
        struct mg_str *ah = mg_http_get_header(hm, "Authorization");
        if (!ah || ah->len == 0) {
            file_upload_send_response(c, API_ERROR_UNAUTHORIZED, "Authentication required");
            return;
        }
        char u[32]={0}, p[32]={0};
        mg_http_creds(hm, u, sizeof(u), p, sizeof(p));
        if (!u[0] || !p[0] || strcmp(u, AUTH_ADMIN_USERNAME) || !auth_mgr_verify_password(p)) {
            file_upload_send_response(c, API_ERROR_UNAUTHORIZED, "Invalid credentials");
            return;
        }

        /* Content-Length */
        struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
        size_t total = cl ? (size_t)strtoul(cl->buf, NULL, 10) : 0;
        if (!total) {
            file_upload_send_response(c, API_ERROR_INVALID_REQUEST, "Content-Length required");
            return;
        }

        /* Query params */
        char fs[16]="flash", dir[MAX_PATH_LEN]="/", fn[MAX_FILE_NAME_LEN]={0};
        mg_http_get_var(&hm->query, "fs", fs, sizeof(fs));
        mg_http_get_var(&hm->query, "path", dir, sizeof(dir));
        mg_http_get_var(&hm->query, "filename", fn, sizeof(fn));
        if (!fn[0]) {
            file_upload_send_response(c, API_ERROR_INVALID_REQUEST, "Filename required");
            return;
        }

        FS_Type_t fst = parse_fs_type(fs);
        size_t maxsz = (fst == FS_SD) ? FILE_UPLOAD_SD_MAX_SIZE : FILE_UPLOAD_FLASH_MAX_SIZE;
        if (total > maxsz) {
            file_upload_send_response(c, API_ERROR_INVALID_REQUEST, "File too large");
            return;
        }
        if (fst == FS_SD && !device_service_storage_is_sd_connected()) {
            file_upload_send_response(c, API_ERROR_SERVICE_UNAVAILABLE, "SD not available");
            return;
        }
        if (fst == FS_FLASH && !storage_is_lfs_mounted()) {
            file_upload_send_response(c, API_ERROR_SERVICE_UNAVAILABLE,
                                      "Flash FS not mounted, format required");
            return;
        }

        char fpath[MAX_PATH_LEN];
        build_full_path(dir, fn, fpath, sizeof(fpath));

        /* Never open the target with "w": truncating an existing file before
         * the body arrives means any mid-upload failure destroys the original
         * (cleanup removes the partial). Write to a temp sibling instead and
         * rename over the target only after the last byte lands. */
        static uint32_t s_upload_seq = 0;
        char tpath[MAX_PATH_LEN];
        if (snprintf(tpath, sizeof(tpath), "%s.%08x.upld",
                     fpath, (unsigned)(++s_upload_seq)) >= (int)sizeof(tpath)) {
            file_upload_send_response(c, API_ERROR_INVALID_REQUEST, "Path too long");
            return;
        }

        void *fd = disk_file_fopen(fst, tpath, "w");
        if (!fd) {
            file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "Cannot create file");
            return;
        }

        ctx = (file_upload_ctx_t *)buffer_calloc(1, sizeof(*ctx));
        if (!ctx) {
            disk_file_fclose(fst, fd); disk_file_remove(fst, tpath);
            file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "OOM");
            return;
        }

        ctx->magic          = FILE_UPLOAD_CTX_MAGIC;
        ctx->fs_type        = fst;
        ctx->fd             = fd;
        ctx->content_length = total;
        ctx->initialized    = AICAM_TRUE;
        strncpy(ctx->full_path, tpath, sizeof(ctx->full_path) - 1);
        strncpy(ctx->final_path, fpath, sizeof(ctx->final_path) - 1);

        c->fn_data = ctx;
        c->pfn     = NULL;
        mg_iobuf_del(&c->recv, 0, hm->head.len);
        return;
    }

    /* ====== Phase 2 - write body data to file (one chunk per event) ====== */
    if (ctx && ctx->initialized && !ctx->failed && c->recv.len > 0) {
        size_t len = c->recv.len;
        size_t rem = ctx->content_length - ctx->total_received;
        if (len > rem) len = rem;

        /* Copy at most enough to fill one write buffer chunk */
        size_t sp = FILE_UPLOAD_STREAM_BUF_SIZE - ctx->write_buf_pos;
        size_t ch = (len < sp) ? len : sp;
        memcpy(ctx->write_buf + ctx->write_buf_pos, c->recv.buf, ch);
        ctx->write_buf_pos += ch;
        mg_iobuf_del(&c->recv, 0, ch);
        ctx->total_received += ch;

        if (ctx->write_buf_pos >= FILE_UPLOAD_STREAM_BUF_SIZE) {
            /* Flush one full chunk to disk */
            if (disk_file_fwrite(ctx->fs_type, ctx->fd, ctx->write_buf,
                                 FILE_UPLOAD_STREAM_BUF_SIZE)
                != FILE_UPLOAD_STREAM_BUF_SIZE) {
                /* cleanup (not bare buffer_free): failed is set, so it closes
                 * the still-open fd and removes the partial file instead of
                 * leaking the handle on every failed upload. */
                ctx->failed = AICAM_TRUE;
                file_upload_cleanup(ctx); c->fn_data = NULL;
                file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "Write failed");
                return;
            }
            ctx->write_buf_pos = 0;
            disk_file_fflush(ctx->fs_type, ctx->fd);
        }

        if (ctx->total_received >= ctx->content_length) {
            /* flush final partial buffer */
            if (ctx->write_buf_pos > 0) {
                if (disk_file_fwrite(ctx->fs_type, ctx->fd, ctx->write_buf,
                                     ctx->write_buf_pos) != (int)ctx->write_buf_pos) {
                    /* same as above: close fd + drop the partial file */
                    ctx->failed = AICAM_TRUE;
                    file_upload_cleanup(ctx); c->fn_data = NULL;
                    file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "Final write failed");
                    return;
                }
                disk_file_fflush(ctx->fs_type, ctx->fd);
            }
            disk_file_fclose(ctx->fs_type, ctx->fd);
            ctx->fd = NULL;

            /* Full body landed: promote the temp sibling to the target path.
             * flash: lfs_rename is an atomic overwrite; SD: the fx backend
             * parks an existing destination under a backup name first.
             * Until this succeeds the pre-existing file at the target is
             * untouched. */
            if (disk_file_rename(ctx->fs_type, ctx->full_path, ctx->final_path) != 0) {
                ctx->failed = AICAM_TRUE;
                file_upload_cleanup(ctx); c->fn_data = NULL;
                file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "Rename failed");
                return;
            }

            unsigned ssz = (unsigned)ctx->total_received;
            char rpath[MAX_PATH_LEN];
            strncpy(rpath, ctx->final_path, sizeof(rpath) - 1);
            rpath[sizeof(rpath) - 1] = 0;
            buffer_free(ctx); c->fn_data = NULL;

            /* Build the body with cJSON: the path can contain quotes,
             * backslashes or unicode characters that a raw %s would emit
             * unescaped, producing invalid JSON. */
            cJSON *body = cJSON_CreateObject();
            cJSON *data = cJSON_CreateObject();
            if (body && data) {
                if (!cJSON_AddItemToObject(body, "data", data))
                    cJSON_Delete(data); /* not adopted: free it here */
                else if (cJSON_AddBoolToObject(body, "success", 1) &&
                         cJSON_AddStringToObject(body, "error_code", "NONE") &&
                         cJSON_AddStringToObject(body, "message",
                                                 "File uploaded successfully") &&
                         cJSON_AddStringToObject(data, "path", rpath) &&
                         cJSON_AddNumberToObject(data, "size", ssz)) {
                    char *jb = cJSON_PrintUnformatted(body);
                    if (jb) {
                        mg_http_reply(c, 200,
                            "Content-Type: application/json\r\n"
                            "Connection: close\r\n"
                            "Access-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
                            "%s", jb);
                        cJSON_free(jb);
                        c->is_draining = 1;
                        cJSON_Delete(body);
                        return;
                    }
                }
            }
            cJSON_Delete(body);
            file_upload_send_response(c, API_ERROR_INTERNAL_ERROR, "Response build failed");
        }
        return;
    }
}

/* ============================================================================
 * Streaming File Download
 * ============================================================================
 * Intercepted in file_download_handler.  We hand the connection to a poll-
 * driven handler that reads the file one chunk at a time and pushes data
 * via mg_send(), keeping the event loop responsive for the watchdog.
 * ============================================================================ */

#define FILE_DOWNLOAD_CTX_MAGIC  0x444C5546u
#define FILE_DOWNLOAD_CHUNK_SIZE 8192

typedef struct {
    uint32_t  magic;
    FS_Type_t fs_type;
    void     *fd;
    size_t    remaining;
    size_t    sent;
} file_download_ctx_t;

static void file_download_cleanup(file_download_ctx_t *ctx)
{
    if (ctx && ctx->fd) { disk_file_fclose(ctx->fs_type, ctx->fd); ctx->fd = NULL; }
    buffer_free(ctx);
}

static void file_download_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    file_download_ctx_t *ctx = (file_download_ctx_t *)c->fn_data;
    if (!ctx || ctx->magic != FILE_DOWNLOAD_CTX_MAGIC) return;

    if (ev == MG_EV_POLL || ev == MG_EV_WRITE) {
        /* Don't overflow the send buffer */
        if (c->send.len > 8192) return;

        if (ctx->remaining > 0) {
            size_t chunk_size = (ctx->remaining > FILE_DOWNLOAD_CHUNK_SIZE)
                                ? FILE_DOWNLOAD_CHUNK_SIZE : ctx->remaining;
            uint8_t *chunk = (uint8_t *)hal_mem_alloc_large(chunk_size);
            if (!chunk) { c->is_draining = 1; return; }
            int n = disk_file_fread(ctx->fs_type, ctx->fd, chunk, chunk_size);
            if (n > 0) {
                mg_send(c, chunk, (size_t)n);
                ctx->remaining -= (size_t)n;
                ctx->sent += (size_t)n;
                /* Keep the AP sleep timer alive while the body streams: this
                 * connection was handed off and never fires MG_EV_HTTP_MSG,
                 * so a download longer than the low-power 90 s window would
                 * be killed by a mid-flight sleep. Reset only on real
                 * progress — a vanished peer stops producing chunks and the
                 * idle timeout still applies. */
                web_server_ap_sleep_timer_reset();
            } else {
                c->is_draining = 1;
            }
            hal_mem_free(chunk);
        } else {
            c->is_draining = 1;
        }
    } else if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        if (ctx) { file_download_cleanup(ctx); c->fn_data = NULL; }
    }
}

/* ==================== API Handlers ==================== */

/**
 * @brief GET /api/v1/files/list?fs=flash|sd&path=/
 */
aicam_result_t file_list_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "GET"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");

    char fs_str[16] = "flash";
    char dir_path[MAX_PATH_LEN] = "/";
    get_query_param(ctx, "fs", fs_str, sizeof(fs_str));
    get_query_param(ctx, "path", dir_path, sizeof(dir_path));

    FS_Type_t fs_type = parse_fs_type(fs_str);

    // Verify SD is available if requesting SD
    if (!file_handler_check_fs(ctx, fs_type)) return AICAM_OK;

    cJSON *response_json = cJSON_CreateObject();
    cJSON *entries_array = cJSON_CreateArray();
    if (!response_json || !entries_array) {
        if (response_json) cJSON_Delete(response_json);
        if (entries_array) cJSON_Delete(entries_array);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate response");
    }

    cJSON_AddStringToObject(response_json, "fs", fs_str);
    cJSON_AddStringToObject(response_json, "path", dir_path);

    // Open directory
    void *dd = disk_file_opendir(fs_type, dir_path);
    if (!dd) {
        cJSON_AddItemToObject(response_json, "entries", entries_array);
        cJSON_AddStringToObject(response_json, "error", "Failed to open directory");
        char *json_str = cJSON_Print(response_json);
        aicam_result_t ret = api_response_success(ctx, json_str, "Directory listing");
        cJSON_Delete(response_json);
        return ret;
    }

    // Read directory entries - use correct struct per FS type
    int count = 0;

    if (fs_type == FS_SD) {
        sd_entry_t e;
        while (disk_file_readdir(fs_type, dd, (char *)&e) > 0 && count < MAX_ENTRIES_PER_DIR) {
            bool is_dir = (e.type == 1);
            if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) continue;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", e.name);
            cJSON_AddStringToObject(item, "type", is_dir ? "dir" : "file");
            cJSON_AddNumberToObject(item, "size", (double)(is_dir ? 0 : e.size));
            cJSON_AddNumberToObject(item, "mtime", (double)e.mtime);
            cJSON_AddItemToArray(entries_array, item);
            count++;
        }
    } else {
        flash_entry_t e;
        while (disk_file_readdir(fs_type, dd, (char *)&e) > 0 && count < MAX_ENTRIES_PER_DIR) {
            bool is_dir = (e.type == 2);
            if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) continue;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", e.name);
            cJSON_AddStringToObject(item, "type", is_dir ? "dir" : "file");
            cJSON_AddNumberToObject(item, "size", (double)(is_dir ? 0 : e.size));
            char full_path[MAX_PATH_LEN];
            build_full_path(dir_path, e.name, full_path, sizeof(full_path));
            struct stat st;
            if (disk_file_stat(fs_type, full_path, &st) == 0)
                cJSON_AddNumberToObject(item, "mtime", (double)st.st_mtime);
            else
                cJSON_AddNumberToObject(item, "mtime", 0);
            cJSON_AddItemToArray(entries_array, item);
            count++;
        }
    }

    disk_file_closedir(fs_type, dd);

    cJSON_AddItemToObject(response_json, "entries", entries_array);
    cJSON_AddNumberToObject(response_json, "count", count);

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "Directory listing retrieved");
    cJSON_Delete(response_json);
    return ret;
}

/**
 * @brief GET /api/v1/files/download?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_download_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "GET"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");

    char fs_str[16] = "flash";
    char file_path[MAX_PATH_LEN] = {0};
    get_query_param(ctx, "fs", fs_str, sizeof(fs_str));
    get_query_param(ctx, "path", file_path, sizeof(file_path));

    if (file_path[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "File path is required");
    }

    FS_Type_t fs_type = parse_fs_type(fs_str);

    // Verify SD is available
    if (!file_handler_check_fs(ctx, fs_type)) return AICAM_OK;

    // Get file stat first
    struct stat st;
    if (disk_file_stat(fs_type, file_path, &st) != 0) {
        return api_response_error(ctx, API_ERROR_NOT_FOUND, "File not found");
    }
    if (st.st_mode & S_IFDIR) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Cannot download a directory");
    }

    // Open file
    void *fd = disk_file_fopen(fs_type, file_path, "r");
    if (!fd) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to open file");
    }

    // Extract filename and send headers
    const char *filename = strrchr(file_path, '/');
    if (filename) filename++; else filename = file_path;

    mg_printf(ctx->conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Disposition: attachment; filename=\"%.128s\"\r\n"
              "Content-Length: %lu\r\n"
              "Connection: close\r\n"
              "\r\n",
              get_mime_type(filename), filename, (unsigned long)st.st_size);

    // Event-driven download - follows OTA export pattern
    file_download_ctx_t *dc = (file_download_ctx_t *)buffer_calloc(1, sizeof(*dc));
    if (!dc) { disk_file_fclose(fs_type, fd);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "OOM"); }
    dc->magic     = FILE_DOWNLOAD_CTX_MAGIC;
    dc->fs_type   = fs_type;
    dc->fd        = fd;
    dc->remaining = (size_t)st.st_size;
    dc->sent      = 0;

    ctx->conn->fn_data = dc;
    ctx->conn->fn      = file_download_event_handler;

    // Tell router not to send additional response
    return AICAM_ERROR_NOT_SENT_AGAIN;
}

/**
 * @brief POST /api/v1/files/upload?fs=flash|sd&path=/dir&filename=myfile.txt
 *        Body: raw file content
 */
aicam_result_t file_upload_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "POST"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");

    char fs_str[16] = "flash";
    char dir_path[MAX_PATH_LEN] = "/";
    char filename[MAX_FILE_NAME_LEN] = {0};
    get_query_param(ctx, "fs", fs_str, sizeof(fs_str));
    get_query_param(ctx, "path", dir_path, sizeof(dir_path));
    get_query_param(ctx, "filename", filename, sizeof(filename));

    if (filename[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Filename is required");
    }

    FS_Type_t fs_type = parse_fs_type(fs_str);

    // Verify SD is available
    if (!file_handler_check_fs(ctx, fs_type)) return AICAM_OK;

    // Check body
    if (!ctx->request.body || ctx->request.content_length == 0) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "File content is empty");
    }

    // Build full path
    char full_path[MAX_PATH_LEN];
    build_full_path(dir_path, filename, full_path, sizeof(full_path));

    // Write file
    void *fd = disk_file_fopen(fs_type, full_path, "w");
    if (!fd) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create file");
    }

    int written = disk_file_fwrite(fs_type, fd, ctx->request.body, ctx->request.content_length);
    disk_file_fclose(fs_type, fd);

    if (written < 0) {
        disk_file_remove(fs_type, full_path);  // Clean up partial file
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to write file");
    }

    // Success response
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "fs", fs_str);
    cJSON_AddStringToObject(response_json, "path", full_path);
    cJSON_AddStringToObject(response_json, "filename", filename);
    cJSON_AddNumberToObject(response_json, "size", (double)ctx->request.content_length);

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "File uploaded successfully");
    cJSON_Delete(response_json);
    return ret;
}

/* Recursively remove a file or directory tree. Both LittleFS (lfs_remove) and
 * FAT (fx_directory_delete) refuse to remove a non-empty directory, so deleting
 * a populated folder means walking it, deleting every child (recursing into
 * subdirs), then removing the now-empty directory itself.
 *
 * SD/FileX quirk: readdir iterates the media's GLOBAL default directory
 * (sd_filex_opendir calls fx_directory_default_set and never restores it), so
 * nesting opendir/readdir would clobber the parent's cursor. We therefore
 * collect all child names up front, close the dir, THEN recurse.
 *
 * Returns 0 on full success, -1 on any failure (the tree may be partially
 * deleted). Depth- and per-dir-count-capped to bound stack/heap use. */
#define RM_MAX_DEPTH   16
#define RM_MAX_ENTRIES 128
static int remove_recursive(FS_Type_t fs, const char *path, int depth)
{
    struct stat st;
    if (disk_file_stat(fs, path, &st) != 0) return -1;
    if (!(st.st_mode & S_IFDIR)) {
        return disk_file_remove(fs, path);                 /* regular file */
    }
    if (depth >= RM_MAX_DEPTH) return -1;                   /* too deep — bail */

    char (*names)[256] = (char (*)[256])hal_mem_alloc_large((size_t)RM_MAX_ENTRIES * 256);
    if (!names) return -1;
    int n = 0;

    void *dd = disk_file_opendir(fs, path);
    if (!dd) { hal_mem_free(names); return -1; }
    if (fs == FS_SD) {
        sd_entry_t e;
        while (disk_file_readdir(fs, dd, (char *)&e) > 0 && n < RM_MAX_ENTRIES) {
            if (e.name[0] == '.' && (e.name[1] == '\0' ||
                (e.name[1] == '.' && e.name[2] == '\0'))) continue;   /* "." / ".." */
            strncpy(names[n], e.name, 255); names[n][255] = '\0';
            n++;
        }
    } else {
        flash_entry_t e;
        while (disk_file_readdir(fs, dd, (char *)&e) > 0 && n < RM_MAX_ENTRIES) {
            if (e.name[0] == '.' && (e.name[1] == '\0' ||
                (e.name[1] == '.' && e.name[2] == '\0'))) continue;
            strncpy(names[n], e.name, 255); names[n][255] = '\0';
            n++;
        }
    }
    disk_file_closedir(fs, dd);                              /* close BEFORE recursing */

    int rc = 0;
    for (int i = 0; i < n; i++) {
        char child[MAX_PATH_LEN];
        build_full_path(path, names[i], child, sizeof(child));
        if (remove_recursive(fs, child, depth + 1) != 0) rc = -1;
    }
    hal_mem_free(names);

    if (disk_file_remove(fs, path) != 0) rc = -1;           /* remove now-empty dir */
    return rc;
}

/**
 * @brief DELETE /api/v1/files?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_delete_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "DELETE"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only DELETE method is allowed");

    char fs_str[16] = "flash";
    char file_path[MAX_PATH_LEN] = {0};
    get_query_param(ctx, "fs", fs_str, sizeof(fs_str));
    get_query_param(ctx, "path", file_path, sizeof(file_path));
    char recursive_str[8] = {0};
    get_query_param(ctx, "recursive", recursive_str, sizeof(recursive_str));
    bool recursive = (strcmp(recursive_str, "true") == 0);

    if (file_path[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "File path is required");
    }

    FS_Type_t fs_type = parse_fs_type(fs_str);

    // Verify SD is available
    if (!file_handler_check_fs(ctx, fs_type)) return AICAM_OK;

    // Verify file exists
    struct stat st;
    if (disk_file_stat(fs_type, file_path, &st) != 0) {
        return api_response_error(ctx, API_ERROR_NOT_FOUND, "File not found");
    }

    bool is_dir = (st.st_mode & S_IFDIR) != 0;
    /* recursive=true on a directory → walk and delete the whole tree (both
     * filesystems reject remove() on a non-empty dir). Otherwise a plain
     * remove: empty dirs succeed, non-empty dirs fail with DIR_NOT_EMPTY. */
    int result = (is_dir && recursive) ? remove_recursive(fs_type, file_path, 0)
                                       : disk_file_remove(fs_type, file_path);
    if (result != 0) {
        if (is_dir) {
            return api_response_error(ctx, API_BUSINESS_ERROR_DIR_NOT_EMPTY,
                                      "Failed to delete folder; it may be non-empty or in use");
        }
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to delete file");
    }

    // Extract filename for response
    const char *fname = strrchr(file_path, '/');
    if (fname) fname++; else fname = file_path;

    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "fs", fs_str);
    cJSON_AddStringToObject(response_json, "path", file_path);
    cJSON_AddStringToObject(response_json, "filename", fname);
    cJSON_AddBoolToObject(response_json, "deleted", 1);

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "File deleted successfully");
    cJSON_Delete(response_json);
    return ret;
}

/**
 * @brief PUT /api/v1/files/rename
 *        Body: {"fs": "flash", "old_path": "/old.txt", "new_path": "/new.txt"}
 */
aicam_result_t file_rename_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "PUT"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only PUT method is allowed");

    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    cJSON *fs_item = cJSON_GetObjectItem(request_json, "fs");
    cJSON *old_item = cJSON_GetObjectItem(request_json, "old_path");
    cJSON *new_item = cJSON_GetObjectItem(request_json, "new_path");

    if (!old_item || !new_item || !cJSON_IsString(old_item) || !cJSON_IsString(new_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Both old_path and new_path are required");
    }

    const char *fs_str = (fs_item && cJSON_IsString(fs_item)) ? fs_item->valuestring : "flash";
    FS_Type_t fs_type = parse_fs_type(fs_str);

    // Verify SD is available
    if (fs_type == FS_SD && !device_service_storage_is_sd_connected()) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "SD card not available");
    }
    if (fs_type == FS_FLASH && !storage_is_lfs_mounted()) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                  "Flash FS not mounted, format required");
    }

    int result = disk_file_rename(fs_type, old_item->valuestring, new_item->valuestring);
    if (result != 0) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to rename file");
    }

    /* fs_str / old_item / new_item point into request_json — the tree must
     * outlive the response construction (deleting it first was a UAF). */
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "fs", fs_str);
    cJSON_AddStringToObject(response_json, "old_path", old_item->valuestring);
    cJSON_AddStringToObject(response_json, "new_path", new_item->valuestring);

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "File renamed successfully");
    cJSON_Delete(response_json);
    cJSON_Delete(request_json);
    return ret;
}

/**
 * @brief GET /api/v1/files/preview?fs=flash|sd&path=/file.txt
 */
aicam_result_t file_preview_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "GET"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");

    char fs_str[16] = "flash";
    char file_path[MAX_PATH_LEN] = {0};
    get_query_param(ctx, "fs", fs_str, sizeof(fs_str));
    get_query_param(ctx, "path", file_path, sizeof(file_path));

    if (file_path[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "File path is required");
    }

    FS_Type_t fs_type = parse_fs_type(fs_str);

    if (!file_handler_check_fs(ctx, fs_type)) return AICAM_OK;

    struct stat st;
    if (disk_file_stat(fs_type, file_path, &st) != 0) {
        return api_response_error(ctx, API_ERROR_NOT_FOUND, "File not found");
    }

    // Determine size limit based on type
    const char *fname = strrchr(file_path, '/');
    if (fname) fname++; else fname = file_path;
    size_t max_size = is_previewable_image(fname) ? MAX_PREVIEW_SIZE_IMAGE : MAX_PREVIEW_SIZE_TEXT;

    if ((size_t)st.st_size > max_size) {
        cJSON *response_json = cJSON_CreateObject();
        cJSON_AddBoolToObject(response_json, "too_large", 1);
        cJSON_AddNumberToObject(response_json, "file_size", (double)st.st_size);
        cJSON_AddNumberToObject(response_json, "max_preview_size", (double)max_size);
        char *json_str = cJSON_Print(response_json);
        aicam_result_t ret = api_response_success(ctx, json_str, "File too large for preview");
        cJSON_Delete(response_json);
        return ret;
    }

    void *fd = disk_file_fopen(fs_type, file_path, "r");
    if (!fd) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to open file");
    }

    size_t file_size = (size_t)st.st_size;

    if (is_previewable_image(fname)) {
        // Send image binary through the event-driven chunked sender used by
        // downloads. mg_send copies into the connection send buffer, so an
        // inline read+send loop would grow and repeatedly realloc a near-2MB
        // queue (the preview size cap) instead of streaming an 8KB working
        // set, and an allocation failure mid-loop would strand the client
        // with an already-advertised Content-Length.
        mg_printf(ctx->conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %lu\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  get_mime_type(fname), (unsigned long)file_size);

        file_download_ctx_t *dc = (file_download_ctx_t *)buffer_calloc(1, sizeof(*dc));
        if (!dc) {
            disk_file_fclose(fs_type, fd);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Memory allocation failed");
        }
        dc->magic     = FILE_DOWNLOAD_CTX_MAGIC;
        dc->fs_type   = fs_type;
        dc->fd        = fd;              /* closed by the event handler cleanup */
        dc->remaining = file_size;
        dc->sent      = 0;

        ctx->conn->fn_data = dc;
        ctx->conn->fn      = file_download_event_handler;

        return AICAM_ERROR_NOT_SENT_AGAIN;
    }

    uint8_t *buffer = NULL;
    int bytes_read = 0;

    if (file_size > 0) {
        buffer = (uint8_t *)hal_mem_alloc_large(file_size);
        if (!buffer) {
            disk_file_fclose(fs_type, fd);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Memory allocation failed");
        }
        bytes_read = disk_file_fread(fs_type, fd, buffer, file_size);
        disk_file_fclose(fs_type, fd);
        if (bytes_read < 0) {
            hal_mem_free(buffer);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to read file");
        }
    } else {
        disk_file_fclose(fs_type, fd);
    }

    // Return text content in JSON response
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "filename", fname);
    cJSON_AddStringToObject(response_json, "mime_type", get_mime_type(fname));
    cJSON_AddNumberToObject(response_json, "size", (double)bytes_read);

    char *content = (char *)hal_mem_alloc_large(bytes_read + 1);
    if (content) {
        memcpy(content, buffer, bytes_read);
        content[bytes_read] = '\0';
        cJSON_AddStringToObject(response_json, "content", content);
        hal_mem_free(content);
    }

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "File preview retrieved");
    cJSON_Delete(response_json);
    if (buffer) hal_mem_free(buffer);
    return ret;
}

/**
 * @brief PUT /api/v1/files/edit
 *        Body: {"fs": "flash", "path": "/file.txt", "content": "new content"}
 */
aicam_result_t file_edit_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "PUT"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only PUT method is allowed");

    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    cJSON *fs_item = cJSON_GetObjectItem(request_json, "fs");
    cJSON *path_item = cJSON_GetObjectItem(request_json, "path");
    cJSON *content_item = cJSON_GetObjectItem(request_json, "content");

    if (!path_item || !cJSON_IsString(path_item) || !content_item || !cJSON_IsString(content_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Both path and content are required");
    }

    const char *fs_str = (fs_item && cJSON_IsString(fs_item)) ? fs_item->valuestring : "flash";
    const char *file_path = path_item->valuestring;
    const char *new_content = content_item->valuestring;
    FS_Type_t fs_type = parse_fs_type(fs_str);

    if (fs_type == FS_SD && !device_service_storage_is_sd_connected()) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "SD card not available");
    }
    if (fs_type == FS_FLASH && !storage_is_lfs_mounted()) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE,
                                  "Flash FS not mounted, format required");
    }

    // Safety: only allow editing text files
    if (!is_text_editable(file_path)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Only text-based files can be edited");
    }

    // Write the new content
    void *fd = disk_file_fopen(fs_type, file_path, "w");
    if (!fd) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to open file for writing");
    }

    size_t content_len = strlen(new_content);
    int written = disk_file_fwrite(fs_type, fd, new_content, content_len);
    disk_file_fclose(fs_type, fd);

    if (written < 0 || (size_t)written != content_len) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to write file content");
    }

    /* fs_str / file_path point into request_json — the tree must outlive the
     * response construction (deleting it first was a UAF). */
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "fs", fs_str);
    cJSON_AddStringToObject(response_json, "path", file_path);
    cJSON_AddNumberToObject(response_json, "size", (double)content_len);

    char *json_str = cJSON_Print(response_json);
    aicam_result_t ret = api_response_success(ctx, json_str, "File edited successfully");
    cJSON_Delete(response_json);
    cJSON_Delete(request_json);
    return ret;
}

/**
 * @brief POST /api/v1/files/create - Create a new file or directory
 */
aicam_result_t file_create_handler(http_handler_context_t *ctx)
{
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    if (!web_api_verify_method(ctx, "POST"))
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");

    cJSON *req = web_api_parse_body(ctx);
    if (!req) return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON body");

    cJSON *fs_j   = cJSON_GetObjectItem(req, "fs");
    cJSON *path_j = cJSON_GetObjectItem(req, "path");
    cJSON *name_j = cJSON_GetObjectItem(req, "name");
    cJSON *type_j = cJSON_GetObjectItem(req, "type");

    if (!path_j || !name_j || !type_j || !cJSON_IsString(path_j) ||
        !cJSON_IsString(name_j) || !cJSON_IsString(type_j)) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "path, name, type required");
    }

    const char *fs   = (fs_j && cJSON_IsString(fs_j)) ? fs_j->valuestring : "flash";
    const char *path = path_j->valuestring;
    const char *name = name_j->valuestring;
    const char *type = type_j->valuestring;
    FS_Type_t fst = parse_fs_type(fs);

    if (fst == FS_SD && !device_service_storage_is_sd_connected()) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "SD card not available");
    }

    char fp[MAX_PATH_LEN];
    build_full_path(path, name, fp, sizeof(fp));

    int res;
    if (strcmp(type, "dir") == 0) {
        res = disk_file_mkdir(fst, fp);
    } else {
        void *fd = disk_file_fopen(fst, fp, "w");
        if (fd) { disk_file_fclose(fst, fd); res = 0; }
        else res = -1;
    }
    if (res != 0) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create item");
    }

    /* fs / name / type point into req — the tree must outlive the response
     * construction (deleting it first was a UAF). */
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "fs", fs);
    cJSON_AddStringToObject(out, "path", fp);
    cJSON_AddStringToObject(out, "name", name);
    cJSON_AddStringToObject(out, "type", type);
    char *js = cJSON_Print(out);
    aicam_result_t ret = api_response_success(ctx, js, "Created successfully");
    cJSON_Delete(out);
    cJSON_Delete(req);
    return ret;
}

/* ==================== Route Registration ==================== */

static api_route_t file_routes[] = {
    {
        .path = API_PATH_PREFIX "/files/list",
        .method = "GET",
        .handler = file_list_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/download",
        .method = "GET",
        .handler = file_download_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/upload",
        .method = "POST",
        .handler = file_upload_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files",
        .method = "DELETE",
        .handler = file_delete_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/rename",
        .method = "PUT",
        .handler = file_rename_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/preview",
        .method = "GET",
        .handler = file_preview_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/edit",
        .method = "PUT",
        .handler = file_edit_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/files/create",
        .method = "POST",
        .handler = file_create_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
};

#define FILE_ROUTE_COUNT (sizeof(file_routes) / sizeof(api_route_t))

aicam_result_t web_api_register_file_module(void)
{
    for (size_t i = 0; i < FILE_ROUTE_COUNT; i++) {
        aicam_result_t result = http_server_register_route(&file_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register file route %s: %d",
                          file_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("File API module registered successfully (%zu routes)", FILE_ROUTE_COUNT);
    return AICAM_OK;
}
