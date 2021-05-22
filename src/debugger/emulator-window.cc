/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include "imgui.h"

#include "imgui-helpers.h"

// static
const char Debugger::s_emulator_window_name[] = "Binjgb";

Debugger::EmulatorWindow::EmulatorWindow(Debugger* d) : Window(d) {}

void Debugger::EmulatorWindow::Tick() {
  if (!is_open) return;

  if (ImGui::Begin(s_emulator_window_name, &is_open)) {
    int fb_width = d->host_init.use_sgb_border ? SGB_SCREEN_WIDTH : SCREEN_WIDTH;
    int fb_height = d->host_init.use_sgb_border ? SGB_SCREEN_HEIGHT : SCREEN_HEIGHT;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    HostTexture* fb_texture = host_get_frame_buffer_texture(d->host);
    ImVec2 avail_size = ImGui::GetContentRegionAvail();
    f32 w = avail_size.x, h = avail_size.y;
    f32 aspect = w / h;
    f32 want_aspect = (f32)fb_width / fb_height;
    ImVec2 image_size(aspect < want_aspect ? w : h * want_aspect,
                      aspect < want_aspect ? w / want_aspect : h);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 image_ul = cursor + (avail_size - image_size) * 0.5f;
    ImVec2 image_br = image_ul + image_size;
    draw_list->PushClipRect(image_ul, image_br);

    ImVec2 ul_uv(0, 0);
    ImVec2 br_uv((f32)fb_width / fb_texture->width,
                 (f32)fb_height / fb_texture->height);

    draw_list->AddImage((ImTextureID)fb_texture->handle, image_ul, image_br,
                        ul_uv, br_uv);

    if (d->highlight_obj) {
      f32 scale = image_size.x / fb_width;
      ObjSize obj_size = emulator_get_obj_size(d->e);
      Obj obj = emulator_get_obj(d->e, d->highlight_obj_index);

      // The OBJ position is already offset so it draws from the top-left,
      // but this means that the coordinates are sometimes positive when they
      // should be negative (e.g. 255 should be drawn as -1). This code adds
      // the offset back in, wrapped to 255, and draws from the bottom-right
      // instead.
      ImVec2 obj_pos(static_cast<u8>(obj.x + OBJ_X_OFFSET),
                     static_cast<u8>(obj.y + OBJ_Y_OFFSET));
      ImVec2 br_pos = image_ul + obj_pos * scale;
      ImVec2 ul_pos = br_pos - k8x16OBJSize * scale;
      if (obj_size == OBJ_SIZE_8X8) {
        br_pos.y -= kTileSize.y * scale;
      }
      draw_list->AddRectFilled(ul_pos, br_pos, kHighlightColor);
    }

    draw_list->PopClipRect();
  }
  ImGui::End();
}

