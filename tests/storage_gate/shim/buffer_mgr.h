#ifndef SHIM_BUFFER_MGR_H
#define SHIM_BUFFER_MGR_H
#include <stddef.h>
void *buffer_calloc(size_t count, size_t size);
void buffer_free(void *ptr);
#endif
