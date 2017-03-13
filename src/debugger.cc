/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "emulator-debug.h"
#include "options.h"
#include "host.h"

static const char* s_rom_filename;

static void print_log_systems(void) {
  PRINT_ERROR("valid log systems:\n");
  for (int i = 0; i < NUM_LOG_SYSTEMS; ++i) {
    PRINT_ERROR("  %s\n",
                emulator_get_log_system_name(static_cast<LogSystem>(i)));
  }
}

static void usage(int argc, char** argv) {
  PRINT_ERROR(
      "usage: %s [options] <in.gb>\n"
      "  -h,--help      help\n"
      "  -t,--trace     trace each instruction\n"
      "  -l,--log S=N   set log level for system S to N\n\n",
      argv[0]);

  print_log_systems();
}

void parse_arguments(int argc, char** argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'t', "trace", 0},
    {'l', "log", 1},
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

          case 'l': {
            const char* log_system_name = result.value;
            const char* equals = strchr(result.value, '=');
            if (!equals) {
              PRINT_ERROR("invalid log level format, should be S=N\n");
              continue;
            }

            LogSystem system = NUM_LOG_SYSTEMS;
            for (int i = 0; i < NUM_LOG_SYSTEMS; ++i) {
              const char* name =
                  emulator_get_log_system_name(static_cast<LogSystem>(i));
              if (strncmp(log_system_name, name, strlen(name)) == 0) {
                system = static_cast<LogSystem>(i);
                break;
              }
            }

            if (system == NUM_LOG_SYSTEMS) {
              PRINT_ERROR("unknown log system: %.*s\n",
                          (int)(equals - result.value), result.value);
              print_log_systems();
              continue;
            }
            emulator_set_log_level(system,
                                   static_cast<LogLevel>(atoi(equals + 1)));
            break;
          }

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

  option_parser_delete(parser);
  return;

error:
  usage(argc, argv);
  option_parser_delete(parser);
  exit(1);
}

#define SAVE_EXTENSION ".sav"

/* Copied from binjgb.c; probably will diverge. */
int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  int result = 1;
  parse_arguments(argc, argv);

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom_filename = s_rom_filename;
  emulator_init.audio_frequency = audio_frequency;
  emulator_init.audio_frames = audio_frames;
  struct Emulator* e = emulator_new(&emulator_init);

  HostInit host_init;
  ZERO_MEMORY(host_init);
  host_init.render_scale = 4;
  host_init.audio_frequency = audio_frequency;
  host_init.audio_frames = audio_frames;
  struct Host* host = host_new(&host_init, e);

  const char* save_filename = replace_extension(s_rom_filename, SAVE_EXTENSION);
  emulator_read_ext_ram_from_file(e, save_filename);

  while (host_poll_events(host)) {
    HostConfig config = host_get_config(host);
    if (config.paused) {
      host_delay(host, VIDEO_FRAME_MS);
      continue;
    }

    EmulatorEvent event = host_run_emulator(host);
    if (!config.no_sync) {
      host_synchronize(host);
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      host_render_video(host);
      if (config.step) {
        config.paused = TRUE;
        config.step = FALSE;
        host_set_config(host, &config);
      }
    }
    if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
      host_render_audio(host);
    }
  }

  emulator_write_ext_ram_to_file(e, save_filename);
  result = 0;
  if (host) {
    host_delete(host);
  }
  if (e) {
    emulator_delete(e);
  }
  return result;
}
