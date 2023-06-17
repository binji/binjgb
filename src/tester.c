/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#ifndef _MSC_VER
#include <sys/time.h>
#endif

#ifdef TESTER_DEBUGGER
#include "emulator-debug.h"
#else
#include "emulator.h"
#endif

#include "joypad.h"
#include "options.h"

#define AUDIO_FREQUENCY 44100
/* This value is arbitrary. Why not 1/10th of a second? */
#define AUDIO_FRAMES ((AUDIO_FREQUENCY / 10) * SOUND_OUTPUT_COUNT)
#define DEFAULT_FRAMES 60
#define MAX_PRINT_OPS_LIMIT 512
#define MAX_PROFILE_LIMIT 1000

static const char* s_joypad_filename;
static int s_frames = DEFAULT_FRAMES;
static const char* s_output_ppm;
static Bool s_animate;
static Bool s_print_ops;
static u32 s_print_ops_limit = MAX_PRINT_OPS_LIMIT;
static Bool s_profile;
static u32 s_profile_limit = 30;
static const char* s_rom_filename;
static u32 s_random_seed = 0xcabba6e5;
static u32 s_builtin_palette;
static Bool s_force_dmg;
static Bool s_use_sgb_border;


Result write_frame_ppm(Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "wb");
  int width = s_use_sgb_border ? SGB_SCREEN_WIDTH : SCREEN_WIDTH;
  int height = s_use_sgb_border ? SGB_SCREEN_HEIGHT : SCREEN_HEIGHT;
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fprintf(f, "P3\n%u %u\n255\n", width, height) >= 0,
            "fputs failed.\n");
  int x, y;
  RGBA* data = *emulator_get_frame_buffer(e);
  RGBA* sgb_data = *emulator_get_sgb_frame_buffer(e);
  if (s_use_sgb_border) {
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x) {
        RGBA pixel = *sgb_data++;
        if (x >= SGB_SCREEN_LEFT && x < SGB_SCREEN_LEFT + SCREEN_WIDTH &&
            y >= SGB_SCREEN_TOP && y < SGB_SCREEN_TOP + SCREEN_HEIGHT) {
          if (pixel == 0) {
            pixel = *data;
          }
          data++;
        }
        u8 b = (pixel >> 16) & 0xff;
        u8 g = (pixel >> 8) & 0xff;
        u8 r = (pixel >> 0) & 0xff;
        CHECK_MSG(fprintf(f, "%3u %3u %3u ", r, g, b) >= 0,
                  "fprintf failed.\n");
      }
      CHECK_MSG(fputs("\n", f) >= 0, "fputs failed.\n");
    }
  } else {
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x) {
        RGBA pixel = *data++;
        u8 b = (pixel >> 16) & 0xff;
        u8 g = (pixel >> 8) & 0xff;
        u8 r = (pixel >> 0) & 0xff;
        CHECK_MSG(fprintf(f, "%3u %3u %3u ", r, g, b) >= 0,
                  "fprintf failed.\n");
      }
      CHECK_MSG(fputs("\n", f) >= 0, "fputs failed.\n");
    }
  }
  fclose(f);
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

