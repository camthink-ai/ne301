#ifndef MEM_GUARD_STUB_DEBUG_H
#define MEM_GUARD_STUB_DEBUG_H
#include <stdio.h>
typedef int (*cmd_handler)(int argc, char *argv[]);
typedef struct {
    const char *name;
    const char *help;
    cmd_handler handler;
} debug_cmd_reg_t;
typedef void (*driver_cmd_register_func_t)(void);
#define LOG_CORE_FATAL(...) ((void)0)
#define LOG_CORE_ERROR(...) ((void)0)
#define LOG_CORE_WARN(...)  ((void)0)
#define LOG_CORE_INFO(...)  ((void)0)
#define LOG_CORE_DEBUG(...) ((void)0)
#define LOG_CORE_SIMPLE(...) ((void)0)
#define LOG_SVC_FATAL(...)  ((void)0)
#define LOG_SVC_ERROR(...)  ((void)0)
#define LOG_SVC_WARN(...)   ((void)0)
#define LOG_SVC_INFO(...)   ((void)0)
#define LOG_SVC_DEBUG(...)  ((void)0)
#define LOG_DRV_FATAL(...)  ((void)0)
void mem_guard_record_diag(const char *line);
#define LOG_DRV_ERROR(...) do { char m_[256]; snprintf(m_, sizeof(m_), __VA_ARGS__); \
    mem_guard_record_diag(m_); } while (0)
#define LOG_DRV_WARN(...)   ((void)0)
#define LOG_DRV_INFO(...)   ((void)0)
#define LOG_DRV_DEBUG(...)  ((void)0)
#define LOG_SIMPLE(...)     ((void)0)
#define LOG_USER(...)       ((void)0)
void debug_cmdline_register(debug_cmd_reg_t *cmd_table, int n);
int driver_cmd_register_callback(const char *name, driver_cmd_register_func_t register_func);
#endif
