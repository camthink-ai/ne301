/**
 * @file debug.c
 * @brief AI Camera Debug System Implementation
 * @details Debug system implementation providing command line interface, logging, and YModem transfer
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "usart.h"
#include "debug.h"
#include "generic_file.h"
#include "drtc.h"
#include "generic_ymodem.h"
#include "storage.h"
#include "common_utils.h"
#include "cmsis_os2.h"
#include "cli_cmd.h"
#include "json_config_internal.h"

/* ==================== Missing Macro Definitions ==================== */

// Memory alignment and section macros (if not already defined in common_utils.h)
#ifndef ALIGN_32
#define ALIGN_32 __attribute__ ((aligned (32)))
#endif

#ifndef UNCACHED
#define UNCACHED __attribute__ ((section (".uncached_bss")))
#endif

#ifndef IN_PSRAM
#define IN_PSRAM __attribute__ ((section (".psram_bss")))
#endif

// stat.h related macros (if not available)
#ifndef S_IFREG
#define S_IFREG  0100000  // Regular file
#define S_IFDIR  0040000  // Directory
#endif

// LittleFS includes for type definitions
#include "lfs.h"

#ifdef  STM32N6_DK_BOARD
#define H_UART huart1
#else
#define H_UART huart2
#endif
/* ==================== Private Variables ==================== */

static debug_context_t g_debug_ctx = {0};

static uint8_t debug_tread_stack[1024 * 32] ALIGN_32 IN_PSRAM;
// RTOS task attributes
const osThreadAttr_t debug_task_attributes = {
    .name = "debugTask",
    .priority = (osPriority_t) osPriorityHigh7,
    // .stack_size = 16 * 1024
    .stack_mem = debug_tread_stack,
    .stack_size = sizeof(debug_tread_stack),
};

const osThreadAttr_t ymodem_task_attributes = {
    .name = "ymodemTask",
    .priority = (osPriority_t) osPriorityHigh,
    .stack_size = 4 * 1024
};

/* ==================== Async Log Writer ==================== *
 * log_message() (under log_mutex) enqueues formatted lines here; a low-
 * priority drain task persists them via log_file_flush_line() (which takes
 * NO log_mutex — the slow LittleFS I/O thus never blocks log callers).
 *
 * Ring is SPSC: producers are serialized by log_mutex (only one log_message
 * runs at a time), and the drainer is exclusive (drain task OR debug_flush_logs,
 * mutually excluded by g_log_flush_mtx). __DMB() orders the head/tail updates
 * the same way the cmd_queue already does. */

#define LOG_QUEUE_SLOTS   32
#define LOG_LINE_MAX      256   /* matches LOG_MAX_LINE in generic_log.c */

static char    g_log_ring[LOG_QUEUE_SLOTS][LOG_LINE_MAX] ALIGN_32 IN_PSRAM;
static int     g_log_ring_len[LOG_QUEUE_SLOTS];
static volatile uint16_t g_log_ring_wr;   /* producer index (under log_mutex) */
static volatile uint16_t g_log_ring_rd;   /* consumer index (drainer only)   */
static volatile uint32_t g_log_dropped;   /* lines dropped because ring full */
static osSemaphoreId_t   g_log_sem;       /* "data available" event (binary) */
static osMutexId_t       g_log_flush_mtx; /* serializes drainers             */
static osThreadId_t      g_log_writer_task;

static uint8_t log_writer_stack[1024 * 4] ALIGN_32 IN_PSRAM;
const osThreadAttr_t log_writer_task_attributes = {
    .name = "logWriter",
    .priority = (osPriority_t) osPriorityLow,   /* below upload/storage/debug */
    .stack_mem = log_writer_stack,
    .stack_size = sizeof(log_writer_stack),
};

/* ==================== Private Function Declarations ==================== */

static aicam_result_t debug_uart_mode_switch(debug_mode_e mode);
static void debug_task_function(void *argument);
static aicam_result_t debug_load_config(void);
static aicam_result_t debug_init_logging(void);
static aicam_result_t debug_init_uart(void);
static aicam_result_t debug_init_cmdline(void);
static aicam_result_t debug_init_ymodem(void);
static void debug_uart_output(char c);
static void debug_uart_output_str(const char* str);
static void debug_lock(void);
static void debug_unlock(void);
static void debug_log_lock(void);
static void debug_log_unlock(void);


// File operations for generic_log
static void* debug_log_fopen(const char *filename, const char *mode);
static int debug_log_fclose(void *handle);
static int debug_log_remove(const char *filename);
static int debug_log_rename(const char *oldname, const char *newname);
static long debug_log_ftell(void *handle);
static int debug_log_fseek(void *handle, long offset, int whence);
static int debug_log_fflush(void *handle);
static int debug_log_fwrite(void *handle, const void *buf, size_t size);
static int debug_log_fstat(const char *filename, struct stat *st);
static int debug_log_get_free_bytes(uint64_t *out_free);
static uint64_t debug_log_get_time(void);
static void debug_uart_log_output(const char *msg, int len);

