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

#define GET_CYCLES(x) ((x).cycles)
#define CMP_LT(x, y) ((x) < (y))
#define CMP_GT(x, y) ((x) > (y))

#define LOWER_BOUND(Type, var, init_begin, init_end, to_find, GET, CMP) \
  Type* var = NULL;                                                     \
  if (init_end - init_begin != 0) {                                     \
    Type* begin = init_begin; /* Inclusive. */                          \
    Type* end = init_end;     /* Exclusive. */                          \
    while (end - begin > 1) {                                           \
      Type* mid = begin + ((end - begin) / 2);                          \
      if (to_find == GET(*mid)) {                                       \
        begin = mid;                                                    \
        break;                                                          \
      } else if (CMP(to_find, GET(*mid))) {                             \
        end = mid;                                                      \
      } else {                                                          \
        begin = mid + 1;                                                \
      }                                                                 \
    }                                                                   \
    var = begin;                                                        \
  }

#define CHECK_WRITE(count, dst, dst_max_end) \
  do {                                       \
    if ((dst) + (count) > (dst_max_end)) {   \
      return NULL;                           \
    }                                        \
  } while (0)

/* RLE encoded as follows:
 * - non-runs are written directly
 * - runs are written with the first two bytes of the run , followed by the
 *   number of subsequent bytes in the run (i.e. count - 2) encoded as a
 *   varint. */
#define ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end) \
  do {                                                                  \
    u8* dst = dst_begin;                                                \
    assert(src_size > 0);                                               \
    size_t src_left = src_size - 1;                                     \
                                                                        \
    /* Always write the first byte. */                                  \
    u8 last = READ();                                                   \
    CHECK_WRITE(1, dst, dst_max_end);                                   \
    *dst++ = last;                                                      \
                                                                        \
    u32 count = 1;                                                      \
    for (; src_left > 0; src_left--) {                                  \
      u8 next = READ();                                                 \
      if (next == last) {                                               \
        count++;                                                        \
        continue;                                                       \
      }                                                                 \
      CHECK_WRITE(1, dst, dst_max_end);                                 \
      *dst++ = last;                                                    \
      if (count >= 2) {                                                 \
        dst = write_varint(count - 2, dst, dst_max_end);                \
        if (!dst) {                                                     \
          return NULL;                                                  \
        }                                                               \
      }                                                                 \
                                                                        \
      last = next;                                                      \
      count = 1;                                                        \
    }                                                                   \
                                                                        \
    dst_new_end =                                                       \
        count >= 2 ? write_varint(count - 2, dst, dst_max_end) : dst;   \
  } while (0)

#define DECODE_RLE(WRITE, src, src_size)   \
  do {                                     \
    assert(src_size > 0);                  \
    size_t src_left = src_size - 1;        \
                                           \
    u8 last = *src++;                      \
    WRITE(last);                           \
    for (; src_left > 0; src_left--) {     \
      u8 next = *src++;                    \
      if (next == last) {                  \
        u32 count = read_varint(&src) + 1; \
        for (; count > 0; count--) {       \
          WRITE(last);                     \
        }                                  \
      } else {                             \
        WRITE(next);                       \
        last = next;                       \
      }                                    \
    }                                      \
  } while (0)

typedef f32 HostAudioSample;
#define AUDIO_SPEC_FORMAT AUDIO_F32
#define AUDIO_SPEC_CHANNELS 2
#define AUDIO_SPEC_SAMPLE_SIZE sizeof(HostAudioSample)
#define AUDIO_FRAME_SIZE (AUDIO_SPEC_SAMPLE_SIZE * AUDIO_SPEC_CHANNELS)
#define AUDIO_CONVERT_SAMPLE_FROM_U8(X, fvol) ((fvol) * (X) * (1 / 256.0f))
#define AUDIO_TARGET_QUEUED_SIZE (2 * host->audio.spec.size)
#define AUDIO_MAX_QUEUED_SIZE (5 * host->audio.spec.size)
#define JOYPAD_BUFFER_DEFAULT_CAPACITY 4096

#define INVALID_CYCLES (~0ULL)

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

typedef struct JoypadSearchResult {
  JoypadBuffer* buffer;
  JoypadState* state;
} JoypadSearchResult;

typedef enum RewindStateKind {
  RewindStateKind_Base,
  RewindStateKind_Diff,
} RewindStateKind;

typedef struct RewindStateInfo {
  Cycles cycles;
  u8* data;
  size_t size;
  RewindStateKind kind;
} RewindStateInfo;

