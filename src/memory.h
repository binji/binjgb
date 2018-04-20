/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_MEMORY_H_
#define BINJGB_MEMORY_H_

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRACE_MEMORY 0

#if TRACE_MEMORY

#define xmalloc(size) xmalloc_(__FILE__, __LINE__, size)
#define xfree(p) xfree_(__FILE__, __LINE__, p)
#define xcalloc(count, size) xcalloc_(__FILE__, __LINE__, count, size)

/* Use these instead to make it easier to track memory usage. */
void* xmalloc_(const char* file, int line, size_t);
void xfree_(const char* file, int line, void*);
void* xcalloc_(const char* file, int line, size_t, size_t);

#else

#define xmalloc malloc
#define xfree free
#define xcalloc calloc

#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*  BINJGB_MEMORY_H_ */
