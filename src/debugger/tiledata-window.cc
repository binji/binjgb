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
const char Debugger::s_tiledata_window_name[] = "TileData";

Debugger::TiledataWindow::TiledataWindow(Debugger* d) : Window(d) {
  memset(palette_rgba, 0, sizeof(palette_rgba));
}

void Debugger::TiledataWindow::Tick() {
  if (!is_open) return;

  if (ImGui::Begin(Debugger::s_tiledata_window_name, &is_open)) {
    ImGui::SliderInt("Scale", &scale, 1, 5);

    ImGui::Checkbox("Color Auto", &color_auto);

    memset(tile_palette_index, 0, sizeof(tile_palette_index));
    if (color_auto) {
      CalculateAutoPaletteColors();
      CalculateAutoTilePaletteIndex(LAYER_TYPE_BG);
      CalculateAutoTilePaletteIndex(LAYER_TYPE_WINDOW);
      CalculateAutoObjPaletteIndex();
    } else {
      if (d->is_cgb) {
        static const char* palette_names[] = {"BGCP", "OBCP"};

        ImGui::Combo("Palette", &cgb_palette_type, palette_names);
        ImGui::SliderInt("Index", &cgb_palette_index, 0, 7);
        cgb_palette_index = CLAMP(cgb_palette_index, 0, 7);

        palette_rgba[0] = emulator_get_cgb_palette_rgba(
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

          static const char* color_set_names[] = {"BGP", "OBP0", "OBP1"};
          ImGui::Combo("Color Set", &color_set, color_set_names);

          static const char* color_names[] = {"White", "Light Gray", "Dark Gray",
                                              "Black"};
          ImGui::Combo("Color 0", &custom_palette.color[0], color_names);
          ImGui::Combo("Color 1", &custom_palette.color[1], color_names);
          ImGui::Combo("Color 2", &custom_palette.color[2], color_names);
          ImGui::Combo("Color 3", &custom_palette.color[3], color_names);
          palette_rgba[0] = palette_to_palette_rgba(
              d->e, (PaletteType)color_set, custom_palette);
        } else {
          palette_rgba[0] =
              emulator_get_palette_rgba(d->e, (PaletteType)palette_type);
        }
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
            d->DrawTile(draw_list, tile_index, ul_pos, scale,
                        palette_rgba[tile_palette_index[tile_index]]);
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
  ImGui::End();
}

void Debugger::TiledataWindow::CalculateAutoPaletteColors() {
  palette_rgba[0] =
      PaletteRGBA{RGBA_WHITE, RGBA_LIGHT_GRAY, RGBA_DARK_GRAY, RGBA_BLACK};
  if (d->is_cgb) {
    for (int pal = 0; pal < 8; ++pal) {
      palette_rgba[1 + pal] =
          emulator_get_cgb_palette_rgba(d->e, CGB_PALETTE_TYPE_BGCP, pal);
      palette_rgba[9 + pal] =
          emulator_get_cgb_palette_rgba(d->e, CGB_PALETTE_TYPE_OBCP, pal);
    }
  } else if (d->is_sgb) {
    for (int pal = 0; pal < 4; ++pal) {
      palette_rgba[1 + pal] = emulator_get_sgb_palette_rgba(d->e, pal);
    }
    palette_rgba[5] = emulator_get_palette_rgba(d->e, PALETTE_TYPE_OBP0);
    palette_rgba[6] = emulator_get_palette_rgba(d->e, PALETTE_TYPE_OBP1);
  } else {
    palette_rgba[1] = emulator_get_palette_rgba(d->e, PALETTE_TYPE_BGP);
    palette_rgba[2] = emulator_get_palette_rgba(d->e, PALETTE_TYPE_OBP0);
    palette_rgba[3] = emulator_get_palette_rgba(d->e, PALETTE_TYPE_OBP1);
  }
}

void Debugger::TiledataWindow::CalculateAutoTilePaletteIndex(
    LayerType layer_type) {
  if ((layer_type == LAYER_TYPE_BG && !emulator_get_bg_display(d->e)) ||
      (layer_type == LAYER_TYPE_WINDOW && !emulator_get_window_display(d->e))) {
    return;
  }

  TileMapSelect map_select = emulator_get_tile_map_select(d->e, layer_type);
  TileDataSelect data_select = emulator_get_tile_data_select(d->e);

  TileMap tile_map;
  emulator_get_tile_map(d->e, map_select, tile_map);

  u8 left, right, top, bottom;
  if (layer_type == LAYER_TYPE_BG) {
    u8 scx, scy;
    emulator_get_bg_scroll(d->e, &scx, &scy);
    left = scx >> 3;
    right = (SCREEN_WIDTH + scx + 7) >> 3;
    top = scy >> 3;
    bottom = (SCREEN_HEIGHT + scy + 7) >> 3;
  } else {
    u8 wx, wy;
    assert(layer_type == LAYER_TYPE_WINDOW);
    emulator_get_window_scroll(d->e, &wx, &wy);
    left = top = 0;
    right = (SCREEN_WIDTH - wx + 7) >> 3;
    bottom = (SCREEN_HEIGHT - wy + 7) >> 3;
  }

  TileMap tile_map_attr;
  u8 sgb_attr_map[90];
  if (d->is_cgb) {
    emulator_get_tile_map_attr(d->e, map_select, tile_map_attr);
  } else if (d->is_sgb) {
    emulator_get_sgb_attr_map(d->e, sgb_attr_map);
  }

  for (u8 tiley = top; tiley < bottom; ++tiley) {
    for (u8 tilex = left; tilex < right; ++tilex) {
      u8 x = tilex << 3;
      u8 y = tiley << 3;
      int map_index = (y >> 3) * TILE_MAP_WIDTH + (x >> 3);
      int tile_index = tile_map[map_index];
      if (data_select == TILE_DATA_8800_97FF) {
        tile_index = 256 + (s8)tile_index;
      }

      if (d->is_cgb) {
        u8 attr = tile_map_attr[map_index];
        if (attr & 0x08) {
          tile_index += 0x180;
        }
        tile_palette_index[tile_index] = 1 + (attr & 7);
      } else if (d->is_sgb) {
        int idx = (y >> 3) * (SCREEN_WIDTH >> 3) + (x >> 3);
        u8 palidx = (sgb_attr_map[idx >> 2] >> (2 * (3 - (idx & 3)))) & 3;
        tile_palette_index[tile_index] = 1 + palidx;
      } else {
        tile_palette_index[tile_index] = 1;
      }
    }
  }
}

void Debugger::TiledataWindow::CalculateAutoObjPaletteIndex() {
  ObjSize obj_size = emulator_get_obj_size(d->e);
  for (int obj_index = 0; obj_index < 40; ++obj_index) {
    Obj obj = emulator_get_obj(d->e, obj_index);
    bool visible = static_cast<bool>(obj_is_visible(&obj));
    if (visible) {
      int tile_index = obj.tile;
      int pal_index = 0;
      if (d->is_cgb) {
        if (obj.bank) {
          tile_index += 0x180;
        }
        pal_index = 9 + obj.cgb_palette;
      } else if (d->is_sgb) {
        pal_index = 5 + obj.palette;
      } else {
        pal_index = 2 + obj.palette;
      }

      if (obj_size == OBJ_SIZE_8X16) {
        tile_palette_index[tile_index & ~1] = pal_index;
        tile_palette_index[tile_index | 1] = pal_index;
      } else {
        tile_palette_index[tile_index] = pal_index;
      }
    }
  }
}
