/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdint.h>
#include <stdio.h>

/* This value is arbitrary. Why not 1/10th of a second? */
#define GB_CHANNEL_SAMPLES ((APU_CYCLES_PER_SECOND / 10) * SOUND_OUTPUT_COUNT)
#define TEST_TIMEOUT_SEC 30

#include "binjgb.c"

static Result init_audio_buffer(AudioBuffer* audio_buffer) {
  uint32_t gb_channel_samples =
      (GB_CHANNEL_SAMPLES + AUDIO_BUFFER_EXTRA_CHANNEL_SAMPLES);
  size_t buffer_size = gb_channel_samples * sizeof(audio_buffer->data[0]);
  audio_buffer->data = malloc(buffer_size);
  CHECK_MSG(audio_buffer->data != NULL, "Audio buffer allocation failed.\n");
  audio_buffer->end = audio_buffer->data + gb_channel_samples;
  audio_buffer->position = audio_buffer->data;
  return OK;
error:
  return ERROR;
}

Result write_frame_ppm(Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "wb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fputs("P3\n160 144\n255\n", f) >= 0, "fputs failed.\n");
  uint8_t x, y;
  RGBA* data = &e->frame_buffer[0];
  for (y = 0; y < SCREEN_HEIGHT; ++y) {
    for (x = 0; x < SCREEN_WIDTH; ++x) {
      RGBA pixel = *data++;
      uint8_t b = (pixel >> 16) & 0xff;
      uint8_t g = (pixel >> 8) & 0xff;
      uint8_t r = (pixel >> 0) & 0xff;
      CHECK_MSG(fprintf(f, "%3u %3u %3u ", r, g, b) >= 0, "fprintf failed.\n");
    }
    CHECK_MSG(fputs("\n", f) >= 0, "fputs failed.\n");
  }
  fclose(f);
  return OK;
error:
  if (f) {
    fclose(f);
  }
  return ERROR;
}

int main(int argc, char** argv) {
  clock_gettime(CLOCK_MONOTONIC, &s_start_time);
  --argc; ++argv;
  RomData rom_data;
  Emulator emulator;
  Emulator* e = &emulator;
  AudioBuffer audio_buffer;

  s_never_trace = 1;
  s_log_level_memory = 0;
  s_log_level_ppu = 0;
  s_log_level_apu = 0;
  s_log_level_io = 0;
  s_log_level_interrupt = 0;

  ZERO_MEMORY(rom_data);
  ZERO_MEMORY(emulator);
  ZERO_MEMORY(audio_buffer);

  CHECK_MSG(argc == 3, "usage: tester <in.gb> <frames> <out.ppm>\n");
  const char* rom_filename = argv[0];
  int frames = atoi(argv[1]);
  const char* output_ppm = argv[2];
  CHECK(SUCCESS(read_rom_data_from_file(rom_filename, &rom_data)));
  CHECK(SUCCESS(init_audio_buffer(&audio_buffer)));
  CHECK(SUCCESS(init_emulator(e, &rom_data, &audio_buffer)));

  /* Run for N frames, measured by audio samples (measuring using video is
   * tricky, as the LCD can be disabled. Even when the sound unit is disabled,
   * we still produce audio samples at a fixed rate. */
  uint32_t total_samples =
      (uint32_t)((double)frames * APU_CYCLES_PER_SECOND * PPU_FRAME_CYCLES *
                 SOUND_OUTPUT_COUNT / CPU_CYCLES_PER_SECOND);
  LOG("frames = %u total_samples = %u\n", frames, total_samples);
  double timeout_ms =
      get_time_ms() + TEST_TIMEOUT_SEC * MILLISECONDS_PER_SECOND;
  EmulatorEvent event = 0;
  Bool finish_at_next_frame = FALSE;
  while (TRUE) {
    event = run_emulator_until_event(e, event, GB_CHANNEL_SAMPLES, timeout_ms);
    if (event & EMULATOR_EVENT_TIMEOUT) {
      LOG("test timed out.\n");
      goto error;
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      if (finish_at_next_frame) {
        break;
      }
    }
    if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
      if (total_samples > GB_CHANNEL_SAMPLES) {
        total_samples -= GB_CHANNEL_SAMPLES;
      } else {
        total_samples = 0;
        finish_at_next_frame = TRUE;
      }
    }
  }
  CHECK(SUCCESS(write_frame_ppm(e, output_ppm)));

  return 0;
error:
  return 1;
}
