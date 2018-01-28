/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_REWIND_H_
#define BINJGB_REWIND_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RewindInfoKind_Base,
  RewindInfoKind_Diff,
} RewindInfoKind;

typedef struct {
  Cycles cycles;
  u8* data;
  size_t size;
  RewindInfoKind kind;
} RewindInfo;

typedef struct {
  RewindInfo* begin; /* begin <= end; if begin == end range is empty. */
  RewindInfo* end;   /* end is exclusive. */
} RewindInfoRange;

typedef struct {
  u8* begin;
  u8* end;
} RewindDataRange;

typedef struct RewindBuffer {
 /*
  * |                  rewind buffer                      |
  * |                                                     |
  * | dr[0] | ... | dr[1] | ....... | ir[1] | ... | ir[0] |
  *
  * (dr == data_range, ir == info_range)
  *
  * All RewindInfo in ir[0] has a corresponding data range in dr[0]. Similarly,
  * all RewindInfo in ir[1] has a corresponding data range in dr[1].
  *
  * When new data is written, ir[0].begin moves left, which my overwrite old
  * data in ir[1]. The data is written after dr[0].end. If the newly written
  * data overlaps the beginning of dr[1], the associated RewindInfo in ir[1] is
  * removed.
  *
  * When ir[1].begin and dr[1].end cross, the buffer is filled. At this point,
  * dr[0] is moved to dr[1], and ir[0] is moved to ir[1]. (Just the pointers
  * move, not the actual data). Then dr[0] is reset to an empty data range and
  * ir[0] is reset to an empty info range.
  *
  * TODO(binji):: you can always recalculate the data ranges from the
  * information in the info ranges. Remove?
  *
  */
  RewindDataRange data_range[2];
  RewindInfoRange info_range[2];
  FileData last_state;
  FileData last_base_state;
  Cycles last_base_state_cycles;
  int frames_until_next_base;

  /* Data is decompressed into these states when rewinding. */
  FileData rewind_diff_state;

  /* Stats */
  size_t total_kind_bytes[2];
  size_t total_uncompressed_bytes;
} RewindBuffer;

typedef struct {
  size_t info_range_index;
  RewindInfo* info;
  JoypadStateIter joypad_iter;
  Bool rewinding;
} RewindState;

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_REWIND_H_ */