// Async log writer (drain task + enqueue callback)
static void debug_log_enqueue(const char *line, int len);
static void debug_log_writer_task(void *argument);
static void debug_log_drain_ring(void);

// driver command register all
static void driver_cmd_register_all(void);

/* ==================== Built-in Command Implementations ==================== */

int debug_cmd_sysinfo(int argc, char* argv[])
{
    printf("=== AICAM System Information ===\r\n");
    printf("Device Name: %s\r\n", "AICAM-Camera");  // TODO: Get from config
    printf("Hardware Version: %s\r\n", "1.0");
    printf("Software Version: %s\r\n", "1.0.0");
    printf("Build Date: %s %s\r\n", __DATE__, __TIME__);
    printf("System Uptime: %llu seconds\r\n", g_debug_ctx.stats.uptime_seconds);
    printf("Debug Mode: %s\r\n", 
           g_debug_ctx.current_mode == DEBUG_MODE_COMMAND ? "Command" :
           g_debug_ctx.current_mode == DEBUG_MODE_YMODEM ? "YModem" : "Disabled");
    return 0;
}

int debug_cmd_config(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: config <get|set|save|load|reset>\r\n");
        return -1;
    }
    
    if (strcmp(argv[1], "get") == 0) {
        printf("=== Debug Configuration ===\r\n");
        printf("Console Log Level: %s\r\n", log_level_to_string(g_debug_ctx.config.console_level));
        printf("File Log Level: %s\r\n", log_level_to_string(g_debug_ctx.config.file_level));
        printf("Log File Size: %u KB\r\n", (unsigned int)g_debug_ctx.config.log_file_size / 1024);
        printf("Log Rotation Count: %u\r\n", g_debug_ctx.config.log_rotation_count);
        printf("UART Echo: %s\r\n", g_debug_ctx.config.uart_echo_enable ? "Enabled" : "Disabled");
        printf("Timestamp: %s\r\n", g_debug_ctx.config.timestamp_enable ? "Enabled" : "Disabled");
    } else if (strcmp(argv[1], "save") == 0) {
        // TODO: Save current config to JSON config manager
        printf("Configuration saved\r\n");
    } else if (strcmp(argv[1], "load") == 0) {
        if (debug_load_config() == AICAM_OK) {
            printf("Configuration loaded\r\n");
        } else {
            printf("Failed to load configuration\r\n");
        }
    } else if (strcmp(argv[1], "reset") == 0) {
        // Reset to default configuration
        json_config_reset_to_default(NULL);
        g_debug_ctx.config.console_level = LOG_LEVEL_INFO;
        g_debug_ctx.config.file_level = LOG_LEVEL_DEBUG;
        g_debug_ctx.config.log_file_size = DEBUG_DEFAULT_LOG_FILE_SIZE;
        g_debug_ctx.config.log_rotation_count = DEBUG_DEFAULT_LOG_FILE_COUNT;
        g_debug_ctx.config.uart_echo_enable = AICAM_TRUE;
        g_debug_ctx.config.timestamp_enable = AICAM_TRUE;
        printf("Configuration reset to defaults\r\n");
    } else {
        printf("Invalid config command\r\n");
        return -1;
    }
    
    return 0;
}

int debug_cmd_loglevel(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: loglevel <debug|info|warn|error> [file]\r\n");
        printf("Current console level: %s\r\n", log_level_to_string(g_debug_ctx.config.console_level));
        printf("Current file level: %s\r\n", log_level_to_string(g_debug_ctx.config.file_level));
        return 0;
    }
    
    log_level_e level;
    if (strcmp(argv[1], "debug") == 0) {
        level = LOG_LEVEL_DEBUG;
    } else if (strcmp(argv[1], "info") == 0) {
        level = LOG_LEVEL_INFO;
    } else if (strcmp(argv[1], "warn") == 0) {
        level = LOG_LEVEL_WARN;
    } else if (strcmp(argv[1], "error") == 0) {
        level = LOG_LEVEL_ERROR;
    } else {
        printf("Invalid log level. Valid levels: debug, info, warn, error\r\n");
        return -1;
    }
    
    if (argc > 2 && strcmp(argv[2], "file") == 0) {
        g_debug_ctx.config.file_level = level;
        printf("File log level set to: %s\r\n", log_level_to_string(level));
    } else {
        g_debug_ctx.config.console_level = level;
        printf("Console log level set to: %s\r\n", log_level_to_string(level));
    }

    debug_update_config();
    return 0;
}

int debug_cmd_meminfo(int argc, char* argv[])
{
    printf("=== Memory Information ===\r\n");
    // TODO: Add more memory statistics if available
    return 0;
}

