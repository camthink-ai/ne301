/* Host shim for including the REAL Custom/Services/Device/device_service.h.
 * Supplies only what device_service.h's declarations reference; storage_info_t
 * itself comes verbatim from the real header. aicam_bool_t replicates the
 * production definition (C enum -> int -> 4 bytes on host and ARM32 alike) so
 * the storage_info_t layout matches the firmware build byte-for-byte. */
#ifndef AICAM_TYPES_H
#define AICAM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AICAM_FALSE = 0,
    AICAM_TRUE = 1
} aicam_bool_t;

typedef enum {
    AICAM_OK = 0,
    AICAM_ERROR = -1,
    AICAM_ERROR_INVALID_PARAM = -2,
    AICAM_ERROR_INVALID_DATA = -3,
    AICAM_ERROR_NO_MEMORY = -4,
    AICAM_ERROR_TIMEOUT = -5,
    AICAM_ERROR_BUSY = -6,
    AICAM_ERROR_NOT_FOUND = -7,
    AICAM_ERROR_NOT_SUPPORTED = -8
} aicam_result_t;

/* Only used by pointer in device_service.h prototypes. */
typedef struct { int _opaque; } device_info_config_t;
typedef struct { int _opaque; } nn_result_t;
typedef struct { int _opaque; } jpegc_params_t;
typedef struct { int _opaque; } light_config_t;

/* Embedded by value in camera_config_t (device_service.h:78); its internal
 * layout only affects camera_config_t, never storage_info_t. */
typedef struct { int _opaque; } image_config_t;

#endif /* AICAM_TYPES_H */
