/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "rewind.h"

#include <assert.h>
#include <stdlib.h>

#include "emulator.h"

#define INVALID_CYCLES (~0ULL)

#define GET_CYCLES(x) ((x).cycles)
#define CMP_GT(x, y) ((x) > (y))

#define CHECK_WRITE(count, dst, dst_max_end) \
  do {                                       \
    if ((dst) + (count) > (dst_max_end)) {   \
      return NULL;                           \
    }                                        \
  } while (0)

/* RLE encoded as follows:
 * - non-runs are written directly
 * - runs are written with the first two bytes of the run , followed by the
 *   number of subsequent bytes in the run (i.e. count - 2) encoded as a
 *   varint. */
#define ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end) \
  do {                                                                  \
    u8* dst = dst_begin;                                                \
    assert(src_size > 0);                                               \
    const u8* src_end = src + src_size;                                 \
    /* Always write the first byte. */                                  \
    u8 last = READ();                                                   \
    CHECK_WRITE(1, dst, dst_max_end);                                   \
    *dst++ = last;                                                      \
    while (src < src_end) {                                             \
      u8 next = READ();                                                 \
      if (next == last) {                                               \
        u32 count = 0;                                                  \
        while (src < src_end) {                                         \
          next = READ();                                                \
          if (next != last) {                                           \
            break;                                                      \
          }                                                             \
          count++;                                                      \
        }                                                               \
        CHECK_WRITE(1, dst, dst_max_end);                               \
        *dst++ = last;                                                  \
        dst = write_varint(count, dst, dst_max_end);                    \
        if (!dst) {                                                     \
          return NULL;                                                  \
        }                                                               \
        if (src == src_end) {                                           \
          break;                                                        \
        }                                                               \
      }                                                                 \
      CHECK_WRITE(1, dst, dst_max_end);                                 \
      *dst++ = next;                                                    \
      last = next;                                                      \
    }                                                                   \
    dst_new_end = dst;                                                  \
  } while (0)

#define DECODE_RLE(WRITE, src, src_size)   \
  do {                                     \
    assert(src_size > 0);                  \
    const u8* src_end = src + src_size;    \
    u8 last = *src++;                      \
    WRITE(last);                           \
    while (src < src_end) {                \
      u8 next = *src++;                    \
      if (next == last) {                  \
        u32 count = read_varint(&src) + 1; \
        for (; count > 0; count--) {       \
          WRITE(last);                     \
        }                                  \
      } else {                             \
        WRITE(next);                       \
        last = next;                       \
      }                                    \
    }                                      \
  } while (0)

RewindBuffer* rewind_new(const RewindInit* init, struct Emulator* e) {
  size_t capacity = init->buffer_capacity;
  if (capacity == 0) {
    /* TODO(binji): Probably shouldn't do this anymore. */
    return NULL;
  }

  RewindBuffer* buffer = malloc(sizeof(RewindBuffer));
  ZERO_MEMORY(*buffer);
  buffer->init = *init;

  u8* data = malloc(capacity);
  emulator_init_state_file_data(&buffer->last_state);
  emulator_init_state_file_data(&buffer->last_base_state);
  emulator_init_state_file_data(&buffer->rewind_diff_state);
  buffer->last_base_state_cycles = INVALID_CYCLES;
  buffer->data_range[0].begin = buffer->data_range[0].end = data;
  buffer->data_range[1] = buffer->data_range[0];
  RewindInfo* info = (RewindInfo*)(data + capacity);
  buffer->info_range[0].begin = buffer->info_range[0].end = info;
  buffer->info_range[1] = buffer->info_range[0];
  buffer->frames_until_next_base = 0;

  rewind_append(buffer, e);

  return buffer;
}

void rewind_delete(RewindBuffer* buffer) {
  free(buffer->data_range[0].begin);
  free(buffer);
}

static u8* write_varint(u32 value, u8* dst_begin, u8* dst_max_end) {
  u8* dst = dst_begin;
  if (value < 0x80) {
    CHECK_WRITE(1, dst, dst_max_end);
    *dst++ = (u8)value;
  } else if (value < 0x4000) {
    CHECK_WRITE(2, dst, dst_max_end);
    *dst++ = 0x80 | (value & 0x7f);
    *dst++ = (value >> 7) & 0x7f;
  } else {
    /* If this fires there is a run of 128K. In the current EmulatorState this
     * is impossible. */
    assert(value < 0x20000);
    CHECK_WRITE(3, dst, dst_max_end);
    *dst++ = 0x80 | (value & 0x7f);
    *dst++ = 0x80 | ((value >> 7) & 0x7f);
    *dst++ = (value >> 14) & 0x7f;
  }
  return dst;
}

static u32 read_varint(const u8** src) {
  const u8* s = *src;
  u32 result = 0;
  if ((s[0] & 0x80) == 0) {
    *src += 1;
    return s[0];
  } else if ((s[1] & 0x80) == 0) {
    *src += 2;
    return (s[1] << 7) | (s[0] & 0x7f);
  } else {
    assert((s[2] & 0x80) == 0);
    *src += 3;
    return (s[2] << 14) | ((s[1] & 0x7f) << 7) | (s[0] & 0x7f);
  }
}