int debug_cmd_tasks(int argc, char* argv[])
{
    printf("=== Task Information ===\r\n");
    // TODO: Implement task list display using FreeRTOS APIs
    printf("Task list not implemented yet\r\n");
    return 0;
}

int debug_cmd_devices(int argc, char* argv[])
{
    printf("=== Device List ===\r\n");
    // TODO: Implement device list using device manager
    printf("Device list not implemented yet\r\n");
    return 0;
}

int debug_cmd_ymodem(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: ymodem <send|receive> [filename]\r\n");
        return -1;
    }
    
    if (strcmp(argv[1], "receive") == 0) {
        printf("Starting YModem receive mode...\r\n");
        debug_set_mode(DEBUG_MODE_YMODEM);
        return 0;
    } else if (strcmp(argv[1], "send") == 0 && argc > 2) {
        printf("YModem send not implemented yet\r\n");
        return -1;
    } else {
        printf("Invalid ymodem command\r\n");
        return -1;
    }
}

int debug_cmd_reset(int argc, char* argv[])
{
    printf("System reset in 3 seconds...\r\n");
    /* Flush async log queue so recent log lines are not lost on reset. */
    debug_flush_logs();
    osDelay(3000);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    return 0; // Never reached
}

// Built-in command table
static debug_cmd_reg_t builtin_commands[] = {
    {"sysinfo",     "Display system information",      debug_cmd_sysinfo},
    {"config",      "Configuration management",        debug_cmd_config},
    {"loglevel",    "Set log levels",                  debug_cmd_loglevel},
    {"meminfo",     "Display memory information",      debug_cmd_meminfo},
    {"tasks",       "List running tasks",              debug_cmd_tasks},
    {"devices",     "List registered devices",         debug_cmd_devices},
    {"ymodem",      "YModem file transfer",            debug_cmd_ymodem},
    {"reset",       "System reset",                    debug_cmd_reset},
};

/* ==================== Public API Implementation ==================== */

aicam_result_t debug_system_init(void)
{
    if (g_debug_ctx.initialized) {
        return AICAM_OK;
    }

    // printf("[DEBUG] Initializing debug system...\r\n");
    
    // Initialize debug context
    memset(&g_debug_ctx, 0, sizeof(debug_context_t));
    g_debug_ctx.current_mode = DEBUG_MODE_COMMAND;
    
    // Create RTOS objects first (needed for logging)
    g_debug_ctx.mutex = osMutexNew(NULL);
    g_debug_ctx.log_mutex = osMutexNew(NULL);
    g_debug_ctx.semaphore = osSemaphoreNew(1, 0, NULL);

    // Async log writer resources: binary "data available" event + drainer mutex
    g_log_sem        = osSemaphoreNew(1, 0, NULL);
    g_log_flush_mtx  = osMutexNew(NULL);

    if (!g_debug_ctx.mutex || !g_debug_ctx.log_mutex || !g_debug_ctx.semaphore ||
        !g_log_sem || !g_log_flush_mtx) {
        printf("[ERROR] Failed to create debug RTOS objects\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Allocate queue buffer
    g_debug_ctx.queue_buffer = hal_mem_alloc_fast(DEBUG_QUEUE_SIZE);
    if (!g_debug_ctx.queue_buffer) {
        printf("[ERROR] Failed to allocate debug queue buffer\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }

    // Load configuration 
    aicam_result_t result = debug_load_config();
    if (result != AICAM_OK) {
        printf("[WARN] Failed to load debug config, using defaults\r\n");
    }
    
    // Initialize logging system first (so we can use LOG macros)
    // printf("[DEBUG] Initializing logging...\r\n");
    result = debug_init_logging();
    if (result != AICAM_OK) {
        printf("[ERROR] Failed to initialize logging system\r\n");
        return result;
    }
    
    // Initialize other subsystems
    // printf("[DEBUG] Initializing subsystems...\r\n");
    result = debug_init_uart();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize UART");
        return result;
    }
    
    // printf("[DEBUG] Initializing command line...\r\n");
    result = debug_init_cmdline();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize command line");
        return result;
    }
    
    // printf("[DEBUG] Initializing YModem...\r\n");
    result = debug_init_ymodem();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize YModem");
        return result;
    }
    
    // Create tasks
    // printf("[DEBUG] Creating debug tasks...\r\n");
    g_debug_ctx.debug_task = osThreadNew(debug_task_function, NULL, &debug_task_attributes);

    if (!g_debug_ctx.debug_task ) {
        LOG_CORE_ERROR("Failed to create debug tasks");
        return AICAM_ERROR;
    }

    // Low-priority async log writer (drains the enqueue ring to flash)
    g_log_writer_task = osThreadNew(debug_log_writer_task, NULL, &log_writer_task_attributes);
    if (!g_log_writer_task) {
        LOG_CORE_ERROR("Failed to create log writer task");
        /* Non-fatal: logging falls back to synchronous mode. */
        log_set_async_file(false, NULL);
    }
    
    // printf("[DEBUG] Registering built-in commands...\r\n");
    // Register built-in commands
    result = debug_register_commands(builtin_commands, sizeof(builtin_commands) / sizeof(builtin_commands[0]));
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to register built-in commands");
        return result;
    }

    register_cmds();
    driver_cmd_register_all();
    
    g_debug_ctx.initialized = AICAM_TRUE;
    LOG_CORE_INFO("Debug system initialized successfully");
    
    // test uart output
    // printf("[DEBUG] Testing UART output...\r\n");
    // debug_uart_output_str("UART string output test\r\n");
    // debug_uart_output_str("Prompt test: ");
    // debug_uart_output_str(g_debug_ctx.cmdline.prompt);
    // debug_uart_output_str("\r\n");
    
    return AICAM_OK;
}

