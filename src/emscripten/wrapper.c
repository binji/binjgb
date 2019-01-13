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
#include "memory.h"
#include "rewind.h"

typedef struct {
  Emulator* e;
  RewindBuffer* rewind_buffer;
  JoypadBuffer* joypad_buffer;
  RewindResult rewind_result;
  JoypadStateIter current;
  JoypadStateIter next;
} RewindState;

static Emulator* e;

static EmulatorInit s_init;
static JoypadButtons s_buttons;

Emulator* emulator_new_simple(void* rom_data, size_t rom_size,
                              int audio_frequency, int audio_frames) {
  s_init.rom.data = rom_data;
  s_init.rom.size = rom_size;
  s_init.audio_frequency = audio_frequency;
  s_init.audio_frames = audio_frames;
  s_init.random_seed = 0xcabba6e5;

  e = emulator_new(&s_init);

  return e;
}

f64 emulator_get_ticks_f64(Emulator* e) {
  return (f64)emulator_get_ticks(e);
}

EmulatorEvent emulator_run_until_f64(Emulator* e, f64 until_ticks_f64) {
  return emulator_run_until(e, (Ticks)until_ticks_f64);
}

f64 rewind_get_newest_ticks_f64(RewindBuffer* buf) {
  return (f64)rewind_get_newest_ticks(buf);
}

f64 rewind_get_oldest_ticks_f64(RewindBuffer* buf) {
  return (f64)rewind_get_oldest_ticks(buf);
}

static void default_joypad_callback(JoypadButtons* joyp, void* user_data) {
  JoypadBuffer* joypad_buffer = user_data;
  *joyp = s_buttons;
  Ticks ticks = emulator_get_ticks(e);
  joypad_append_if_new(joypad_buffer, joyp, ticks);
}

void emulator_set_default_joypad_callback(Emulator* e,
                                          JoypadBuffer* joypad_buffer) {
  emulator_set_joypad_callback(e, default_joypad_callback, joypad_buffer);
}

void emulator_set_bw_palette_simple(Emulator* e, u32 type, u32 white,
                                    u32 light_gray, u32 dark_gray, u32 black) {
  assert(type < PALETTE_TYPE_COUNT);
  PaletteRGBA palette = {{white, light_gray, dark_gray, black}};
  emulator_set_bw_palette(e, type, &palette);
}

RewindBuffer* rewind_new_simple(Emulator* e, int frames_per_base_state,
                                size_t buffer_capacity) {
  RewindInit init;
  init.frames_per_base_state = frames_per_base_state;
  init.buffer_capacity = buffer_capacity;
  return rewind_new(&init, e);
}

static RewindState s_rewind_state;

RewindState* rewind_begin(Emulator* e, RewindBuffer* rewind_buffer,
                          JoypadBuffer* joypad_buffer) {
  s_rewind_state.e = e;
  s_rewind_state.rewind_buffer = rewind_buffer;
  s_rewind_state.joypad_buffer = joypad_buffer;
  return &s_rewind_state;
}

static void rewind_joypad_callback(struct JoypadButtons* joyp,
                                   void* user_data) {
  RewindState* state = user_data;
  Ticks ticks = emulator_get_ticks(state->e);
  while (state->next.state && state->next.state->ticks <= ticks) {
    state->current = state->next;
    state->next = joypad_get_next_state(state->next);
  }

  *joyp = joypad_unpack_buttons(state->current.state->buttons);
}

void emulator_set_rewind_joypad_callback(RewindState* state) {
  emulator_set_joypad_callback(e, rewind_joypad_callback, state);
}

Result rewind_to_ticks_wrapper(RewindState* state, f64 ticks_f64) {
  Ticks ticks = (Ticks)ticks_f64;
  CHECK(SUCCESS(
      rewind_to_ticks(state->rewind_buffer, ticks, &state->rewind_result)));
  CHECK(SUCCESS(emulator_read_state(e, &state->rewind_result.file_data)));
  assert(emulator_get_ticks(e) == state->rewind_result.info->ticks);

  state->current =
      joypad_find_state(state->joypad_buffer, emulator_get_ticks(state->e));
  state->next = joypad_get_next_state(state->current);

  return OK;
  ON_ERROR_RETURN;
}

void rewind_end(RewindState* state) {
  if (state->rewind_result.info) {
    rewind_truncate_to(state->rewind_buffer, state->e, &state->rewind_result);
    joypad_truncate_to(state->joypad_buffer, state->current);
  }
}

#define DEFINE_JOYP_SET(name) \
  void set_joyp_##name(Emulator* e, Bool set) { s_buttons.name = set; }

DEFINE_JOYP_SET(up)
DEFINE_JOYP_SET(down)
DEFINE_JOYP_SET(left)
DEFINE_JOYP_SET(right)
DEFINE_JOYP_SET(B)
DEFINE_JOYP_SET(A)
DEFINE_JOYP_SET(start)
DEFINE_JOYP_SET(select)

void* get_frame_buffer_ptr(Emulator* e) {
  return *emulator_get_frame_buffer(e);
}

size_t get_frame_buffer_size(Emulator* e) { return sizeof(FrameBuffer); }

void* get_audio_buffer_ptr(Emulator* e) {
  return emulator_get_audio_buffer(e)->data;
}

size_t get_audio_buffer_capacity(Emulator* e) {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);
  return audio_buffer->end - audio_buffer->data;
}

size_t get_audio_buffer_size(Emulator* e) {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);
  return audio_buffer->position - audio_buffer->data;
}

FileData* ext_ram_file_data_new(Emulator* e) {
  FileData* file_data = xmalloc(sizeof(FileData));
  emulator_init_ext_ram_file_data(e, file_data);
  return file_data;
}

void* get_file_data_ptr(FileData* file_data) {
  return file_data->data;
}

size_t get_file_data_size(FileData* file_data) {
  return file_data->size;
}

void file_data_delete(FileData* file_data) {
  xfree(file_data->data);
  xfree(file_data);
}
