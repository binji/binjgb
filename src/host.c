/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "host.h"

#include <assert.h>

#include "emulator.h"
#include "host-gl.h"
#include "host-ui.h"
#include "joypad.h"
#include "rewind.h"

#define HOOK0(name)                           \
  do                                          \
    if (host->init.hooks.name) {              \
      host->init.hooks.name(&host->hook_ctx); \
    }                                         \
  while (0)

#define HOOK(name, ...)                                    \
  do                                                       \
    if (host->init.hooks.name) {                           \
      host->init.hooks.name(&host->hook_ctx, __VA_ARGS__); \
    }                                                      \
  while (0)

typedef f32 HostAudioSample;
#define AUDIO_SPEC_FORMAT AUDIO_F32
#define AUDIO_SPEC_CHANNELS 2
#define AUDIO_SPEC_SAMPLE_SIZE sizeof(HostAudioSample)
#define AUDIO_FRAME_SIZE (AUDIO_SPEC_SAMPLE_SIZE * AUDIO_SPEC_CHANNELS)
#define AUDIO_CONVERT_SAMPLE_FROM_U8(X, fvol) ((fvol) * (X) * (1 / 255.0f))
#define AUDIO_TARGET_QUEUED_SIZE (2 * host->audio.spec.size)
#define AUDIO_MAX_QUEUED_SIZE (5 * host->audio.spec.size)

typedef struct {
  GLint internal_format;
  GLenum format;
  GLenum type;
} GLTextureFormat;

typedef struct {
  SDL_AudioDeviceID dev;
  SDL_AudioSpec spec;
  u8* buffer; /* Size is spec.size. */
  Bool ready;
  f32 volume; /* [0..1] */
} Audio;

typedef struct {
  RewindResult rewind_result;
  JoypadPlayback joypad_playback;
  Bool rewinding;
} RewindState;

typedef struct Host {
  HostInit init;
  HostConfig config;
  HostHookContext hook_ctx;
  SDL_Window* window;
  SDL_GLContext gl_context;
  SDL_GameController* controller;
  Audio audio;
  u64 start_counter;
  u64 performance_frequency;
  struct HostUI* ui;
  HostTexture* fb_texture;
  HostTexture* sgb_fb_texture;
  JoypadBuffer* joypad_buffer;
  RewindBuffer* rewind_buffer;
  RewindState rewind_state;
  JoypadPlayback joypad_playback;
  Ticks last_ticks;
  Bool key_state[HOST_KEYCODE_COUNT];
} Host;

static Emulator* host_get_emulator(Host* host) {
  return host->hook_ctx.e;
}