aicam_result_t debug_system_deinit(void)
{
    if (!g_debug_ctx.initialized) {
        return AICAM_OK;
    }
    
    // Terminate tasks
    if (g_debug_ctx.debug_task) {
        osThreadTerminate(g_debug_ctx.debug_task);
        g_debug_ctx.debug_task = NULL;
    }
    
    if (g_debug_ctx.ymodem_task) {
        osThreadTerminate(g_debug_ctx.ymodem_task);
        g_debug_ctx.ymodem_task = NULL;
    }

    // Stop async log writer and flush any remaining lines.
    log_set_async_file(false, NULL);
    debug_flush_logs();
    if (g_log_writer_task) {
        osThreadTerminate(g_log_writer_task);
        g_log_writer_task = NULL;
    }
    if (g_log_flush_mtx) {
        osMutexDelete(g_log_flush_mtx);
        g_log_flush_mtx = NULL;
    }
    if (g_log_sem) {
        osSemaphoreDelete(g_log_sem);
        g_log_sem = NULL;
    }
    
    // Clean up RTOS objects
    if (g_debug_ctx.mutex) {
        osMutexDelete(g_debug_ctx.mutex);
        g_debug_ctx.mutex = NULL;
    }
    
    if (g_debug_ctx.log_mutex) {
        osMutexDelete(g_debug_ctx.log_mutex);
        g_debug_ctx.log_mutex = NULL;
    }
    
    if (g_debug_ctx.semaphore) {
        osSemaphoreDelete(g_debug_ctx.semaphore);
        g_debug_ctx.semaphore = NULL;
    }
    
    // Free allocated memory
    if (g_debug_ctx.queue_buffer) {
        hal_mem_free(g_debug_ctx.queue_buffer);
        g_debug_ctx.queue_buffer = NULL;
    }
    
    g_debug_ctx.initialized = AICAM_FALSE;
    LOG_CORE_INFO("Debug system deinitialized");
    
    return AICAM_OK;
}

void debug_register(void)
{
    // TODO: Register with device manager
}

