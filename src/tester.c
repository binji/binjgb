/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define AUDIO_FREQUENCY 44100
/* This value is arbitrary. Why not 1/10th of a second? */
#define AUDIO_FRAMES ((AUDIO_FREQUENCY / 10) * SOUND_OUTPUT_COUNT)
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

static time_t s_start_time;
static void init_time(void) {
  s_start_time = time(NULL);
}

static f64 get_time_sec(void) {
  time_t now = time(NULL);
  return now - s_start_time;
}

int main(int argc, char** argv) {
  init_time();
  Emulator emulator;
  ZERO_MEMORY(emulator);
  Emulator* e = &emulator;

  int frames = DEFAULT_FRAMES;
  const char* output_ppm = NULL;
  Bool animate = FALSE;
  int delta_timeout_sec = DEFAULT_TIMEOUT_SEC;
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
        delta_timeout_sec = atoi(optarg);
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

  CHECK(SUCCESS(read_data_from_file(e, rom_filename)));
  CHECK(SUCCESS(init_emulator(e)));
  CHECK(SUCCESS(
      init_audio_buffer(&e->audio_buffer, AUDIO_FREQUENCY, AUDIO_FRAMES)));

  /* Run for N frames, measured by audio frames (measuring using video is
   * tricky, as the LCD can be disabled. Even when the sound unit is disabled,
   * we still produce audio frames at a fixed rate. */
  u32 total_audio_frames = (u32)((f64)frames * PPU_FRAME_CYCLES *
                                     AUDIO_FREQUENCY / CPU_CYCLES_PER_SECOND +
                                 1);
  printf("frames = %u total_audio_frames = %u\n", frames, total_audio_frames);
  f64 timeout_sec = get_time_sec() + delta_timeout_sec;
  Bool finish_at_next_frame = FALSE;
  u32 animation_frame = 0; /* Will likely differ from PPU frame. */
  u32 next_input_frame = 0;
  u32 next_input_frame_buttons = 0;
  while (TRUE) {
    EmulatorEvent event = run_emulator(e, AUDIO_FRAMES);
    if (get_time_sec() > timeout_sec) {
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
      if (total_audio_frames > AUDIO_FRAMES) {
        total_audio_frames -= AUDIO_FRAMES;
      } else {
        total_audio_frames = 0;
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
