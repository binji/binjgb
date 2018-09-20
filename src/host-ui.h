/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_HOST_UI_H_
#define BINJGB_HOST_UI_H_

#include "common.h"
#include "emulator.h"

union SDL_Event;
struct SDL_Window;
struct HostUI;
struct HostTexture;

#ifdef __cplusplus
extern "C" {
#endif

struct HostUI* host_ui_new(struct SDL_Window*);
void host_ui_delete(struct HostUI*);
void host_ui_event(struct HostUI*, union SDL_Event*);
void host_ui_begin_frame(struct HostUI*, struct HostTexture* fb_texture);
void host_ui_end_frame(struct HostUI*);
intptr_t host_ui_get_frame_buffer_texture(struct HostUI*);
void host_ui_set_palette(struct HostUI*, RGBA palette[4]);
void host_ui_enable_palette(struct HostUI*, Bool enabled);
void host_ui_render_screen_overlay(struct HostUI*, struct HostTexture*);
Bool host_ui_capture_keyboard(struct HostUI*);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_UI_H_ */
