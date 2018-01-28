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
  Cycles cycles;
  u8 buttons;
  u8 padding[3];
} JoypadState;

typedef struct JoypadBuffer {
  JoypadState* data;
  size_t size;
  size_t capacity;
  struct JoypadBuffer *next, *prev;
} JoypadBuffer;

typedef struct {
  JoypadBuffer* buffer;
  JoypadState* state;
} JoypadStateIter;

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_JOYPAD_H_ */
