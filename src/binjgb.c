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
static Bool s_running = TRUE;
static Bool s_step_frame = FALSE;
static Bool s_paused = FALSE;

static void key_down(HostHookContext* ctx, HostKeycode code) {
  EmulatorConfig emu_config = emulator_get_config(ctx->e);
  HostConfig host_config = host_get_config(ctx->host);

  switch (code) {
    case HOST_KEYCODE_1: emu_config.disable_sound[APU_CHANNEL1] ^= 1; break;
    case HOST_KEYCODE_2: emu_config.disable_sound[APU_CHANNEL2] ^= 1; break;
    case HOST_KEYCODE_3: emu_config.disable_sound[APU_CHANNEL3] ^= 1; break;
    case HOST_KEYCODE_4: emu_config.disable_sound[APU_CHANNEL4] ^= 1; break;
    case HOST_KEYCODE_B: emu_config.disable_bg ^= 1; break;
    case HOST_KEYCODE_W: emu_config.disable_window ^= 1; break;
    case HOST_KEYCODE_O: emu_config.disable_obj ^= 1; break;
    case HOST_KEYCODE_F6:
      emulator_write_state_to_file(ctx->e, s_save_state_filename);
      break;
    case HOST_KEYCODE_F9:
      emulator_read_state_from_file(ctx->e, s_save_state_filename);
      break;
    case HOST_KEYCODE_N: s_step_frame = TRUE; s_paused = FALSE; break;
    case HOST_KEYCODE_SPACE: s_paused ^= 1; break;
    case HOST_KEYCODE_ESCAPE: s_running = FALSE; break;
    case HOST_KEYCODE_TAB: host_config.no_sync = TRUE; break;
    default: return;
  }

  emulator_set_config(ctx->e, &emu_config);
  host_set_config(ctx->host, &host_config);
}

static void key_up(HostHookContext* ctx, HostKeycode code) {
  HostConfig host_config = host_get_config(ctx->host);

  switch (code) {
    case HOST_KEYCODE_TAB: host_config.no_sync = FALSE; break;
    case HOST_KEYCODE_F11: host_config.fullscreen ^= 1; break;
    default: return;
  }

  host_set_config(ctx->host, &host_config);
}

int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  --argc; ++argv;
  int result = 1;
  struct Host* host = NULL;
  struct Emulator* e = NULL;

  CHECK_MSG(argc == 1, "no rom file given.\n");
  const char* rom_filename = argv[0];

  FileData rom;
  CHECK(SUCCESS(file_read(rom_filename, &rom)));

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = audio_frequency;
  emulator_init.audio_frames = audio_frames;
  e = emulator_new(&emulator_init);
  CHECK(e != NULL);

  HostInit host_init;
  ZERO_MEMORY(host_init);
  host_init.hooks.key_down = key_down;
  host_init.hooks.key_up = key_up;
  host_init.render_scale = 4;
  host_init.audio_frequency = audio_frequency;
  host_init.audio_frames = audio_frames;
  host = host_new(&host_init, e);
  CHECK(host != NULL);

  const char* save_filename = replace_extension(rom_filename, SAVE_EXTENSION);
  s_save_state_filename = replace_extension(rom_filename, SAVE_STATE_EXTENSION);
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (s_running && host_poll_events(host)) {
    if (!s_paused) {
      host_run_ms(host, refresh_ms);
      if (s_step_frame) {
        host_reset_audio(host);
        s_paused = TRUE;
        s_step_frame = FALSE;
      }
    }
    host_begin_video(host);
    host_end_video(host);
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