static u8* encode_rle(const u8* src, size_t src_size, u8* dst_begin,
                      u8* dst_max_end) {
  u8* dst_new_end;
#define READ() (*src++)
  ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end);
#undef READ
  return dst_new_end;
}

static void decode_rle(const u8* src, size_t src_size, u8* dst, u8* dst_end) {
#define WRITE(x) *dst++ = (x)
  DECODE_RLE(WRITE, src, src_size);
#undef WRITE
  assert(dst == dst_end);
}

static u8* encode_diff(const u8* src, const u8* base, size_t src_size,
                       u8* dst_begin, u8* dst_max_end) {
  u8* dst_new_end;
#define READ() (*src++ - *base++)
  ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end);
#undef READ
  return dst_new_end;
}

static void decode_diff(const u8* src, size_t src_size, const u8* base, u8* dst,
                        u8* dst_end) {
#define WRITE(x) *dst++ = (*base++ + (x))
  DECODE_RLE(WRITE, src, src_size);
#undef WRITE
  assert(dst == dst_end);
}

static RewindInfo* find_first_base_in_range(RewindInfoRange range) {
  RewindInfo* base = range.begin;
  for (; base < range.end; base++) {
    if (base->kind == RewindInfoKind_Base) {
      return base;
    }
  }
  return NULL;
}

void rewind_append(RewindBuffer* buf, struct Emulator* e) {
  Cycles cycles = emulator_get_cycles(e);
  (void)emulator_write_state(e, &buf->last_state);

  /* The new state must be written in sorted order; if it is out of order (from
   * a rewind), then the subsequent saved states should have been cleared
   * first. */
  assert(rewind_get_newest_cycles(buf) == INVALID_CYCLES ||
         cycles > rewind_get_oldest_cycles(buf));

  RewindInfoKind kind;
  if (buf->frames_until_next_base-- == 0) {
    kind = RewindInfoKind_Base;
    buf->frames_until_next_base = buf->init.frames_per_base_state;
  } else {
    kind = RewindInfoKind_Diff;
  }

  RewindDataRange* data_range = buf->data_range;
  RewindInfoRange* info_range = buf->info_range;
  RewindInfo* new_info = --info_range[0].begin;
  if ((u8*)new_info <= data_range[1].end) {
  wrap:
    /* Need to wrap, roll back decrement and swap ranges. */
    info_range[0].begin++;
    info_range[1] = info_range[0];
    info_range[0].begin = info_range[0].end;
    data_range[1] = data_range[0];
    data_range[0].end = data_range[0].begin;

    new_info = --info_range[0].begin;
    assert((u8*)new_info > data_range[1].end);
  }

  u8* data_begin = data_range[0].end;
  u8* data_end_max = (u8*)MIN(info_range[1].begin, new_info);
  u8* data_end;
  switch (kind) {
    case RewindInfoKind_Diff:
      if (buf->last_base_state_cycles != INVALID_CYCLES) {
        data_end = encode_diff(buf->last_state.data, buf->last_base_state.data,
                               buf->last_state.size, data_begin, data_end_max);
        break;
      }
      /* There is no previous base state, so we can't diff. Fallthrough to
       * writing a base state. */

    case RewindInfoKind_Base:
      kind = RewindInfoKind_Base;
      data_end = encode_rle(buf->last_state.data, buf->last_state.size,
                            data_begin, data_end_max);
      memcpy(buf->last_base_state.data, buf->last_state.data,
             buf->last_state.size);
      buf->last_base_state_cycles = cycles;
      break;
  }

  if (data_end == NULL) {
    /* Failed to write, need to wrap. */
    goto wrap;
  }

  assert(data_end <= data_end_max);
  data_range[0].end = data_end;

  /* Check to see how many data chunks we overwrote. */
  RewindInfo* new_end = info_range[1].end;
  while (info_range[1].begin < new_end && new_end[-1].data < data_end) {
    --new_end;
  }

  new_end = MIN(new_end, new_info);

  info_range[1].end = new_end;
  info_range[1].begin = MIN(info_range[1].begin, info_range[1].end);

  new_info->cycles = cycles;
  new_info->data = data_begin;
  new_info->size = data_end - data_begin;
  new_info->kind = kind;

  if (info_range[1].begin < info_range[1].end) {
    data_range[1].begin = info_range[1].end[-1].data;
    data_range[1].end = info_range[1].begin->data + info_range[1].begin->size;
  } else {
    data_range[1].begin = data_range[1].end =
        info_range[0].begin->data + info_range[0].begin->size;
  }

  /* Update stats. */
  buf->total_kind_bytes[kind] += new_info->size;
  buf->total_uncompressed_bytes += buf->last_state.size;
}

