/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_JOYPAD_H_
#define BINJGB_JOYPAD_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  Ticks ticks;
  u8 buttons;
  u8 padding[3];
} JoypadState;

typedef struct JoypadChunk {
  JoypadState* data;
  size_t size;
  size_t capacity;
  struct JoypadChunk *next, *prev;
} JoypadChunk;

typedef struct {
  JoypadChunk* chunk;
  JoypadState* state;
} JoypadStateIter;

typedef struct {
  JoypadChunk sentinel;
  JoypadButtons last_buttons;
} JoypadBuffer;

typedef struct {
  size_t used_bytes;
  size_t capacity_bytes;
} JoypadStats;

JoypadBuffer* joypad_new(void);
void joypad_delete(JoypadBuffer*);
void joypad_append(JoypadBuffer*, JoypadButtons*, Ticks);
void joypad_append_if_new(JoypadBuffer*, JoypadButtons*, Ticks);
JoypadStateIter joypad_find_state(JoypadBuffer*, Ticks);
void joypad_truncate_to(JoypadBuffer*, JoypadStateIter);
JoypadStateIter joypad_get_next_state(JoypadStateIter);
u8 joypad_pack_buttons(JoypadButtons*);
JoypadButtons joypad_unpack_buttons(u8);
JoypadStats joypad_get_stats(JoypadBuffer*);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_JOYPAD_H_ */
