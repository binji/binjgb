/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* This value is arbitrary. Why not 1/10th of a second? */
#define GB_CHANNEL_SAMPLES ((APU_CYCLES_PER_SECOND / 10) * SOUND_OUTPUT_COUNT)
#define DEFAULT_TIMEOUT_SEC 30
#define DEFAULT_FRAMES 60

#include "binjgb.c"

static char* replace_extension(const char* filename, const char* extension) {
  size_t length = strlen(filename) + strlen(extension) + 1; /* +1 for \0. */
  char* result = malloc(length); /* Leaks. */
  char* last_dot = strrchr(filename, '.');
  if (last_dot == NULL) {
    snprintf(result, length, "%s%s", filename, extension);
  } else {
    snprintf(result, length, "%.*s%s", (int)(last_dot - filename), filename,
             extension);
  }
  return result;
}

static Result init_audio_buffer(AudioBuffer* audio_buffer) {
  u32 gb_channel_samples =
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
  u8 x, y;
  RGBA* data = &e->frame_buffer[0];
  for (y = 0; y < SCREEN_HEIGHT; ++y) {
    for (x = 0; x < SCREEN_WIDTH; ++x) {
      RGBA pixel = *data++;
      u8 b = (pixel >> 16) & 0xff;
      u8 g = (pixel >> 8) & 0xff;
      u8 r = (pixel >> 0) & 0xff;
      CHECK_MSG(fprintf(f, "%3u %3u %3u ", r, g, b) >= 0, "fprintf failed.\n");
    }
    CHECK_MSG(fputs("\n", f) >= 0, "fputs failed.\n");
  }
  fclose(f);
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

void usage(int argc, char** argv) {
  PRINT_ERROR(
      "usage: %s [options] <in.gb>\n"
      "  -h       help\n"
      "  -i FILE  read controller input from FILE\n"
      "  -f N     run for N frames (default: %u)\n"
      "  -o FILE  output PPM file to FILE\n"
      "  -a       output an image every frame\n"
      "  -t N     timeout after N seconds (default: %u)\n",
      argv[0],
      DEFAULT_FRAMES,
      DEFAULT_TIMEOUT_SEC);
}

int main(int argc, char** argv) {
  init_time();
  Emulator emulator;
  ZERO_MEMORY(emulator);
  Emulator* e = &emulator;

  s_never_trace = 1;
  s_log_level_memory = 0;
  s_log_level_ppu = 0;
  s_log_level_apu = 0;
  s_log_level_io = 0;
  s_log_level_interrupt = 0;

  int frames = DEFAULT_FRAMES;
  const char* output_ppm = NULL;
  Bool animate = FALSE;
  int timeout_sec = DEFAULT_TIMEOUT_SEC;
  FILE* controller_input_file = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "hi:f:o:at:")) != -1) {
    switch (opt) {
      case 'h':
        usage(argc, argv);
        return 1;
      case 'i':
        CHECK_MSG((controller_input_file = fopen(optarg, "r")) != 0,
                  "Unable to open \"%s\".", optarg);
        break;
      case 'f':
        frames = atoi(optarg);
        break;
      case 'o':
        output_ppm = optarg;
        break;
      case 'a':
        animate = TRUE;
        break;
      case 't':
        timeout_sec = atoi(optarg);
        break;
      default:
        PRINT_ERROR("unknown option: -%c\n", opt);
        break;
    }
  }

  if (optind >= argc) {
    PRINT_ERROR("expected input .gb\n");
    usage(argc, argv);
    return 1;
  }

  const char* rom_filename = argv[optind];

  CHECK(SUCCESS(read_rom_data_from_file(rom_filename, &e->rom_data)));
  CHECK(SUCCESS(init_audio_buffer(&e->audio_buffer)));
  CHECK(SUCCESS(init_emulator(e)));

  /* Run for N frames, measured by audio samples (measuring using video is
   * tricky, as the LCD can be disabled. Even when the sound unit is disabled,
   * we still produce audio samples at a fixed rate. */
  u32 total_samples =
      (u32)((f64)frames * APU_CYCLES_PER_SECOND * PPU_FRAME_CYCLES *
            SOUND_OUTPUT_COUNT / CPU_CYCLES_PER_SECOND);
  LOG("frames = %u total_samples = %u\n", frames, total_samples);
  f64 timeout_ms = get_time_ms() + timeout_sec * MILLISECONDS_PER_SECOND;
  EmulatorEvent event = 0;
  Bool finish_at_next_frame = FALSE;
  u32 animation_frame = 0; /* Will likely differ from PPU frame. */
  u32 next_input_frame = 0;
  u32 next_input_frame_buttons = 0;
  while (TRUE) {
    event = run_emulator_until_event(e, event, GB_CHANNEL_SAMPLES, timeout_ms);
    if (event & EMULATOR_EVENT_TIMEOUT) {
      PRINT_ERROR("test timed out.\n");
      goto error;
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      if (output_ppm && animate) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), ".%08d.ppm", animation_frame++);
        char* result = replace_extension(output_ppm, buffer);
        CHECK(SUCCESS(write_frame_ppm(e, result)));
        free(result);
      }

      /* TODO(binji): use timer rather than NEW_FRAME for timing button
       * presses? */
      if (controller_input_file) {
        if (e->state.ppu.frame >= next_input_frame) {
          e->state.JOYP.A = !!(next_input_frame_buttons & 0x01);
          e->state.JOYP.B = !!(next_input_frame_buttons & 0x02);
          e->state.JOYP.select = !!(next_input_frame_buttons & 0x4);
          e->state.JOYP.start = !!(next_input_frame_buttons & 0x8);
          e->state.JOYP.right = !!(next_input_frame_buttons & 0x10);
          e->state.JOYP.left = !!(next_input_frame_buttons & 0x20);
          e->state.JOYP.up = !!(next_input_frame_buttons & 0x40);
          e->state.JOYP.down = !!(next_input_frame_buttons & 0x80);

          /* Read the next input from the file. */
          char input_buffer[256];
          while (TRUE) {
            fgets(input_buffer, sizeof(input_buffer), controller_input_file);
            char* p = input_buffer;
            while (*p == ' ' || *p == '\t') {
              p++;
            }
            if (*p == '#') {
              continue;
            }
            u32 rel_frame = 0;
            if(sscanf(p, "%u %u", &rel_frame,
                      &next_input_frame_buttons) != 2) {
              fclose(controller_input_file);
              controller_input_file = NULL;
              next_input_frame = UINT32_MAX;
            } else {
              next_input_frame += rel_frame;
            }
            break;
          }
        }
      }

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
  if (output_ppm && !animate) {
    CHECK(SUCCESS(write_frame_ppm(e, output_ppm)));
  }

  return 0;
error:
  return 1;
}
