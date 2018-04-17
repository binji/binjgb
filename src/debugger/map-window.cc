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

    PaletteRGBA palette_rgba;
    TileMap tile_map_attr;
    if (d->is_cgb) {
      emulator_get_tile_map_attr(d->e, map_select, tile_map_attr);
    } else {
      palette_rgba = emulator_get_palette_rgba(d->e, PALETTE_TYPE_BGP);
    }

    int space_at_end =
        ((d->is_cgb ? 7 : 4) + 1) * ImGui::GetFrameHeightWithSpacing();

    const ImVec2 kTileMapSize(TILE_MAP_WIDTH, TILE_MAP_HEIGHT);
    const ImVec2 scaled_tile_size = kTileSize * scale;
    const ImVec2 scaled_tile_map_size = kTileMapSize * scaled_tile_size;
    ImGui::BeginChild("Tiles", ImVec2(0, -space_at_end), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    for (int ty = 0; ty < TILE_MAP_HEIGHT; ++ty) {
      for (int tx = 0; tx < TILE_MAP_WIDTH; ++tx) {
        bool xflip = false, yflip = false;
        ImVec2 ul_pos = cursor + ImVec2(tx, ty) * scaled_tile_size;
        int map_index = ty * TILE_MAP_WIDTH + tx;
        int tile_index = tile_map[map_index];
        if (data_select == TILE_DATA_8800_97FF) {
          tile_index = 256 + (s8)tile_index;
        }
        if (d->is_cgb) {
          u8 attr = tile_map_attr[map_index];
          int palette_index = attr & 7;
          if (attr & 0x08) { tile_index += 0x180; }
          if (attr & 0x20) { xflip = true; }
          if (attr & 0x40) { yflip = true; }
          palette_rgba = emulator_get_cgb_palette_rgba(
              d->e, CGB_PALETTE_TYPE_BGCP, palette_index);
        }
        if (d->DrawTile(draw_list, tile_index, ul_pos, scale, palette_rgba,
                        xflip, yflip)) {
          hovering_map_index = map_index;
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

    ImGui::Dummy(scaled_tile_map_size);
    ImGui::EndChild();
    ImGui::Separator();

    int map_index = hovering_map_index;
    Address map_address =
        (map_select == TILE_MAP_9800_9BFF ? 0x9800 : 0x9c00) + map_index;
    int tile_index = tile_map[hovering_map_index];
    if (data_select == TILE_DATA_8800_97FF) {
      tile_index = 256 + (s8)tile_index;
    }

    ImGui::LabelText("Pos", "%d, %d", map_index & 31, map_index >> 5);
    ImGui::LabelText("Map Address", "%04x", map_address);
    ImGui::LabelText("Tile Index", "%02x", d->GetByteTileIndex(tile_index));
    ImGui::LabelText("Tile Address", "%d:%04x", d->GetTileBank(tile_index),
                     d->GetTileAddr(tile_index));
    if (d->is_cgb) {
      u8 attr = tile_map_attr[hovering_map_index];
      ImGui::LabelText("Flip", "%c%c", (attr & 0x20) ? 'X' : '_',
                       (attr & 0x40) ? 'Y' : '_');
      ImGui::LabelText("Palette", "BGCP%d", attr & 7);
      ImGui::LabelText("Priority", "%s",
                       (attr & 0x80) ? "Above Obj" : "Normal");
    }
  }
  ImGui::EndDock();
}

