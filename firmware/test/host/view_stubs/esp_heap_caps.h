#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#ifdef HOST_HEAP_FAIL
int host_heap_fail(size_t n);
#else
static inline int host_heap_fail(size_t n) { (void)n; return 0; }
#endif
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return host_heap_fail(n) ? NULL : malloc(n); }
static inline void *heap_caps_calloc(size_t k, size_t n, unsigned caps) { (void)caps; return host_heap_fail(k * n) ? NULL : calloc(k, n); }
static inline void heap_caps_free(void *p) { free(p); }
