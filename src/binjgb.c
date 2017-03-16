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

static const char* s_save_state_filename;

static void write_state(HostHookContext* ctx) {
  emulator_write_state_to_file(ctx->e, s_save_state_filename);
}

static void read_state(HostHookContext* ctx) {
  emulator_read_state_from_file(ctx->e, s_save_state_filename);
}

HostHooks s_hooks = {
  .write_state = write_state,
  .read_state = read_state,
};

int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  --argc; ++argv;
  int result = 1;
  struct Host* host = NULL;
  struct Emulator* e = NULL;

  CHECK_MSG(argc == 1, "no rom file given.\n");
  const char* rom_filename = argv[0];

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom_filename = rom_filename;
  emulator_init.audio_frequency = audio_frequency;
  emulator_init.audio_frames = audio_frames;
  e = emulator_new(&emulator_init);

  HostInit host_init;
  ZERO_MEMORY(host_init);
  host_init.render_scale = 4;
  host_init.audio_frequency = audio_frequency;
  host_init.audio_frames = audio_frames;
  host = host_new(&host_init, e);

  const char* save_filename = replace_extension(rom_filename, SAVE_EXTENSION);
  s_save_state_filename = replace_extension(rom_filename, SAVE_STATE_EXTENSION);
  emulator_read_ext_ram_from_file(e, save_filename);

  while (host_poll_events(host)) {
    HostConfig config = host_get_config(host);
    if (config.paused) {
      host_delay(host, VIDEO_FRAME_MS);
      continue;
    }

    EmulatorEvent event = emulator_run(e);
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
error:
  if (host) {
    host_delete(host);
  }
  if (e) {
    emulator_delete(e);
  }
  return result;
}
