/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include "imgui.h"
#include "imgui_dock.h"
#include "imgui-helpers.h"

Debugger::ROMWindow::ROMWindow(Debugger* d) : Window(d) {}

void Debugger::ROMWindow::Init() {
  int rom_size = emulator_get_rom_size(d->e);
  // ROM size should always be a power of two.
  assert(rom_size != 0 && (rom_size & (rom_size - 1)) == 0);

  // Try to make it as square as possible, while keeping the sides
  // powers-of-two.
  rom_texture_width = rom_size;
  rom_texture_height = 1;
  while (rom_texture_width >= rom_texture_height) {
    rom_texture_width >>= 1;
    rom_texture_height <<= 1;
  }

  rom_texture = host_create_texture(d->host, rom_texture_width,
                                    rom_texture_height, HOST_TEXTURE_FORMAT_U8);
  emulator_clear_rom_usage();
}

void Debugger::ROMWindow::Tick() {
  if (ImGui::BeginDock("ROM", &is_open)) {
    host_upload_texture(d->host, rom_texture, rom_texture_width,
                        rom_texture_height, emulator_get_rom_usage());

    PaletteRGBA palette = {
        {0xff202020u, 0xff00ff00u, 0xffff0000u, 0xffff00ffu}};

    size_t rom_size = emulator_get_rom_size(d->e);
    u8* rom_usage = emulator_get_rom_usage();

    if (ImGui::Button("Dump")) {
      FileData file_data;
      file_data.data = rom_usage;
      file_data.size = rom_size;
      file_write(d->rom_usage_filename, &file_data);
    }
    ImGui::SliderInt("Scale", &scale, 1, 16);

    if (--counter <= 0) {
      counter = 60;
      ZERO_MEMORY(usage_bytes);
      for (size_t i = 0; i < rom_size; ++i) {
        usage_bytes[rom_usage[i]]++;
      }
    }

    ImGui::Text("Unknown: %s (%.0f%%)", d->PrettySize(usage_bytes[0]).c_str(),
                (f64)usage_bytes[0] * 100 / rom_size);
    ImGui::Text("Data: %s (%.0f%%)", d->PrettySize(usage_bytes[2]).c_str(),
                (f64)usage_bytes[2] * 100 / rom_size);
    ImGui::Text("Code: %s (%.0f%%)", d->PrettySize(usage_bytes[3]).c_str(),
                (f64)usage_bytes[3] * 100 / rom_size);

    ImGui::Separator();

    ImVec2 avail_size = ImGui::GetContentRegionAvail();
    s32 avail_x = (s32)(avail_size.x - ImGui::GetStyle().ScrollbarSize);
    avail_x -= avail_x % scale;
    ImVec2 child_size(avail_x, rom_texture_width * scale * rom_texture_height *
                                       scale / avail_x +
                                   scale);

    ImGui::BeginChild("Data");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    d->SetPaletteAndEnable(draw_list, palette);
    ImTextureID texture_id = (ImTextureID)rom_texture->handle;
    draw_list->PushTextureID(texture_id);
    draw_list->PushClipRect(cursor, cursor + child_size, true);

    f32 scroll_y = ImGui::GetScrollY();
    f32 inv_scale = 1.f / scale;
    s32 min_y = (s32)(scroll_y * inv_scale);
    s32 max_y = (s32)(
        std::min((scroll_y + avail_size.y + scale), child_size.y) * inv_scale);
    s32 unscaled_w = avail_x / scale;

    s32 x = 0;
    s32 y = min_y;
    s32 tx = (y * unscaled_w + x) % rom_texture_width;
    s32 ty = (y * unscaled_w + x) / rom_texture_width;

    ImVec2 inv_tex_size(1.f / rom_texture_width, 1.f / rom_texture_height);

    while (y < max_y && ty < rom_texture_height) {
      ImVec2 ul_pos = cursor + ImVec2(x, y) * scale;
      ImVec2 ul_uv = ImVec2(tx, ty) * inv_tex_size;

      s32 strip_w = std::min(unscaled_w - x, rom_texture_width - tx);

      ImVec2 br_pos = cursor + ImVec2(x + strip_w, y + 1) * scale;
      ImVec2 br_uv = ImVec2(tx + strip_w, ty + 1) * inv_tex_size;

      x += strip_w;
      if (x >= unscaled_w) {
        x -= unscaled_w;
        y += 1;
      }

      tx += strip_w;
      if (tx >= rom_texture_width) {
        tx -= rom_texture_width;
        ty += 1;
      }

      draw_list->AddImage(texture_id, ul_pos, br_pos, ul_uv, br_uv);
    }

    draw_list->PopTextureID();
    d->DisablePalette(draw_list);

    ImGui::PopClipRect();
    ImGui::Dummy(child_size);
    if (ImGui::IsItemHovered()) {
      ImVec2 mouse_pos = (ImGui::GetMousePos() - cursor) * inv_scale;
      s32 rom_loc = (s32)mouse_pos.y * unscaled_w + (s32)mouse_pos.x;

      if (rom_loc < rom_texture_width * rom_texture_height) {
        u32 bank = rom_loc >> 14;
        u32 addr = (rom_loc & 0x3fff) + (bank == 0 ? 0 : 0x4000);
        ImGui::SetTooltip("%02x:%04x", bank, addr);
      }
    }
    ImGui::EndChild();
  }
  ImGui::EndDock();
}
