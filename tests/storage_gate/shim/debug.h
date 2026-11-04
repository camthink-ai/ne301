#ifndef SHIM_DEBUG_H
#define SHIM_DEBUG_H
#include <stdio.h>
#define LOG_CORE_FATAL(...) ((void)0)
#define LOG_CORE_ERROR(...) do { fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } while (0)
#define LOG_CORE_WARN(...)  ((void)0)
#define LOG_CORE_INFO(...)  ((void)0)
#define LOG_CORE_DEBUG(...) ((void)0)
#define LOG_CORE_SIMPLE(...) ((void)0)
#define LOG_SVC_FATAL(...)  ((void)0)
#define LOG_SVC_ERROR(...)  do { fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } while (0)
#define LOG_SVC_WARN(...)   ((void)0)
#define LOG_SVC_INFO(...)   ((void)0)
#define LOG_SVC_DEBUG(...)  ((void)0)
#define LOG_DRV_FATAL(...)  ((void)0)
#define LOG_DRV_ERROR(...)  ((void)0)
#define LOG_DRV_WARN(...)   ((void)0)
#define LOG_DRV_INFO(...)   ((void)0)
#define LOG_DRV_DEBUG(...)  ((void)0)
#define LOG_SIMPLE(...)     ((void)0)
#define LOG_USER(...)       ((void)0)
#endif