typedef struct RewindStateInfoRange {
  RewindStateInfo* begin; /* begin <= end; if begin == end range is empty. */
  RewindStateInfo* end;   /* end is exclusive. */
} RewindStateInfoRange;

typedef struct RewindDataRange {
  u8* begin;
  u8* end;
} RewindDataRange;

typedef struct RewindBuffer {
 /* All RewindStateInfo elements and their associated data are stored in the
  * same buffer. The data grows from lower addresses to higher addresses
  * starting from the beginning of the buffer. The info grows in the opposite
  * direction, from the opposite end. When the buffer fills, the older values
  * are overwritten. When this happens, there may be a gap in the used data;
  * this is why there are two data_ranges. Thus, there are two info_ranges to
  * maintain symmetry. All RewindStateInfo elements in info_range[i] have data
  * in data_range[i]. */
  RewindDataRange data_range[2];
  RewindStateInfoRange info_range[2];
  FileData last_state;
  FileData last_base_state;
  Cycles last_base_state_cycles;
  int frames_until_next_base;

  /* Data is decompressed into these states when seeking. */
  FileData seek_base_state;
  FileData seek_diff_state;

  /* Stats */
  size_t total_kind_bytes[2];
  size_t total_uncompressed_bytes;
} RewindBuffer;

typedef struct {
  SDL_AudioDeviceID dev;
  SDL_AudioSpec spec;
  u8* buffer; /* Size is spec.size. */
  Bool ready;
  f32 volume; /* [0..1] */
} Audio;

typedef struct Host {
  HostInit init;
  HostConfig config;
  HostHookContext hook_ctx;
  SDL_Window* window;
  SDL_GLContext gl_context;
  Audio audio;
  u64 start_counter;
  u64 performance_frequency;
  struct HostUI* ui;
  HostTexture* fb_texture;
  JoypadBuffer joypad_buffer_sentinel;
  RewindBuffer rewind_buffer;
  JoypadButtons last_joypad_buttons;
  Cycles last_cycles;
} Host;

static struct Emulator* host_get_emulator(Host* host) {
  return host->hook_ctx.e;
}

static Result host_init_video(Host* host) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
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
  struct Emulator* e = host_get_emulator(host);
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
  struct Emulator* e = host_get_emulator(host);
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

static JoypadSearchResult host_find_joypad(Host* host, Cycles cycles) {
  /* TODO(binji): Use a skip list if this is too slow? */
  JoypadSearchResult result;
  JoypadBuffer* first_buffer = host->joypad_buffer_sentinel.next;
  JoypadBuffer* last_buffer = host->joypad_buffer_sentinel.prev;
  assert(first_buffer->size != 0 && last_buffer->size != 0);
  Cycles first_cycles = first_buffer->data[0].cycles;
  Cycles last_cycles = last_buffer->data[last_buffer->size - 1].cycles;
  if (cycles <= first_cycles) {
    /* At or past the beginning. */
    result.buffer = first_buffer;
    result.state = &first_buffer->data[0];
    return result;
  } else if (cycles >= last_cycles) {
    /* At or past the end. */
    result.buffer = last_buffer;
    result.state = &last_buffer->data[last_buffer->size - 1];
    return result;
  } else if (cycles - first_cycles < last_cycles - cycles) {
    /* Closer to the beginning. */
    JoypadBuffer* buffer = first_buffer;
    while (cycles >= buffer->data[buffer->size - 1].cycles) {
      buffer = buffer->next;
    }
    result.buffer = buffer;
  } else {
    /* Closer to the end. */
    JoypadBuffer* buffer = last_buffer;
    while (cycles < buffer->data[buffer->size - 1].cycles) {
      buffer = buffer->next;
    }
    result.buffer = buffer;
  }

  JoypadState* begin = result.buffer->data;
  JoypadState* end = begin + result.buffer->size;
  LOWER_BOUND(JoypadState, lower_bound, begin, end, cycles, GET_CYCLES, CMP_LT);
  assert(lower_bound != NULL); /* The buffer should not be empty. */

  result.state = lower_bound;
  assert(result.state->cycles <= cycles);
  return result;
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

static u8* write_varint(u32 value, u8* dst_begin, u8* dst_max_end) {
  u8* dst = dst_begin;
  if (value < 0x80) {
    CHECK_WRITE(1, dst, dst_max_end);
    *dst++ = (u8)value;
  } else if (value < 0x4000) {
    CHECK_WRITE(2, dst, dst_max_end);
    *dst++ = 0x80 | (value & 0x7f);
    *dst++ = (value >> 7) & 0x7f;
  } else {
    /* If this fires there is a run of 128K. In the current EmulatorState this
     * is impossible. */
    assert(value < 0x20000);
    CHECK_WRITE(3, dst, dst_max_end);
    *dst++ = 0x80 | (value & 0x7f);
    *dst++ = 0x80 | ((value >> 7) & 0x7f);
    *dst++ = (value >> 14) & 0x7f;
  }
  return dst;
}

static u32 read_varint(const u8** src) {
  const u8* s = *src;
  u32 result = 0;
  if ((s[0] & 0x80) == 0) {
    return s[0];
  } else if ((s[1] & 0x80) == 0) {
    return (s[1] << 7) | (s[0] & 0x7f);
  } else {
    assert((s[2] & 0x80) == 0);
    return (s[2] << 14) | ((s[1] & 0x7f) << 7) | (s[0] & 0x7f);
  }
}

static u8* encode_rle(const u8* src, size_t src_size, u8* dst_begin,
                      u8* dst_max_end) {
  u8* dst_new_end;
#define READ() (*src++)
  ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end);
#undef READ
  return dst_new_end;
}

