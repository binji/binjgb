/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_MEMORY_H_
#define BINJGB_MEMORY_H_

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRACE_MEMORY 0

#if TRACE_MEMORY

#define xmalloc(size) xmalloc_(__FILE__, __LINE__, size)
#define xrealloc(size) xrealloc_(__FILE__, __LINE__, p, size)
#define xfree(p) xfree_(__FILE__, __LINE__, p)
#define xcalloc(count, size) xcalloc_(__FILE__, __LINE__, count, size)
#define xstrdup(s) xstrdup_(__FILE__, __LINE__, s)

/* Use these instead to make it easier to track memory usage. */
void* xmalloc_(const char* file, int line, size_t);
void* xrealloc_(const char* file, int line, void*, size_t);
void xfree_(const char* file, int line, void*);
void* xcalloc_(const char* file, int line, size_t, size_t);
char* xstrdup_(const char* file, int line, const char*);

#else

#define xmalloc malloc
#define xrealloc realloc
#define xfree free
#define xcalloc calloc
#define xstrdup strdup

#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*  BINJGB_MEMORY_H_ */