aicam_result_t debug_register_commands(const debug_cmd_reg_t *cmd_table, size_t count)
{
    if (!cmd_table || count == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    for (size_t i = 0; i < count; i++) {
        cmdline_register(&g_debug_ctx.cmdline, cmd_table[i].name, cmd_table[i].help, cmd_table[i].handler);
    }
    
    return AICAM_OK;
}

aicam_result_t debug_set_mode(debug_mode_e mode)
{
    return debug_uart_mode_switch(mode);
}

debug_mode_e debug_get_mode(void)
{
    return g_debug_ctx.current_mode;
}

aicam_result_t debug_update_config(void)
{
    //write update config to nvs
    log_config_t log_config = {0};
    aicam_result_t result = json_config_get_log_config(&log_config);
    if (result != AICAM_OK) {
        printf("[WARN] Failed to get log config, using defaults\r\n");
        return result;
    }

    log_config.log_level = g_debug_ctx.config.console_level;
    log_config.log_file_size_kb = g_debug_ctx.config.log_file_size / 1024;
    log_config.log_file_count = g_debug_ctx.config.log_rotation_count;

    result = json_config_set_log_config(&log_config);
    if (result != AICAM_OK) {
        printf("[WARN] Failed to set log config, using defaults\r\n");
        return result;
    }
    
    //reregister log module  
    log_register_module(DEBUG_MODULE_DRIVER, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_HAL, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_CORE, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_SERVICE, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_TASK, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_APP, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module("SYSTEM", g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module("SIMPLE", g_debug_ctx.config.console_level, g_debug_ctx.config.file_level); 
    
    return AICAM_OK;
}

aicam_result_t debug_get_stats(debug_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(stats, &g_debug_ctx.stats, sizeof(debug_stats_t));
    return AICAM_OK;
}

aicam_result_t debug_reset_stats(void)
{
    memset(&g_debug_ctx.stats, 0, sizeof(debug_stats_t));
    return AICAM_OK;
}

void debug_IRQHandler(UART_HandleTypeDef *huart)
{
    // Handle UART interrupt
    if (huart == &H_UART) {
        if (g_debug_ctx.current_mode == DEBUG_MODE_COMMAND) {
            // Command mode - process character
            char received_char = g_debug_ctx.uart_rx_byte;
            
            // use lock free way
            // note: here assume single byte write is atomic operation
            cmd_queue_t *q = &g_debug_ctx.cmd_queue;
            
            // ensure queue is initialized
            if (q->buffer && q->size > 0) {
                uint16_t next_wr = (q->wr + 1) % q->size;
                
                if (next_wr != q->rd) {
                    q->buffer[q->wr] = received_char;
                    // Data Memory Barrier ensure write order
                    __DMB();
                    q->wr = next_wr;
                } else {
                    // queue full, update error statistics
                    g_debug_ctx.stats.uart_errors++;
                }
            }
            
            // continue receive next char
            HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
        }
        // if ymodem mode, add ymodem processing logic here
        else if (g_debug_ctx.current_mode == DEBUG_MODE_YMODEM) {
            // TODO: Handle YModem data reception
            // continue receive
            HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
        }
    }
}

void debug_process_char(char c)
{
    // Add character to command line queue
    if (!queue_put(&g_debug_ctx.cmd_queue, c)) {
        // Queue full, ignore character
        g_debug_ctx.stats.uart_errors++;
    }
}

aicam_result_t debug_register_log_output(log_custom_output_func_t func)
{
    return (log_add_custom_output(func) == 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t debug_set_console_output(aicam_bool_t enable)
{
    log_set_output_enabled(OUTPUT_CONSOLE, enable == AICAM_TRUE);
    return AICAM_OK;
}

aicam_result_t debug_flush_logs(void)
{
    /* Synchronously drain the async log queue so pending lines are persisted
     * before a reset / sleep. Safe to call from any thread (drainers are
     * serialized by g_log_flush_mtx). Called from debug_cmd_reset() below. */
    debug_log_drain_ring();
    return AICAM_OK;
}

/* ==================== Private Function Implementations ==================== */

static aicam_result_t debug_uart_mode_switch(debug_mode_e mode)
{
    osMutexAcquire(g_debug_ctx.mutex, osWaitForever);
    
    if (mode == g_debug_ctx.current_mode) {
        osMutexRelease(g_debug_ctx.mutex);
        return AICAM_OK;
    }
    
    // Stop current mode
    if (g_debug_ctx.current_mode == DEBUG_MODE_COMMAND) {
        HAL_UART_AbortReceive_IT(&H_UART);
        printf("[DEBUG] Command mode stopped\r\n");
    }
    else if (g_debug_ctx.current_mode == DEBUG_MODE_YMODEM) {
        HAL_UART_AbortReceive_IT(&H_UART);
        printf("[DEBUG] YModem mode stopped\r\n");
    }
    
    // Start new mode
    g_debug_ctx.current_mode = mode;
    
    if (mode == DEBUG_MODE_COMMAND) {
        // start command mode uart receive
        HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
        printf("[DEBUG] Command mode started\r\n");
    }
    else if (mode == DEBUG_MODE_YMODEM) {
        // start ymodem mode uart receive
        HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
        printf("[DEBUG] YModem mode started\r\n");
        // notify ymodem task to start work
        osSemaphoreRelease(g_debug_ctx.semaphore);
    }
    else if (mode == DEBUG_MODE_DISABLED) {
        printf("[DEBUG] Debug mode disabled\r\n");
    }
    
    osMutexRelease(g_debug_ctx.mutex);
    return AICAM_OK;
}

static void debug_task_function(void *argument)
{
    // printf("[DEBUG] Debug task started\r\n");
    
    // ensure uart receive interrupt is started
    if (g_debug_ctx.current_mode == DEBUG_MODE_COMMAND) {
        HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
        // printf("[DEBUG] UART interrupt initialized\r\n");
    }
    

    osDelay(100);
    
    // debug_uart_output_str("\r\n");  // first line feed
    // debug_uart_output_str(g_debug_ctx.cmdline.prompt);  
    // printf("[DEBUG] Initial prompt displayed\r\n");
    
    while (1) {
        if (!queue_empty(&g_debug_ctx.cmd_queue)) {
            cmdline_process(&g_debug_ctx.cmdline);
        } else {
            osDelay(10);
        }
        
  
        if (g_debug_ctx.current_mode != DEBUG_MODE_COMMAND) {
            // printf("[DEBUG] Switching mode, debug task paused\r\n");
            while (g_debug_ctx.current_mode != DEBUG_MODE_COMMAND) {
                osDelay(100);
            }
            HAL_UART_Receive_IT(&H_UART, &g_debug_ctx.uart_rx_byte, 1);
            // printf("[DEBUG] Back to command mode\r\n");
            debug_uart_output_str("\r\n");
            debug_uart_output_str(g_debug_ctx.cmdline.prompt);
        }
    }
}

static aicam_result_t debug_load_config(void)
{
    aicam_result_t result;
    uint8_t temp_u8 = 0;
    uint32_t temp_u32 = 0;

    /* Base defaults aligned with `default_config` / NVS keys in json_config_nvs.c (cf. quick_storage) */
    g_debug_ctx.config.console_level = (log_level_e)default_config.log_config.log_level;
    g_debug_ctx.config.file_level = LOG_LEVEL_WARN;
    g_debug_ctx.config.log_file_size =
        (unsigned int)(default_config.log_config.log_file_size_kb * 1024U);
    g_debug_ctx.config.log_rotation_count = default_config.log_config.log_file_count;
    g_debug_ctx.config.uart_echo_enable = AICAM_TRUE;
    g_debug_ctx.config.timestamp_enable = AICAM_TRUE;

    result = json_config_nvs_read_uint8(NVS_KEY_LOG_LEVEL, &temp_u8);
    if (result == AICAM_OK) {
        g_debug_ctx.config.console_level = (log_level_e)temp_u8;
    }

    result = json_config_nvs_read_uint32(NVS_KEY_LOG_FILE_SIZE, &temp_u32);
    if (result == AICAM_OK && temp_u32 > 0U) {
        g_debug_ctx.config.log_file_size = (unsigned int)(temp_u32 * 1024U);
    }

    result = json_config_nvs_read_uint32(NVS_KEY_LOG_FILE_COUNT, &temp_u32);
    if (result == AICAM_OK && temp_u32 > 0U) {
        g_debug_ctx.config.log_rotation_count = temp_u32;
    }

    // printf("[DEBUG] Config from NVS, console level: %d, file level: %d\r\n",
    //        (int)g_debug_ctx.config.console_level, (int)g_debug_ctx.config.file_level);

    return AICAM_OK;
}

static aicam_result_t debug_init_logging(void)
{
    // Initialize file operations structure
    g_debug_ctx.log_file_ops.fopen = debug_log_fopen;
    g_debug_ctx.log_file_ops.fclose = debug_log_fclose;
    g_debug_ctx.log_file_ops.remove = debug_log_remove;
    g_debug_ctx.log_file_ops.rename = debug_log_rename;
    g_debug_ctx.log_file_ops.ftell = debug_log_ftell;
    g_debug_ctx.log_file_ops.fseek = debug_log_fseek;
    g_debug_ctx.log_file_ops.fflush = debug_log_fflush;
    g_debug_ctx.log_file_ops.fwrite = debug_log_fwrite;
    g_debug_ctx.log_file_ops.fstat = debug_log_fstat;
    g_debug_ctx.log_file_ops.get_free_bytes = debug_log_get_free_bytes;
    
    // Initialize log manager
    int result = log_init(&g_debug_ctx.log_manager, 
                         debug_log_lock, 
                         debug_log_unlock, 
                         &g_debug_ctx.log_file_ops,
                         debug_log_get_time);
    
    if (result != 0) {
        return AICAM_ERROR;
    }
    
    // Register debug modules
    log_register_module(DEBUG_MODULE_DRIVER, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_HAL, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_CORE, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_SERVICE, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_TASK, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module(DEBUG_MODULE_APP, g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module("SYSTEM", g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    log_register_module("SIMPLE", g_debug_ctx.config.console_level, g_debug_ctx.config.file_level);
    
    // Add console output
    log_add_output(OUTPUT_CONSOLE, NULL, 0, 0);
    
    // Add file output
    log_add_output(OUTPUT_FILE, DEBUG_DEFAULT_LOG_FILE_NAME, 
                   g_debug_ctx.config.log_file_size, 
                   g_debug_ctx.config.log_rotation_count);
    
    // Add custom UART output
    log_add_custom_output(debug_uart_log_output);

    /* Switch file output to async mode: log_message() will enqueue each line
     * to the low-priority logWriter task instead of writing synchronously, so
     * callers never block on LittleFS I/O. */
    log_set_async_file(true, debug_log_enqueue);

    return AICAM_OK;
}

static aicam_result_t debug_init_uart(void)
{
    // UART is already initialized by HAL, just setup our handlers
    return AICAM_OK;
}

static aicam_result_t debug_init_cmdline(void)
{
    queue_init(&g_debug_ctx.cmd_queue, g_debug_ctx.queue_buffer, DEBUG_QUEUE_SIZE, debug_lock, debug_unlock);
    cmdline_init(&g_debug_ctx.cmdline, &g_debug_ctx.cmd_queue, debug_uart_output, NULL, DEBUG_PROMPT_STR);
    cmdline_register_output_str(&g_debug_ctx.cmdline, debug_uart_output_str);
    
    return AICAM_OK;
}

static aicam_result_t debug_init_ymodem(void)
{
    // TODO: Initialize YModem if available
    return AICAM_OK;
}

static void debug_uart_output(char c)
{
    // HAL_UART_Transmit(&H_UART, (uint8_t*)&c, 1, 10);
    printf("%c", c);
}

static void debug_uart_output_str(const char* str)
{
    // if (str) {
    //     while (*str) {
    //         HAL_UART_Transmit(&H_UART, (uint8_t*)str, 1, 10);
    //         str++;
    //     }
    // }

    if (str) {
        printf("%s", str);
    }
}


static void debug_lock(void)
{
    if (g_debug_ctx.mutex) {
        osMutexAcquire(g_debug_ctx.mutex, osWaitForever);
    }
}

static void debug_unlock(void)
{
    if (g_debug_ctx.mutex) {
        osMutexRelease(g_debug_ctx.mutex);
    }
}

static void debug_log_lock(void)
{
    if (g_debug_ctx.log_mutex) {
        osMutexAcquire(g_debug_ctx.log_mutex, osWaitForever);
    }
}

static void debug_log_unlock(void)
{
    if (g_debug_ctx.log_mutex) {
        osMutexRelease(g_debug_ctx.log_mutex);
    }
}


/* ==================== Utility Functions ==================== */

const char* log_level_to_string(log_level_e level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_WARN:    return "WARN";
        case LOG_LEVEL_ERROR:   return "ERROR";
        case LOG_LEVEL_FATAL:   return "FATAL";
        case LOG_LEVEL_SIMPLE:  return "SIMPLE";
        default:                return "UNKNOWN";
    }
}

/* ==================== File Operations for generic_log ==================== */

static void* debug_log_fopen(const char *filename, const char *mode)
{
    // Use storage system for file operations
    return flash_lfs_fopen(filename, mode);
}

static int debug_log_fclose(void *handle)
{
    return flash_lfs_fclose((lfs_file_t*)handle);
}

static int debug_log_remove(const char *filename)
{
    return flash_lfs_remove(filename);
}

static int debug_log_rename(const char *oldname, const char *newname)
{
    return flash_lfs_rename(oldname, newname);
}

static long debug_log_ftell(void *handle)
{
    // LittleFS doesn't have ftell, return current position
    return (long)flash_lfs_ftell((lfs_file_t*)handle);
}

static int debug_log_fseek(void *handle, long offset, int whence)
{
    return flash_lfs_fseek((lfs_file_t*)handle, offset, whence);
}

static int debug_log_fflush(void *handle)
{
    return flash_lfs_fflush((lfs_file_t*)handle);
}

static int debug_log_fwrite(void *handle, const void *buf, size_t size)
{
    return flash_lfs_fwrite((lfs_file_t*)handle, buf, size);
}

static int debug_log_fstat(const char *filename, struct stat *st)
{
    return flash_lfs_stat(filename, st);
}

/* Free-space probe for the file-log circuit breaker. aicam.log lives on the
 * internal-flash LittleFS.
 *
 * We intentionally DON'T call storage_get_disk_info() here — that runs
 * lfs_fs_size(), a full-FS traverse whose cost grows linearly with file count.
 * With thousands of capture records, a single call takes 3-4 seconds and
 * trips the watchdog. Instead, return a generous value so the log layer
 * attempts the write; the circuit breaker in log_message() disables file
 * output on the first failed write (LFS_ERR_NOSPC). This trades a one-line
 * retry on a full FS for O(1) boot time. */
static int debug_log_get_free_bytes(uint64_t *out_free)
{
    storage_disk_info_t info;
    memset(&info, 0, sizeof(info));
    /* Only check the mounted flag (RAM, no I/O) — skip the free-space traverse. */
    info.mounted = storage_is_lfs_mounted();
    if (!info.mounted) {
        return -1;  /* FS not ready — log layer will retry, not disable */
    }
    *out_free = 64ULL * 1024 * 1024;  /* report plenty — rely on write-failure breaker */
    return 0;
}

static uint64_t debug_log_get_time(void)
{
    // Get system time in milliseconds
    return (uint64_t)rtc_get_local_timestamp();
}

static void debug_uart_log_output(const char *msg, int len)
{
    // Output log message to UART
    // HAL_UART_Transmit(&H_UART, (uint8_t*)msg, len, 200);
    printf("%.*s", len, msg);
}

/* ---- Async log writer implementation ---- *
 * Enqueue runs under log_mutex (single producer). Drain is exclusive via
 * g_log_flush_mtx (single consumer). Both use __DMB() to order ring indices. */

static void debug_log_enqueue(const char *line, int len)
{
    if (!line || len <= 0) return;
    if (len > LOG_LINE_MAX) len = LOG_LINE_MAX;   /* truncate over-long lines */

    uint16_t next_wr = (uint16_t)((g_log_ring_wr + 1) % LOG_QUEUE_SLOTS);
    if (next_wr == g_log_ring_rd) {
        /* Ring full: drop the incoming (newest) line. Lost history is preferred
         * to blocking the caller. A dropped-count summary is emitted by the
         * drainer on its next pass. */
        g_log_dropped++;
        return;
    }
    memcpy(g_log_ring[g_log_ring_wr], line, (size_t)len);
    g_log_ring_len[g_log_ring_wr] = len;
    __DMB();
    g_log_ring_wr = next_wr;

    /* Binary "data available" event: release is a no-op if already signaled
     * (osErrorResource), which we intentionally ignore — the drainer will
     * sweep all pending slots on wake. */
    if (g_log_sem) (void)osSemaphoreRelease(g_log_sem);
}

static void debug_log_drain_ring(void)
{
    if (!g_log_flush_mtx) return;
    osMutexAcquire(g_log_flush_mtx, osWaitForever);

    for (;;) {
        if (g_log_ring_rd == g_log_ring_wr) break;   /* empty */
        __DMB();
        int len = g_log_ring_len[g_log_ring_rd];
        if (len > 0 && len <= LOG_LINE_MAX) {
            log_file_flush_line(g_log_ring[g_log_ring_rd], len);
        }
        __DMB();
        g_log_ring_rd = (uint16_t)((g_log_ring_rd + 1) % LOG_QUEUE_SLOTS);
    }

    /* If lines were dropped, write a single summary line so the loss is
     * visible in the log file. */
    uint32_t dropped = g_log_dropped;
    if (dropped > 0) {
        g_log_dropped = 0;
        char sum[LOG_LINE_MAX];
        int n = snprintf(sum, sizeof(sum),
            "[LOG] %lu log line(s) dropped (queue full)\r\n",
            (unsigned long)dropped);
        if (n > 0) log_file_flush_line(sum, n);
    }

    osMutexRelease(g_log_flush_mtx);
}

static void debug_log_writer_task(void *argument)
{
    (void)argument;
    for (;;) {
        /* Block until a producer signals "data available". */
        if (g_log_sem && osSemaphoreAcquire(g_log_sem, osWaitForever) == osOK) {
            debug_log_drain_ring();
        } else {
            /* Fallback: poll periodically in case a signal was coalesced. */
            osDelay(100);
            debug_log_drain_ring();
        }
    }
}


/* ==================== Driver Command Registration System ==================== */

#define MAX_DRIVER_CMD_CALLBACKS 32

typedef void (*driver_cmd_register_func_t)(void);

typedef struct {
    char name[16];
    driver_cmd_register_func_t register_func;
} driver_cmd_callback_t;

static driver_cmd_callback_t g_driver_cmd_callbacks[MAX_DRIVER_CMD_CALLBACKS];
static int g_driver_cmd_callback_count = 0;

/**
 * @brief Register a driver command registration function
 */
int driver_cmd_register_callback(const char* name, driver_cmd_register_func_t register_func)
{
    if (!name || !register_func) {
        return -1;
    }
    
    if (g_driver_cmd_callback_count >= MAX_DRIVER_CMD_CALLBACKS) {
        LOG_CORE_WARN("Too many driver command callbacks, ignoring %s", name);
            return -1;
    }

    for (int i = 0; i < g_driver_cmd_callback_count; i++) {
        if (strcmp(g_driver_cmd_callbacks[i].name, name) == 0) {
            LOG_CORE_WARN("Driver command callback %s already registered, ignoring", name);
            return -1;
        }
    }
    
    strncpy(g_driver_cmd_callbacks[g_driver_cmd_callback_count].name, name, sizeof(g_driver_cmd_callbacks[0].name) - 1);
    g_driver_cmd_callbacks[g_driver_cmd_callback_count].name[sizeof(g_driver_cmd_callbacks[0].name) - 1] = '\0';
    g_driver_cmd_callbacks[g_driver_cmd_callback_count].register_func = register_func;
    g_driver_cmd_callback_count++;
    
    LOG_CORE_DEBUG("Registered driver command callback: %s", name);
    return 0;
}

/**
 * @brief Execute all registered driver command registration callbacks
 */
static void driver_cmd_register_all(void)
{
    LOG_CORE_INFO("Registering driver commands from %d modules:", g_driver_cmd_callback_count);
    
    for (int i = 0; i < g_driver_cmd_callback_count; i++) {
        LOG_CORE_INFO("  - %s", g_driver_cmd_callbacks[i].name);
        if (g_driver_cmd_callbacks[i].register_func) {
            g_driver_cmd_callbacks[i].register_func();
        }
    }
    
    LOG_CORE_INFO("Driver command registration completed");
}


void debug_cmdline_register(debug_cmd_reg_t *cmd_table, int n)
{
    debug_register_commands(cmd_table, n);
}

void debug_uart_init(void)
{
    // UART initialization is handled by HAL
}

void debug_output_register(log_custom_output_func_t func)
{
    debug_register_log_output(func);
}

void debug_cmdline_input(char c)
{
    debug_process_char(c);
}
