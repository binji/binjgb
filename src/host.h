/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_HOST_H_
#define BINJGB_HOST_H_

#include "common.h"
#include "joypad.h"
#include "rewind.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOREACH_HOST_KEYCODE(_) \
  _(A)                          \
  _(B)                          \
  _(C)                          \
  _(D)                          \
  _(E)                          \
  _(F)                          \
  _(G)                          \
  _(H)                          \
  _(I)                          \
  _(J)                          \
  _(K)                          \
  _(L)                          \
  _(M)                          \
  _(N)                          \
  _(O)                          \
  _(P)                          \
  _(Q)                          \
  _(R)                          \
  _(S)                          \
  _(T)                          \
  _(U)                          \
  _(V)                          \
  _(W)                          \
  _(X)                          \
  _(Y)                          \
  _(Z)                          \
  _(1)                          \
  _(2)                          \
  _(3)                          \
  _(4)                          \
  _(5)                          \
  _(6)                          \
  _(7)                          \
  _(8)                          \
  _(9)                          \
  _(0)                          \
  _(RETURN)                     \
  _(ESCAPE)                     \
  _(BACKSPACE)                  \
  _(TAB)                        \
  _(SPACE)                      \
  _(MINUS)                      \
  _(EQUALS)                     \
  _(LEFTBRACKET)                \
  _(RIGHTBRACKET)               \
  _(BACKSLASH)                  \
  _(SEMICOLON)                  \
  _(APOSTROPHE)                 \
  _(GRAVE)                      \
  _(COMMA)                      \
  _(PERIOD)                     \
  _(SLASH)                      \
  _(F1)                         \
  _(F2)                         \
  _(F3)                         \
  _(F4)                         \
  _(F5)                         \
  _(F6)                         \
  _(F7)                         \
  _(F8)                         \
  _(F9)                         \
  _(F10)                        \
  _(F11)                        \
  _(F12)                        \
  _(HOME)                       \
  _(PAGEUP)                     \
  _(DELETE)                     \
  _(END)                        \
  _(PAGEDOWN)                   \
  _(RIGHT)                      \
  _(LEFT)                       \
  _(DOWN)                       \
  _(UP)                         \
  _(LSHIFT)

typedef enum HostKeycode {
  HOST_KEYCODE_UNKNOWN,
#define HOST_KEYCODE_ENUM(NAME) HOST_KEYCODE_##NAME,
  FOREACH_HOST_KEYCODE(HOST_KEYCODE_ENUM)
#undef HOST_KEYCODE_ENUM
} HostKeycode;

struct Emulator;
struct Host;
typedef u32 EmulatorEvent;

typedef struct HostHookContext {
  struct Host* host;
  struct Emulator* e;
  void* user_data;
} HostHookContext;

typedef struct HostHooks {
  void* user_data;
  void (*audio_add_buffer)(HostHookContext*, int old_available,
                           int new_available);
  void (*audio_buffer_ready)(HostHookContext*, int new_available);
  void (*audio_buffer_full)(HostHookContext*);
  void (*key_down)(HostHookContext*, HostKeycode key);
  void (*key_up)(HostHookContext*, HostKeycode key);
} HostHooks;

typedef struct HostInit {
  HostHooks hooks;
  int render_scale;
  int audio_frequency;
  int audio_frames;
  f32 audio_volume;
  RewindInit rewind;
} HostInit;

typedef struct HostConfig {
  Bool no_sync;
  Bool fullscreen;
} HostConfig;

typedef enum HostTextureFormat {
  HOST_TEXTURE_FORMAT_RGBA,
  HOST_TEXTURE_FORMAT_U8,
} HostTextureFormat;

typedef struct HostTexture {
  HostTextureFormat format;
  int width;
  int height;
  intptr_t handle;
} HostTexture;

struct Host* host_new(const HostInit*, struct Emulator*);
void host_delete(struct Host*);
Bool host_poll_events(struct Host*);
void host_run_ms(struct Host*, f64 delta_ms);
void host_step(struct Host*);
void host_render_audio(struct Host*);
void host_reset_audio(struct Host*);
void host_set_audio_volume(struct Host*, f32 volume);
f64 host_get_monitor_refresh_ms(struct Host*);
f64 host_get_time_ms(struct Host*);
void host_set_config(struct Host*, const HostConfig*);
HostConfig host_get_config(struct Host*);
void host_begin_video(struct Host*);
void host_end_video(struct Host*);
void host_set_palette(struct Host*, RGBA palette[4]);
void host_enable_palette(struct Host*, Bool enabled);
void host_render_screen_overlay(struct Host*, struct HostTexture*);

/* Rewind support. */

Ticks host_oldest_ticks(struct Host*);
Ticks host_newest_ticks(struct Host*);

Ticks host_get_rewind_oldest_ticks(struct Host*);
Ticks host_get_rewind_newest_ticks(struct Host*);
JoypadStats host_get_joypad_stats(struct Host*);
RewindStats host_get_rewind_stats(struct Host*);

void host_begin_rewind(struct Host*);
Result host_rewind_to_ticks(struct Host*, Ticks ticks);
void host_end_rewind(struct Host*);
Bool host_is_rewinding(struct Host*);

HostTexture* host_get_frame_buffer_texture(struct Host*);
HostTexture* host_create_texture(struct Host*, int w, int h, HostTextureFormat);
void host_upload_texture(struct Host*, HostTexture*, int w, int h,
                         const void* data);
void host_destroy_texture(struct Host*, HostTexture*);


#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_H_ */
