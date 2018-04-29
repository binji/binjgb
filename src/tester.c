/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef _MSC_VER
#include <sys/time.h>
#endif

#include "emulator-debug.h"
#include "options.h"

#define AUDIO_FREQUENCY 44100
/* This value is arbitrary. Why not 1/10th of a second? */
#define AUDIO_FRAMES ((AUDIO_FREQUENCY / 10) * SOUND_OUTPUT_COUNT)
#define DEFAULT_FRAMES 60

static FILE* s_controller_input_file;
static int s_frames = DEFAULT_FRAMES;
static const char* s_output_ppm;
static Bool s_animate;
static Bool s_print_ops;
static int s_print_ops_limit = 512;
static const char* s_rom_filename;

Result write_frame_ppm(struct Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "wb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fputs("P3\n160 144\n255\n", f) >= 0, "fputs failed.\n");
  u8 x, y;
  RGBA* data = *emulator_get_frame_buffer(e);
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
      "  -h,--help            help\n"
      "  -t,--trace           trace each instruction\n"
      "  -l,--log S=N         set log level for system S to N\n"
      "  -i,--input FILE      read controller input from FILE\n"
      "  -f,--frames N        run for N frames (default: %u)\n"
      "  -o,--output FILE     output PPM file to FILE\n"
      "  -a,--animate         output an image every frame\n"
      "     --print-ops       print execution count of each opcode\n"
      "     --print-ops-limit max opcodes to print\n",
      argv[0],
      DEFAULT_FRAMES);

  emulator_print_log_systems();
}

static f64 get_time_sec(void) {
#ifdef _MSC_VER
  // TODO(binji): Windows equivalent of gettimeofday.
  return 0;
#else
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return (f64)tp.tv_sec + (f64)tp.tv_usec / 1000000.0;
#endif
}

void parse_options(int argc, char**argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'t', "trace", 0},
    {'l', "log", 1},
    {'i', "input", 1},
    {'f', "frames", 1},
    {'o', "output", 1},
    {'a', "animate", 0},
    {0, "print-ops-limit", 1},
    {0, "print-ops", 0},
  };

  struct OptionParser* parser = option_parser_new(
      options, sizeof(options) / sizeof(options[0]), argc, argv);

  int errors = 0;
  int done = 0;
  while (!done) {
    OptionResult result = option_parser_next(parser);
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

          case 't':
            emulator_set_trace(TRUE);
            break;

          case 'l':
            switch (emulator_set_log_level_from_string(result.value)) {
              case SET_LOG_LEVEL_ERROR_NONE:
                break;

              case SET_LOG_LEVEL_ERROR_INVALID_FORMAT:
                PRINT_ERROR("invalid log level format, should be S=N\n");
                break;

              case SET_LOG_LEVEL_ERROR_UNKNOWN_LOG_SYSTEM: {
                const char* equals = strchr(result.value, '=');
                PRINT_ERROR("unknown log system: %.*s\n",
                            (int)(equals - result.value), result.value);
                emulator_print_log_systems();
                break;
              }
            }
            break;

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

          default:
            if (strcmp(result.option->long_name, "print-ops") == 0) {
              s_print_ops = TRUE;
            } else if (strcmp(result.option->long_name, "print-ops-limit") ==
                       0) {
              s_print_ops_limit = atoi(result.value);
            } else {
              abort();
            }
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
    goto error;
  }

  option_parser_delete(parser);
  return;

error:
  usage(argc, argv);
  option_parser_delete(parser);
  exit(1);
}

typedef struct {
  u16 opcode;
  u32 count;
} OpcodeCount;

int compare_opcode_count(const void* a, const void* b) {
  OpcodeCount* pa = (OpcodeCount*)a;
  OpcodeCount* pb = (OpcodeCount*)b;
  return (int)pb->count - (int)pa->count;
}

