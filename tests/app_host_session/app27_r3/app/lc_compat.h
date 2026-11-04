
#ifndef LC_COMPAT_H
#define LC_COMPAT_H

#include <stddef.h>

#ifdef LC_APP_TARGET

void *lc_memcpy(void *dst, const void *src, size_t n);
void *lc_memset(void *dst, int c, size_t n);
int   lc_memcmp(const void *a, const void *b, size_t n);
size_t lc_strlen(const char *s);
int   lc_strcmp(const char *a, const char *b);
int   lc_strncmp(const char *a, const char *b, size_t n);

size_t lc_strlcpy(char *dst, const char *src, size_t n);

#else

#include <string.h>
static inline void *lc_memcpy(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
static inline void *lc_memset(void *dst, int c, size_t n) { return memset(dst, c, n); }
static inline int   lc_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
static inline size_t lc_strlen(const char *s) { return strlen(s); }
static inline int   lc_strcmp(const char *a, const char *b) { return strcmp(a, b); }
static inline int   lc_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static inline size_t lc_strlcpy(char *dst, const char *src, size_t n)
{
    size_t l = strlen(src);
    if (n == 0) return l;
    size_t c = (l < n - 1u) ? l : n - 1u;
    memcpy(dst, src, c);
    dst[c] = '\0';
    return l;
}

#endif
#endif
