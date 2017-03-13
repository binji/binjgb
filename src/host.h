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
  void (*audio_underflow)(HostHookContext*, int desired, int available);
  void (*audio_overflow)(HostHookContext*, int old_available);
  void (*audio_add_buffer)(HostHookContext*, int old_available,
                           int new_available);
  void (*audio_buffer_ready)(HostHookContext*, int new_available);
  void (*desync)(HostHookContext*, f64 now_ms, f64 gb_ms, f64 real_ms);
  void (*sync_wait)(HostHookContext*, f64 now_ms, f64 delta_ms, f64 gb_ms,
                    f64 real_ms);
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
EmulatorEvent host_run_emulator(struct Host*);
void host_render_video(struct Host*);
void host_render_audio(struct Host*);
void host_synchronize(struct Host*);
f64 host_get_time_ms(struct Host*);
void host_delay(struct Host*, f64 ms);
void host_set_config(struct Host*, const HostConfig*);
HostConfig host_get_config(struct Host*);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_H_ */
