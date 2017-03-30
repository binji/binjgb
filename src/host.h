/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_HOST_H_
#define BINJGB_HOST_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

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
  void (*write_state)(HostHookContext*);
  void (*read_state)(HostHookContext*);
} HostHooks;

typedef struct HostInit {
  HostHooks hooks;
  int render_scale;
  int audio_frequency;
  int audio_frames;
} HostInit;

typedef struct HostConfig {
  Bool no_sync;
  Bool paused;
  Bool step;
  Bool fullscreen;
} HostConfig;

typedef struct HostTexture {
  int width;
  int height;
  intptr_t handle;
} HostTexture;

struct Host* host_new(const HostInit*, struct Emulator*);
void host_delete(struct Host*);
Bool host_poll_events(struct Host*);
void host_run_ms(struct Host*, f64 delta_ms);
void host_render_audio(struct Host*);
f64 host_get_monitor_refresh_ms(struct Host*);
f64 host_get_time_ms(struct Host*);
void host_set_config(struct Host*, const HostConfig*);
HostConfig host_get_config(struct Host*);
void host_begin_video(struct Host*);
void host_end_video(struct Host*);

HostTexture* host_get_frame_buffer_texture(struct Host*);
HostTexture* host_create_texture(struct Host*, int w, int h);
void host_upload_texture(struct Host*, HostTexture*, int w, int h, RGBA* data);
void host_destroy_texture(struct Host*, HostTexture*);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_H_ */
