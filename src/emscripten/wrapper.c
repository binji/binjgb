/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdlib.h>

#include "emulator.h"
#include "joypad.h"
#include "rewind.h"

typedef struct {
  struct Emulator* e;
  RewindBuffer* rewind_buffer;
  JoypadBuffer* joypad_buffer;
  RewindResult rewind_result;
  JoypadStateIter current;
  JoypadStateIter next;
} RewindState;

static struct Emulator* e;

static EmulatorInit s_init;
static JoypadButtons s_buttons;

struct Emulator* emulator_new_simple(void* rom_data, size_t rom_size,
                                     int audio_frequency, int audio_frames,
                                     JoypadBuffer* joypad_buffer) {
  s_init.rom.data = rom_data;
  s_init.rom.size = rom_size;
  s_init.audio_frequency = audio_frequency;
  s_init.audio_frames = audio_frames;

  e = emulator_new(&s_init);

  return e;
}

static void default_joypad_callback(JoypadButtons* joyp, void* user_data) {
  JoypadBuffer* joypad_buffer = user_data;
  *joyp = s_buttons;
  Cycles cycles = emulator_get_cycles(e);
  joypad_append_if_new(joypad_buffer, joyp, cycles);
}

void emulator_set_default_joypad_callback(struct Emulator* e,
                                          JoypadBuffer* joypad_buffer) {
  emulator_set_joypad_callback(e, default_joypad_callback, joypad_buffer);
}

RewindBuffer* rewind_new_simple(struct Emulator* e, int frames_per_base_state,
                                size_t buffer_capacity) {
  RewindInit init;
  init.frames_per_base_state = frames_per_base_state;
  init.buffer_capacity = buffer_capacity;
  return rewind_new(&init, e);
}

RewindState* rewind_begin(struct Emulator* e, RewindBuffer* rewind_buffer,
                          JoypadBuffer* joypad_buffer) {
  RewindState* state = malloc(sizeof(RewindState));
  state->e = e;
  state->rewind_buffer = rewind_buffer;
  state->joypad_buffer = joypad_buffer;
  return state;
}

static void rewind_joypad_callback(struct JoypadButtons* joyp,
                                   void* user_data) {
  RewindState* state = user_data;
  Cycles cycles = emulator_get_cycles(state->e);
  while (state->next.state && state->next.state->cycles <= cycles) {
    state->current = state->next;
    state->next = joypad_get_next_state(state->next);
  }

  *joyp = joypad_unpack_buttons(state->current.state->buttons);
}

void emulator_set_rewind_joypad_callback(RewindState* state) {
  emulator_set_joypad_callback(e, rewind_joypad_callback, state);
}

Result rewind_to_cycles_wrapper(RewindState* state, Cycles cycles) {
  CHECK(SUCCESS(
      rewind_to_cycles(state->rewind_buffer, cycles, &state->rewind_result)));
  CHECK(SUCCESS(emulator_read_state(e, &state->rewind_result.file_data)));
  assert(emulator_get_cycles(e) == state->rewind_result.info->cycles);

  state->current =
      joypad_find_state(state->joypad_buffer, emulator_get_cycles(state->e));
  state->next = joypad_get_next_state(state->current);

  return OK;
  ON_ERROR_RETURN;
}

void rewind_end(RewindState* state) {
  if (state->rewind_result.info) {
    rewind_truncate_to(state->rewind_buffer, &state->rewind_result);
    joypad_truncate_to(state->joypad_buffer, state->current);
  }
  free(state);
}

#define DEFINE_JOYP_SET(name) \
  void set_joyp_##name(struct Emulator* e, Bool set) { s_buttons.name = set; }

DEFINE_JOYP_SET(up)
DEFINE_JOYP_SET(down)
DEFINE_JOYP_SET(left)
DEFINE_JOYP_SET(right)
DEFINE_JOYP_SET(B)
DEFINE_JOYP_SET(A)
DEFINE_JOYP_SET(start)
DEFINE_JOYP_SET(select)

void* get_frame_buffer_ptr(struct Emulator* e) {
  return *emulator_get_frame_buffer(e);
}

size_t get_frame_buffer_size(struct Emulator* e) { return sizeof(FrameBuffer); }

void* get_audio_buffer_ptr(struct Emulator* e) {
  return emulator_get_audio_buffer(e)->data;
}

size_t get_audio_buffer_capacity(struct Emulator* e) {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);
  return audio_buffer->end - audio_buffer->data;
}

size_t get_audio_buffer_size(struct Emulator* e) {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);
  return audio_buffer->position - audio_buffer->data;
}
