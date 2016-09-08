/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>

#define NO_SDL

#include "binjgb.c"

#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLE_SIZE 2

Emulator* new_emulator(void) {
  Emulator* e = malloc(sizeof(Emulator));
  return e;
}

void init_rom_data(Emulator* e, void* data, size_t size) {
  e->rom_data.data = data;
  e->rom_data.size = size;
}

/* TODO: refactor to share with binjgb.c */
static u32 get_gb_channel_samples(u32 freq, size_t buffer_bytes) {
  size_t samples = buffer_bytes / (AUDIO_CHANNELS * AUDIO_SAMPLE_SIZE) + 1;
  return (u32)((f64)samples * APU_CYCLES_PER_SECOND / freq) *
         SOUND_OUTPUT_COUNT;
}

Result init_audio_buffer(Emulator* e, u32 freq, size_t buffer_size) {
  u32 gb_channel_samples = get_gb_channel_samples(freq, buffer_size) +
                           AUDIO_BUFFER_EXTRA_CHANNEL_SAMPLES;
  size_t samples_buffer_size =
      gb_channel_samples * sizeof(e->audio_buffer.data[0]);
  e->audio_buffer.data = malloc(samples_buffer_size); /* Leaks. */
  CHECK_MSG(e->audio_buffer.data != NULL, "Audio buffer allocation failed.\n");
  e->audio_buffer.end = e->audio_buffer.data + gb_channel_samples;
  e->audio_buffer.position = e->audio_buffer.data;
  return OK;
error:
  return ERROR;
}

Result init_emulator(Emulator* e);
EmulatorEvent run_emulator_until_event(Emulator* e,
                                       EmulatorEvent last_event,
                                       u32 requested_samples,
                                       f64 until_ms);

void set_joyp_up(Emulator* e, Bool set) { e->state.JOYP.up = set; }
void set_joyp_down(Emulator* e, Bool set) { e->state.JOYP.down = set; }
void set_joyp_left(Emulator* e, Bool set) { e->state.JOYP.left = set; }
void set_joyp_right(Emulator* e, Bool set) { e->state.JOYP.right = set; }
void set_joyp_b(Emulator* e, Bool set) { e->state.JOYP.B = set; }
void set_joyp_a(Emulator* e, Bool set) { e->state.JOYP.A = set; }
void set_joyp_start(Emulator* e, Bool set) { e->state.JOYP.start = set; }
void set_joyp_select(Emulator* e, Bool set) { e->state.JOYP.select = set; }

u32 get_cycles(Emulator* e) { return e->state.cycles; }

void* get_frame_buffer_ptr(Emulator* e) {
  return e->frame_buffer;
}

size_t get_frame_buffer_size(Emulator* e) {
  return sizeof(e->frame_buffer);
}

void* get_audio_buffer_ptr(Emulator* e) {
  return e->audio_buffer.data;
}

size_t get_audio_buffer_size(Emulator* e) {
  return e->audio_buffer.position - e->audio_buffer.data;
}
