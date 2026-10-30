
#ifndef LC_ARENA_H
#define LC_ARENA_H

#include <stddef.h>
#include <stdint.h>

#define LC_ARENA_SIZE (96u * 1024u)

void  lcapp_arena_init(void);
void *lcapp_alloc(size_t sz);
void  lcapp_free(void *p);
size_t lcapp_arena_used(void);
size_t lcapp_arena_high_water(void);

#endif
