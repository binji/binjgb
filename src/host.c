/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "host.h"

#include <assert.h>

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include "emulator.h"

#define HOOK0(name)                             \
  do                                            \
    if (host->config.hooks.name) {              \
      host->config.hooks.name(&host->hook_ctx); \
    }                                           \
  while (0)

#define HOOK(name, ...)                                      \
  do                                                         \
    if (host->config.hooks.name) {                           \
      host->config.hooks.name(&host->hook_ctx, __VA_ARGS__); \
    }                                                        \
  while (0)

#define FOREACH_GLEXT_PROC(V)                                    \
  V(glAttachShader, PFNGLATTACHSHADERPROC)                       \
  V(glBindBuffer, PFNGLBINDBUFFERPROC)                           \
  V(glBufferData, PFNGLBUFFERDATAPROC)                           \
  V(glCompileShader, PFNGLCOMPILESHADERPROC)                     \
  V(glCreateProgram, PFNGLCREATEPROGRAMPROC)                     \
  V(glCreateShader, PFNGLCREATESHADERPROC)                       \
  V(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
  V(glGenBuffers, PFNGLGENBUFFERSPROC)                           \
  V(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC)             \
  V(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)             \
  V(glGetProgramiv, PFNGLGETPROGRAMIVPROC)                       \
  V(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)               \
  V(glGetShaderiv, PFNGLGETSHADERIVPROC)                         \
  V(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)           \
  V(glLinkProgram, PFNGLLINKPROGRAMPROC)                         \
  V(glShaderSource, PFNGLSHADERSOURCEPROC)                       \
  V(glUniform1i, PFNGLUNIFORM1IPROC)                             \
  V(glUseProgram, PFNGLUSEPROGRAMPROC)                           \
  V(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define V(name, type) type name;
FOREACH_GLEXT_PROC(V)
#undef V

#define DESTROY_IF(ptr, destroy) \
  if (ptr) {                     \
    destroy(ptr);                \
    ptr = NULL;                  \
  }

#define AUDIO_SPEC_FORMAT AUDIO_U16
#define AUDIO_SPEC_CHANNELS 2
#define AUDIO_SPEC_SAMPLE_SIZE sizeof(HostAudioSample)
#define AUDIO_FRAME_SIZE (AUDIO_SPEC_SAMPLE_SIZE * AUDIO_SPEC_CHANNELS)
#define AUDIO_CONVERT_SAMPLE_FROM_U8(X) ((X) << 8)
typedef u16 HostAudioSample;
/* Try to keep the audio buffer filled to |number of frames| *
 * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER frames. */
#define AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER 1.5
#define AUDIO_MAX_BUFFER_SIZE_MULTIPLIER 4
/* One buffer will be requested every AUDIO_BUFFER_REFILL_MS milliseconds. */
#define AUDIO_BUFFER_REFILL_MS                        \
  ((host->audio.spec.samples / AUDIO_SPEC_CHANNELS) * \
   MILLISECONDS_PER_SECOND / host->audio.spec.freq)
/* If the emulator is running behind by AUDIO_MAX_SLOW_DESYNC_MS milliseconds
 * (or ahead by AUDIO_MAX_FAST_DESYNC_MS), it won't try to catch up, and
 * instead just forcibly resync. */
#define AUDIO_MAX_SLOW_DESYNC_MS (0.5 * AUDIO_BUFFER_REFILL_MS)
#define AUDIO_MAX_FAST_DESYNC_MS (2 * AUDIO_BUFFER_REFILL_MS)

#define CHECK_LOG(var, kind, status_enum, kind_str)      \
  do {                                                   \
    GLint status;                                        \
    glGet##kind##iv(var, status_enum, &status);          \
    if (!status) {                                       \
      GLint length;                                      \
      glGet##kind##iv(var, GL_INFO_LOG_LENGTH, &length); \
      GLchar* log = malloc(length + 1); /* Leaks. */     \
      glGet##kind##InfoLog(var, length, NULL, log);      \
      PRINT_ERROR(kind_str " ERROR: %s\n", log);         \
      goto error;                                        \
    }                                                    \
  } while (0)

#define COMPILE_SHADER(var, type, source)           \
  GLuint var = glCreateShader(type);                \
  glShaderSource(var, 1, &(source), NULL);          \
  glCompileShader(var);                             \
  CHECK_LOG(var, Shader, GL_COMPILE_STATUS, #type); \
  glAttachShader(program, var);

typedef struct {
  SDL_AudioDeviceID dev;
  SDL_AudioSpec spec;
  u8* buffer;
  u8* buffer_end;
  u8* read_pos;
  u8* write_pos;
  size_t buffer_capacity;         /* Total capacity in bytes of the buffer. */
  size_t buffer_available;        /* Number of bytes available for reading. */
  size_t buffer_target_available; /* Try to keep the buffer this size. */
  Bool ready; /* Set to TRUE when audio is first rendered. */
} HostAudio;

typedef struct Host {
  HostConfig config;
  HostHookContext hook_ctx;
  SDL_Window* window;
  SDL_GLContext gl_context;
  HostAudio audio;
  u32 last_sync_cycles;  /* GB CPU cycle count of last synchronization. */
  f64 last_sync_real_ms; /* Wall clock time of last synchronization. */
  u64 start_counter;
  u64 performance_frequency;
} Host;

Result host_init_video(Host* host) {
  static const f32 s_buffer[] = {
    -1, -1,  0, SCREEN_HEIGHT / 256.0f,
    +1, -1,  SCREEN_WIDTH / 256.0f, SCREEN_HEIGHT / 256.0f,
    -1, +1,  0, 0,
    +1, +1,  SCREEN_WIDTH / 256.0f, 0,
  };

  static const char* s_vertex_shader =
      "attribute vec2 aPos;\n"
      "attribute vec2 aTexCoord;\n"
      "varying vec2 vTexCoord;\n"
      "void main(void) {\n"
      "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "  vTexCoord = aTexCoord;\n"
      "}\n";

  static const char* s_fragment_shader =
      "varying vec2 vTexCoord;\n"
      "uniform sampler2D uSampler;\n"
      "void main(void) {\n"
      "  gl_FragColor = texture2D(uSampler, vTexCoord);\n"
      "}\n";

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  host->window = SDL_CreateWindow("binjgb", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED,
                                  SCREEN_WIDTH * host->config.render_scale,
                                  SCREEN_HEIGHT * host->config.render_scale,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  CHECK_MSG(host->window != NULL, "SDL_CreateWindow failed.\n");
  host->gl_context = SDL_GL_CreateContext(host->window);
  GLint major;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  CHECK_MSG(major >= 2, "Unable to create GL context at version 2.\n");
#define V(name, type)                  \
  name = SDL_GL_GetProcAddress(#name); \
  CHECK_MSG(name != 0, "Unable to get GL function: " #name);
  FOREACH_GLEXT_PROC(V)
#undef V
  GLuint buffer, texture;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(s_buffer), s_buffer, GL_STATIC_DRAW);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  GLuint program = glCreateProgram();
  COMPILE_SHADER(vertex_shader, GL_VERTEX_SHADER, s_vertex_shader);
  COMPILE_SHADER(fragment_shader, GL_FRAGMENT_SHADER, s_fragment_shader);
  glLinkProgram(program);
  CHECK_LOG(program, Program, GL_LINK_STATUS, "GL_PROGRAM");
  glUseProgram(program);
  GLint aPos = glGetAttribLocation(program, "aPos");
  GLint aTexCoord = glGetAttribLocation(program, "aTexCoord");
  glEnableVertexAttribArray(aPos);
  glEnableVertexAttribArray(aTexCoord);
  glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, sizeof(f32) * 4, 0);
  glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(f32) * 4,
                        (void*)(sizeof(f32) * 2));
  glUniform1i(glGetUniformLocation(program, "uSampler"), 0);
  return OK;
error:
  SDL_Quit();
  return ERROR;
}

static void host_audio_callback(void* userdata, u8* dst, int len) {
  memset(dst, 0, len);
  Host* host = userdata;
  HostAudio* audio = &host->audio;
  if (len > (int)audio->buffer_available) {
    HOOK(audio_underflow, audio->buffer_available, len);
    len = (int)audio->buffer_available;
  }
  if (audio->read_pos + len > audio->buffer_end) {
    size_t len1 = audio->buffer_end - audio->read_pos;
    size_t len2 = len - len1;
    memcpy(dst, audio->read_pos, len1);
    memcpy(dst + len1, audio->buffer, len2);
    audio->read_pos = audio->buffer + len2;
  } else {
    memcpy(dst, audio->read_pos, len);
    audio->read_pos += len;
  }
  audio->buffer_available -= len;
}

static void host_init_time(Host* host) {
  host->performance_frequency = SDL_GetPerformanceFrequency();
  host->start_counter = SDL_GetPerformanceCounter();
}

f64 host_get_time_ms(Host* host) {
  u64 now = SDL_GetPerformanceCounter();
  return (f64)(now - host->start_counter) * 1000 / host->performance_frequency;
}

void host_delay(Host* host, f64 ms) {
  SDL_Delay((u32)ms);
}

static Result host_init_audio(Host* host) {
  host->last_sync_cycles = 0;
  host->last_sync_real_ms = host_get_time_ms(host);

  SDL_AudioSpec want;
  want.freq = host->config.frequency;
  want.format = AUDIO_SPEC_FORMAT;
  want.channels = AUDIO_SPEC_CHANNELS;
  want.samples = host->config.samples;
  want.callback = host_audio_callback;
  want.userdata = host;
  host->audio.dev = SDL_OpenAudioDevice(NULL, 0, &want, &host->audio.spec, 0);
  CHECK_MSG(host->audio.dev != 0, "SDL_OpenAudioDevice failed.\n");

  host->audio.buffer_target_available =
      (size_t)(host->audio.spec.size * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER);

  size_t buffer_capacity =
      (size_t)(host->audio.spec.size * AUDIO_MAX_BUFFER_SIZE_MULTIPLIER);
  host->audio.buffer_capacity = buffer_capacity;

  host->audio.buffer = malloc(buffer_capacity); /* Leaks. */
  CHECK_MSG(host->audio.buffer != NULL, "Audio buffer allocation failed.\n");
  memset(host->audio.buffer, 0, buffer_capacity);

  host->audio.buffer_end = host->audio.buffer + buffer_capacity;
  host->audio.read_pos = host->audio.write_pos = host->audio.buffer;
  return OK;
  ON_ERROR_RETURN;
}

Bool host_poll_events(Host* host) {
  Emulator* e = host->hook_ctx.e;
  Bool running = TRUE;
  SDL_Event event;
  EmulatorConfig old_config = e->config;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_WINDOWEVENT:
        switch (event.window.event) {
          case SDL_WINDOWEVENT_RESIZED: {
            f32 w = event.window.data1, h = event.window.data2;
            f32 aspect = w / h, want_aspect = (f32)SCREEN_WIDTH / SCREEN_HEIGHT;
            f32 new_w = aspect < want_aspect ? w : h * want_aspect;
            f32 new_h = aspect < want_aspect ? w / want_aspect : h;
            glViewport((w - new_w) * 0.5f, (h - new_h) * 0.5f, new_w, new_h);
            break;
          }
        }
        break;
      case SDL_KEYDOWN:
        switch (event.key.keysym.scancode) {
          case SDL_SCANCODE_1: e->config.disable_sound[CHANNEL1] ^= 1; break;
          case SDL_SCANCODE_2: e->config.disable_sound[CHANNEL2] ^= 1; break;
          case SDL_SCANCODE_3: e->config.disable_sound[CHANNEL3] ^= 1; break;
          case SDL_SCANCODE_4: e->config.disable_sound[CHANNEL4] ^= 1; break;
          case SDL_SCANCODE_B: e->config.disable_bg ^= 1; break;
          case SDL_SCANCODE_W: e->config.disable_window ^= 1; break;
          case SDL_SCANCODE_O: e->config.disable_obj ^= 1; break;
          case SDL_SCANCODE_F6: HOOK0(write_state); break;
          case SDL_SCANCODE_F9: HOOK0(read_state); break;
          case SDL_SCANCODE_N: e->config.step = 1; e->config.paused = 0; break;
          case SDL_SCANCODE_SPACE: e->config.paused ^= 1; break;
          case SDL_SCANCODE_ESCAPE: running = FALSE; break;
          default: break;
        }
        /* fall through */
      case SDL_KEYUP: {
        Bool down = event.type == SDL_KEYDOWN;
        switch (event.key.keysym.scancode) {
          case SDL_SCANCODE_TAB: e->config.no_sync = down; break;
          case SDL_SCANCODE_F11: if (!down) e->config.fullscreen ^= 1; break;
          default: break;
        }
        break;
      }
      case SDL_QUIT: running = FALSE; break;
      default: break;
    }
  }
  if (old_config.no_sync != e->config.no_sync) {
    SDL_GL_SetSwapInterval(e->config.no_sync ? 0 : 1);
  }
  if (old_config.fullscreen != e->config.fullscreen) {
    SDL_SetWindowFullscreen(
        host->window, e->config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }
  return running;
}

EmulatorEvent host_run_emulator(Host* host) {
  Emulator* e = host->hook_ctx.e;
  size_t size =
      NEXT_MODULO(host->audio.buffer_available, host->audio.spec.size);
  return run_emulator(e, (u32)(size / AUDIO_FRAME_SIZE));
}

void host_render_video(Host* host) {
  Emulator* e = host->hook_ctx.e;
  glClearColor(0.1f, 0.1f, 0.1f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA,
                  GL_UNSIGNED_BYTE, e->frame_buffer);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  SDL_GL_SwapWindow(host->window);
}

void host_synchronize(Host* host) {
  Emulator* e = host->hook_ctx.e;
  f64 now_ms = host_get_time_ms(host);
  f64 gb_ms = (f64)(e->state.cycles - host->last_sync_cycles) *
              MILLISECONDS_PER_SECOND / CPU_CYCLES_PER_SECOND;
  f64 real_ms = now_ms - host->last_sync_real_ms;
  f64 delta_ms = gb_ms - real_ms;
  f64 delay_until_ms = now_ms + delta_ms;
  if (delta_ms < -AUDIO_MAX_SLOW_DESYNC_MS ||
      delta_ms > AUDIO_MAX_FAST_DESYNC_MS) {
    HOOK(desync, now_ms, gb_ms, real_ms);
    /* Major desync; don't try to catch up, just reset. But our audio buffer
     * is probably behind (or way ahead), so pause to refill. */
    host->last_sync_real_ms = now_ms;
    SDL_PauseAudioDevice(host->audio.dev, 1);
    host->audio.ready = FALSE;
    SDL_LockAudioDevice(host->audio.dev);
    host->audio.read_pos = host->audio.write_pos = host->audio.buffer;
    host->audio.buffer_available = 0;
    SDL_UnlockAudioDevice(host->audio.dev);
  } else {
    if (real_ms < gb_ms) {
      HOOK(sync_wait, now_ms, delta_ms, gb_ms, real_ms);
      do {
        SDL_Delay((u32)delta_ms);
        now_ms = host_get_time_ms(host);
        delta_ms = delay_until_ms - now_ms;
      } while (delta_ms > 0);
    }
    host->last_sync_real_ms = delay_until_ms;
  }
  host->last_sync_cycles = e->state.cycles;
}

void host_render_audio(Host* host) {
  assert(AUDIO_SPEC_CHANNELS == SOUND_OUTPUT_COUNT);
  Emulator* e = host->hook_ctx.e;
  HostAudio* audio = &host->audio;
  u8* src = e->audio_buffer.data;

  SDL_LockAudioDevice(audio->dev);
  size_t old_buffer_available = audio->buffer_available;
  size_t src_frames = AUDIO_BUFFER_FRAMES(e);
  size_t max_dst_frames =
      (audio->buffer_capacity - audio->buffer_available) / AUDIO_FRAME_SIZE;
  size_t frames = MIN(src_frames, max_dst_frames);
  HostAudioSample* dst = (HostAudioSample*)audio->write_pos;
  HostAudioSample* dst_end = (HostAudioSample*)audio->buffer_end;
  for (size_t i = 0; i < frames; i++) {
    assert(dst + 2 <= dst_end);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++);
    if (dst == dst_end) {
      dst = (HostAudioSample*)audio->buffer;
    }
  }
  audio->write_pos = (u8*)dst;
  audio->buffer_available += frames * AUDIO_FRAME_SIZE;
  size_t new_buffer_available = audio->buffer_available;
  SDL_UnlockAudioDevice(audio->dev);

  if (frames < src_frames) {
    HOOK(audio_overflow, old_buffer_available);
  } else {
    HOOK(audio_add_buffer, old_buffer_available, new_buffer_available);
  }
  if (!audio->ready && new_buffer_available >= audio->buffer_target_available) {
    HOOK(audio_buffer_ready, new_buffer_available);
    audio->ready = TRUE;
    SDL_PauseAudioDevice(audio->dev, 0);
  }
}

static void joypad_callback(Emulator* e, void* user_data) {
  Host* sdl = user_data;
  const u8* state = SDL_GetKeyboardState(NULL);
  e->state.JOYP.up = state[SDL_SCANCODE_UP];
  e->state.JOYP.down = state[SDL_SCANCODE_DOWN];
  e->state.JOYP.left = state[SDL_SCANCODE_LEFT];
  e->state.JOYP.right = state[SDL_SCANCODE_RIGHT];
  e->state.JOYP.B = state[SDL_SCANCODE_Z];
  e->state.JOYP.A = state[SDL_SCANCODE_X];
  e->state.JOYP.start = state[SDL_SCANCODE_RETURN];
  e->state.JOYP.select = state[SDL_SCANCODE_BACKSPACE];
}

Result host_init(Host* host, Emulator* e) {
  CHECK_MSG(SDL_Init(SDL_INIT_EVERYTHING) == 0, "SDL_init failed.\n");
  host_init_time(host);
  CHECK(SUCCESS(host_init_video(host)));
  CHECK(SUCCESS(host_init_audio(host)));
  CHECK(SUCCESS(init_audio_buffer(e, host->audio.spec.freq,
                                  host->audio.spec.size / AUDIO_FRAME_SIZE)));
  host->last_sync_cycles = e->state.cycles;
  e->joypad_callback.func = joypad_callback;
  e->joypad_callback.user_data = host;
  return OK;
  ON_ERROR_RETURN;
}

Host* host_new(const HostConfig *config, Emulator* e) {
  Host* host = malloc(sizeof(Host));
  host->config = *config;
  host->hook_ctx.host = host;
  host->hook_ctx.e = e;
  host->hook_ctx.user_data = host->config.hooks.user_data;
  CHECK(SUCCESS(host_init(host, e)));
  return host;
error:
  free(host);
  return NULL;
}

void host_delete(Host* host) {
  SDL_GL_DeleteContext(host->gl_context);
  SDL_DestroyWindow(host->window);
  SDL_Quit();
  free(host);
}
