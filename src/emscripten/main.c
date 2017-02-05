/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>

#define NO_SDL

#include "binjgb.c"

Emulator* new_emulator(void) { return malloc(sizeof(Emulator)); }

void clear_emulator(Emulator* e) {
  free(e->audio_buffer.data);
  memset(e, 0, sizeof(Emulator));
}

void init_rom_data(Emulator* e, void* data, size_t size) {
  e->file_data.data = data;
  e->file_data.size = size;
}

void set_joyp_up(Emulator* e, Bool set) { e->state.JOYP.up = set; }
void set_joyp_down(Emulator* e, Bool set) { e->state.JOYP.down = set; }
void set_joyp_left(Emulator* e, Bool set) { e->state.JOYP.left = set; }
void set_joyp_right(Emulator* e, Bool set) { e->state.JOYP.right = set; }
void set_joyp_b(Emulator* e, Bool set) { e->state.JOYP.B = set; }
void set_joyp_a(Emulator* e, Bool set) { e->state.JOYP.A = set; }
void set_joyp_start(Emulator* e, Bool set) { e->state.JOYP.start = set; }
void set_joyp_select(Emulator* e, Bool set) { e->state.JOYP.select = set; }
u32 get_cycles(Emulator* e) { return e->state.cycles; }
void* get_frame_buffer_ptr(Emulator* e) { return e->frame_buffer; }
size_t get_frame_buffer_size(Emulator* e) { return sizeof(e->frame_buffer); }
void* get_audio_buffer_ptr(Emulator* e) { return e->audio_buffer.data; }
size_t get_audio_buffer_capacity(Emulator* e) {
  return e->audio_buffer.end - e->audio_buffer.data;
}
size_t get_audio_buffer_size(Emulator* e) {
  return e->audio_buffer.position - e->audio_buffer.data;
}