void usage(int argc, char** argv) {
  static const char usage[] =
      "usage: %s [options] <in.gb>\n"
      "  -h,--help            help\n"
#ifdef TESTER_DEBUGGER
      "  -t,--trace           trace each instruction\n"
      "  -l,--log S=N         set log level for system S to N\n"
#endif
      "  -j,--joypad FILE     read joypad input from FILE\n"
      "  -f,--frames N        run for N frames (default: %u)\n"
      "  -o,--output FILE     output PPM file to FILE\n"
      "  -a,--animate         output an image every frame\n"
#ifdef TESTER_DEBUGGER
      "     --print-ops       print execution count of each opcode\n"
      "     --print-ops-limit max opcodes to print\n"
      "     --profile         print execution count of each opcode\n"
      "     --profile-limit   max opcodes to print\n"
#endif
      "  -s,--seed SEED       random seed used for initializing RAM\n"
      "  -P,--palette PAL     use a builtin palette for DMG\n"
      "     --force-dmg       force running as a DMG (original gameboy)\n"
      "     --sgb-border         draw the super gameboy border\n";

  PRINT_ERROR(usage, argv[0], DEFAULT_FRAMES);

#ifdef TESTER_DEBUGGER
  emulator_print_log_systems();
#endif
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
#ifdef TESTER_DEBUGGER
    {'t', "trace", 0},
    {'l', "log", 1},
#endif
    {'j', "joypad", 1},
    {'f', "frames", 1},
    {'o', "output", 1},
    {'a', "animate", 0},
#ifdef TESTER_DEBUGGER
    {0, "print-ops-limit", 1},
    {0, "print-ops", 0},
    {0, "profile-limit", 1},
    {0, "profile", 0},
#endif
    {'s', "seed", 1},
    {'P', "palette", 1},
    {0, "force-dmg", 0},
    {0, "sgb-border", 0},
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

#ifdef TESTER_DEBUGGER
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
#endif

          case 'j':
            s_joypad_filename = result.value;
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

          case 's':
            s_random_seed = atoi(result.value);
            break;

          case 'P':
            s_builtin_palette = atoi(result.value);
            break;

          default:
#ifdef TESTER_DEBUGGER
            if (strcmp(result.option->long_name, "print-ops") == 0) {
              s_print_ops = TRUE;
              emulator_set_opcode_count_enabled(TRUE);
            } else if (strcmp(result.option->long_name, "print-ops-limit") ==
                       0) {
              s_print_ops_limit = atoi(result.value);
              if (s_print_ops_limit >= MAX_PRINT_OPS_LIMIT) {
                s_print_ops_limit = MAX_PRINT_OPS_LIMIT;
              }
            } else if (strcmp(result.option->long_name, "profile") == 0) {
              s_profile = TRUE;
              emulator_set_profiling_enabled(TRUE);
            } else if (strcmp(result.option->long_name, "profile-limit") == 0) {
              s_profile_limit = atoi(result.value);
              if (s_profile_limit >= MAX_PROFILE_LIMIT) {
                s_profile_limit = MAX_PROFILE_LIMIT;
              }
#else
            if (FALSE) {
#endif
            } else if (strcmp(result.option->long_name, "force-dmg") == 0) {
              s_force_dmg = TRUE;
            } else if (strcmp(result.option->long_name, "sgb-border") == 0) {
              s_use_sgb_border = TRUE;
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

#ifdef TESTER_DEBUGGER
typedef struct {
  u32 value;
  u32 count;
} U32Pair;

int compare_pair(const void* a, const void* b) {
  U32Pair* pa = (U32Pair*)a;
  U32Pair* pb = (U32Pair*)b;
  int dcount = (int)pb->count - (int)pa->count;
  if (dcount != 0) {
    return dcount;
  }
  return (int)pa->value - (int)pb->value;
}

void print_ops(void) {
  u32* opcode_count = emulator_get_opcode_count();
  u32* cb_opcode_count = emulator_get_cb_opcode_count();

  U32Pair pairs[512];
  ZERO_MEMORY(pairs);

  u32 i;
  for (i = 0; i < 512; i += 2) {
    u8 op = i / 2;
    pairs[i].value = op;
    pairs[i].count = opcode_count[op];
    pairs[i + 1].value = 0xcb00 | op;
    pairs[i + 1].count = cb_opcode_count[op];
  }

  qsort(pairs, ARRAY_SIZE(pairs), sizeof(pairs[0]), compare_pair);

  printf("  op:      count -   mnemonic\n");
  printf("--------------------------------\n");
  char mnemonic[100];
  u64 total = 0;
  int distinct = 0;
  Bool skipped = FALSE;
  for (i = 0; i < 512; ++i) {
    if (pairs[i].count > 0) {
      u16 opcode = pairs[i].value;
      if (i < s_print_ops_limit) {
        if (opcode == 0xcb) continue;
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
  printf("total: %" PRIu64 "\n", total);
}

void swap_pair(U32Pair* a, U32Pair* b) {
  U32Pair temp = *a;
  *a = *b;
  *b = temp;
}

void insert_heap(U32Pair* min_heap, u32 index, u32 value, u32 count) {
  min_heap[index].value = value;
  min_heap[index].count = count;
  while (index > 0) {
    u32 parent = (index - 1) / 2;
    if (min_heap[parent].count <= min_heap[index].count) {
      break;
    }
    swap_pair(&min_heap[parent], &min_heap[index]);
    index = parent;
  }
}

void extract_min_heap(U32Pair* min_heap, u32 last_index) {
  min_heap[0] = min_heap[last_index];
  u32 index = 0;
  while (index * 2 + 1 < last_index) {
    u32 left_index = index * 2 + 1, right_index = index * 2 + 2;
    u32 min_index = index;
    if (left_index < last_index &&
        min_heap[min_index].count > min_heap[left_index].count) {
      min_index = left_index;
    }
    if (right_index < last_index &&
        min_heap[min_index].count > min_heap[right_index].count) {
      min_index = right_index;
    }
    if (min_index == index) {
      break;
    }
    swap_pair(&min_heap[min_index], &min_heap[index]);
    index = min_index;
  }
}

void print_profile(Emulator* e) {
  u32 rom_size = emulator_get_rom_size(e);
  u32* counters = emulator_get_profiling_counters();
  const u32 heap_limit = s_profile_limit;
  U32Pair* min_heap = xcalloc(heap_limit + 1, sizeof(U32Pair));

  u32 i;
  for (i = 0; i < heap_limit; ++i) {
    insert_heap(min_heap, i, i, counters[i]);
  }

  for (i = heap_limit; i < rom_size; ++i) {
    extract_min_heap(min_heap, heap_limit);
    insert_heap(min_heap, heap_limit, i, counters[i]);
  }

  qsort(min_heap, heap_limit + 1, sizeof(U32Pair), compare_pair);
  U32Pair* pairs = min_heap;

  printf("     count -   instr\n");
  printf("-------------------------------------------------\n");
  char disasm[100];
  for (i = 0; i < s_profile_limit; ++i) {
    if (pairs[i].count > 0) {
      emulator_disassemble_rom(e, pairs[i].value, disasm, sizeof(disasm));
      printf("%10d - %s\n", pairs[i].count, disasm);
    }
  }
  xfree(pairs);
}
#endif

int main(int argc, char** argv) {
  int result = 1;
  Emulator* e = NULL;
  JoypadBuffer* joypad_buffer = NULL;

  parse_options(argc, argv);

  FileData rom;
  CHECK(SUCCESS(file_read_aligned(s_rom_filename, MINIMUM_ROM_SIZE, &rom)));

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = AUDIO_FREQUENCY;
  emulator_init.audio_frames = AUDIO_FRAMES;
  emulator_init.random_seed = s_random_seed;
  emulator_init.builtin_palette = s_builtin_palette;
  emulator_init.force_dmg = s_force_dmg;
  e = emulator_new(&emulator_init);
  CHECK(e != NULL);

  JoypadPlayback joypad_playback;
  if (s_joypad_filename) {
    FileData file_data;
    CHECK(SUCCESS(file_read(s_joypad_filename, &file_data)));
    CHECK(SUCCESS(joypad_read(&file_data, &joypad_buffer)));
    file_data_delete(&file_data);
    emulator_set_joypad_playback_callback(e, joypad_buffer, &joypad_playback);
  }

#ifdef TESTER_DEBUGGER
  /* Disable rom usage collecting since it's slow and not useful here. */
  emulator_set_rom_usage_enabled(FALSE);
#endif

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

      if (finish_at_next_frame) {
        break;
      }
    }
    if (event & EMULATOR_EVENT_UNTIL_TICKS) {
      finish_at_next_frame = TRUE;
      until_ticks += PPU_FRAME_TICKS;
    }
    if (event & EMULATOR_EVENT_INVALID_OPCODE) {
      printf("!! hit invalid opcode, pc=");
#ifdef TESTER_DEBUGGER
      printf("%04x\n", emulator_get_registers(e).PC);
#else
      printf("???\n");
#endif
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

#ifdef TESTER_DEBUGGER
  if (s_print_ops) {
    print_ops();
  }

  if (s_profile) {
    print_profile(e);
  }
#endif

  result = 0;
error:
  if (joypad_buffer) {
    joypad_delete(joypad_buffer);
  }
  if (e) {
    emulator_delete(e);
  }
  return result;
}
