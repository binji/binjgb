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

Debugger::TiledataWindow::TiledataWindow(Debugger* d) : Window(d) {}

void Debugger::TiledataWindow::Tick() {
  if (ImGui::BeginDock("TileData", &is_open)) {
    ImGui::SliderInt("Scale", &scale, 1, 5);

    PaletteRGBA palette_rgba;

    if (d->is_cgb) {
      static const char* palette_names[] = {"BGCP", "OBCP"};

      ImGui::Combo("Palette", &cgb_palette_type, palette_names);
      ImGui::SliderInt("Index", &cgb_palette_index, 0, 7);
      cgb_palette_index = CLAMP(cgb_palette_index, 0, 7);

      palette_rgba = emulator_get_cgb_palette_rgba(
          d->e, (CgbPaletteType)cgb_palette_type, cgb_palette_index);
    } else {
      static const int kPaletteCustom = 3;
      static const char* palette_names[] = {"BGP", "OBP0", "OBP1", "Custom"};

      ImGui::Combo("Palette", &palette_type, palette_names);

      if (palette_type == kPaletteCustom) {
        for (int i = 0; i < 3; ++i) {
          char label[16];
          snprintf(label, sizeof(label), "Copy from %s", palette_names[i]);
          if (ImGui::Button(label)) {
            custom_palette =
                emulator_get_palette(d->e, static_cast<PaletteType>(i));
          }
        }

        static const char* color_names[] = {"White", "Light Gray", "Dark Gray",
                                            "Black"};
        ImGui::Combo("Color 0", &custom_palette.color[0], color_names);
        ImGui::Combo("Color 1", &custom_palette.color[1], color_names);
        ImGui::Combo("Color 2", &custom_palette.color[2], color_names);
        ImGui::Combo("Color 3", &custom_palette.color[3], color_names);
        palette_rgba = palette_to_palette_rgba(custom_palette);
      } else {
        palette_rgba =
            emulator_get_palette_rgba(d->e, (PaletteType)palette_type);
      }
    }

    ImGui::Checkbox("8x16", &size8x16);
    ImGui::SliderInt("Width", &wrap_width, 1, 48);
    ImGui::Separator();

    int tile_count = 384 * (d->is_cgb ? 2 : 1);
    int tw = wrap_width;
    int th = (tile_count + tw - 1) / tw;

    int space_at_end = (2 + 1) * ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("Tiles", ImVec2(0, -space_at_end), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    int width = TILE_DATA_TEXTURE_WIDTH / kTileSize.x;
    if (size8x16) {
      th = (th + 1) & ~1;
    }
    ImVec2 scaled_tile_size = kTileSize * scale;
    for (int ty = 0; ty < th; ++ty) {
      for (int tx = 0; tx < tw; ++tx) {
        int tile_index;
        if (size8x16) {
          tile_index = (ty & ~1) * tw + (tx * 2);
          if ((ty & 1) == 1) {
            tile_index++;
          }
        } else {
          tile_index = ty * tw + tx;
        }
        ImVec2 ul_pos = cursor + ImVec2(tx, ty) * scaled_tile_size;
        ImVec2 br_pos = ul_pos + scaled_tile_size;
        bool is_hovering =
            d->DrawTile(draw_list, tile_index, ul_pos, scale, palette_rgba);
        if (d->highlight_tile && d->highlight_tile_index == tile_index) {
          draw_list->AddRectFilled(ul_pos, br_pos, kHighlightColor);
        }
        if (is_hovering) {
          hovering_tile_index = tile_index;
        }
      }
    }
    d->highlight_tile = false;
    ImGui::Dummy(ImVec2(tw, th) * scaled_tile_size);
    ImGui::EndChild();
    ImGui::Separator();

    ImGui::LabelText("Tile Index", "%02x",
                     d->GetByteTileIndex(hovering_tile_index));
    ImGui::LabelText("Address", "%d:%04x", d->GetTileBank(hovering_tile_index),
                     d->GetTileAddr(hovering_tile_index));
  }
  ImGui::EndDock();
}
