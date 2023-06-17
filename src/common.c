/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* replace_extension(const char* filename, const char* extension) {
  size_t length = strlen(filename) + strlen(extension) + 1; /* +1 for \0. */
  char* result = xmalloc(length);
  char* last_dot = strrchr(filename, '.');
  if (last_dot == NULL) {
    snprintf(result, length, "%s%s", filename, extension);
  } else {
    snprintf(result, length, "%.*s%s", (int)(last_dot - filename), filename,
             extension);
  }
  return result;
}

static Result get_file_size(FILE* f, long* out_size) {
  CHECK_MSG(fseek(f, 0, SEEK_END) >= 0, "fseek to end failed.\n");
  long size = ftell(f);
  CHECK_MSG(size >= 0, "ftell failed.");
  CHECK_MSG(fseek(f, 0, SEEK_SET) >= 0, "fseek to beginning failed.\n");
  *out_size = size;
  return OK;
  ON_ERROR_RETURN;
}

Result file_read(const char* filename, FileData* out_file_data) {
  return file_read_aligned(filename, 1, out_file_data);
}

Result file_read_aligned(const char* filename, size_t align,
                         FileData* out_file_data) {
  FILE* f = fopen(filename, "rb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  long size;
  CHECK(SUCCESS(get_file_size(f, &size)));
  long aligned_size = ALIGN_UP(size, align);
  u8* data = xcalloc(1, aligned_size);
  CHECK_MSG(data, "allocation failed.\n");
  CHECK_MSG(fread(data, size, 1, f) == 1, "fread failed.\n");
  fclose(f);
  out_file_data->data = data;
  out_file_data->size = aligned_size;
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

Result file_write(const char* filename, const FileData* file_data) {
  FILE* f = fopen(filename, "wb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fwrite(file_data->data, file_data->size, 1, f) == 1,
            "fwrite failed.\n");
  fclose(f);
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

void file_data_delete(FileData* file_data) {
  xfree(file_data->data);
  file_data->size = 0;
  file_data->data = NULL;
}
