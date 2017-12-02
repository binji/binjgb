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

#define DESTROY_IF(ptr, destroy) \
  if (ptr) {                     \
    destroy(ptr);                \
    ptr = NULL;                  \
  }

typedef f32 HostAudioSample;
#define AUDIO_SPEC_FORMAT AUDIO_F32
#define AUDIO_SPEC_CHANNELS 2
#define AUDIO_SPEC_SAMPLE_SIZE sizeof(HostAudioSample)
#define AUDIO_FRAME_SIZE (AUDIO_SPEC_SAMPLE_SIZE * AUDIO_SPEC_CHANNELS)
#define AUDIO_CONVERT_SAMPLE_FROM_U8(X, fvol) ((fvol) * (X) * (1 / 256.0f))
#define AUDIO_TARGET_QUEUED_SIZE (2 * host->audio.spec.size)
#define AUDIO_MAX_QUEUED_SIZE (5 * host->audio.spec.size)
#define JOYPAD_BUFFER_DEFAULT_CAPACITY 4096

typedef struct {
  GLint internal_format;
  GLenum format;
  GLenum type;
} GLTextureFormat;

typedef struct {
  Cycles cycles;
  u8 buttons;
  u8 padding[3];
} JoypadState;

typedef struct JoypadBuffer {
  JoypadState* data;
  size_t size;
  size_t capacity;
  struct JoypadBuffer *next, *prev;
} JoypadBuffer;

typedef struct {
  SDL_AudioDeviceID dev;
  SDL_AudioSpec spec;
  u8* buffer; /* Size is spec.size. */
  Bool ready;
  f32 volume; /* [0..1] */
} HostAudio;

typedef struct Host {
  HostInit init;
  HostConfig config;
  HostHookContext hook_ctx;
  SDL_Window* window;
  SDL_GLContext gl_context;
  HostAudio audio;
  u64 start_counter;
  u64 performance_frequency;
  struct HostUI* ui;
  HostTexture* fb_texture;
  JoypadBuffer joypad_buffer_sentinel;
  JoypadButtons last_joypad_buttons;
} Host;

static struct Emulator* host_get_emulator(Host* host) {
  return host->hook_ctx.e;
}