static void decode_rle(const u8* src, size_t src_size, u8* dst, u8* dst_end) {
#define WRITE(x) *dst++ = (x)
  DECODE_RLE(WRITE, src, src_size);
#undef WRITE
  assert(dst == dst_end);
}

static u8* encode_diff(const u8* src, const u8* base, size_t src_size,
                       u8* dst_begin, u8* dst_max_end) {
  u8* dst_new_end;
#define READ() (*src++ - *base++)
  ENCODE_RLE(READ, src_size, dst_begin, dst_max_end, dst_new_end);
#undef READ
  return dst_new_end;
}

static void decode_diff(const u8* src, size_t src_size, const u8* base, u8* dst,
                        u8* dst_end) {
#define WRITE(x) *dst++ = (*base++ + (x))
  DECODE_RLE(WRITE, src, src_size);
#undef WRITE
  assert(dst == dst_end);
}

static RewindStateInfo* append_rewind_state(Host* host) {
  RewindBuffer* buf = &host->rewind_buffer;
  struct Emulator* e = host_get_emulator(host);
  Cycles cycles = emulator_get_cycles(e);
  (void)emulator_write_state(e, &buf->last_state);

  /* The new state must be written in sorted order; if it is out of order (from
   * a rewind), then the subsequent saved states should have been cleared
   * first. */
  assert(host_get_rewind_last_cycles(host) == INVALID_CYCLES ||
         cycles > host_get_rewind_first_cycles(host));

  RewindStateKind kind;
  if (buf->frames_until_next_base-- == 0) {
    kind = RewindStateKind_Base;
    buf->frames_until_next_base = host->init.frames_per_base_state;
  } else {
    kind = RewindStateKind_Diff;
  }

  RewindDataRange* data_range = buf->data_range;
  RewindStateInfoRange* info_range = buf->info_range;
  RewindStateInfo* new_info = --info_range[1].begin;
  if ((u8 *)new_info <= data_range[1].end) {
  wrap:
    /* Need to wrap, roll back decrement and swap ranges. */
    info_range[1].begin++;
    info_range[0] = info_range[1];
    info_range[1].begin = info_range[1].end;
    data_range[1] = data_range[0];
    data_range[0].end = data_range[0].begin;

    new_info = --info_range[1].begin;
    assert((u8*)new_info > data_range[1].end);
  }

  u8* data_begin = data_range[0].end;
  u8* data_end_max = (u8*)MIN(info_range[0].begin, new_info);
  u8* data_end;
  switch (kind) {
    case RewindStateKind_Diff:
      if (buf->last_base_state_cycles != INVALID_CYCLES) {
        data_end = encode_diff(buf->last_state.data, buf->last_base_state.data,
                               buf->last_state.size, data_begin, data_end_max);
        break;
      }
      /* There is no previous base state, so we can't diff. Fallthrough to
       * writing a base state. */

    case RewindStateKind_Base:
      kind = RewindStateKind_Base;
      data_end = encode_rle(buf->last_state.data, buf->last_state.size,
                            data_begin, data_end_max);
      memcpy(buf->last_base_state.data, buf->last_state.data,
             buf->last_state.size);
      buf->last_base_state_cycles = cycles;
      break;
  }

  if (data_end == NULL) {
    /* Failed to write, need to wrap. */
    goto wrap;
  }

  assert(data_end <= data_end_max);
  data_range[0].end = data_end;

  /* Check to see how many data chunks we overwrote. */
  RewindStateInfo* new_end = info_range[0].end;
  while (info_range[0].begin < new_end && new_end[-1].data < data_end) {
    --new_end;
  }

  new_end = MIN(new_end, new_info);

  info_range[0].end = new_end;
  info_range[0].begin = MIN(info_range[0].begin, info_range[0].end);

  new_info->cycles = cycles;
  new_info->data = data_begin;
  new_info->size = data_end - data_begin;
  new_info->kind = kind;

  if (info_range[0].begin < info_range[0].end) {
    data_range[1].begin = info_range[0].end[-1].data;
    data_range[1].end = info_range[0].begin->data + info_range[0].begin->size;
  } else {
    data_range[1].begin = data_range[1].end =
        info_range[1].begin->data + info_range[1].begin->size;
  }

  /* Update stats. */
  buf->total_kind_bytes[kind] += new_info->size;
  buf->total_uncompressed_bytes += buf->last_state.size;

  return new_info;
}

