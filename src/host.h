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

#define HOST_FRAME_BUFFER_TEXTURE_WIDTH 256
#define HOST_FRAME_BUFFER_TEXTURE_HEIGHT 256

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

struct Host* host_new(const HostInit*, struct Emulator*);
void host_delete(struct Host*);
Bool host_poll_events(struct Host*);
void host_run_ms(struct Host*, f64 delta_ms);
void host_render_audio(struct Host*);
f64 host_get_monitor_refresh_ms(struct Host*);
f64 host_get_time_ms(struct Host*);
void host_set_config(struct Host*, const HostConfig*);
HostConfig host_get_config(struct Host*);
intptr_t host_get_frame_buffer_texture(struct Host*);
void host_begin_video(struct Host*);
void host_end_video(struct Host*);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_H_ */
