/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "memory.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if TRACE_MEMORY

void* xmalloc_(const char* file, int line, size_t size) {
  void* p = malloc(size);
  printf("%s:%d: %s(%" PRIu64 ") => %p\n", file, line, __func__, (uint64_t)size,
         p);
  return p;
}

void xfree_(const char* file, int line, void* p) {
  printf("%s:%d: %s(%p)\n", file, line, __func__, p);
  free(p);
}

void* xcalloc_(const char* file, int line, size_t count, size_t size) {
  void* p = calloc(count, size);
  printf("%s:%d: %s(%" PRIu64 ", %" PRIu64 ") => %p\n", file, line, __func__,
         (uint64_t)count, (uint64_t)size, p);
  return p;
}

#endif
