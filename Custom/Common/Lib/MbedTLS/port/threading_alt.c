#include "common.h"

#if defined(MBEDTLS_THREADING_C)
#if defined(MBEDTLS_THREADING_ALT)
#include "mbedtls/threading.h"
#include "mbedtls/error.h"
#include "threading_alt.h"

void mbedtls_alt_mutex_init(mbedtls_threading_mutex_t * mutex)
{
    if (mutex == NULL || mutex->mutex != NULL) {
        return;
    }
    mutex->mutex = osMutexNew(NULL);
}

void mbedtls_alt_mutex_free(mbedtls_threading_mutex_t * mutex)
{
    if (mutex->mutex != NULL) {
        osMutexDelete(mutex->mutex);
        mutex->mutex = NULL;
    }
}

int mbedtls_alt_mutex_lock(mbedtls_threading_mutex_t * mutex)
{
    int ret = MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;

    if (mutex->mutex != NULL) {
        if (osMutexAcquire(mutex->mutex, osWaitForever) == osOK) {
            ret = 0;
        } else {
            ret = MBEDTLS_ERR_THREADING_MUTEX_ERROR;
        }
    }
    return ret;
}

int mbedtls_alt_mutex_unlock(mbedtls_threading_mutex_t * mutex)
{
    int ret = MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;

    if (mutex->mutex != NULL) {
        if (osMutexRelease(mutex->mutex) == osOK) {
            ret = 0;
        } else {
            ret = MBEDTLS_ERR_THREADING_MUTEX_ERROR;
        }
    }
    return ret;
}

static unsigned mbedtls_threading_alt_inited;

void mbedtls_threading_alt_init(void)
{
    if (mbedtls_threading_alt_inited != 0u) {
        return;
    }

    mbedtls_threading_set_alt(mbedtls_alt_mutex_init,
                               mbedtls_alt_mutex_free,
                               mbedtls_alt_mutex_lock,
                               mbedtls_alt_mutex_unlock);
    mbedtls_threading_alt_inited = 1u;
}

void mbedtls_threading_alt_deinit(void)
{
    mbedtls_threading_free_alt();
}

#endif /* MBEDTLS_THREADING_ALT */
#endif /* MBEDTLS_THREADING_C */