Result host_init_video(Host* host) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  host->window = SDL_CreateWindow(
      "binjgb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_WIDTH * host->init.render_scale,
      SCREEN_HEIGHT * host->init.render_scale,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  CHECK_MSG(host->window != NULL, "SDL_CreateWindow failed.\n");

  host->gl_context = SDL_GL_CreateContext(host->window);
  GLint major;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  CHECK_MSG(major >= 2, "Unable to create GL context at version 2.\n");
  host_gl_init_procs();

  host->ui = host_ui_new(host->window);
  host->fb_texture = host_create_texture(host, SCREEN_WIDTH, SCREEN_HEIGHT,
                                         HOST_TEXTURE_FORMAT_RGBA);
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

  host->audio.buffer = calloc(1, host->audio.spec.size);
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
  struct Emulator* e = host->hook_ctx.e;
  Bool running = TRUE;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    host_ui_event(host->ui, &event);

    switch (event.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        HostKeycode keycode = scancode_to_keycode(event.key.keysym.scancode);
        if (event.type == SDL_KEYDOWN) {
          HOOK(key_down, keycode);
        } else {
          HOOK(key_up, keycode);
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
  host_ui_begin_frame(host->ui, host->fb_texture);
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
  struct Emulator* e = host->hook_ctx.e;
  HostAudio* audio = &host->audio;
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

static JoypadBuffer* alloc_joypad_buffer(size_t capacity) {
  JoypadBuffer* buffer = malloc(sizeof(JoypadBuffer));
  ZERO_MEMORY(*buffer);
  buffer->data = malloc(capacity * sizeof(JoypadState));
  buffer->capacity = capacity;
  return buffer;
}

static JoypadState* host_alloc_joypad_state(Host* host) {
  JoypadBuffer* tail = host->joypad_buffer_sentinel.prev;
  if (tail->size >= tail->capacity) {
    JoypadBuffer* new_buffer =
        alloc_joypad_buffer(JOYPAD_BUFFER_DEFAULT_CAPACITY);
    new_buffer->next = &host->joypad_buffer_sentinel;
    new_buffer->prev = tail;
    host->joypad_buffer_sentinel.prev = tail->next = new_buffer;
    tail = new_buffer;
  }
  return  &tail->data[tail->size++];
}

static u8 pack_buttons(JoypadButtons* buttons) {
  return (buttons->down << 7) | (buttons->up << 6) | (buttons->left << 5) |
         (buttons->right << 4) | (buttons->start << 3) |
         (buttons->select << 2) | (buttons->B << 1) | (buttons->A << 0);
}

static Bool joypad_buttons_are_equal(JoypadButtons* lhs, JoypadButtons* rhs) {
  return lhs->down == rhs->down && lhs->up == rhs->up &&
         lhs->left == rhs->left && lhs->right == rhs->right &&
         lhs->start == rhs->start && lhs->select == rhs->select &&
         lhs->B == rhs->B && lhs->A == rhs->A;
}

static void host_store_joypad(Host* host, JoypadButtons* buttons) {
  JoypadState* state = host_alloc_joypad_state(host);
  state->cycles = emulator_get_cycles(host_get_emulator(host));
  state->buttons = pack_buttons(buttons);
  host->last_joypad_buttons = *buttons;
}

static void host_store_joypad_if_new(Host* host, JoypadButtons* buttons) {
  if (!joypad_buttons_are_equal(buttons, &host->last_joypad_buttons)) {
    host_store_joypad(host, buttons);
  }
}

static void joypad_callback(JoypadButtons* joyp, void* user_data) {
  Host* host = user_data;
  const u8* state = SDL_GetKeyboardState(NULL);
  joyp->up = state[SDL_SCANCODE_UP];
  joyp->down = state[SDL_SCANCODE_DOWN];
  joyp->left = state[SDL_SCANCODE_LEFT];
  joyp->right = state[SDL_SCANCODE_RIGHT];
  joyp->B = state[SDL_SCANCODE_Z];
  joyp->A = state[SDL_SCANCODE_X];
  joyp->start = state[SDL_SCANCODE_RETURN];
  joyp->select = state[SDL_SCANCODE_BACKSPACE];

  host_store_joypad_if_new(host, joyp);
}

static void host_init_joypad(Host* host, struct Emulator* e) {
  emulator_set_joypad_callback(e, joypad_callback, host);
  host->joypad_buffer_sentinel.next = host->joypad_buffer_sentinel.prev =
      &host->joypad_buffer_sentinel;
  ZERO_MEMORY(host->last_joypad_buttons);
  host_store_joypad(host, &host->last_joypad_buttons);
}

Result host_init(Host* host, struct Emulator* e) {
  CHECK_MSG(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0,
            "SDL_init failed.\n");
  host_init_time(host);
  CHECK(SUCCESS(host_init_video(host)));
  CHECK(SUCCESS(host_init_audio(host)));
  host_init_joypad(host, e);
  return OK;
  ON_ERROR_RETURN;
}

static void host_handle_event(struct Host* host, EmulatorEvent event) {
  struct Emulator* e = host->hook_ctx.e;
  if (event & EMULATOR_EVENT_NEW_FRAME) {
    host_upload_texture(host, host->fb_texture, SCREEN_WIDTH, SCREEN_HEIGHT,
                        *emulator_get_frame_buffer(e));
  }
  if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    host_render_audio(host);
    HOOK0(audio_buffer_full);
  }
}

void host_run_ms(struct Host* host, f64 delta_ms) {
  struct Emulator* e = host->hook_ctx.e;
  Cycles delta_cycles = (Cycles)(delta_ms * CPU_CYCLES_PER_SECOND / 1000);
  Cycles until_cycles = emulator_get_cycles(e) + delta_cycles;
  while (1) {
    EmulatorEvent event = emulator_run_until(e, until_cycles);
    host_handle_event(host, event);
    if (event & EMULATOR_EVENT_UNTIL_CYCLES) {
      break;
    }
  }
}

void host_step(struct Host* host) {
  struct Emulator* e = host->hook_ctx.e;
  EmulatorEvent event = emulator_step(e);
  host_handle_event(host, event);
}

Host* host_new(const HostInit *init, struct Emulator* e) {
  Host* host = calloc(1, sizeof(Host));
  host->init = *init;
  host->hook_ctx.host = host;
  host->hook_ctx.e = e;
  host->hook_ctx.user_data = host->init.hooks.user_data;
  CHECK(SUCCESS(host_init(host, e)));
  return host;
error:
  free(host);
  return NULL;
}

static void host_destroy_joypad(Host* host) {
  JoypadBuffer* current = host->joypad_buffer_sentinel.next;
  while (current != &host->joypad_buffer_sentinel) {
    JoypadBuffer* next = current->next;
    free(current->data);
    free(current);
    current = next;
  }
}

void host_delete(Host* host) {
  if (host) {
    host_destroy_texture(host, host->fb_texture);
    SDL_GL_DeleteContext(host->gl_context);
    SDL_DestroyWindow(host->window);
    SDL_Quit();
    host_destroy_joypad(host);
    free(host->audio.buffer);
    free(host);
  }
}

void host_set_config(struct Host* host, const HostConfig* new_config) {
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

HostConfig host_get_config(struct Host* host) {
  return host->config;
}

f64 host_get_monitor_refresh_ms(struct Host* host) {
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

void host_set_palette(struct Host* host, RGBA palette[4]) {
  host_ui_set_palette(host->ui, palette);
}

void host_enable_palette(struct Host* host, Bool enabled) {
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

HostTexture* host_get_frame_buffer_texture(struct Host* host) {
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

HostTexture* host_create_texture(struct Host* host, int w, int h,
                                 HostTextureFormat format) {
  HostTexture* texture = malloc(sizeof(HostTexture));
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

void host_upload_texture(struct Host* host, HostTexture* texture, int w, int h,
                         const void* data) {
  assert(w <= texture->width);
  assert(h <= texture->height);
  glBindTexture(GL_TEXTURE_2D, texture->handle);
  GLTextureFormat gl_format = host_apply_texture_format(texture->format);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, gl_format.format,
                  gl_format.type, data);
}

void host_destroy_texture(struct Host* host, HostTexture* texture) {
  GLuint tex = texture->handle;
  glDeleteTextures(1, &tex);
  free(texture);
}