Result rewind_to_cycles(RewindBuffer* buf, Cycles cycles,
                        RewindResult* out_result) {
  RewindInfoRange* info_range = buf->info_range;

  int info_range_index;
  if (cycles >= info_range[0].end[-1].cycles) {
    info_range_index = 0;
  } else if (cycles >= info_range[1].end[-1].cycles) {
    info_range_index = 1;
  } else {
    return ERROR;
  }

  RewindInfo* begin = info_range[info_range_index].begin;
  RewindInfo* end = info_range[info_range_index].end;
  LOWER_BOUND(RewindInfo, found, begin, end, cycles, GET_CYCLES, CMP_GT);
  assert(found);
  assert(found >= begin && found < end);

  /* We actually want upper bound, so increment if it wasn't an exact match. */
  if (found->cycles != cycles) {
    assert(found + 1 != end);
    ++found;
    // HACK: Rewind one more, if available -- this way we'll render frames when
    // rewinding.
    if (found + 1 < end) {
      ++found;
    }
  }

  assert(found->cycles <= cycles);

  FileData* file_data = NULL;
  if (found->kind == RewindInfoKind_Base) {
    file_data = &buf->last_base_state;
    decode_rle(found->data, found->size, file_data->data,
               file_data->data + file_data->size);
    buf->last_base_state_cycles = found->cycles;
  } else {
    assert(found->kind == RewindInfoKind_Diff);
    /* Find the previous base state. */
    RewindInfoRange range = {found, end};
    RewindInfo* base_info = find_first_base_in_range(range);
    if (!base_info) {
      if (info_range_index == 1) {
        /* No previous base state, can't decode. */
        return ERROR;
      }

      /* Search the previous range. */
      base_info = find_first_base_in_range(info_range[1]);
      if (!base_info) {
        return ERROR;
      }
    }

    FileData* base = &buf->last_base_state;
    decode_rle(base_info->data, base_info->size, base->data,
               base->data + base->size);
    buf->last_base_state_cycles = base_info->cycles;

    file_data = &buf->rewind_diff_state;
    decode_diff(found->data, found->size, base->data, file_data->data,
                file_data->data + file_data->size);
  }

  out_result->info_range_index = info_range_index;
  out_result->info = found;
  out_result->file_data = *file_data;
  return OK;
}

void rewind_truncate_to(RewindBuffer* buffer, RewindResult* result) {
  /* Remove data from rewind buffer that are now invalid. */
  int info_range_index = result->info_range_index;
  RewindInfo* info = result->info;
  RewindDataRange* data_range = buffer->data_range;
  RewindInfoRange* info_range = buffer->info_range;
  info_range[info_range_index].begin = info;
  data_range[info_range_index].end = info->data + info->size;
  if (info_range_index == 1) {
    info_range[0].begin = info_range[0].end;
    data_range[0].end = data_range[0].begin;
  }
}

static Bool is_rewind_range_empty(RewindInfoRange* r) {
  return r->end == r->begin;
}

Cycles rewind_get_oldest_cycles(RewindBuffer* buffer) {
  RewindInfoRange* info_range = buffer->info_range;
  /* info_range[1] is always older than info_range[0], if it exists, so check
   * that first. */
  int i;
  for (i = 1; i >= 0; --i) {
    if (!is_rewind_range_empty(&info_range[i])) {
      /* end is exclusive. */
      return info_range[i].end[-1].cycles;
    }
  }

  return INVALID_CYCLES;
}

Cycles rewind_get_newest_cycles(RewindBuffer* buffer) {
  RewindInfoRange* info_range = buffer->info_range;

  int i;
  for (i = 0; i < 2; ++i) {
    if (!is_rewind_range_empty(&info_range[i])) {
      return info_range[i].begin[0].cycles;
    }
  }

  return INVALID_CYCLES;
}

RewindStats rewind_get_stats(RewindBuffer* buffer) {
  RewindStats stats;
  stats.base_bytes = buffer->total_kind_bytes[RewindInfoKind_Base];
  stats.diff_bytes = buffer->total_kind_bytes[RewindInfoKind_Diff];
  stats.uncompressed_bytes = buffer->total_uncompressed_bytes;
  stats.used_bytes = 0;
  stats.capacity_bytes = buffer->init.buffer_capacity;

  u8* begin = buffer->data_range[0].begin;
  int i;
  for (i = 0; i < 2; ++i) {
    RewindDataRange* data_range = &buffer->data_range[i];
    RewindInfoRange* info_range = &buffer->info_range[i];
    stats.used_bytes += data_range->end - data_range->begin;
    stats.used_bytes +=
        (info_range->end - info_range->begin) * sizeof(RewindInfo);

    stats.data_ranges[i*2+0] = data_range->begin - begin;
    stats.data_ranges[i*2+1] = data_range->end - begin;
    stats.info_ranges[i*2+0] = (u8*)info_range->begin - begin;
    stats.info_ranges[i*2+1] = (u8*)info_range->end - begin;
  }

  return stats;
}
