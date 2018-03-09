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

Debugger::MapWindow::MapWindow(Debugger* d) : Window(d) {}

void Debugger::MapWindow::Tick() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Map", &is_open)) {
    static const char* layer_names[] = {"BG", "Window"};

    ImGui::SliderInt("Scale", &scale, 1, 5);
    ImGui::Combo("Layer", &layer_type, layer_names);
    ImGui::Checkbox("Highlight", &highlight);
    ImGui::Separator();

    bool display = false;
    u8 scroll_x, scroll_y;
    switch (layer_type) {
      case LAYER_TYPE_BG:
        display = emulator_get_bg_display(d->e);
        emulator_get_bg_scroll(d->e, &scroll_x, &scroll_y);
        break;
      case LAYER_TYPE_WINDOW:
        display = emulator_get_window_display(d->e);
        emulator_get_window_scroll(d->e, &scroll_x, &scroll_y);
        break;
    }

    ImGui::LabelText("Display", "%s", display ? "On" : "Off");
    ImGui::LabelText("Scroll", "%d, %d", scroll_x, scroll_y);

    TileMapSelect map_select = emulator_get_tile_map_select(d->e, layer_type);
    TileDataSelect data_select = emulator_get_tile_data_select(d->e);
    TileMap tile_map;
    emulator_get_tile_map(d->e, map_select, tile_map);
    PaletteRGBA palette_rgba =
        emulator_get_palette_rgba(d->e, PALETTE_TYPE_BGP);

    const ImVec2 scaled_tile_size = kTileSize * scale;
    const ImVec2 scaled_tile_map_size = kTileMapSize * scaled_tile_size;
    ImGui::BeginChild("Tiles", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(cursor, cursor + scaled_tile_map_size, true);
    for (int ty = 0; ty < TILE_MAP_HEIGHT; ++ty) {
      for (int tx = 0; tx < TILE_MAP_WIDTH; ++tx) {
        ImVec2 ul_pos = cursor + ImVec2(tx, ty) * scaled_tile_size;
        int tile_index = tile_map[ty * TILE_MAP_WIDTH + tx];
        if (data_select == TILE_DATA_8800_97FF) {
          tile_index = 256 + (s8)tile_index;
        }
        if (d->tiledata_image.DrawTile(draw_list, tile_index, ul_pos, scale,
                                       palette_rgba)) {
          ImGui::SetTooltip("tile: %u (0x%04x)", tile_index,
                            0x8000 + tile_index * 16);
          d->highlight_tile = true;
          d->highlight_tile_index = tile_index;
        }
      }
    }

    if (display && highlight) {
      // The BG layer wraps, but the window layer doesn't. Also, the window
      // layer always displays the lower-right corner of its map.
      switch (layer_type) {
        case LAYER_TYPE_BG: {
          ImVec2 ul_pos = cursor + ImVec2(scroll_x, scroll_y) * scale;
          ImVec2 br_pos = ul_pos + kScreenSize * scale;
          for (int y = -1; y <= 0; ++y) {
            for (int x = -1; x <= 0; ++x) {
              ImVec2 offset = ImVec2(x, y) * scaled_tile_map_size;
              draw_list->AddRect(ul_pos + offset, br_pos + offset,
                                 kHighlightColor, 0, ~0, 4.0f);
            }
          }
          break;
        }
        case LAYER_TYPE_WINDOW: {
          ImVec2 ul_pos = cursor;
          ImVec2 br_pos =
              ul_pos + (kScreenSize - ImVec2(scroll_x, scroll_y)) * scale;
          draw_list->AddRect(ul_pos, br_pos, kHighlightColor, 0, ~0, 4.0f);
          break;
        }
      }
    }

    ImGui::PopClipRect();
    ImGui::Dummy(scaled_tile_map_size);
    ImGui::EndChild();
  }
  ImGui::EndDock();
}