static Result host_init_video(Host* host) {
  Emulator* e = host_get_emulator(host);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  int width = host->init.use_sgb_border ? SGB_SCREEN_WIDTH : SCREEN_WIDTH;
  int height = host->init.use_sgb_border ? SGB_SCREEN_HEIGHT : SCREEN_HEIGHT;

  host->window = SDL_CreateWindow(
      "binjgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      width * host->init.render_scale, height * host->init.render_scale,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  CHECK_MSG(host->window != NULL, "SDL_CreateWindow failed.\n");

  host->gl_context = SDL_GL_CreateContext(host->window);
  SDL_GL_SetSwapInterval(1);
  GLint major;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  CHECK_MSG(major >= 2, "Unable to create GL context at version 2.\n");
  host_gl_init_procs();

  host->ui = host_ui_new(host->window, host->init.use_sgb_border);
  host->fb_texture = host_create_texture(host, SCREEN_WIDTH, SCREEN_HEIGHT,
                                         HOST_TEXTURE_FORMAT_RGBA);
  if (host->init.use_sgb_border) {
    host->sgb_fb_texture = host_create_texture(
        host, SGB_SCREEN_WIDTH, SGB_SCREEN_HEIGHT, HOST_TEXTURE_FORMAT_RGBA);
  }
  return OK;
error:
  SDL_Quit();
  return ERROR;
}

static void host_init_time(Host* host) {
  host->performance_frequency = SDL_GetPerformanceFrequency();
  host->start_counter = SDL_GetPerformanceCounter();
}

f64 host_get_time_ms(Host* host) {
  u64 now = SDL_GetPerformanceCounter();
  return (f64)(now - host->start_counter) * 1000 / host->performance_frequency;
}

static Result host_init_audio(Host* host) {
  host->audio.ready = FALSE;
  host_set_audio_volume(host, host->init.audio_volume);
  SDL_AudioSpec want;
  want.freq = host->init.audio_frequency;
  want.format = AUDIO_SPEC_FORMAT;
  want.channels = AUDIO_SPEC_CHANNELS;
  want.samples = host->init.audio_frames * AUDIO_SPEC_CHANNELS;
  want.callback = NULL;
  want.userdata = host;
  host->audio.dev = SDL_OpenAudioDevice(NULL, 0, &want, &host->audio.spec, 0);
  CHECK_MSG(host->audio.dev != 0, "SDL_OpenAudioDevice failed.\n");

  host->audio.buffer = xcalloc(1, host->audio.spec.size);
  CHECK_MSG(host->audio.buffer != NULL, "Audio buffer allocation failed.\n");
  return OK;
  ON_ERROR_RETURN;
}

static HostKeycode scancode_to_keycode(SDL_Scancode scancode) {
  static HostKeycode s_map[SDL_NUM_SCANCODES] = {
#define V(NAME) [SDL_SCANCODE_##NAME] = HOST_KEYCODE_##NAME,
    FOREACH_HOST_KEYCODE(V)
#undef V
  };
  assert(scancode < SDL_NUM_SCANCODES);
  return s_map[scancode];
}

Bool host_poll_events(Host* host) {
  Emulator* e = host_get_emulator(host);
  Bool running = TRUE;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    host_ui_event(host->ui, &event);

    switch (event.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        HostKeycode keycode = scancode_to_keycode(event.key.keysym.scancode);
        if (!host_ui_capture_keyboard(host->ui)) {
          host->key_state[keycode] = event.type == SDL_KEYDOWN;
        }
        if (event.type == SDL_KEYDOWN) {
          HOOK(key_down, keycode);
        } else {
          HOOK(key_up, keycode);
        }
        break;
      }
      case SDL_CONTROLLERDEVICEADDED:
        if (!host->controller) {
          host->controller = SDL_GameControllerOpen(event.cdevice.which);
        }
        break;
      case SDL_CONTROLLERDEVICEREMOVED: {
        if (host->controller) {
          SDL_GameControllerClose(host->controller);
          host->controller = NULL;
        }
        break;
      }
      case SDL_QUIT:
        running = FALSE;
        break;
      default: break;
    }
  }

  return running;
}

void host_begin_video(Host* host) {
  host_ui_begin_frame(host->ui, host->fb_texture, host->sgb_fb_texture);
}

void host_end_video(Host* host) {
  host_ui_end_frame(host->ui);
}

void host_reset_audio(Host* host) {
  host->audio.ready = FALSE;
  SDL_ClearQueuedAudio(host->audio.dev);
  SDL_PauseAudioDevice(host->audio.dev, 1);
}

void host_set_audio_volume(Host* host, f32 volume) {
  host->audio.volume = CLAMP(volume, 0, 1);
}

void host_render_audio(Host* host) {
  Emulator* e = host_get_emulator(host);
  Audio* audio = &host->audio;
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);

  size_t src_frames = audio_buffer_get_frames(audio_buffer);
  size_t max_dst_frames = audio->spec.size / AUDIO_FRAME_SIZE;
  size_t frames = MIN(src_frames, max_dst_frames);
  u8* src = audio_buffer->data;
  HostAudioSample* dst = (HostAudioSample*)audio->buffer;
  HostAudioSample* dst_end = dst + frames * AUDIO_SPEC_CHANNELS;
  assert((u8*)dst_end <= audio->buffer + audio->spec.size);
  f32 volume = audio->volume;
  size_t i;
  for (i = 0; i < frames; i++) {
    assert(dst + 2 <= dst_end);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++, volume);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++, volume);
  }
  u32 queued_size = SDL_GetQueuedAudioSize(audio->dev);
  if (queued_size < AUDIO_MAX_QUEUED_SIZE) {
    u32 buffer_size = (u8*)dst_end - (u8*)audio->buffer;
    SDL_QueueAudio(audio->dev, audio->buffer, buffer_size);
    HOOK(audio_add_buffer, queued_size, queued_size + buffer_size);
    queued_size += buffer_size;
  }
  if (!audio->ready && queued_size >= AUDIO_TARGET_QUEUED_SIZE) {
    HOOK(audio_buffer_ready, queued_size);
    audio->ready = TRUE;
    SDL_PauseAudioDevice(audio->dev, 0);
  }
}

