#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "queue.h"
#include "task.h"
#include "cmsis_os.h"
#include "wic_log.h"

#if defined(WAKECORE_VERSION)
#define APP_VERSION             WAKECORE_VERSION
#else
#define APP_VERSION             "0.2.7.115"  
#endif

/// @brief Error code
typedef enum {
    SYS_OK = 0,
    SYS_ERR_INVALID_ARG = -0XFF,
    SYS_ERR_INVALID_STATE,
    SYS_ERR_INVALID_SIZE,
    SYS_ERR_INVALID_FMT,
    SYS_ERR_NO_MEM,
    SYS_ERR_NOT_FOUND,
    SYS_ERR_NOT_SUPPORTED,
    SYS_ERR_NOT_FINISHED,
    SYS_ERR_TIMEOUT,
    SYS_ERR_CHECK,
    SYS_ERR_RESULT,
    SYS_ERR_MUTEX,
    SYS_ERR_HAL,
    SYS_ERR_FAILED,
    SYS_ERR_UNKNOW,
} sys_err_t;

#ifdef __cplusplus
}
#endif
#endif /* __SYS_CONFIG_H */
