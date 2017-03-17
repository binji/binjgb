/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>

#include "emulator.h"

static EmulatorInit s_init;
static JoypadButtons s_buttons;

struct Emulator* emulator_new_simple(void* rom_data, size_t rom_size,
                                     int audio_frequency, int audio_frames) {
  s_init.rom.data = rom_data;
  s_init.rom.size = rom_size;
  s_init.audio_frequency = audio_frequency;
  s_init.audio_frames = audio_frames;
  return emulator_new(&s_init);
}

#define DEFINE_JOYP_SET(name)                          \
  void set_joyp_##name(struct Emulator* e, Bool set) { \
    s_buttons.name = set;                              \
    emulator_set_joypad_buttons(e, &s_buttons);        \
  }

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