static void joypad_callback(JoypadButtons* joyp, void* user_data) {
  Host* host = user_data;
  joyp->up = host->key_state[HOST_KEYCODE_UP];
  joyp->down = host->key_state[HOST_KEYCODE_DOWN];
  joyp->left = host->key_state[HOST_KEYCODE_LEFT];
  joyp->right = host->key_state[HOST_KEYCODE_RIGHT];
  joyp->B = host->key_state[HOST_KEYCODE_Z];
  joyp->A = host->key_state[HOST_KEYCODE_X];
  joyp->start = host->key_state[HOST_KEYCODE_RETURN];
  joyp->select = host->key_state[HOST_KEYCODE_TAB];

  /* Prefer controller input, if any */
  if (host->controller) {
#define AXIS(gb, dpad, axis, op, value)                                        \
  joyp->gb = joyp->gb ||                                                       \
             SDL_GameControllerGetButton(host->controller,                     \
                                         SDL_CONTROLLER_BUTTON_DPAD_##dpad) || \
             (SDL_GameControllerGetAxis(host->controller,                      \
                                        SDL_CONTROLLER_AXIS_##axis) op value)
#define BUTTON(gb, sdl)                               \
  joyp->gb = joyp->gb || SDL_GameControllerGetButton( \
                             host->controller, SDL_CONTROLLER_BUTTON_##sdl)
    AXIS(up, UP, LEFTY, <=, -0x4000);
    AXIS(down, DOWN, LEFTY, >=, 0x3fff);
    AXIS(left, LEFT, LEFTX, <=, -0x4000);
    AXIS(right, RIGHT, LEFTX, >=, 0x3fff);
    BUTTON(B, X); /* On my gamepad, X is nicer for this than B. */
    BUTTON(A, A);
    BUTTON(start, START);
    BUTTON(select, BACK);
#undef GAMEPAD
  }

  Ticks ticks = emulator_get_ticks(host_get_emulator(host));
  joypad_append_if_new(host->joypad_buffer, joyp, ticks);
}

static Result host_init_joypad(Host* host, Emulator* e) {
  if (host->init.joypad_filename) {
    FileData file_data;
    CHECK(SUCCESS(file_read(host->init.joypad_filename, &file_data)));
    CHECK(SUCCESS(joypad_read(&file_data, &host->joypad_buffer)));
    file_data_delete(&file_data);
    emulator_set_joypad_playback_callback(
        host_get_emulator(host), host->joypad_buffer, &host->joypad_playback);
  } else {
    emulator_set_joypad_callback(e, joypad_callback, host);
    host->joypad_buffer = joypad_new();
  }
  return OK;
  ON_ERROR_RETURN;
}

static void append_rewind_state(Host* host) {
  if (host->rewind_state.rewinding) {
    return;
  }

  rewind_append(host->rewind_buffer, host_get_emulator(host));
}

Ticks host_get_rewind_oldest_ticks(struct Host* host) {
  return rewind_get_oldest_ticks(host->rewind_buffer);
}

Ticks host_get_rewind_newest_ticks(struct Host* host) {
  return rewind_get_newest_ticks(host->rewind_buffer);
}

JoypadStats host_get_joypad_stats(struct Host* host) {
  return joypad_get_stats(host->joypad_buffer);
}

RewindStats host_get_rewind_stats(struct Host* host) {
  return rewind_get_stats(host->rewind_buffer);
}

Result host_write_joypad_to_file(struct Host* host, const char* filename) {
  Result result = ERROR;
  FileData file_data;
  joypad_init_file_data(host->joypad_buffer, &file_data);
  joypad_write(host->joypad_buffer, &file_data);
  CHECK(SUCCESS(file_write(filename, &file_data)));
  result = OK;
error:
  file_data_delete(&file_data);
  return result;
}

static void host_handle_event(Host* host, EmulatorEvent event) {
  Emulator* e = host_get_emulator(host);
  if (event & EMULATOR_EVENT_NEW_FRAME) {
    host_upload_texture(host, host->fb_texture, SCREEN_WIDTH, SCREEN_HEIGHT,
                        *emulator_get_frame_buffer(e));
    if (host->init.use_sgb_border) {
      host_upload_texture(host, host->sgb_fb_texture, SGB_SCREEN_WIDTH,
                          SGB_SCREEN_HEIGHT, *emulator_get_sgb_frame_buffer(e));
    }

    append_rewind_state(host);
  }
  if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    host_render_audio(host);
    HOOK0(audio_buffer_full);
  }
}

static EmulatorEvent host_run_until_ticks(struct Host* host, Ticks ticks) {
  Emulator* e = host_get_emulator(host);
  assert(emulator_get_ticks(e) <= ticks);
  EmulatorEvent event;
  do {
    event = emulator_run_until(e, ticks);
    host_handle_event(host, event);
  } while (!(event & (EMULATOR_EVENT_UNTIL_TICKS | EMULATOR_EVENT_BREAKPOINT |
                      EMULATOR_EVENT_INVALID_OPCODE)));
  return event;
}

void host_begin_rewind(Host* host) {
  assert(!host->rewind_state.rewinding);
  host->rewind_state.rewinding = TRUE;
}

Result host_rewind_to_ticks(Host* host, Ticks ticks) {
  assert(host->rewind_state.rewinding);

  RewindResult* result = &host->rewind_state.rewind_result;
  CHECK(SUCCESS(rewind_to_ticks(host->rewind_buffer, ticks, result)));

  Emulator* e = host_get_emulator(host);
  CHECK(SUCCESS(emulator_read_state(e, &result->file_data)));
  assert(emulator_get_ticks(e) == result->info->ticks);

  if (emulator_get_ticks(e) < ticks) {
    /* Save old joypad callback. */
    JoypadCallbackInfo old_jci = emulator_get_joypad_callback(e);
    emulator_set_joypad_playback_callback(e, host->joypad_buffer,
                                          &host->rewind_state.joypad_playback);
    host_run_until_ticks(host, ticks);
    /* Restore old joypad callback. */
    emulator_set_joypad_callback(e, old_jci.callback, old_jci.user_data);
  }

  return OK;
  ON_ERROR_RETURN;
}

void host_end_rewind(Host* host) {
  Ticks ticks = emulator_get_ticks(host_get_emulator(host));
  assert(host->rewind_state.rewinding);

  if (host->rewind_state.rewind_result.info) {
    Emulator* e = host_get_emulator(host);
    rewind_truncate_to(host->rewind_buffer, e,
                       &host->rewind_state.rewind_result);
    if (!host->init.joypad_filename) {
      joypad_truncate_to(host->joypad_buffer,
                         host->rewind_state.joypad_playback.current);
      /* Append the current joypad state. */
      JoypadButtons buttons;
      joypad_callback(&buttons, host);
    }
    host->last_ticks = emulator_get_ticks(e);
  }

  memset(&host->rewind_state, 0, sizeof(host->rewind_state));
}

Bool host_is_rewinding(Host* host) {
  return host->rewind_state.rewinding;
}

Result host_init(Host* host, Emulator* e) {
  CHECK_MSG(
      SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0,
      "SDL_init failed.\n");
  host_init_time(host);
  CHECK(SUCCESS(host_init_video(host)));
  CHECK(SUCCESS(host_init_audio(host)));
  host_init_joypad(host, e);
  host->rewind_buffer = rewind_new(&host->init.rewind, e);
  host->last_ticks = emulator_get_ticks(e);
  return OK;
  ON_ERROR_RETURN;
}

EmulatorEvent host_run_ms(struct Host* host, f64 delta_ms) {
  assert(!host->rewind_state.rewinding);
  Emulator* e = host_get_emulator(host);
  Ticks delta_ticks = (Ticks)(delta_ms * CPU_TICKS_PER_SECOND / 1000);
  Ticks until_ticks = emulator_get_ticks(e) + delta_ticks;
  EmulatorEvent event = host_run_until_ticks(host, until_ticks);
  host->last_ticks = emulator_get_ticks(e);
  return event;
}

EmulatorEvent host_step(Host* host) {
  assert(!host->rewind_state.rewinding);
  Emulator* e = host_get_emulator(host);
  EmulatorEvent event = emulator_step(e);
  host_handle_event(host, event);
  host->last_ticks = emulator_get_ticks(e);
  return event;
}

Host* host_new(const HostInit *init, Emulator* e) {
  Host* host = xcalloc(1, sizeof(Host));
  host->init = *init;
  host->hook_ctx.host = host;
  host->hook_ctx.e = e;
  host->hook_ctx.user_data = host->init.hooks.user_data;
  CHECK(SUCCESS(host_init(host, e)));
  return host;
error:
  xfree(host);
  return NULL;
}

void host_delete(Host* host) {
  if (host) {
    if (host->init.use_sgb_border) {
      host_destroy_texture(host, host->sgb_fb_texture);
    }
    host_destroy_texture(host, host->fb_texture);
    SDL_GL_DeleteContext(host->gl_context);
    SDL_DestroyWindow(host->window);
    SDL_Quit();
    joypad_delete(host->joypad_buffer);
    rewind_delete(host->rewind_buffer);
    xfree(host->audio.buffer);
    xfree(host);
  }
}

void host_set_config(Host* host, const HostConfig* new_config) {
  if (host->config.no_sync != new_config->no_sync) {
    SDL_GL_SetSwapInterval(new_config->no_sync ? 0 : 1);
    host_reset_audio(host);
  }

  if (host->config.fullscreen != new_config->fullscreen) {
    SDL_SetWindowFullscreen(host->window, new_config->fullscreen
                                              ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                              : 0);
  }
  host->config = *new_config;
}

HostConfig host_get_config(Host* host) {
  return host->config;
}

f64 host_get_monitor_refresh_ms(Host* host) {
  int refresh_rate_hz = 0;
  SDL_DisplayMode mode;
  if (SDL_GetWindowDisplayMode(host->window, &mode) == 0) {
    refresh_rate_hz = mode.refresh_rate;
  }
  if (refresh_rate_hz == 0) {
    refresh_rate_hz = 60;
  }
  return 1000.0 / refresh_rate_hz;
}

void host_set_palette(Host* host, RGBA palette[4]) {
  host_ui_set_palette(host->ui, palette);
}

void host_enable_palette(Host* host, Bool enabled) {
  host_ui_enable_palette(host->ui, enabled);
}

static u32 next_power_of_two(u32 n) {
  assert(n != 0);
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return n + 1;
}

HostTexture* host_get_frame_buffer_texture(Host* host) {
  return host->fb_texture;
}

static GLTextureFormat host_apply_texture_format(HostTextureFormat format) {
  GLTextureFormat result;
  switch (format) {
    case HOST_TEXTURE_FORMAT_RGBA:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      result.internal_format = GL_RGBA8;
      result.format = GL_RGBA;
      result.type = GL_UNSIGNED_BYTE;
      break;

    case HOST_TEXTURE_FORMAT_U8:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      result.internal_format = GL_R8;
      result.format = GL_RED;
      result.type = GL_UNSIGNED_BYTE;
      break;

    default:
      assert(0);
  }

  return result;
}

HostTexture* host_create_texture(Host* host, int w, int h,
                                 HostTextureFormat format) {
  HostTexture* texture = xmalloc(sizeof(HostTexture));
  texture->width = next_power_of_two(w);
  texture->height = next_power_of_two(h);

  GLuint handle;
  glGenTextures(1, &handle);
  glBindTexture(GL_TEXTURE_2D, handle);
  GLTextureFormat gl_format = host_apply_texture_format(format);
  glTexImage2D(GL_TEXTURE_2D, 0, gl_format.internal_format, texture->width,
               texture->height, 0, gl_format.format, gl_format.type, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  texture->handle = handle;
  texture->format = format;
  return texture;
}

void host_upload_texture(Host* host, HostTexture* texture, int w, int h,
                         const void* data) {
  assert(w <= texture->width);
  assert(h <= texture->height);
  glBindTexture(GL_TEXTURE_2D, texture->handle);
  GLTextureFormat gl_format = host_apply_texture_format(texture->format);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, gl_format.format,
                  gl_format.type, data);
}

void host_destroy_texture(Host* host, HostTexture* texture) {
  GLuint tex = texture->handle;
  glDeleteTextures(1, &tex);
  xfree(texture);
}

void host_render_screen_overlay(struct Host* host,
                                struct HostTexture* texture) {
  host_ui_render_screen_overlay(host->ui, texture);
}

Ticks host_oldest_ticks(Host* host) {
  return 0;
}

Ticks host_newest_ticks(Host* host) {
  return host->last_ticks;
}
