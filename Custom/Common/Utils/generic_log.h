#ifndef GENERIC_LOG_H
#define GENERIC_LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>

typedef void (*log_lock_func_t)(void);
typedef void (*log_unlock_func_t)(void);
typedef uint64_t (*log_get_time_func_t)(void);

typedef void* (*log_fopen_func_t)(const char *filename, const char *mode);
typedef int   (*log_fclose_func_t)(void *handle);
typedef int   (*log_remove_func_t)(const char *filename);
typedef int   (*log_rename_func_t)(const char *oldname, const char *newname);
typedef long  (*log_ftell_func_t)(void *handle);
typedef int   (*log_fseek_func_t)(void *handle, long offset, int whence);
typedef int   (*log_fflush_func_t)(void *handle);
typedef int   (*log_fwrite_func_t)(void *handle, const void *buf, size_t size);
typedef int   (*log_stat_func_t)(const char *filename, struct stat *st);
typedef int   (*log_get_free_bytes_func_t)(uint64_t *out_free_bytes);  /* 0 = ok, -1 = FS not ready */
typedef void (*log_custom_output_func_t)(const char *msg, int len);

/* Async file-output enqueue callback. When async file mode is enabled via
 * log_set_async_file(), the OUTPUT_FILE branch of log_message() no longer
 * writes to disk inline; instead it hands each formatted, ANSI-stripped line
 * to this callback (expected non-blocking). A background task later drains
 * the queue and calls log_file_flush_line() to perform the actual write. */
typedef void (*log_enqueue_cb_t)(const char *line, int len);

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL,
    LOG_SIMPLE,
    LOG_LEVEL_COUNT
} LogLevel;

typedef struct log_custom_output_node {
    log_custom_output_func_t func;
    struct log_custom_output_node *next;
} log_custom_output_node_t;

typedef struct {
    log_fopen_func_t   fopen;
    log_fclose_func_t  fclose;
    log_remove_func_t  remove;
    log_rename_func_t  rename;
    log_ftell_func_t   ftell;
    log_fseek_func_t   fseek;
    log_fflush_func_t  fflush;
    log_fwrite_func_t  fwrite;
    log_stat_func_t    fstat;
    log_get_free_bytes_func_t get_free_bytes;  /* optional; NULL ⇒ skip free-space pre-check */
} log_file_ops_t;

typedef struct {
    const char *name;
    LogLevel level;
    LogLevel file_level;
} log_module_t;

typedef enum {
    OUTPUT_CONSOLE,
    OUTPUT_FILE,
    OUTPUT_CUSTOM
} OutputType;

typedef struct {
    OutputType type;
    union {
        struct {
            char *filename;
            size_t max_size;
            int max_files;
            bool disabled;        /* hard-disabled for this run — set at registration (low free
                                   * space) or by the circuit breaker on a write/open/rename
                                   * failure. Not re-enabled by log_set_output_enabled. */
        } file;
        struct {
            log_custom_output_func_t func;
        } custom;
    } config;
    bool enabled;
} log_output_t;

typedef struct {
    log_module_t *modules;
    size_t module_count;
    log_output_t *outputs;
    size_t output_count;
    log_file_ops_t *file_ops;
    log_lock_func_t lock;
    log_unlock_func_t unlock;
    log_get_time_func_t get_time_func;
    log_custom_output_node_t *custom_outputs;
    bool thread_safe;
    /* When async_file is true, OUTPUT_FILE dispatch enqueues via enqueue_cb
     * instead of writing synchronously. The actual write (rotate+fopen+fwrite+
     * fclose + circuit breaker) is performed by log_file_flush_line(), which
     * the drain task calls. Keeps callers from blocking on LittleFS I/O. */
    bool async_file;
    log_enqueue_cb_t enqueue_cb;
} log_manager_t;

int log_register_module(const char *name, LogLevel level, LogLevel file_level);
int log_add_custom_output(log_custom_output_func_t func);
int log_remove_custom_output(log_custom_output_func_t func);
int log_add_output(OutputType type, const char *filename, size_t max_size, int max_files);
int log_set_output_enabled(OutputType type, bool enabled);
void log_message(LogLevel level, const char *module_name, const char *format, ...);
int log_init(log_manager_t *mgr, log_lock_func_t lock, log_unlock_func_t unlock, log_file_ops_t *file_ops, log_get_time_func_t get_time_func);

/* Enable/disable async file output. When enabled, log_message() forwards each
 * file-destined line to cb (non-blocking) instead of writing inline. The
 * caller-owned drain task must invoke log_file_flush_line() to persist. */
void log_set_async_file(bool enable, log_enqueue_cb_t cb);

/* Persist one already-formatted, ANSI-stripped line to the file output,
 * performing rotation + circuit-breaker handling. Intended to be called by the
 * async drain task (NOT the log_message() caller). Takes log_mutex internally. */
void log_file_flush_line(const char *line, int len);
#endif