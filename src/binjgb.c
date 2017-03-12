/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "emulator.h"
#include "host.h"

#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"

static Emulator s_emulator;
static const char* s_save_state_filename;

static void write_state(HostHookContext* ctx) {
  write_state_to_file(ctx->e, s_save_state_filename);
}

static void read_state(HostHookContext* ctx) {
  read_state_from_file(ctx->e, s_save_state_filename);
}

HostHooks s_hooks = {
  .write_state = write_state,
  .read_state = read_state,
};

int main(int argc, char** argv) {
  --argc; ++argv;
  int result = 1;
  struct Host* host = NULL;

  CHECK_MSG(argc == 1, "no rom file given.\n");
  const char* rom_filename = argv[0];
  Emulator* e = &s_emulator;

  CHECK(SUCCESS(read_rom_data_from_file(e, rom_filename)));
  CHECK(SUCCESS(init_emulator(e)));

  HostConfig host_config;
  ZERO_MEMORY(host_config);
  host_config.render_scale = 4;
  host_config.frequency = 44100;
  host_config.samples = 2048;
  host = host_new(&host_config, e);

  const char* save_filename = replace_extension(rom_filename, SAVE_EXTENSION);
  s_save_state_filename = replace_extension(rom_filename, SAVE_STATE_EXTENSION);
  read_ext_ram_from_file(e, save_filename);

  while (host_poll_events(host)) {
    if (e->config.paused) {
      host_delay(host, VIDEO_FRAME_MS);
      continue;
    }

    EmulatorEvent event = host_run_emulator(host);
    if (!e->config.no_sync) {
      host_synchronize(host);
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      host_render_video(host);
      if (e->config.step) {
        e->config.paused = TRUE;
        e->config.step = FALSE;
      }
    }
    if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
      host_render_audio(host);
    }
  }

  write_ext_ram_to_file(e, save_filename);
  result = 0;
error:
  if (host) {
    host_delete(host);
  }
  return result;
}