static void host_init_rewind_buffer(Host* host) {
  size_t capacity = host->init.rewind_buffer_capacity;
  if (capacity == 0) {
    return;
  }

  u8* data = malloc(capacity);
  RewindBuffer* buf = &host->rewind_buffer;
  emulator_init_state_file_data(&buf->last_state);
  emulator_init_state_file_data(&buf->last_base_state);
  emulator_init_state_file_data(&buf->seek_base_state);
  emulator_init_state_file_data(&buf->seek_diff_state);
  buf->last_base_state_cycles = INVALID_CYCLES;
  buf->data_range[0].begin = buf->data_range[0].end = data;
  buf->data_range[1] = buf->data_range[0];
  RewindStateInfo* info = (RewindStateInfo*)(data + capacity);
  buf->info_range[1].begin = buf->info_range[1].end = info;
  buf->info_range[0] = buf->info_range[1];
  buf->frames_until_next_base = 0;
  append_rewind_state(host);
}

static Bool is_rewind_range_empty(RewindStateInfoRange* r) {
  return r->end == r->begin;
}

static Bool is_in_rewind_range(RewindStateInfoRange* range, Cycles cycles) {
  if (is_rewind_range_empty(range)) {
    return FALSE;
  }

  /* begin is inclusive and end is exclusive. */
  return range->begin->cycles <= cycles && cycles <= range->end[-1].cycles;
}

Cycles host_get_rewind_first_cycles(struct Host* host) {
  RewindStateInfoRange* info_range = host->rewind_buffer.info_range;
  /* info_range[0] is always older than info_range[1], if it exists, so check
   * that first. */
  int i;
  for (i = 0; i < 2; ++i) {
    if (!is_rewind_range_empty(&info_range[i])) {
      /* end is exclusive. */
      return info_range[i].end[-1].cycles;
    }
  }

  return INVALID_CYCLES;
}

Cycles host_get_rewind_last_cycles(struct Host* host) {
  RewindStateInfoRange* info_range = host->rewind_buffer.info_range;
  /* info_range[1] is always newer than info_range[0]. There's no need to check
   * info_range[0], because if info_range[1] doesn't exist, then info_range[0]
   * won't exist either. */
  if (!is_rewind_range_empty(&info_range[1])) {
    return info_range[1].begin->cycles;
  } else {
    assert(is_rewind_range_empty(&info_range[0]));
  }

  return INVALID_CYCLES;
}

size_t host_get_rewind_base_bytes(struct Host* host) {
  return host->rewind_buffer.total_kind_bytes[RewindStateKind_Base];
}

size_t host_get_rewind_diff_bytes(struct Host* host) {
  return host->rewind_buffer.total_kind_bytes[RewindStateKind_Diff];
}

size_t host_get_rewind_uncompressed_bytes(struct Host* host) {
  return host->rewind_buffer.total_uncompressed_bytes;
}

void host_get_rewind_buffer_usage(struct Host* host, size_t* out_used,
                                  size_t* out_capacity) {
  size_t used = 0;
  int i;
  for (i = 0; i < 2; ++i) {
    RewindDataRange* data_range = &host->rewind_buffer.data_range[i];
    RewindStateInfoRange* info_range = &host->rewind_buffer.info_range[i];
    used += data_range->end - data_range->begin;
    used += (info_range->end - info_range->begin) * sizeof(RewindStateInfo);
  }

  *out_used = used;
  *out_capacity = host->init.rewind_buffer_capacity;
}

