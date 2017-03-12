/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "emulator.h"
#include "options.h"

#define AUDIO_FREQUENCY 44100
/* This value is arbitrary. Why not 1/10th of a second? */
#define AUDIO_FRAMES ((AUDIO_FREQUENCY / 10) * SOUND_OUTPUT_COUNT)
#define DEFAULT_TIMEOUT_SEC 30
#define DEFAULT_FRAMES 60

static FILE* s_controller_input_file;
static int s_frames = DEFAULT_FRAMES;
static const char* s_output_ppm;
static Bool s_animate;
static int s_delta_timeout_sec = DEFAULT_TIMEOUT_SEC;
static const char* s_rom_filename;

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
      "  -h,--help          help\n"
      "  -i,--input FILE    read controller input from FILE\n"
      "  -f,--frames N      run for N frames (default: %u)\n"
      "  -o,--output FILE   output PPM file to FILE\n"
      "  -a,--animate       output an image every frame\n"
      "  -t,--timeout N     timeout after N seconds (default: %u)\n",
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

void parse_options(int argc, char**argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'i', "input", 1},
    {'f', "frames", 1},
    {'o', "output", 1},
    {'a', "animate", 0},
    {'t', "timeout", 1},
  };

  struct OptionParser* parser = new_option_parser(
      options, sizeof(options) / sizeof(options[0]), argc, argv);

  int errors = 0;
  int done = 0;
  while (!done) {
    OptionResult result = parse_next_option(parser);
    switch (result.kind) {
      case OPTION_RESULT_KIND_UNKNOWN:
        PRINT_ERROR("ERROR: Unknown option: %s.\n\n", result.arg);
        goto error;

      case OPTION_RESULT_KIND_EXPECTED_VALUE:
        PRINT_ERROR("ERROR: Option --%s requires a value.\n\n",
                    result.option->long_name);
        goto error;

      case OPTION_RESULT_KIND_BAD_SHORT_OPTION:
        PRINT_ERROR("ERROR: Short option -%c is too long: %s.\n\n",
                    result.option->short_name, result.arg);
        goto error;

      case OPTION_RESULT_KIND_OPTION:
        switch (result.option->short_name) {
          case 'h':
            goto error;

          case 'i':
            CHECK_MSG((s_controller_input_file = fopen(result.value, "r")) != 0,
                      "ERROR: Unable to open \"%s\".\n\n", result.value);
            break;

          case 'f':
            s_frames = atoi(result.value);
            break;

          case 'o':
            s_output_ppm = result.value;
            break;

          case 'a':
            s_animate = TRUE;
            break;

          case 't':
            s_delta_timeout_sec = atoi(result.value);
            break;

          default:
            assert(0);
            break;
        }
        break;

      case OPTION_RESULT_KIND_ARG:
        s_rom_filename = result.value;
        break;

      case OPTION_RESULT_KIND_DONE:
        done = 1;
        break;
    }
  }

  if (!s_rom_filename) {
    PRINT_ERROR("ERROR: expected input .gb\n\n");
    usage(argc, argv);
    goto error;
  }

  destroy_option_parser(parser);
  return;

error:
  usage(argc, argv);
  destroy_option_parser(parser);
  exit(1);
}

int main(int argc, char** argv) {
  init_time();
  Emulator emulator;
  ZERO_MEMORY(emulator);
  Emulator* e = &emulator;

  parse_options(argc, argv);

  CHECK(SUCCESS(read_rom_data_from_file(e, s_rom_filename)));
  CHECK(SUCCESS(init_emulator(e)));
  CHECK(SUCCESS(init_audio_buffer(e, AUDIO_FREQUENCY, AUDIO_FRAMES)));

  /* Run for N frames, measured by audio frames (measuring using video is
   * tricky, as the LCD can be disabled. Even when the sound unit is disabled,
   * we still produce audio frames at a fixed rate. */
  u32 total_audio_frames = (u32)((f64)s_frames * PPU_FRAME_CYCLES *
                                     AUDIO_FREQUENCY / CPU_CYCLES_PER_SECOND +
                                 1);
  printf("frames = %u total_audio_frames = %u\n", s_frames, total_audio_frames);
  f64 timeout_sec = get_time_sec() + s_delta_timeout_sec;
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
      if (s_output_ppm && s_animate) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), ".%08d.ppm", animation_frame++);
        const char* result = replace_extension(s_output_ppm, buffer);
        CHECK(SUCCESS(write_frame_ppm(e, result)));
        free((char*)result);
      }

      /* TODO(binji): use timer rather than NEW_FRAME for timing button
       * presses? */
      if (s_controller_input_file) {
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
          while (fgets(input_buffer, sizeof(input_buffer),
                       s_controller_input_file)) {
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
              fclose(s_controller_input_file);
              s_controller_input_file = NULL;
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
  if (s_output_ppm && !s_animate) {
    CHECK(SUCCESS(write_frame_ppm(e, s_output_ppm)));
  }

  return 0;
error:
  return 1;
}