void print_ops(void) {
  u32* opcode_count = emulator_get_opcode_count();
  u32* cb_opcode_count = emulator_get_cb_opcode_count();

  OpcodeCount pairs[512];
  ZERO_MEMORY(pairs);

  int i;
  for (i = 0; i < 256;) {
    pairs[i].opcode = i;
    pairs[i].count = opcode_count[i];
    ++i;
    pairs[i].opcode = 0xcb00 | i;
    pairs[i].count = cb_opcode_count[i];
    ++i;
  }

  qsort(pairs, ARRAY_SIZE(pairs), sizeof(pairs[0]), compare_opcode_count);

  printf("  op:      count -   mnemonic\n");
  printf("--------------------------------\n");
  char mnemonic[100];
  u64 total = 0;
  int distinct = 0;
  Bool skipped = FALSE;
  for (i = 0; i < 512; ++i) {
    if (pairs[i].count > 0) {
      u16 opcode = pairs[i].opcode;
      if (i < s_print_ops_limit) {
        if (opcode < 0x100) {
          printf("  %02x", opcode);
        } else {
          printf("%04x", opcode);
        }
        emulator_get_opcode_mnemonic(opcode, mnemonic, sizeof(mnemonic));
        printf(": %10d - %s\n", pairs[i].count, mnemonic);
      } else {
        skipped = TRUE;
      }
      ++distinct;
      total += pairs[i].count;
    }
  }
  if (skipped) {
    printf("  ...\n");
  }
  printf("distinct: %d\n", distinct);
  printf("total: %lu\n", total);
}

int main(int argc, char** argv) {
  int result = 1;
  struct Emulator* e = NULL;

  parse_options(argc, argv);

  FileData rom;
  CHECK(SUCCESS(file_read(s_rom_filename, &rom)));

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = AUDIO_FREQUENCY;
  emulator_init.audio_frames = AUDIO_FRAMES;
  e = emulator_new(&emulator_init);
  CHECK(e != NULL);

  u32 total_ticks = (u32)(s_frames * PPU_FRAME_TICKS);
  u32 until_ticks = emulator_get_ticks(e) + total_ticks;
  printf("frames = %u total_ticks = %u\n", s_frames, total_ticks);
  Bool finish_at_next_frame = FALSE;
  u32 animation_frame = 0; /* Will likely differ from PPU frame. */
  u32 next_input_frame = 0;
  u32 next_input_frame_buttons = 0;
  f64 start_time = get_time_sec();
  while (TRUE) {
    EmulatorEvent event = emulator_run_until(e, until_ticks);
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      if (s_output_ppm && s_animate) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), ".%08d.ppm", animation_frame++);
        const char* result = replace_extension(s_output_ppm, buffer);
        CHECK(SUCCESS(write_frame_ppm(e, result)));
        xfree((char*)result);
      }

      /* TODO(binji): use timer rather than NEW_FRAME for timing button
       * presses? */
      if (s_controller_input_file) {
        if (emulator_get_ppu_frame(e) >= next_input_frame) {
          JoypadButtons buttons;
          buttons.A = !!(next_input_frame_buttons & 0x01);
          buttons.B = !!(next_input_frame_buttons & 0x02);
          buttons.select = !!(next_input_frame_buttons & 0x4);
          buttons.start = !!(next_input_frame_buttons & 0x8);
          buttons.right = !!(next_input_frame_buttons & 0x10);
          buttons.left = !!(next_input_frame_buttons & 0x20);
          buttons.up = !!(next_input_frame_buttons & 0x40);
          buttons.down = !!(next_input_frame_buttons & 0x80);
          emulator_set_joypad_buttons(e, &buttons);

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
    if (event & EMULATOR_EVENT_UNTIL_TICKS) {
      finish_at_next_frame = TRUE;
      until_ticks += PPU_FRAME_TICKS;
    }
    if (event & EMULATOR_EVENT_INVALID_OPCODE) {
      printf("!! hit invalid opcode, pc=%04x\n", emulator_get_registers(e).PC);
      break;
    }
  }
  f64 host_time = get_time_sec() - start_time;
  Ticks real_total_ticks = emulator_get_ticks(e);
  f64 gb_time = (f64)real_total_ticks / CPU_TICKS_PER_SECOND;
  printf("time: gb=%.1fs host=%.1fs (%.1fx)\n", gb_time, host_time,
         gb_time / host_time);

  if (s_output_ppm && !s_animate) {
    CHECK(SUCCESS(write_frame_ppm(e, s_output_ppm)));
  }

  if (s_print_ops) {
    print_ops();
  }

  result = 0;
error:
  if (e) {
    emulator_delete(e);
  }
  return result;
}