static RewindStateInfo* find_first_base_in_range(RewindStateInfoRange range) {
  RewindStateInfo* base = range.begin;
  for (; base < range.end; base++) {
    if (base->kind == RewindStateKind_Base) {
      return base;
    }
  }
  return NULL;
}

static void host_handle_event(Host* host, EmulatorEvent event) {
  struct Emulator* e = host_get_emulator(host);
  if (event & EMULATOR_EVENT_NEW_FRAME) {
    host_upload_texture(host, host->fb_texture, SCREEN_WIDTH, SCREEN_HEIGHT,
                        *emulator_get_frame_buffer(e));

    append_rewind_state(host);
  }
  if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    host_render_audio(host);
    HOOK0(audio_buffer_full);
  }
}

static void host_run_until_cycles(struct Host* host, Cycles cycles) {
  struct Emulator* e = host_get_emulator(host);
  while (1) {
    EmulatorEvent event = emulator_run_until(e, cycles);
    host_handle_event(host, event);
    if (event & EMULATOR_EVENT_UNTIL_CYCLES) {
      break;
    }
  }
  host->last_cycles = emulator_get_cycles(e);
}

Result host_seek_to_cycles(Host* host, Cycles cycles) {
  RewindBuffer* buf = &host->rewind_buffer;
  RewindStateInfoRange* info_range = buf->info_range;
  int i;
  for (i = 0; i < 2; ++i) {
    if (is_in_rewind_range(&info_range[i], cycles)) {
      break;
    }
  }

  if (i == 2) {
    return ERROR;
  }

  RewindStateInfo* begin = info_range[i].begin;
  RewindStateInfo* end = info_range[i].end;
  LOWER_BOUND(RewindStateInfo, found, begin, end, cycles, GET_CYCLES, CMP_GT);
  assert(found);
  assert(found->cycles <= cycles);

  FileData* file_data;
  if (found->kind == RewindStateKind_Base) {
    file_data = &buf->seek_base_state;
    decode_rle(found->data, found->size, file_data->data,
               file_data->data + file_data->size);
  } else {
    assert(found->kind == RewindStateKind_Diff);
    /* Find the previous base state. */
    RewindStateInfoRange range = {found, end};
    RewindStateInfo* base_info = find_first_base_in_range(range);
    if (!base_info) {
      if (info_range == &buf->info_range[0]) {
        /* No previous base state, can't decode. */
        return ERROR;
      }

      /* Search the previous range. */
      base_info = find_first_base_in_range(info_range[0]);
      if (!base_info) {
        return ERROR;
      }
    }

    FileData* base = &buf->seek_base_state;
    decode_rle(base_info->data, base_info->size, base->data,
               base->data + base->size);

    file_data = &buf->seek_diff_state;
    decode_diff(found->data, found->size, base->data, file_data->data,
                file_data->data + file_data->size);
  }

  struct Emulator* e = host_get_emulator(host);
  CHECK(SUCCESS(emulator_read_state(e, file_data)));
  // TODO remove states from rewind buffer that are now invalid.
  host_run_until_cycles(host, cycles);

  return OK;
  ON_ERROR_RETURN;
}

Result host_init(Host* host, struct Emulator* e) {
  CHECK_MSG(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0,
            "SDL_init failed.\n");
  host_init_time(host);
  CHECK(SUCCESS(host_init_video(host)));
  CHECK(SUCCESS(host_init_audio(host)));
  host_init_joypad(host, e);
  host_init_rewind_buffer(host);
  host->last_cycles = emulator_get_cycles(e);
  return OK;
  ON_ERROR_RETURN;
}

void host_run_ms(struct Host* host, f64 delta_ms) {
  struct Emulator* e = host_get_emulator(host);
  Cycles delta_cycles = (Cycles)(delta_ms * CPU_CYCLES_PER_SECOND / 1000);
  Cycles until_cycles = emulator_get_cycles(e) + delta_cycles;
  host_run_until_cycles(host, until_cycles);
}

void host_step(Host* host) {
  struct Emulator* e = host_get_emulator(host);
  EmulatorEvent event = emulator_step(e);
  host_handle_event(host, event);
  host->last_cycles = emulator_get_cycles(e);
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
    free(host->rewind_buffer.data_range[0].begin);
    free(host->audio.buffer);
    free(host);
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
  free(texture);
}

void host_render_screen_overlay(struct Host* host,
                                struct HostTexture* texture) {
  host_ui_render_screen_overlay(host->ui, texture);
}

Cycles host_first_cycles(Host* host) {
  return host->joypad_buffer_sentinel.next->data[0].cycles;
}

Cycles host_last_cycles(Host* host) {
  return host->last_cycles;
}
