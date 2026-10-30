#include "buffer_mgr.h"
#include <stdlib.h>

void *buffer_calloc(size_t count, size_t size)
{
    void *memory = calloc(count, size);
    if (memory == NULL) {
        return NULL;
    }

    return memory;
}

void buffer_free(void *ptr)
{
    if (ptr != NULL) {
        free(ptr);
    }
}
